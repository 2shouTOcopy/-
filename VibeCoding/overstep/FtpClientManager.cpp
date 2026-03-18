/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   ftp client manager class
  *
  * @author  zhoufeng20
  * @date    2024/08/27
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2024/08/27  |V1.0.0  |zhoufeng20           |创建代码文档
  * @warning 
  */

#include <thread>
#include <iomanip>
#include <fstream>
#include <future>
#include <cstdint>

#include "FtpClientManager.h"
#include "FtpClientMonitor.h"
#include "mm.h"
#include "bmp.h"
#include "net.h"
#include "utils.h"
#include "algo_common.h"
#include "sensor_capability.h"
#include "infra/ToolFunc.h"
#include "parameter/IParamCtrl.h"
#include "thread/ThreadApi.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "log/log.h"
#include "raw.h"

#define FTP_CFG_ADDR_OK                                      0
#define FTP_CFG_PORT_OK                                      1
#define FTP_CFG_NAME_OK                                      2
#define FTP_CFG_PASS_OK                                      3

#define FTP_CFG_ALL_OK (                                     \
								BIT_MASK(FTP_CFG_ADDR_OK)    \
								| BIT_MASK(FTP_CFG_PORT_OK)  \
								| BIT_MASK(FTP_CFG_NAME_OK)  \
								| BIT_MASK(FTP_CFG_PASS_OK))

void* ftpClientProcThread(void* argv)
{
	FtpClientManager* instance = (FtpClientManager*)argv;
	instance->ftpClientProc();
	return nullptr;
}

FtpClientManager::FtpClientManager()
	: m_nLogId(0),
		  m_nImgDataSize(0),
		  m_nConvertBufSize(0),
		  m_pConvertImageBuf(nullptr),
		  m_fifoArray(nullptr),
		  m_bRunning(false),
	  m_bNeedRelogin(false),
	  m_bEnd(false),
	  m_bRootDirChange(true),
	  m_bTextInit(false)
{
	memset(&m_queue, 0, sizeof(m_queue));
}

FtpClientManager::~FtpClientManager()
{
	DeInit();
}

void FtpClientManager::setLogId(int nLogId)
{
	m_nLogId = nLogId;

	std::string logFile = "/mnt/log/" + std::to_string(nLogId) + "ftp.log";
	m_ftpClient.add_observer(std::make_shared<FtpMonitor>(logFile));
}

int FtpClientManager::enqueueFtpData(const struct FtpFifoParam *data)
{
	if (NULL == data)
	{
		LOGE("invalid data is null!\r\n");
		return IMVS_EC_NULL_PTR;
	}

	std::lock_guard<std::mutex> lock(m_queueMutex);
	struct FtpFifoParam *param = (struct FtpFifoParam*)sfifo_in_addr(&m_queue);
	if (NULL == param)
	{
		return IMVS_EC_NULL_PTR;
	}

	param->usedLen = data->usedLen;
	param->type = data->type;
	snprintf(param->fileName, FILE_NAME_MAXSIZE, "%s", data->fileName);
	snprintf(param->dirName, DIR_NAME_MAXSIZE, "%s", data->dirName);

	param->image.format = data->image.format;
	param->image.width = data->image.width;
	param->image.height = data->image.height;
	param->image.step[0] = data->image.step[0];
	memcpy(param->image.data[0], data->image.data[0], data->usedLen);

	sfifo_in_skip(&m_queue);

	return IMVS_EC_OK;
}

void FtpClientManager::setTextTransEnable(bool enable)
{
	if (enable && !m_bTextInit)
	{
		m_lastTxtTime = std::chrono::steady_clock::now();
		m_bTextInit = true;
	}
}

void FtpClientManager::enqueueTextData(const std::string& text)
{
	std::lock_guard<std::mutex> lock(m_txtMutex);
	m_strTxtBuffer.append(text).append("\r\n");
}

