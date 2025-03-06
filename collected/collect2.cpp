/*
 * collectd.cpp
 *
 * 这是 collectd 的 daemon 入口的 C++ 版本示例，仅保留 Linux 平台相关逻辑（KERNEL_LINUX）。
 * 其他平台的分支（如 WIN32、kstat、sysctl 等）已被删除。
 *
 * 作者：参考原 collectd 源码
 */

#include "cmd.h"
#include "collectd.h"
#include "configfile.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <string>
#include <vector>

// 假定全局变量和宏（如 interval_g、timeout_g、CONFIGFILE、PLUGINDIR 等）
// 已在 collectd.h 或其它头文件中定义。

// 全局控制变量，用于终止主循环
static int loop = 0;

/* Linux 平台下的主机名初始化函数 */
static int init_hostname() {
    const char *str = global_option_get("Hostname");
    if (str && str[0] != '\0') {
        hostname_set(str);
        return 0;
    }

    // Linux 下使用 sysconf 获取主机名缓冲区大小
    long hostname_len = sysconf(_SC_HOST_NAME_MAX);
    if (hostname_len == -1)
        hostname_len = NI_MAXHOST;
    std::vector<char> hostname(hostname_len);
    if (gethostname(hostname.data(), hostname_len) != 0) {
        std::fprintf(stderr, "`gethostname' failed and no hostname was configured.\n");
        return -1;
    }
    hostname_set(hostname.data());

    // 如果配置中要求 FQDNLookup，则通过 getaddrinfo 尝试解析
    str = global_option_get("FQDNLookup");
    if (IS_FALSE(str))
        return 0;
    struct addrinfo *ai_list = nullptr;
    struct addrinfo ai_hints {};
    ai_hints.ai_flags = AI_CANONNAME;
    int status = getaddrinfo(hostname.data(), nullptr, &ai_hints, &ai_list);
    if (status != 0) {
        ERROR("Looking up \"%s\" failed. You have set the \"FQDNLookup\" option, but I cannot resolve my hostname to a fully qualified domain name. Please fix the network configuration.", hostname.data());
        return -1;
    }
    for (struct addrinfo *ai_ptr = ai_list; ai_ptr; ai_ptr = ai_ptr->ai_next) {
        if (ai_ptr->ai_canonname == nullptr)
            continue;
        hostname_set(ai_ptr->ai_canonname);
        break;
    }
    freeaddrinfo(ai_list);
    return 0;
}

/* Linux 平台下全局变量初始化 */
static int init_global_variables() {
    interval_g = cf_get_default_interval();
    assert(interval_g > 0);
    DEBUG("interval_g = %.3f;", CDTIME_T_TO_DOUBLE(interval_g));

    const char *str = global_option_get("Timeout");
    if (str == nullptr)
        str = "2";
    timeout_g = std::atoi(str);
    if (timeout_g <= 1) {
        std::fprintf(stderr, "Cannot set the timeout to a correct value.\nPlease check your settings.\n");
        return -1;
    }
    DEBUG("timeout_g = %i;", timeout_g);

    if (init_hostname() != 0)
        return -1;
    DEBUG("hostname_g = %s;", hostname_g);
    return 0;
}

/* 改变工作目录。此处使用 C++ 的 std::string 进行简单包装 */
static int change_basedir(const std::string &orig_dir, bool create) {
    std::string dir = orig_dir;
    // 去掉末尾的 '/' 字符
    while (!dir.empty() && dir.back() == '/')
        dir.pop_back();
    if (dir.empty())
        return -1;
    if (chdir(dir.c_str()) == 0)
        return 0;
    else if (!create || errno != ENOENT) {
        ERROR("change_basedir: chdir (%s): %s", dir.c_str(), STRERRNO);
        return -1;
    }
    if (mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
        ERROR("change_basedir: mkdir (%s): %s", dir.c_str(), STRERRNO);
        return -1;
    }
    if (chdir(dir.c_str()) != 0) {
        ERROR("change_basedir: chdir (%s): %s", dir.c_str(), STRERRNO);
        return -1;
    }
    return 0;
}

/* 打印用法信息并退出 */
static void exit_usage(int status) {
    std::printf("Usage: " PACKAGE_NAME " [OPTIONS]\n\n"
                "Available options:\n"
                "  General:\n"
                "    -C <file>       Configuration file.\n"
                "                    Default: " CONFIGFILE "\n"
                "    -t              Test config and exit.\n"
                "    -T              Test plugin read and exit.\n"
                "    -P <file>       PID-file.\n"
                "                    Default: " PIDFILE "\n"
                "    -B              Don't create the BaseDir\n"
                "    -h              Display help (this message)\n"
                "\nBuiltin defaults:\n"
                "  Config file       " CONFIGFILE "\n"
                "  PID file          " PIDFILE "\n"
                "  Plugin directory  " PLUGINDIR "\n"
                "  Data directory    " PKGLOCALSTATEDIR "\n"
                "\n" PACKAGE_NAME " " PACKAGE_VERSION ", http://collectd.org/\n"
                "by Florian octo Forster <octo@collectd.org>\n"
                "for contributions see `AUTHORS'\n");
    std::exit(status);
}

