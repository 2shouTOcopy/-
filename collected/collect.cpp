#include "collect.h"

// 构造函数
CollectDaemon::CollectDaemon(bool daemonize) : daemonize_(daemonize) {}

// 启动 `collect`
void CollectDaemon::run() {
    if (daemonize_) {
        daemonize();
    }

    // 安装信号处理
    setup_signal_handlers();

    // 初始化 Collect
    if (!initialize()) {
        std::cerr << "Initialization failed. Exiting..." << std::endl;
        exit(EXIT_FAILURE);
    }

    // 进入主循环
    main_loop();
}

// 让进程成为 Daemon
void CollectDaemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork() failed: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);  // 父进程退出
    }

    // 创建新会话
    if (setsid() < 0) {
        std::cerr << "setsid() failed: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // 进行二次 fork，防止进程重新获得终端
    pid = fork();
    if (pid < 0) {
        std::cerr << "Second fork failed: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // 修改 umask，确保文件权限可用
    umask(0);

    // 更改工作目录到根目录
    if (chdir("/") < 0) {
        std::cerr << "chdir() to / failed: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    // 关闭标准输入、输出、错误，并重定向到 /dev/null
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null != -1) {
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        close(dev_null);
    }

    // 写入 PID 文件
    write_pid_file();
}

// 处理信号
void CollectDaemon::signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        std::cerr << "Received termination signal. Shutting down..." << std::endl;
        remove_pid_file();
        exit(EXIT_SUCCESS);
    }
}

// 安装信号处理
void CollectDaemon::setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
}

// 写入 PID 文件
void CollectDaemon::write_pid_file() {
    std::ofstream pid_file("/var/run/collect.pid");
    if (pid_file.is_open()) {
        pid_file << getpid() << std::endl;
        pid_file.close();
    } else {
        std::cerr << "Failed to write PID file." << std::endl;
    }
}

// 移除 PID 文件
void CollectDaemon::remove_pid_file() {
    unlink("/var/run/collect.pid");
}

// 进程初始化
bool CollectDaemon::initialize() {
    std::cout << "Initializing Collect..." << std::endl;
    return true;  // 这里可以添加插件加载、配置解析等逻辑
}

// 进入主循环
void CollectDaemon::main_loop() {
    std::cout << "Collect is running..." << std::endl;
    while (true) {
        sleep(10);  // 这里可以替换成实际的任务执行逻辑
    }
}

// 入口函数
int main(int argc, char *argv[]) {
    bool daemonize = true;
    CollectDaemon daemon(daemonize);
    daemon.run();
    return 0;
}