void FtpClientManager::sendTextData()
{
	if (!m_bTextInit) return;

	std::unique_lock<std::mutex> lock(m_txtMutex, std::try_to_lock);
	if (!lock.owns_lock()) return;

	const auto now = std::chrono::steady_clock::now();

	const bool timeoutTrigger = (now - m_lastTxtTime) >= std::chrono::minutes(1);
	const bool hasContent = !m_strTxtBuffer.empty();

	if (timeoutTrigger && hasContent)
	{
		const auto sysNow = std::chrono::system_clock::now();
		const auto t = std::chrono::system_clock::to_time_t(sysNow);
		std::stringstream filename;
		filename << std::put_time(std::localtime(&t), "%Y_%m_%d_%H_%M.txt");

		FtpFifoParam param = {0};
		param.type = TO_TXT;
		const size_t bufSize = static_cast<size_t>(m_nImgDataSize);
		param.usedLen = std::min(m_strTxtBuffer.size(), bufSize);
		param.image.data[0] = (void*)m_strTxtBuffer.data();
		snprintf(param.fileName, FILE_NAME_MAXSIZE, "%s", filename.str().c_str());

		enqueueFtpData(&param);

		m_strTxtBuffer.clear();
		m_lastTxtTime = now;
	}
}

void FtpClientManager::setAnonymousLogin(bool enable)
{
	if (enable)
	{
		m_cfgInfo.cfgState |= BIT_MASK(FTP_CFG_NAME_OK);
		m_cfgInfo.cfgState |= BIT_MASK(FTP_CFG_PASS_OK);
	}
	else
	{
		if (m_cfgInfo.username.empty())
		{
			m_cfgInfo.cfgState &= BIT_UNMASK(FTP_CFG_NAME_OK);
		}
		if (m_cfgInfo.password.empty())
		{
			m_cfgInfo.cfgState &= BIT_UNMASK(FTP_CFG_PASS_OK);
		}
		
	}

	if (enable != m_cfgInfo.anonymousLogin)
	{
		m_bNeedRelogin = true;
		m_cfgInfo.anonymousLogin = enable;
	}
}

void FtpClientManager::setUsername(const char *username)
{
	if (NULL == username)
	{
		m_cfgInfo.cfgState &= BIT_UNMASK(FTP_CFG_NAME_OK);
		return ;
	}

	m_cfgInfo.cfgState |= BIT_MASK(FTP_CFG_NAME_OK);

	if (std::string(username) != m_cfgInfo.username)
	{
		m_bNeedRelogin = true;
		m_cfgInfo.username = std::string(username);
	}
}

void FtpClientManager::setPassword(const char *password)
{
	if (NULL == password)
	{
		m_cfgInfo.cfgState &= BIT_UNMASK(FTP_CFG_PASS_OK);
		return ;
	}

	m_cfgInfo.cfgState |= BIT_MASK(FTP_CFG_PASS_OK);

	if (std::string(password) != m_cfgInfo.password)
	{
		m_bNeedRelogin = true;
		m_cfgInfo.password = std::string(password);
	}
}

void FtpClientManager::setAddr(const char *strAddr)
{
	unsigned int addr = inet_addr(strAddr);
	if (IPADDR_ANY == addr || IPADDR_NONE == addr)
	{
		m_cfgInfo.cfgState &= BIT_UNMASK(FTP_CFG_ADDR_OK);
		return ;
	}

	m_cfgInfo.cfgState |= BIT_MASK(FTP_CFG_ADDR_OK);

	if (std::string(strAddr) != m_cfgInfo.addr)
	{
		m_bNeedRelogin = true;
		m_cfgInfo.addr = std::string(strAddr);
	}
}

void FtpClientManager::setPort(unsigned short port)
{
	m_cfgInfo.cfgState |= BIT_MASK(FTP_CFG_PORT_OK);

	if (port != m_cfgInfo.port)
	{
		m_bNeedRelogin = true;
		m_cfgInfo.port = port;
	}
}

void FtpClientManager::setRootDir(const char* szPath)
{
	m_strRootDirClient = szPath;
	m_bRootDirChange = true;
}

bool FtpClientManager::getReLoginState()
{
	return m_bNeedRelogin;
}

bool FtpClientManager::isConnect()
{
	return m_ftpClient.is_connected();
}

void FtpClientManager::processQueue(struct simple_fifo *pQueue)
{
	try
	{
		struct FtpFifoParam *pParam = (struct FtpFifoParam *)sfifo_out_addr(pQueue);

		handleFifoData(pParam);

		sfifo_out_skip(pQueue);
	}
	catch (const std::exception &e)
	{
		LOGE("Exception during file upload: %s\n", e.what());
		logout();
		m_bNeedRelogin = true;
	}
}

bool FtpClientManager::performLogin()
{
	try
	{
		logout();
		return login();
	}
	catch (const std::exception &e)
	{
		LOGE("FTP exception during login: %s\n", e.what());
		return false;
	}
}

