#ifndef FTP_CLIENT_MANAGER_H
#define FTP_CLIENT_MANAGER_H

#include <atomic>
#include <vector>
#include <cstring>
#include <memory>
#include <string>
#include <iostream>
#include <chrono>
#include <mutex>
#include <queue>

#include <ftp/client.hpp>
#include <ftp/stream/istream_adapter.hpp>

#include "hka_types.h"
#include "simple_fifo.h"
#include "FtpClientUtils.h"

#define FILE_NAME_MAXSIZE 256
#define DIR_NAME_MAXSIZE 256

enum eTransportOption
{
	TO_JPG = 0,
	TO_BMP = 1,
	TO_LOG = 2,
	TO_TXT = 3
};

// 定义FTP FIFO参数结构体
struct FtpFifoParam 
{
	uint32_t usedLen;
	HKA_IMAGE image;
	char fileName[FILE_NAME_MAXSIZE];
	char dirName[DIR_NAME_MAXSIZE];
	int  type;
};

class FtpClientManager : public std::enable_shared_from_this<FtpClientManager> 
{
public:
	/**
	 * @brief FTP客户端配置信息
	 */
	typedef struct FtpClientConfig
	{
		std::string addr;					///< FTP服务器地址
		unsigned short port;				///< FTP服务器端口
		std::string username; 				///< FTP登录用户名
		std::string password; 				///< FTP登录密码
		unsigned int cfgState;				///< FTP配置状态
		bool anonymousLogin;				///< 匿名登录

		FtpClientConfig() : port(0), cfgState(0), anonymousLogin(false) {}
	} FtpClientConfig;


	static constexpr auto FTP_FIFO_DEPTH = 6; 

public:
	FtpClientManager();

	~FtpClientManager();

	int Init();

	void DeInit();

	void ftpClientProc();

	void setAnonymousLogin(bool enable);

	void setUsername(const char *username);

	void setPassword(const char *password);

	void setAddr(const char *strAddr);

	void setPort(unsigned short port);

	int enqueueFtpData(const struct FtpFifoParam *data);

	void enqueueTextData(const std::string& text);

	void setLogId(int nLogId);

	bool getReLoginState();

	bool isConnect();

	int noopCheckAsync();

	void setRootDir(const char* szPath);

	void setTextTransEnable(bool enable);

private:
	bool login();

	void logout();

	int noopCheck();

	void taskProc();

	void sendTextData();

	bool performLogin();

	void processQueue(struct simple_fifo *pQueue);

	void handleFifoData(struct FtpFifoParam *pParam);

	bool rootDirectoryChange();

	bool createDirectory(const std::string& strDir);

	bool handleDirectory(const std::string& strDirName);

	std::unique_ptr<std::istream> makeIstreamByFormat(struct FtpFifoParam* pParam);

	int m_nLogId;
	int m_nImgDataSize;
	int m_nConvertBufSize;
	char *m_pConvertImageBuf;

	ftp::client m_ftpClient;
	FtpClientUtils m_utils;

	FtpClientConfig m_cfgInfo;
	struct FtpFifoParam* m_fifoArray;
	struct simple_fifo m_queue;

	std::atomic<bool> m_bRunning;
	std::atomic<bool> m_bNeedRelogin;
	std::atomic<bool> m_bEnd;
	std::atomic<bool> m_bRootDirChange;

	std::string m_strRootDirServer;
	std::string m_strRootDirClient;
	std::string m_strCurrentDir;

	std::mutex m_taskMutex;
	std::mutex m_queueMutex;
	std::mutex m_txtMutex;

	std::atomic<bool> m_bTextInit;
	std::string m_strTxtBuffer;
	std::chrono::steady_clock::time_point m_lastTxtTime;
	
	std::queue<std::function<void()>> m_taskQueue;
};

#endif // FTP_CLIENT_MANAGER_HPP

