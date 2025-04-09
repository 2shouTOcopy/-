/**
 * collectd_main.cpp
 *
 * 说明：
 * 1. 该示例使用 C++14 标准进行编写，保留了与原先 C 代码近似的执行逻辑和函数结构。
 * 2. 大部分依赖函数（如 plugin_flush、stop_collectd、run_loop 等）假设仍由 collectd
 *    核心或相关库提供，因此此处仅做声明，不做具体实现。
 * 3. 如果有特定的命名规范或需要将函数封装到类中，可根据需求调整。
 * 4. 仅演示如何使用 C++14 对原 C 代码进行相对“现代化”的改写，核心功能和调用逻辑与原版保持一致。
 */

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

// 假设以下头文件仍旧是 collectd 提供：
#include "cmd.h"        // 包含 init_config(...) 等声明
#include "collectd.h"   // 包含 global_option_get(...) 等声明
#include "utils/common/common.h" // 包含宏 STRERRNO 等
#include <cerrno>

// 如果有需要，可以使用类似的方式来替代 GCC 的 __attribute__((unused))
#if defined(__GNUC__) || defined(__clang__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

// 或者在 C++17 以后可用 [[maybe_unused]] 替代
// 在 C++14 可以通过自定义宏或直接忽略

// ----- 转写自原 C 代码中的函数声明 -----

/**
 * 假设 collectd.c / collectd.h 中提供的函数，用于停止主循环
 * 来自您给的原代码: stop_collectd()
 */
extern "C" void stop_collectd();

/**
 * 用于启动主循环并最终返回退出码
 * 来自您给的原代码: run_loop(bool test_readall, void (*notify_func)(void) = nullptr)
 */
extern "C" int run_loop(bool test_readall, void (*notify_func)(void) = nullptr);

/**
 * 全局选项获取函数
 * 来自您给的原代码: global_option_get(...)
 */
extern "C" const char* global_option_get(const char* key);

/**
 * 初始化并解析命令行等
 * 来自您给的原代码: init_config(argc, argv) => struct cmdline_config
 */
extern "C" struct cmdline_config init_config(int argc, char** argv);

/**
 * 主循环中需要的一些函数
 * 来自您给的原代码: do_init(), do_shutdown() 等
 */
extern "C" int do_init();
extern "C" int do_shutdown();
extern "C" int plugin_flush(const char* plugin, cdtime_t timeout, const char* ident);

// ----- 结束外部 C 风格函数声明 -----

// 这是原代码中的结构，用于保存命令行配置。
struct cmdline_config {
  bool daemonize;
  bool create_basedir;
  const char* configfile;
  bool test_config = false;
  bool test_readall = false;
};

/**
 * @brief 线程函数，用于异步执行 flush 操作
 */
static void* do_flush(void* UNUSED arg)
{
  INFO("Flushing all data.");
  // 将所有数据 flush
  plugin_flush(nullptr, 0, nullptr);
  INFO("Finished flushing all data.");
  // 线程结束
  pthread_exit(nullptr);
  return nullptr;
}

/**
 * @brief SIGINT 信号处理函数
 */
static void sig_int_handler(int UNUSED signal)
{
  stop_collectd();
}

/**
 * @brief SIGTERM 信号处理函数
 */
static void sig_term_handler(int UNUSED signal)
{
  stop_collectd();
}

/**
 * @brief SIGUSR1 信号处理函数，异步地 flush 数据
 */
static void sig_usr1_handler(int UNUSED signal)
{
  pthread_t thread;
  pthread_attr_t attr;
  
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  // 创建异步 flush 线程
  if (pthread_create(&thread, &attr, do_flush, nullptr) != 0) {
    ERROR("Failed to create flush thread: %s", STRERRNO);
  }
  pthread_attr_destroy(&attr);
}

#if COLLECT_DAEMON
/**
 * @brief 写 PID 文件
 */
