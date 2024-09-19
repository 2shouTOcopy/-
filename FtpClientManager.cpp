// FtpClientManager.cpp

#include "FtpClientManager.hpp"

// ================== FtpTrans 类实现 ==================

FtpTrans::FtpTrans() {
    // 初始化FIFO队列
}

FtpTrans::~FtpTrans() {
    // 清理资源
}

void FtpTrans::setParameters(/* 参数设置 */) {
    // 设置参数
}

void FtpTrans::enqueueData(const std::vector<uint8_t>& data, const std::string& fileName, const std::string& dirName) {
    FtpFifoParam ftpParam;
    ftpParam.used_len = data.size();
    ftpParam.buf = data;
    std::strncpy(ftpParam.file_name, fileName.c_str(), FILE_NAME_MAXSIZE);
    std::strncpy(ftpParam.dir_name, dirName.c_str(), DIR_NAME_MAXSIZE);
    ftpParam.send_state = 0;

    {
        std::lock_guard<std::mutex> lock(fifoMutex_);
        fifoQueue_.push(ftpParam);
    }
    fifoCondVar_.notify_one();
}

// ================== FtpClientManager 类实现 ==================

FtpClientManager::FtpClientManager(FtpTrans& ftpTrans)
    : ftpTrans_(ftpTrans),
      fifoMutex_(ftpTrans_.fifoMutex_),
      fifoCondVar_(ftpTrans_.fifoCondVar_),
      running_(false),
      needRelogin_(false),
      idleCount_(0),
      end_(false),
      rootDirChangeEnable_(true),
      directoryOption_(0),
      maxFileCount_(0),
      incrementDirectory_(false),
      fileCount_(0),
      directoryIndex_(1) {
    // 初始化FTP客户端
}

FtpClientManager::~FtpClientManager() {
    stop();
}

void FtpClientManager::start() {
    running_ = true;
    workerThread_ = std::thread(&FtpClientManager::threadFunc, this);
}