bool FtpClientManager::login()
{
	if (FTP_CFG_ALL_OK != m_cfgInfo.cfgState)
	{
		LOGD("FTP client incomplete cfg.\n");
		return false;
	}

	ftp::replies replies = m_ftpClient.connect(m_cfgInfo.addr, m_cfgInfo.port);

	if (!replies.get_replies().empty() && !replies.get_replies().back().is_positive())
	{
		LOGE("Failed to connect to FTP server.\n");
		return false;
	}

	if (m_cfgInfo.anonymousLogin)
	{
		replies = m_ftpClient.login("anonymous", "anonymous");
	}
	else
	{
		replies = m_ftpClient.login(m_cfgInfo.username, m_cfgInfo.password);
	}

	if (!replies.get_replies().empty() && !replies.get_replies().back().is_positive())
	{
		LOGE("Failed to login with provided credentials.\n");
		return false;
	}

	ftp::reply reply = m_ftpClient.get_current_directory();
	if (reply.is_negative())
	{
		LOGE("Failed get server root directory: %s\n", reply.get_status_string().c_str());
		return false;
	}

	m_strRootDirServer = m_utils.extractDirectory(reply.get_status_string());

	return true;
}

void FtpClientManager::logout()
{
	try
	{
		if (m_ftpClient.is_connected())
		{
			try
			{
				//scmvs自带服务器不支持重置指令
				//m_ftpClient.logout();

				m_ftpClient.disconnect(true);
			}
			catch (const std::exception &e)
			{
				LOGE("Graceful disconnect failed: %s\n", e.what());
				m_ftpClient.disconnect(false);
			}
		}
	}
	catch (const std::exception &e)
	{
		LOGE("Exception during logout: %s\n", e.what());
	}
}

