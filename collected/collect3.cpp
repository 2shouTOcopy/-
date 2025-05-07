#include "collect.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ──── 单例实例 ──── */
CollectDaemon& CollectDaemon::instance() {
    static CollectDaemon d;
    return d;
}

/* ---------- 路径辅助 ---------- */
bool CollectDaemon::pathExists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0;
}

int CollectDaemon::mkdirRecursive(const char* dir) {
    if (!dir || !*dir) return -1;
    std::string tmp;
    for (const char* p = dir; *p; ++p) {
        tmp.push_back(*p);
        if (*p != '/' && *(p+1)) continue;
        if (::mkdir(tmp.c_str(), 0755) == 0)        continue;
        if (errno == EEXIST)                        continue;
        return -1;
    }
    return 0;
}

/* ---------- CLI / 配置 ---------- */
void CollectDaemon::configure(int argc, char** argv) {
    parseCmdline(argc, argv);
    loadConfig();
    setupSignals();
}

void CollectDaemon::parseCmdline(int argc, char** argv) {
    int c;
    while ((c = ::getopt(argc, argv, "BhtTfC:P:f")) != -1) {
        switch (c) {
        case 'B': opt_.create_basedir = false; break;
        case 'C': opt_.config_file    = optarg; break;
        case 't': opt_.test_config    = true;  break;
        case 'T': opt_.test_readall   = true; opt_.daemonize = false; break;
        case 'P': opt_.pid_file       = optarg; break;
        case 'f': opt_.daemonize      = false; break;
        case 'h':
            std::printf("Usage: collect [OPTIONS]\n"
                        "  -C <file>  Config file\n"
                        "  -P <file>  PID file\n"
                        "  -f         Foreground\n"
                        "  -B         Don't create BaseDir\n"
                        "  -t         Test config only\n"
                        "  -T         Test read all\n"
                        "  -h         Help\n");
            std::exit(EXIT_SUCCESS);
        default: std::exit(EXIT_FAILURE);
        }
    }
}

void CollectDaemon::loadConfig() {
    if (!pathExists(opt_.config_file))
        throw std::runtime_error("Config file missing: " + opt_.config_file);

    if (cf_read(opt_.config_file.c_str()) != 0)
        throw std::runtime_error("Parse config failed");

    if (opt_.create_basedir) {
        const char* bd = global_option_get("BaseDir");
        if (bd && *bd) {
            if (mkdirRecursive(bd) != 0)
                throw std::runtime_error("mkdir BaseDir failed");
            if (::chdir(bd) != 0)
                throw std::runtime_error("chdir BaseDir failed");
        }
    }
}

/* ---------- run / loop ---------- */
int CollectDaemon::run() {
    if (opt_.test_config) return EXIT_SUCCESS;
    if (opt_.daemonize)   daemonize();

    try {
        initialize();
        int rc = loop();
        cleanup();
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        cleanup();
        return EXIT_FAILURE;
    }
}

void CollectDaemon::initialize() {
    if (plugin_init_all() != 0)
        throw std::runtime_error("plugin_init_all failed");
}

int CollectDaemon::loop() {
    running_.store(true);
    while (running_) {
        executePlugins();

        cdtime_t step = plugin_get_interval();
        struct timespec ts;
        CDTIME_T_TO_TIMESPEC(step, &ts);

        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk,
                     std::chrono::seconds(ts.tv_sec) +
                     std::chrono::nanoseconds(ts.tv_nsec),
                     [this]{ return !running_.load();});
    }
    return EXIT_SUCCESS;
}

void CollectDaemon::executePlugins() {
    if (opt_.test_readall) plugin_read_all_once();
    else                   plugin_read_all();
}

/* ---------- stop / cleanup ---------- */
void CollectDaemon::stop() {
    running_.store(false);
    cv_.notify_all();
}

CollectDaemon::~CollectDaemon() { cleanup(); }

void CollectDaemon::cleanup() {
    plugin_shutdown_all();
    removePidfile();
}

void CollectDaemon::createPidfile() const {
    if (opt_.pid_file.empty()) return;
    FILE* fp = ::fopen(opt_.pid_file.c_str(), "w");
    if (!fp) throw std::runtime_error("open pidfile failed");
    std::fprintf(fp, "%d\n", static_cast<int>(::getpid()));
    ::fclose(fp);
}

void CollectDaemon::removePidfile() const {
    if (!opt_.pid_file.empty() && pathExists(opt_.pid_file))
        ::unlink(opt_.pid_file.c_str());
}

void CollectDaemon::redirectStdio() const {
    int fd = ::open("/dev/null", O_RDWR);
    if (fd == -1 ||
        ::dup2(fd, STDIN_FILENO)  == -1 ||
        ::dup2(fd, STDOUT_FILENO) == -1 ||
        ::dup2(fd, STDERR_FILENO) == -1)
        throw std::runtime_error("redirect stdio failed");
    ::close(fd);
}

void CollectDaemon::daemonize() {
    if (::fork() > 0) std::exit(EXIT_SUCCESS);
    if (::setsid() == -1)
        throw std::runtime_error("setsid failed");
    createPidfile();
    redirectStdio();
}

/* ---------- signals / flush ---------- */
void CollectDaemon::setupSignals() {
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = sigIntHandler;
    sigaction(SIGINT , &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sa.sa_handler = sigUsr1Handler;
    sigaction(SIGUSR1, &sa, nullptr);
}

void CollectDaemon::sigIntHandler(int)  { instance().stop(); }
void CollectDaemon::sigTermHandler(int) { instance().stop(); }

void* CollectDaemon::flushThread(void*) {
    INFO("Flushing all data.");
    plugin_flush(nullptr, 0, nullptr);
    INFO("Flushing done.");
    return nullptr;
}

void CollectDaemon::sigUsr1Handler(int) {
    pthread_t tid{};
    plugin_thread_create(&tid, flushThread, nullptr, "flush");
}

/* ---------- end collect.cpp ---------- */