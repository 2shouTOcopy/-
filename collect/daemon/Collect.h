#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

#include "PluginService.h"

struct CmdOptions
{
    bool daemonize = true;
    bool create_basedir = true;
    bool test_config = false;
    bool test_readall = false;
    bool test_flushall = false;
    std::string config_file = CONFIGFILE;
    std::string pid_file = PIDFILE;
};

class CollectDaemon
{
public:
    static CollectDaemon &instance(); // 单例

    /* CLI 入口：在 cmd.cpp 调用 */
    void configure(int argc, char **argv);
    int run();   // 真正的 main 函数体
    void stop(); // 供 SIGINT / SIGTERM 回调使用

private:
    CollectDaemon() = default;
    ~CollectDaemon();

    /* 配置、初始化、主循环 */
    void parseCmdline(int argc, char **argv);
    void loadConfig();
    void initialize();
    int loop();
    void cleanup();

    /* 守护化 / PID 文件 */
    void daemonize();
    void createPidfile() const;
    void removePidfile() const;
    void redirectStdio() const;

    /* BaseDir 助手 */
    static bool pathExists(const std::string &p);
    static int mkdirRecursive(const char *dir);

    /* 信号 / flush */
    void setupSignals();
    static void sigIntHandler(int);
    static void sigTermHandler(int);
    static void sigUsr1Handler(int);
    static void *flushThread(void *);

private:
    CmdOptions opt_;
    std::atomic<bool> running_{false};
    std::mutex mtx_;
    std::condition_variable cv_;

    CollectDaemon(const CollectDaemon &) = delete;
    CollectDaemon &operator=(const CollectDaemon &) = delete;
};