std::unique_ptr<std::istream> FtpClientManager::makeIstreamByFormat(struct FtpFifoParam* pParam)
{
	if (nullptr == pParam || nullptr == pParam->image.data[0] || pParam->usedLen == 0)
	{
		LOGE("invalid ftp data, pParam=%p data=%p usedLen=%u\n",
			pParam, (pParam ? pParam->image.data[0] : nullptr), (pParam ? pParam->usedLen : 0));
		return nullptr;
	}

	if (m_nImgDataSize <= 0 || m_nConvertBufSize <= 0 || nullptr == m_pConvertImageBuf || nullptr == m_fifoArray || nullptr == m_fifoArray[0].image.data[0])
	{
		LOGE("ftp memory not ready, imgSize=%d convertSize=%d convertBuf=%p fifo=%p store=%p\n",
			m_nImgDataSize, m_nConvertBufSize, m_pConvertImageBuf, m_fifoArray,
			(m_fifoArray ? m_fifoArray[0].image.data[0] : nullptr));
		return nullptr;
	}

	if (pParam->usedLen > (uint32_t)m_nImgDataSize)
	{
		LOGE("ftp usedLen overflow, usedLen=%u storeSlot=%d type=%d file=%s\n",
			pParam->usedLen, m_nImgDataSize, pParam->type, pParam->fileName);
		return nullptr;
	}

	{
		uintptr_t out_addr = (uintptr_t)pParam->image.data[0];
		uintptr_t store_start = (uintptr_t)m_fifoArray[0].image.data[0];
		uintptr_t store_span = (uintptr_t)m_nImgDataSize * (uintptr_t)FTP_FIFO_DEPTH;
		uintptr_t store_end = store_start + store_span;
		uintptr_t out_end = out_addr + (uintptr_t)m_nImgDataSize;
		if ((store_end < store_start) || (out_end < out_addr)
			|| (out_addr < store_start) || (out_end > store_end))
		{
			LOGE("ftp output ptr invalid, out=%p outEnd=0x%zx store=[0x%zx,0x%zx)\n",
				pParam->image.data[0], (size_t)out_end, (size_t)store_start, (size_t)store_end);
			return nullptr;
		}
	}

	if (TO_LOG == pParam->type)
	{
		auto src = std::make_unique<std::ifstream>(pParam->fileName, std::ios_base::binary);
		if (!src->is_open())
		{
			LOGE("read %s ifstream failed!\n", pParam->fileName);
			return nullptr;
		}
		return std::move(src);
	}
	else if (TO_JPG == pParam->type || TO_TXT == pParam->type)
	{
		auto src = std::make_unique<std::stringstream>();
		src->write((char*)pParam->image.data[0], pParam->usedLen);
		return std::move(src);
	}
	else if (TO_BMP == pParam->type)
	{
		BMPENCPARAM strBmpEncParam = {0};
		HKA_IMAGE *img = &pParam->image;
		uint64_t pixel_count = (uint64_t)img->width * (uint64_t)img->height;
		uint64_t mono_need = pixel_count;
		uint64_t color_need = pixel_count * 3ULL;
		if ((0 == img->width) || (0 == img->height))
		{
			LOGE("invalid image size, width=%u height=%u file=%s\n", img->width, img->height, pParam->fileName);
			return nullptr;
		}

		if (mono_need > (uint64_t)m_nConvertBufSize || color_need > (uint64_t)m_nConvertBufSize)
		{
			LOGE("image size too large for convert buffer, mono_need=%llu color_need=%llu convert=%d\n",
				(unsigned long long)mono_need, (unsigned long long)color_need, m_nConvertBufSize);
			return nullptr;
		}

		if (HKA_IMG_RGB_RGB24_P3 == img->format)
		{
			if (pParam->usedLen < color_need)
			{
				LOGE("rgb image len too small, usedLen=%u need=%llu file=%s\n",
					pParam->usedLen, (unsigned long long)color_need, pParam->fileName);
				return nullptr;
			}

			int cvt_ret = imgConvertP3ToC3(pParam->image.data[0], pParam->usedLen, m_pConvertImageBuf, m_nConvertBufSize, pParam->image.width, pParam->image.height);
			if (0 != cvt_ret)
			{
				LOGE("imgConvertP3ToC3 failed, ret=%d len=%u w=%u h=%u\n", cvt_ret, pParam->usedLen, pParam->image.width, pParam->image.height);
				return nullptr;
			}
			strBmpEncParam.image_pixel_bit = COLOR_IMAGE_PIXEL_BIT;
			strBmpEncParam.input_len = (u32)color_need;
		}
		else
		{
			if (pParam->usedLen < mono_need)
			{
				LOGE("mono image len too small, usedLen=%u need=%llu file=%s\n",
					pParam->usedLen, (unsigned long long)mono_need, pParam->fileName);
				return nullptr;
			}

			memcpy(m_pConvertImageBuf, (void*)img->data[0], (size_t)mono_need);
			strBmpEncParam.image_pixel_bit = MONO_IMAGE_PIXEL_BIT;
			strBmpEncParam.input_len = (u32)mono_need;
		}
	
		strBmpEncParam.input_data = (void *)m_pConvertImageBuf;
		strBmpEncParam.image_height = img->height;
		strBmpEncParam.image_width = img->width;
		strBmpEncParam.output_bufsize = m_nImgDataSize;
		strBmpEncParam.output_data = img->data[0];
	
		int nErrCode = mono8_2_bmp(&strBmpEncParam);
		if (IMVS_EC_OK != nErrCode)
		{
			LOGE("mono8 convert bmp failed: %d\n", nErrCode);
			return nullptr;
		}

		pParam->usedLen = strBmpEncParam.output_len;
		auto src = std::make_unique<std::stringstream>();
		src->write((char*)pParam->image.data[0], pParam->usedLen);
		return std::move(src);
	}
	else
	{
		LOGE("Unsupported format: %d\n", pParam->type);
		return nullptr;
	}
	return nullptr;
}

void FtpClientManager::handleFifoData(struct FtpFifoParam *pParam)
{
	if (NULL == pParam || pParam->usedLen <= 0 || NULL == pParam->image.data[0])
	{
		LOGE("No data to send.\n");
		return ;
	}

	std::string fileName(pParam->fileName);
	std::string dirName(pParam->dirName);

	if (!handleDirectory(dirName))
	{
		m_bRootDirChange = true;
		return;
	}

	std::string remotePath;
	if (TO_LOG == pParam->type)
	{
		remotePath = m_strCurrentDir + m_utils.getFilename(fileName);
	}
	else
	{
		remotePath = m_strCurrentDir + fileName;
	}

	auto stream = makeIstreamByFormat(pParam);
	if (!stream)
	{
		LOGE("failed to create stream for file:%s\n", fileName.c_str());
		return;
	}

	ftp::istream_adapter adapter(*stream);

	ftp::replies replies = m_ftpClient.upload_file(adapter, remotePath);

	const std::vector<ftp::reply> & reply_list = replies.get_replies();
	if (!reply_list.empty() && !reply_list.back().is_positive())
	{
		LOGE("Failed to upload file: %s, reply: %s\n", remotePath.c_str(), 
			reply_list.back().get_status_string().c_str());
		throw std::runtime_error("Upload failed: " + reply_list.back().get_status_string());
	}
	else
	{
		LOGI("Successfully uploaded file: %s\n", remotePath.c_str());
	}
}

