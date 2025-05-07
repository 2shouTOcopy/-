// Collect.cpp
#include "Collect.h"

#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <signal.h>
#include <system_error>
#include <unistd.h>

/* static */ CollectDaemon* CollectDaemon::self_ = nullptr;

/* ---------- public 入口 ---------- */
int CollectDaemon::run(int argc, char** argv)
{
    CollectDaemon daemon;
    self_ = &daemon;

    daemon.parseCmdline(argc, argv);
    daemon.loadConfig();

    if (daemon.opt_.test_config)
        return EXIT_SUCCESS;

    if (daemon.opt_.daemonize)
        daemon.daemonize();

    daemon.setupSignals();
    daemon.initialize();
    int rc = daemon.mainLoop();
    daemon.cleanup();
    return rc;
}

/* ---------- 析构自动清理 ---------- */
CollectDaemon::~CollectDaemon()
{
    cleanup();
}

/* ---------- CLI ---------- */
void CollectDaemon::parseCmdline(int argc, char** argv)
{
    int c;
    while ((c = getopt(argc, argv, "BhtTfC:P:")) != -1) {
        switch (c) {
        case 'B': opt_.create_basedir = false; break;
        case 'C': opt_.config_file = optarg;   break;
        case 't': opt_.test_config  = true;    break;
        case 'T': opt_.test_readall = true; opt_.daemonize = false; break;
        case 'P': opt_.pid_file     = optarg;  break;
        case 'f': opt_.daemonize    = false;   break;
        case 'h': exitUsage(EXIT_SUCCESS);
        default : exitUsage(EXIT_FAILURE);
        }
    }
}

/* ---------- config / init ---------- */
void CollectDaemon::loadConfig()
{
    if (!fs::exists(opt_.config_file))
        throw std::runtime_error("Config file not found: " + opt_.config_file);

    if (cf_read(opt_.config_file.c_str()) != 0)
        throw std::runtime_error("Parse config failed");

    if (opt_.create_basedir) {
        const char* basedir = global_option_get("BaseDir");
        if (basedir && *basedir) {
            fs::create_directories(basedir);
            fs::current_path(basedir);
        }
    }
}

void CollectDaemon::daemonize()
{
    if (fork() > 0) std::exit(EXIT_SUCCESS);
    if (setsid() == -1)
        throw std::system_error(errno,std::system_category(),"setsid");

    createPidfile();
    redirectStdio();
}

void CollectDaemon::createPidfile() const
{
    if (opt_.pid_file.empty()) return;
    std::ofstream fp(opt_.pid_file, std::ios::trunc);
    if (!fp) throw std::runtime_error("Cannot create PID file " + opt_.pid_file);
    fp << ::getpid() << '\n';
}

void CollectDaemon::redirectStdio() const
{
    int nullfd = ::open("/dev/null", O_RDWR);
    if (nullfd == -1 ||
        ::dup2(nullfd, STDIN_FILENO)  == -1 ||
        ::dup2(nullfd, STDOUT_FILENO) == -1 ||
        ::dup2(nullfd, STDERR_FILENO) == -1)
        throw std::system_error(errno,std::system_category(),"dup2");
    ::close(nullfd);
}

/* ---------- signals ---------- */
void CollectDaemon::sigHandler(int sig)
{
    if (sig==SIGINT || sig==SIGTERM) {
        if (self_) {
            self_->running_.store(false);
            self_->cv_.notify_all();
        }
    }
}

void CollectDaemon::flushHandler(int)
{
    if (self_) self_->asyncFlush();
}

void CollectDaemon::setupSignals()
{
    struct sigaction sa{};
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = sigHandler;
    sigaction(SIGINT , &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sa.sa_handler = flushHandler;
    sigaction(SIGUSR1, &sa, nullptr);
}

/* ---------- async flush ---------- */
void CollectDaemon::asyncFlush()
{
    pthread_t tid{};
    plugin_thread_create(&tid,
        [](void*)->void*{
            INFO("Flushing all data.");
            plugin_flush(nullptr,0,nullptr);
            INFO("Flush done.");
            return nullptr;
        },
        nullptr, "flush");
}

/* ---------- initialize / main loop ---------- */
void CollectDaemon::initialize()
{
    if (plugin_init_all() != 0)
        throw std::runtime_error("plugin_init_all failed");
}

int CollectDaemon::mainLoop()
{
    running_.store(true);
    while (running_) {
        executePlugins();

        cdtime_t step = plugin_get_interval();
        auto next = std::chrono::steady_clock::now() +
                    CDTIME_T_TO_STD_DURATION(step);

        std::unique_lock lk(mtx_);
        cv_.wait_until(lk, next, [this]{return !running_.load();});
    }
    return EXIT_SUCCESS;
}

void CollectDaemon::executePlugins()
{
    if (opt_.test_readall)
         plugin_read_all_once();
    else plugin_read_all();
}

/* ---------- cleanup / utils ---------- */
void CollectDaemon::cleanup()
{
    plugin_shutdown_all();
    if (!opt_.pid_file.empty() && fs::exists(opt_.pid_file))
        fs::remove(opt_.pid_file);
}

[[noreturn]] void CollectDaemon::exitUsage(int status)
{
    std::printf(
        "Usage: collect [OPTIONS]\n"
        "  -C <file>  Config file (default %s)\n"
        "  -P <file>  PID file   (default %s)\n"
        "  -f         Foreground (no daemon)\n"
        "  -B         Don't create BaseDir\n"
        "  -t         Test config only\n"
        "  -T         Test read-all (no daemon)\n"
        "  -h         This help\n",
        CONFIGFILE, PIDFILE);
    std::exit(status);
}