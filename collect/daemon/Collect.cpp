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

#include "Collect.h"
#include "PluginService.h"
#include "../oconfig/configfile.h"

CollectDaemon &CollectDaemon::instance()
{
	static CollectDaemon ins;
	return ins;
}

CollectDaemon::~CollectDaemon() { cleanup(); }

bool CollectDaemon::pathExists(const std::string &p)
{
	struct stat st{};
	return ::stat(p.c_str(), &st) == 0;
}

int CollectDaemon::mkdirRecursive(const char *dir)
{
    if (!dir || !*dir)
        return -1;
    std::string tmp;
    for (const char *p = dir; *p; ++p)
    {
        tmp.push_back(*p);
        if (*p != '/' && *(p + 1))
            continue;
        if (::mkdir(tmp.c_str(), 0755) == 0)
            continue;
        if (errno == EEXIST)
            continue;
        return -1;
    }
    return 0;
}

void CollectDaemon::configure(int argc, char **argv)
{
    parseCmdline(argc, argv);
    loadConfig();
    setupSignals();
}

void CollectDaemon::parseCmdline(int argc, char **argv)
{
    int c;
    while ((c = ::getopt(argc, argv, "BhtTfC:P:F")) != -1)
    {
        switch (c)
        {
        case 'B':
            opt_.create_basedir = false;
            break;
        case 'C':
            opt_.config_file = optarg;
            break;
        case 't':
            opt_.test_config = true;
            break;
        case 'T':
            opt_.test_readall = true;
            opt_.daemonize = false;
            break;
        case 'P':
            opt_.pid_file = optarg;
            break;
        case 'f':
            opt_.daemonize = false;
            break;
        case 'F':
            opt_.test_flushall = true;
            break;
        case 'h':
            std::printf("Usage: collect [OPTIONS]\n"
                        "  -C <file>  Config file\n"
                        "  -P <file>  PID file\n"
                        "  -f         Foreground\n"
                        "  -F         Test flush all\n"
                        "  -B         Don't create BaseDir\n"
                        "  -t         Test config only\n"
                        "  -T         Test read all\n"
                        "  -h         Help\n");
            std::exit(EXIT_SUCCESS);
        default:
            std::fprintf(stderr, "Error: Unknown option -%c\n", c);
            std::exit(EXIT_FAILURE);
        }
    }
}

void CollectDaemon::loadConfig()
{
    if (!pathExists(opt_.config_file))
    {
        throw std::runtime_error("Config file missing: " + opt_.config_file);
	}

    if (ConfigManager::Instance().Read(opt_.config_file.c_str()) != 0)
    {
        throw std::runtime_error("Parse config failed");
	}

    if (opt_.create_basedir)
    {
        std::string strDir = ConfigManager::Instance().GetGlobalOption("BaseDir");
        if (!strDir.empty())
        {
            if (mkdirRecursive(strDir.c_str()) != 0)
            {
                throw std::runtime_error("mkdir BaseDir failed");
			}
            if (::chdir(strDir.c_str()) != 0)
            {
                throw std::runtime_error("chdir BaseDir failed");
			}
        }
    }
}

/* ---------- run / loop ---------- */
int CollectDaemon::run()
{
    if (opt_.test_config) return EXIT_SUCCESS;

    if (opt_.daemonize) daemonize();

    try
    {
        initialize();
        int rc = loop();
        return rc;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        return EXIT_FAILURE;
    }
}

void CollectDaemon::initialize()
{
    if (PluginService::Instance().initAll() != 0)
    {
        throw std::runtime_error("plugin_init_all failed");
	}
}