static int pidfile_create()
{
  FILE* fh = nullptr;
  const char* file = global_option_get("PIDFile");
  if (!file) {
    return 1;
  }

  fh = fopen(file, "w");
  if (!fh) {
    ERROR("fopen(%s): %s", file, STRERRNO);
    return 1;
  }

  fprintf(fh, "%d\n", static_cast<int>(getpid()));
  fclose(fh);
  return 0;
}

/**
 * @brief 移除 PID 文件
 */
static int pidfile_remove()
{
  const char* file = global_option_get("PIDFile");
  if (!file) {
    return 0;
  }
  return unlink(file);
}
#endif // COLLECT_DAEMON

/**
 * @brief 主函数入口，使用 C++14 实现
 *
 * @param argc 参数个数
 * @param argv 参数数组
 * @return int 退出状态码
 */
int main(int argc, char** argv)
{
  // 解析命令行与配置
  cmdline_config config = init_config(argc, argv);

#if COLLECT_DAEMON
  // 安装 SIGCHLD 处理器，忽略子进程退出
  {
    struct sigaction sig_chld_action {};
    sig_chld_action.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sig_chld_action, nullptr);
  }

  // 如果需要以 daemon 方式运行，则 fork 一次
  if (config.daemonize) {
    pid_t pid = fork();
    if (pid < 0) {
      // 出错
      std::cerr << "fork: " << STRERRNO << std::endl;
      return 1;
    } else if (pid > 0) {
      // 父进程直接退出
      return 0;
    }
    // 子进程逻辑
    setsid(); // detach from session

    if (pidfile_create()) {
      // 如果写 pidfile 失败，直接退出
      exit(2);
    }
    // 关闭标准输入/输出并重定向到 /dev/null
    close(STDERR_FILENO);
    close(STDOUT_FILENO);
    close(STDIN_FILENO);

    int status = open("/dev/null", O_RDWR);
    if (status != 0) {
      ERROR("Could not connect `STDIN' to `/dev/null' (status %d)", status);
      return 1;
    }

    status = dup(0);
    if (status != 1) {
      ERROR("Could not connect `STDOUT' to `/dev/null' (status %d)", status);
      return 1;
    }

    status = dup(0);
    if (status != 2) {
      ERROR("Could not connect `STDERR' to `/dev/null' (status %d)", status);
      return 1;
    }
  }
#endif // COLLECT_DAEMON

  // 忽略 SIGPIPE
  {
    struct sigaction sig_pipe_action {};
    sig_pipe_action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sig_pipe_action, nullptr);
  }

  // 安装 SIGINT、SIGTERM、SIGUSR1 等信号处理器
  {
    struct sigaction sig_int_action {};
    sig_int_action.sa_handler = sig_int_handler;
    if (sigaction(SIGINT, &sig_int_action, nullptr) != 0) {
      ERROR("Failed to install handler for SIGINT: %s", STRERRNO);
      return 1;
    }

    struct sigaction sig_term_action {};
    sig_term_action.sa_handler = sig_term_handler;
    if (sigaction(SIGTERM, &sig_term_action, nullptr) != 0) {
      ERROR("Failed to install handler for SIGTERM: %s", STRERRNO);
      return 1;
    }

    struct sigaction sig_usr1_action {};
    sig_usr1_action.sa_handler = sig_usr1_handler;
    if (sigaction(SIGUSR1, &sig_usr1_action, nullptr) != 0) {
      ERROR("Failed to install handler for SIGUSR1: %s", STRERRNO);
      return 1;
    }
  }

  // 判断是否需要仅测试 read 回调后退出
  int exit_status = run_loop(config.test_readall);

#if COLLECT_DAEMON
  // 如果是 daemon 方式，退出时移除 pidfile
  if (config.daemonize) {
    pidfile_remove();
  }
#endif // COLLECT_DAEMON

  // 返回最终的退出状态
  return exit_status;
}