/* Linux 下初始化（不包含 kstat、statgrab 等非 Linux 部分） */
static int do_init() {
#if HAVE_SETLOCALE
    if (setlocale(LC_NUMERIC, COLLECTD_LOCALE) == nullptr)
        WARNING("setlocale (\"%s\") failed.", COLLECTD_LOCALE);
    unsetenv("LC_ALL");
    setenv("LC_NUMERIC", COLLECTD_LOCALE, 1);
#endif
    // Linux 下不调用 kstat、statgrab 相关代码
    return plugin_init_all();
}

/* 主循环：每个间隔调用 plugin_read_all，并用 nanosleep 等待下一个间隔 */
static int do_loop() {
    cdtime_t interval = cf_get_default_interval();
    cdtime_t wait_until = cdtime() + interval;
    while (loop == 0) {
        plugin_read_all();
        cdtime_t now = cdtime();
        if (now >= wait_until) {
            WARNING("Not sleeping because the next interval is %.3f seconds in the past!",
                    CDTIME_T_TO_DOUBLE(now - wait_until));
            wait_until = now + interval;
            continue;
        }
        struct timespec ts_wait = CDTIME_T_TO_TIMESPEC(wait_until - now);
        wait_until = wait_until + interval;
        while ((loop == 0) && (nanosleep(&ts_wait, &ts_wait) != 0)) {
            if (errno != EINTR) {
                ERROR("nanosleep failed: %s", STRERRNO);
                return -1;
            }
        }
    }
    return 0;
}

/* 调用所有插件的 shutdown 回调 */
static int do_shutdown() {
    return plugin_shutdown_all();
}

/* 命令行参数解析结构 */
struct cmdline_config {
    bool daemonize;
    bool create_basedir;
    const char *configfile;
    bool test_config;
    bool test_readall;
};

/* 解析命令行参数 */
static void read_cmdline(int argc, char **argv, cmdline_config *config) {
    int c;
    while ((c = getopt(argc, argv, "BhtTfC:P:")) != -1) {
        switch (c) {
        case 'B':
            config->create_basedir = false;
            break;
        case 'C':
            config->configfile = optarg;
            break;
        case 't':
            config->test_config = true;
            break;
        case 'T':
            config->test_readall = true;
            global_option_set("ReadThreads", "-1", 1);
            break;
        case 'P':
#if COLLECT_DAEMON
            global_option_set("PIDFile", optarg, 1);
#endif
            break;
        case 'h':
            exit_usage(EXIT_SUCCESS);
        default:
            exit_usage(EXIT_FAILURE);
        }
    }
}

/* 根据命令行和配置文件进行收集器配置 */
static int configure_collectd(cmdline_config *config) {
    if (cf_read(config->configfile)) {
        std::fprintf(stderr, "Error: Parsing the config file failed!\n");
        return 1;
    }
    const char *basedir = global_option_get("BaseDir");
    if (basedir == nullptr) {
        std::fprintf(stderr, "Don't have a basedir to use. This should not happen. Ever.");
        return 1;
    } else if (change_basedir(basedir, config->create_basedir) != 0) {
        std::fprintf(stderr, "Error: Unable to change to directory `%s'.\n", basedir);
        return 1;
    }
    if (init_global_variables() != 0)
        return 1;
    return 0;
}

/* 停止收集器：修改全局 loop 变量 */
void stop_collectd() {
    loop++;
}

/* 初始化配置（解析命令行、加载配置文件、初始化插件上下文等） */
cmdline_config init_config(int argc, char **argv) {
    cmdline_config config = { true, true, CONFIGFILE, false, false };
    read_cmdline(argc, argv, &config);
    if (optind < argc)
        exit_usage(EXIT_FAILURE);
    plugin_init_ctx();
    if (configure_collectd(&config) != 0)
        std::exit(EXIT_FAILURE);
    if (config.test_config)
        std::exit(EXIT_SUCCESS);
    return config;
}

/* 运行主循环。若 test_readall 为 true 则仅调用一次 plugin_read_all_once */
int run_loop(bool test_readall, void (*notify_func)(void)) {
    int exit_status = 0;
    if (do_init() != 0) {
        ERROR("Error: one or more plugin init callbacks failed.");
        exit_status = 1;
    }
    if (test_readall) {
        if (plugin_read_all_once() != 0) {
            ERROR("Error: one or more plugin read callbacks failed.");
            exit_status = 1;
        }
    } else {
        if (notify_func != nullptr) {
            notify_func();
        }
        INFO("Initialization complete, entering read-loop.");
        do_loop();
    }
    INFO("Exiting normally.");
    if (do_shutdown() != 0) {
        ERROR("Error: one or more plugin shutdown callbacks failed.");
        exit_status = 1;
    }
    return exit_status;
}

/* 主函数 */
int main(int argc, char **argv) {
    cmdline_config config = init_config(argc, argv);
    // 此处 notify_func 可根据需要传入实际回调，或传 nullptr
    int ret = run_loop(config.test_readall, nullptr);
    return ret;
}