void FtpClientManager::stop() {
    end_ = true;
    fifoCondVar_.notify_one();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void FtpClientManager::threadFunc() {
    logInfo("FtpClientManager thread started.");

    while (!end_) {
        if (needRelogin_ || !ftpClient_.is_connected()) {
            logInfo("Not logged in. Attempting to login...");
            rootDirChangeEnable_ = true;
            ftpClient_.disconnect();
            setFtpState("Not logged in");
            if (!login()) {
                logError("Failed to login to FTP server.");
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
            needRelogin_ = false;
            logInfo("Successfully logged in to FTP server.");
        }

        if (ftpClient_.is_connected()) {
            std::unique_lock<std::mutex> lock(fifoMutex_);
            if (ftpTrans_.fifoQueue_.empty()) {
                fifoCondVar_.wait_for(lock, std::chrono::milliseconds(5));

                idleCount_++;
                // 空闲时每隔1秒进行一次连接检测
                if (idleCount_ >= 200) {
                    idleCount_ = 0;
                    lock.unlock();
                    performNoopTest();
                }
            } else {
                FtpFifoParam ftpParam = ftpTrans_.fifoQueue_.front();
                ftpTrans_.fifoQueue_.pop();
                lock.unlock();

                handleFifoData(ftpParam);
            }
        } else {
            // 清空FIFO队列
            std::lock_guard<std::mutex> lock(fifoMutex_);
            while (!ftpTrans_.fifoQueue_.empty()) {
                ftpTrans_.fifoQueue_.pop();
            }

            // 切碎睡眠时间，防止退出时等待过长
            for (int cnt = 0; cnt < 5 && !end_; ++cnt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    running_ = false;
    logInfo("FtpClientManager thread stopped.");
}

bool FtpClientManager::login() {
    ftp::replies replies = ftpClient_.connect(ftpHost_, ftpPort_);
    if (!replies.empty() && !replies.back().is_positive_completion()) {
        logError("Failed to connect to FTP server.");
        return false;
    }

    if (anonymousLogin_) {
        replies = ftpClient_.login("anonymous", "anonymous");
    } else {
        replies = ftpClient_.login(ftpUsername_, ftpPassword_);
    }

    if (!replies.empty() && !replies.back().is_positive_completion()) {
        logError("Failed to login with provided credentials.");
        return false;
    }

    // 设置初始目录为根目录
    ftpClient_.change_current_directory("/");

    return true;
}

void FtpClientManager::logout() {
    ftpClient_.logout();
    ftpClient_.disconnect();
}

void FtpClientManager::handleFifoData(FtpFifoParam& ftpParam) {
    if (ftpParam.used_len > 0) {
        // 设置文件名和目录名
        std::string fileName = ftpParam.file_name;
        std::string dirName = ftpParam.dir_name;

        // 处理目录
        if (!handleDirectory()) {
            setFtpState("Net error");
            rootDirChangeEnable_ = true;
            ftpClient_.disconnect();
            needRelogin_ = true;
            return;
        }

        // 上传文件
        try {
            // 创建输出流
            ftp::stream::input_stream src(ftpParam.buf.data(), ftpParam.used_len);

            // 组合远程路径
            std::string remotePath = currentDirectory_ + "/" + fileName;

            // 执行文件上传
            ftp::replies replies = ftpClient_.upload_file(src, remotePath);

            if (!replies.empty() && !replies.back().is_positive_completion()) {
                logError("Failed to upload file: " + fileName);
                setFtpState("Upload error");
            } else {
                logInfo("Successfully uploaded file: " + remotePath);
                ftpParam.send_state = 1; // FTP_SEND_END
                incrementFileCount();
            }
        } catch (const std::exception& e) {
            logError(std::string("Exception during file upload: ") + e.what());
            setFtpState("Exception error");
        }
    } else {
        logError("No data to send.");
    }
}

void FtpClientManager::performNoopTest() {
    try {
        ftp::reply reply = ftpClient_.send_noop();
        if (!reply.is_positive_completion()) {
            logError("Noop test failed, need to relogin.");
            needRelogin_ = true;
        } else {
            logInfo("Noop test succeeded.");
        }
    } catch (const std::exception& e) {
        logError(std::string("Exception during noop test: ") + e.what());
        needRelogin_ = true;
    }
}

void FtpClientManager::setFtpState(const std::string& state) {
    // 可以在这里更新状态显示或记录日志
    logInfo("FTP State: " + state);
}

void FtpClientManager::logInfo(const std::string& message) {
    // 这里可以使用更高级的日志库
    std::cout << "[INFO] " << message << std::endl;
}

void FtpClientManager::logError(const std::string& message) {
    // 这里可以使用更高级的日志库
    std::cerr << "[ERROR] " << message << std::endl;
}

bool FtpClientManager::handleDirectory() {
    if (directoryOption_ == 0) {
        currentDirectory_ = "/";
        return true;
    }

    if (directoryOption_ == 1) {
        currentDirectory_ = "2024_" + std::to_string(directoryIndex_);
        if (!changeOrCreateDirectory(currentDirectory_)) {
            return false;
        }

        if (maxFileCount_ > 0 && fileCount_ >= maxFileCount_) {
            if (incrementDirectory_) {
                directoryIndex_++;
                fileCount_ = 0;
                currentDirectory_ = "2024_" + std::to_string(directoryIndex_);
                if (!changeOrCreateDirectory(currentDirectory_)) {
                    return false;
                }
            } else {
                logError("File count has reached the maximum limit.");
                return false;
            }
        }
    } else if (directoryOption_ == 2) {
        currentDirectory_ = customDirectoryName_;
        if (!changeOrCreateDirectory(currentDirectory_)) {
            return false;
        }
    }
    return true;
}

bool FtpClientManager::changeOrCreateDirectory(const std::string& dirName) {
    ftp::reply reply = ftpClient_.change_current_directory(dirName);
    if (reply.is_positive_completion()) {
        return true;
    } else {
        reply = ftpClient_.create_directory(dirName);
        if (!reply.is_positive_completion()) {
            logError("Failed to create directory: " + dirName);
            return false;
        }
        reply = ftpClient_.change_current_directory(dirName);
        if (!reply.is_positive_completion()) {
            logError("Failed to change to directory: " + dirName);
            return false;
        }
        return true;
    }
}

void FtpClientManager::incrementFileCount() {
    if (directoryOption_ == 1 && maxFileCount_ > 0) {
        fileCount_++;
    }
}

// 设置目录选项
void FtpClientManager::setDirectoryOption(int option, const std::string& customName = "",
                                          size_t maxFiles = 0, bool incrementEnable = false) {
    directoryOption_ = option;
    customDirectoryName_ = customName;
    maxFileCount_ = maxFiles;
    incrementDirectory_ = incrementEnable;
    fileCount_ = 0;
    directoryIndex_ = 1;
    currentDirectory_ = "/";
}

// 设置FTP连接参数
void FtpClientManager::setFtpConnectionParameters(const std::string& host, uint16_t port,
                                                  bool anonymous, const std::string& username = "",
                                                  const std::string& password = "") {
    ftpHost_ = host;
    ftpPort_ = port;
    anonymousLogin_ = anonymous;
    ftpUsername_ = username;
    ftpPassword_ = password;
}