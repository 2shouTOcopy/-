// FtpClientManager.hpp

#ifndef FTP_CLIENT_MANAGER_HPP
#define FTP_CLIENT_MANAGER_HPP

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <cstring>
#include <memory>
#include <string>
#include <iostream>
#include <chrono>
#include <ftp/client.hpp> // 引入您提供的FTP客户端库

#define FILE_NAME_MAXSIZE 256
#define DIR_NAME_MAXSIZE 256

// 定义FTP FIFO参数结构体
struct FtpFifoParam {
    uint32_t used_len; // 已使用的缓冲区长度
    std::vector<uint8_t> buf; // 使用vector管理缓冲区
    char file_name[FILE_NAME_MAXSIZE];
    char dir_name[DIR_NAME_MAXSIZE];
    uint32_t send_state;
};

// FtpTrans类，负责参数设置和数据入队
class FtpTrans {
public:
    FtpTrans();
    ~FtpTrans();

    void setParameters(/* 参数设置 */);
    void enqueueData(const std::vector<uint8_t>& data, const std::string& fileName, const std::string& dirName);

private:
    // FIFO 队列相关
    std::queue<FtpFifoParam> fifoQueue_;
    std::mutex fifoMutex_;
    std::condition_variable fifoCondVar_;

    friend class FtpClientManager;
};

// FtpClientManager类，负责FTP传输管理
class FtpClientManager : public std::enable_shared_from_this<FtpClientManager> {
public:
    FtpClientManager(FtpTrans& ftpTrans);
    ~FtpClientManager();

    void start();
    void stop();

private:
    void threadFunc();
    bool login();
    void logout();
    void handleFifoData(FtpFifoParam& ftpParam);
    void performNoopTest();

    // FTP客户端
    ftp::client ftpClient_;

    // 线程相关
    std::thread workerThread_;
    std::atomic<bool> running_;
    std::atomic<bool> needRelogin_;

    // FIFO 队列和同步
    FtpTrans& ftpTrans_;
    std::mutex& fifoMutex_;
    std::condition_variable& fifoCondVar_;

    // 状态管理
    std::atomic<int> idleCount_;
    std::atomic<bool> end_;

    // 配置参数
    bool rootDirChangeEnable_;

    // 目录管理相关
    int directoryOption_; // 0: 不创建, 1: 创建, 2: 自定义
    std::string customDirectoryName_;
    size_t maxFileCount_;
    bool incrementDirectory_;
    size_t fileCount_;
    size_t directoryIndex_;
    std::string currentDirectory_;

    // 私有方法
    void setFtpState(const std::string& state);
    void logInfo(const std::string& message);
    void logError(const std::string& message);
    bool handleDirectory();
    void incrementFileCount();

    // FTP连接参数
    std::string ftpHost_;
    uint16_t ftpPort_;
    bool anonymousLogin_;
    std::string ftpUsername_;
    std::string ftpPassword_;
};

#endif // FTP_CLIENT_MANAGER_HPP