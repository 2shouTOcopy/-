#ifndef COLLECT_H
#define COLLECT_H

#include <iostream>
#include <fstream>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

class CollectDaemon {
public:
    explicit CollectDaemon(bool daemonize);

    // 启动 `collect`
    void run();

private:
    bool daemonize_;

    // 让进程成为守护进程
    void daemonize();

    // 处理信号
    static void signal_handler(int sig);
    void setup_signal_handlers();

    // PID 文件管理
    void write_pid_file();
    static void remove_pid_file();

    // 进程初始化
    bool initialize();

    // 主循环
    void main_loop();
};

#endif // COLLECT_H