int FtpClientManager::noopCheckAsync()
{
	std::promise<int> promise;
	std::future<int> future = promise.get_future();

	{
		std::lock_guard<std::mutex> lock(m_taskMutex);
		m_taskQueue.emplace([this, &promise]() {
			try
			{
				ftp::reply reply = m_ftpClient.send_noop();
				if (!reply.is_positive())
				{
					LOGE("Noop asyn test failed, need to relogin.\n");
					m_bNeedRelogin = true;
					promise.set_value(IMVS_EC_PARAM);
				}
				else
				{
					LOGD("Noop asyn test succeeded.\n");
					promise.set_value(IMVS_EC_OK);
				}
			}
			catch (const std::exception &e)
			{
				LOGE("Exception during noop asyn test: %s\n", e.what());
				logout();
				m_bNeedRelogin = true;
				promise.set_value(IMVS_EC_COMMU_INVALID_ADDRESS);
			}
		});
	}

	return future.get();
}

int FtpClientManager::noopCheck()
{
	try
	{
		ftp::reply reply = m_ftpClient.send_noop();
		if (!reply.is_positive())
		{
			LOGE("Noop test failed, need to relogin.\n");
			m_bNeedRelogin = true;
			return IMVS_EC_PARAM;
		}
		else
		{
			LOGD("Noop test succeeded.\n");
		}
	}
	catch (const std::exception &e)
	{
		LOGE("Exception during noop test: %s\n", e.what());
		logout();
		m_bNeedRelogin = true;
		return IMVS_EC_PARAM;
	}
	
	return IMVS_EC_OK;
}

bool FtpClientManager::rootDirectoryChange()
{
	if (false == m_bRootDirChange)
	{
		return true;
	}

	ftp::reply reply = m_ftpClient.change_current_directory(m_strRootDirServer);
	if (!reply.is_positive())
	{
		LOGE("Failed return to root directory: %s\n", reply.get_status_string().c_str());
		return false;
	}

	std::istringstream dirStream(m_strRootDirClient);
	std::string segment;
	std::string currentPath = m_strRootDirServer;

	while (std::getline(dirStream, segment, '/'))
	{
		if (segment.empty() || segment == ".")
		{
			continue;
		}

		if (currentPath.back() != '/')
		{
			currentPath += "/";
		}
		currentPath += segment;

		ftp::reply reply = m_ftpClient.change_current_directory(currentPath);
		if (!reply.is_positive())
		{
			reply = m_ftpClient.create_directory(currentPath);
			if (!reply.is_positive() 
				&& (550 != reply.get_code())
				&& (521 != reply.get_code()))
			{
				LOGE("Failed to create directory: %s, %s\n", 
					currentPath.c_str(), reply.get_status_string().c_str());
				return false;
			}

			reply = m_ftpClient.change_current_directory(currentPath);
			if (!reply.is_positive())
			{
				LOGE("Failed to change to directory: %s, %s\n", 
					currentPath.c_str(), reply.get_status_string().c_str());
				return false;
			}
		}
	}

	m_bRootDirChange = false;
	return true;
}

bool FtpClientManager::createDirectory(const std::string& strDir)
{
	if (strDir.empty())
	{
		m_strCurrentDir.clear();
		return true;
	}

	ftp::reply reply = m_ftpClient.create_directory(strDir);
	if (!reply.is_positive() 
		&& (550 != reply.get_code())
		&& (521 != reply.get_code()))
	{
		LOGE("Failed to create directory %s:%s\n", 
			strDir.c_str(), reply.get_status_string().c_str());
		return false;
	}

	m_strCurrentDir = strDir + "/";
	return true;
}

bool FtpClientManager::handleDirectory(const std::string& strDirName)
{
	try
	{
		if (!rootDirectoryChange())
		{
			return false;
		}
		return createDirectory(strDirName);
	}
	catch (const std::exception& e)
	{
		LOGE("FTP exception caught: %s\n", e.what());
		logout();
		m_bNeedRelogin = true;
		return false;
	}

	return false;
}

void FtpClientManager::taskProc()
{
	std::function<void()> task;
	{
		std::lock_guard<std::mutex> lock(m_taskMutex);
		if (!m_taskQueue.empty())
		{
			task = std::move(m_taskQueue.front());
			m_taskQueue.pop();
		}
	}

	if (task)
	{
		task();
	}

	sendTextData();
}