int CollectDaemon::loop() 
{
    int exit_status = 0;
    running_.store(true);

    const double interval_s = ConfigManager::Instance().GetDefaultInterval();
	INFO(" >>>>>>>>>>>>>>>>>>>>> loop <%lf>", interval_s);

    using namespace std::chrono;
    const auto interval_ns =
        duration_cast<steady_clock::duration>(duration<double>(interval_s));

    auto next_wakeup = steady_clock::now() + interval_ns;

    while (running_.load()) 
	{
        if (opt_.test_readall) 
		{
            if (PluginService::Instance().readAllOnce() != 0)
            {
                exit_status = -1;
			}
            break;
        }
		else if (opt_.test_flushall)
		{
            if (PluginService::Instance().flushAll() != 0)
            {
                exit_status = -1;
			}
            break;
		}

        PluginService::Instance().readAll();

        // 检查是否已经“过期”没睡到指定时间
        auto now = steady_clock::now();
        if (now >= next_wakeup)
		{
            // 计算“晚了多少秒”
            double late_s = duration<double>(now - (next_wakeup - interval_ns)).count();
            WARNING("Not sleeping because the next interval is %.3f seconds in the past!",
                    late_s);

            // 直接以当前时间为基准，下次唤醒：now + interval
            next_wakeup = now + interval_ns;
            continue;
        }

        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_until(lk, next_wakeup,
                       [this]{ return !this->running_.load(); });

        next_wakeup += interval_ns;
    }

    return exit_status;
}

/* ---------- stop / cleanup ---------- */
void CollectDaemon::stop()
{
    running_.store(false);
    cv_.notify_all();
}

void CollectDaemon::cleanup()
{
	//PluginService::Instance().shutdownAll();
    //removePidfile();
}

void CollectDaemon::createPidfile() const
{
    if (opt_.pid_file.empty()) return;

    FILE *fp = ::fopen(opt_.pid_file.c_str(), "w");
    if (!fp)
    {
        throw std::runtime_error("open pidfile failed");
	}
    std::fprintf(fp, "%d\n", static_cast<int>(::getpid()));
    ::fclose(fp);
}

void CollectDaemon::removePidfile() const
{
    if (!opt_.pid_file.empty() && pathExists(opt_.pid_file))
    {
        ::unlink(opt_.pid_file.c_str());
	}
}

void CollectDaemon::redirectStdio() const
{
    int fd = ::open("/dev/null", O_RDWR);
    if (fd == -1 ||
        ::dup2(fd, STDIN_FILENO) == -1 ||
        ::dup2(fd, STDOUT_FILENO) == -1 ||
        ::dup2(fd, STDERR_FILENO) == -1)
    {
        throw std::runtime_error("redirect stdio failed");
	}
    ::close(fd);
}

void CollectDaemon::daemonize()
{
	printf("daemonize\n");
    if (::fork() > 0)
    {
        std::exit(EXIT_SUCCESS);
	}
    if (::setsid() == -1)
    {
        throw std::runtime_error("setsid failed");
	}
    createPidfile();
    redirectStdio();
}

/* ---------- signals / flush ---------- */
void CollectDaemon::setupSignals()
{
	struct sigaction sa{};
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sa.sa_handler = sigIntHandler;
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGTERM, &sa, nullptr);

	sa.sa_handler = sigUsr1Handler;
	sigaction(SIGUSR1, &sa, nullptr);

	sa.sa_handler = sigUsr2Handler;
	sigaction(SIGUSR2, &sa, nullptr);

}

void CollectDaemon::sigIntHandler(int)
{
	instance().stop();
}

void CollectDaemon::sigTermHandler(int)
{
	instance().stop();
}

void *CollectDaemon::flushThread(void *)
{
	PluginService::Instance().flushAll();
	INFO("Manual flush: done.");
	return nullptr;
}

void CollectDaemon::sigUsr1Handler(int)
{
	pthread_t th; 
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&th, &attr, flushThread, nullptr);
	pthread_attr_destroy(&attr);
}

void *CollectDaemon::configThread(void *)
{
	INFO("Manual config: done.");
	
	return nullptr;
}

void CollectDaemon::sigUsr2Handler(int)
{
	pthread_t th;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&th, &attr, configThread, nullptr);
	pthread_attr_destroy(&attr);
}

