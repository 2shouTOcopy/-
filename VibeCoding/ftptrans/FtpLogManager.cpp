/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   ftp client log manager class
  *
  * @author  zhoufeng20
  * @date    2024/10/20
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2024/10/20  |V1.0.0  |zhoufeng20
  * @warning 
  */

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

#include "utils.h"
#include "osal_dir.h"
#include "osal_file.h"
#include "thread/ThreadApi.h"
#include "FtpClientManager.h"
#include "FtpLogManager.h"
#include "log_record/log_record_file.h"
#include "log/log.h"

FtpLogManager::FtpLogManager() 
	: m_pMessageObj(nullptr),
	  m_nLogId(0),
	  m_waitingForTrigger(false),
	  m_stop(false),
	  m_active(false),
	  m_cancelDelay(false)
{
	m_thread = std::thread(&FtpLogManager::processLogTransfer, this);
}

FtpLogManager::~FtpLogManager()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_condition.notify_one();
	if (m_thread.joinable())
	{
		m_thread.join();
	}
}

FtpLogManager& FtpLogManager::getInstance()
{
	static FtpLogManager instance;
	return instance;
}

bool FtpLogManager::enable(FtpClientManager* pClientInstance, int nLogId)
{
	if (m_pMessageObj != nullptr && m_pMessageObj != pClientInstance) 
	{
		return false;
	}

	start();

	if (!osal_is_dir_exist(LOG_RECORD_UNZIP_PATH))
	{
		if (osal_create_dir(LOG_RECORD_UNZIP_PATH) < 0)
		{
			LOGW("create dir %s error\n", LOG_RECORD_UNZIP_PATH);
		}
	}

	m_nLogId = nLogId;
	m_pMessageObj = pClientInstance;
	return true;
}

void FtpLogManager::disable(FtpClientManager* pClientInstance)
{
	if (m_pMessageObj == pClientInstance) 
	{
		stop();
		if (osal_remove_dir(LOG_RECORD_UNZIP_PATH) < 0)
		{
			LOGW("remove dir %s error\n", LOG_RECORD_UNZIP_PATH);
		}
		m_pMessageObj = nullptr;
	}
}

void FtpLogManager::triggerLogTransferWithDelay()
{
	FUNCTION_ENTER(I);
	if (m_waitingForTrigger.exchange(true))
	{
		return;
	}

	m_delayThread = std::thread([this] ()
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		// 等待 30 秒，或等待取消信号
		if (m_delayCondition.wait_for(lock, std::chrono::seconds(30), [this] { return m_cancelDelay; })) 
		{
			LOGE("triggerLogTransferWithDelay canceled\n");
			m_cancelDelay = false;
		} 
		else 
		{
			m_waitingForTrigger = false;
			lock.unlock();
			triggerLogTransfer(); 
		}
		m_waitingForTrigger = false;
	});

	if (m_delayThread.joinable())
	{
		m_delayThread.detach();
	}
}


void FtpLogManager::cancelDelayedTransfer()
{
	FUNCTION_ENTER(I);
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_waitingForTrigger)
	{
		m_cancelDelay = true;
		m_delayCondition.notify_one();
	}
}

void FtpLogManager::start()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_active = true;
	}
	m_condition.notify_one();
}

void FtpLogManager::stop()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_active = false;
	}
}

void FtpLogManager::triggerLogTransfer()
{
	FUNCTION_ENTER(I);

	if (!osal_is_dir_exist(LOG_RECORD_UNZIP_PATH))
	{
		LOGE("log dir %s is not found!\n", LOG_RECORD_UNZIP_PATH);
		return;
	}

	std::string strCmd = "rm -rf " + std::string(LOG_RECORD_UNZIP_PATH) + "*";
	if (utils_do_shell_cmd((char*)strCmd.c_str()) < 0)
	{
		LOGE("strCmd %s run failed!\n", strCmd.c_str());
		return;
	}

	std::string unzipCmd = "unzip -o " + std::string(LOG_RECORD_ZIP_PATH) 
							+ " -d " + std::string(LOG_RECORD_UNZIP_PATH);
	if (utils_do_shell_cmd((char*)unzipCmd.c_str()) < 0)
	{
		LOGE("unzipCmd %s run failed!\n", unzipCmd.c_str());
		return;
	}

	std::string dir;
	std::string logCurFile;
	std::list<std::string> listIdxFile;
	if (false == LogRecord::log_record_read_index_file(dir, listIdxFile, logCurFile))
	{
		LOGE("Get log record index file failed!\n");
		return;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto &logFile : listIdxFile)
	{
		if (m_transferredLogs.find(logFile) == m_transferredLogs.end())
		{
			std::string logFilePath = LOG_RECORD_UNZIP_PATH + logFile;
			if (osal_is_file_exist((char*)logFilePath.c_str()))
			{
				m_logQueue.push(logFilePath);
				m_transferredLogs.insert(logFile);
			}
		}
	}

	m_condition.notify_one();
	LOGI("triggerLogTransfer Leave %d\n", m_active.load());
}

void FtpLogManager::processLogTransfer()
{
	thread_set_name("ftp_log_thread");

	while (true)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_condition.wait(lock, [this]
						{ return (m_active && !m_logQueue.empty()) || m_stop; });

		if (m_stop)
		{
			break;
		}

		std::string logFilePath = m_logQueue.front();
		m_logQueue.pop();
		lock.unlock();

		LOGI("Transfer log:%s\n", logFilePath.c_str());
		
		struct FtpFifoParam stParam = {0};
		stParam.type    = TO_LOG;
		stParam.usedLen = logFilePath.length();
		stParam.image.data[0] = stParam.fileName;
		snprintf(stParam.fileName, FILE_NAME_MAXSIZE, "%s", logFilePath.c_str());

		if (nullptr == m_pMessageObj || !m_pMessageObj->isConnect())
		{
			m_transferredLogs.erase(m_utils.getFilename(logFilePath));
			continue;
		}

		if (m_pMessageObj->enqueueFtpData(&stParam))
		{
			LOGI("Failed enqueue log :%s, maybe fifo full!\n", stParam.fileName);
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			m_logQueue.push(logFilePath);
		}
	}
}