void FtpClientManager::ftpClientProc()
{
	struct simple_fifo *pQueue = &m_queue;
	uint32_t idleCount = 0;

	thread_set_name("ftp_client");

	while (!m_bEnd)
	{
		if (m_bNeedRelogin)
		{
			LOGD("Need relogged in. Attempting to login...\n");
			m_bRootDirChange = true;

			if (!performLogin())
			{
				LOGE("Failed to login to FTP server.\n");
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
			else
			{
				m_bNeedRelogin = false;
				LOGI("Successfully logged in to FTP server.\n");
			}
		}

		taskProc();

		if (!m_ftpClient.is_connected())
		{
			sfifo_drain(pQueue);
			// 切碎睡眠时间，防止退出时等待过长
			for (int cnt = 0; cnt < 5 && !m_bEnd; ++cnt)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			//continue;
		}

		if (sfifo_is_empty(pQueue))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			idleCount++;
			// 空闲时每隔10秒进行一次连接检测
			if (idleCount >= 1000)
			{
				idleCount = 0;
				noopCheck();
			}
			continue;
		}
		processQueue(pQueue);
	}

	logout();

	m_bRunning = false;
	LOGI("FtpClientManager thread stopped.\n");
}

void FtpClientManager::DeInit()
{
	m_bEnd = true;

	while(m_bRunning)
	{
		usleep(10000);
	}

	MMZmemFree((void**)&m_pConvertImageBuf);
	m_nConvertBufSize = 0;

	if (m_fifoArray)
	{
		if (m_fifoArray[0].image.data[0])
		{
			MMZmemFree((void**)&(m_fifoArray[0].image.data[0]));
		}
		MMZmemFree((void**)&(m_fifoArray));
	}
}

int FtpClientManager::Init()
{
	m_fifoArray = (struct FtpFifoParam*)MMZmemAllocHigh(sizeof(struct FtpFifoParam) * FTP_FIFO_DEPTH, 
															8, (char*)"ftpmsg.ftp_fifo");
	if (NULL == m_fifoArray)
	{
		LOGE("malloc ftp fifo array memory failed\r\n");
		return -2;
	}

	memset(m_fifoArray, 0, sizeof(struct FtpFifoParam) * FTP_FIFO_DEPTH);

	int nSenSorSize = SENSOR_SIZE + BMP_EXTRA_SIZE;
	char* pStoreBuf = (char *)MMZmemAllocHigh(nSenSorSize * FTP_FIFO_DEPTH, 
												8, (char*)"ftpmsg.store_buf");
	if (NULL == pStoreBuf)
	{
		LOGE("malloc store fifo img memory [size:%d] failed\r\n", nSenSorSize);
		return IMVS_EC_OUTOFMEMORY;
	}
	memset(pStoreBuf, 0, nSenSorSize * FTP_FIFO_DEPTH);
	LOGI("init store buf size:%d, %p\n", nSenSorSize, pStoreBuf);

	for (int i = 0; i < FTP_FIFO_DEPTH; i++)
	{
		m_fifoArray[i].image.data[0] = pStoreBuf + i * nSenSorSize;
	}
	m_nImgDataSize = nSenSorSize;

	m_nConvertBufSize = SENSOR_SIZE;
	m_pConvertImageBuf = (char *)MMZmemAlloc(m_nConvertBufSize, 8, (char*)"ftpmsg.convert_img");
	if (NULL == m_pConvertImageBuf)
	{
		LOGE("malloc c3 img memory failed\r\n");
		return IMVS_EC_OUTOFMEMORY;
	}
	memset(m_pConvertImageBuf, 0, m_nConvertBufSize);

	sfifo_init(&m_queue, sizeof(struct FtpFifoParam), FTP_FIFO_DEPTH, (void*)(m_fifoArray));

	m_bEnd = false;
	pthread_t ftpCMangThread;
	int ret = thread_spawn_ex(&ftpCMangThread, 0, 
								SCHED_POLICY_RR,
								SCHED_PRI_HIGH_50,
								10 * 1024, 
								ftpClientProcThread, this);
	if (ret < 0)
	{
		LOGE("ftp_client data thread creation failed!\r\n");
		return -3;
	}

	m_bRunning = true;

	LOGI("ftp init ok!\r\n");

	return 0;
}

