#include <unistd.h>
#include <regex>
#include "ftptrans.h"

#include "simple_fifo.h"
#include "osal_misc.h"
#include "sc_errno.h"
#include "sensor_capability.h"
#include "log/log.h"
#include "algoutils.h"

#define DEBUG_GLOBAL_MDC_STRING "CFtpctransModule"
#define FTPTRANS_MOUDLE_ENABLE	"AlgoEnable"
#define FTPTRANS_TXT_TRANS_ENABLE "TxtTransferEnable"
#define FTPTRANS_TEXT_FILE_FORMAT "TextFileFormat"
#define FTPTRANS_TEXT_TIMESTAMP_ENABLE "TextTimestampEnable"
#define FTPTRANS_TEXT_FILE_NAME "TextFileName"
#define FTPTRANS_TEXT_RETENTION_POLICY "TextRetentionPolicy"
#define FTPTRANS_TEXT_RETENTION_COUNT "TextRetentionCount"
#define FTPTRANS_TEXT_RETENTION_TIME_RANGE_SEC "TextRetentionTimeRangeSec"
#define FTPTRANS_TEXT_REFRESH_STEP "TextRefreshStep"
#define FTP_TRANS_ROOT_DIR_REGULAR_EXP        "^((./){1}[0-9A-Za-z/_]{0,29})$"

#define I_FTP_SUB_STATUS        "SINGLE_ftp_sub_status"
#define I_FTP_SUB_FILE_NAME     "SINGLE_ftp_sub_filename"
#define I_FTP_SUB_DIR_NAME      "SINGLE_ftp_sub_dirname"
#define I_FTP_SUB_TXT_INFO      "SINGLE_ftp_sub_txt_info"

CFtptransModule::CFtptransModule()
	: m_pMessageObj(nullptr),
	  m_nSaveStrategy(0),
	  m_nProcessStatus(0),
	  m_nTimestampEnable(0),
	  m_nProcessStatusEnable(0),
	  m_nFrameIdEnable(0),
	  m_nFrameId(0),
	  m_nTransLogEnable(0),
	  m_nTransportType(TO_JPG),
	  m_nDirType(dirNull),
	  m_nFilenameStrategy(FN_DEFAULT)
{
}

CFtptransModule::~CFtptransModule()
{
	DeInit();
}

int CFtptransModule::Process(IN void* hInput, IN void* hOutput)
{
	std::string strTxtInfo;
	char* imageData = NULL;

	int nErrCode = IMVS_EC_OK;
	int nRealImgWidth = 0;
	int nRealImgHeight = 0;
	int status = 0;
	int sub_status = 1;
	int imageDataLen = 0;
	int dataCount = 0;
	struct FtpFifoParam param = {0};
	int MoudleEnable = 0;
	int TxtTransEnable = 0;
	ScFramePtr pFrame;
	std::vector<std::string> strValVec;
	std::vector<std::string> strTxtVec;
	std::vector<int> input_sub_i_vec;

	if ((NULL == hInput) || (NULL == hOutput))
	{
		return IMVS_EC_ALGO_PARAM;
	}

	try
	{
		MoudleEnable = m_paramManage->GetParam<int>(FTPTRANS_MOUDLE_ENABLE);
		TxtTransEnable = m_paramManage->GetParam<int>(FTPTRANS_TXT_TRANS_ENABLE);
	}
	catch (const std::invalid_argument& e)
	{
		LOGE("Get Param Filed %s \n\n", e.what());
		return IMVS_EC_PARAM;
	}

	UP_PSTIME();
	
	LOGD("ftptrans Process start\n");
	do
	{
		if (0 == MoudleEnable)
		{
			break;
		}
		
		pFrame =  VM_M_Get_Frame_ByID(hInput, 0);
		if (!pFrame)
		{
			status = IMVS_EC_ALGO_NO_DATA;
			LOGE("get subscribe failed, no data\r\n");
			break;
		}
	
		if (m_nTransportType == TO_JPG)
		{
			Infra::DataBufferPtr pImgData = nullptr;
			nErrCode = pFrame->getBuffer("Jpg", pImgData);
			if (nErrCode)
			{
				status = IMVS_EC_ALGO_NO_DATA;
				LOGW("get Jpg faild, nErrCode = %d\r\n", nErrCode);
				break;
			}
			imageData = (char*)pImgData->getBuffer();
			imageDataLen = pImgData->getLen();
		}
		else
		{
			AlgoImgInfo *pImgInfo = pFrame->getData<AlgoImgInfo>(I_INHERIT_RAW_IMAGE_RGB_P3, false);
		
			if (pImgInfo == nullptr || pImgInfo->data == nullptr)
			{
				status = IMVS_EC_ALGO_NO_DATA;
				LOGW("no %s img info in image frame\n", I_INHERIT_RAW_IMAGE_RGB_P3);
				break;
			}
			imageData = (char*)pImgInfo->data;
			imageDataLen = pImgInfo->len;
		}

		nErrCode = pFrame->getIntVal(I_INHERIT_REAL_IMG_W, nRealImgWidth);
		if (IMVS_EC_OK != nErrCode)
		{
			status = IMVS_EC_ALGO_NO_DATA;
			LOGE("get subscribe failed, no data\r\n");
			break;
		}

		nErrCode = pFrame->getIntVal(I_INHERIT_REAL_IMG_H, nRealImgHeight);
		if (IMVS_EC_OK != nErrCode)
		{
			status = IMVS_EC_ALGO_NO_DATA;
			LOGE("get subscribe failed, no data\r\n");
			break;
		}

		if (VM_M_GetInt_Dynamic(hInput, I_FTP_SUB_STATUS, dataCount, sub_status, input_sub_i_vec) != IMVS_EC_OK)
		{
			status = IMVS_EC_ALGO_NO_DATA;
			LOGE("get subscribe failed, no data\r\n");
			break;
		}
		
		pFrame =  VM_M_Get_Frame_ByID(hInput, 0);
		if (!pFrame)
		{
			status = IMVS_EC_ALGO_NO_DATA;
			LOGE("get subscribe failed, no data\r\n");
			break;
		}
		if (pFrame->getIntVal("SINGLE_frame_cnt", m_nFrameId))
		{
			status = IMVS_EC_ALGO_NO_DATA;
			LOGE("get subscribe failed, no data\r\n");
			break;
		}

		if (dirSubscribe == m_nDirType)
		{
			if (VM_M_GetString_Dynamic(hInput, I_FTP_SUB_DIR_NAME, dataCount, m_strSubscribeDir, strValVec) != IMVS_EC_OK)
			{
				status = IMVS_EC_ALGO_NO_DATA;
				LOGE("get subscribe failed, no data\r\n");
				break;
			}
		}

		if (FN_SUB == m_nFilenameStrategy)
		{
			if (VM_M_GetString_Dynamic(hInput, I_FTP_SUB_FILE_NAME, dataCount, m_strSubFilename, strValVec) != IMVS_EC_OK)
			{
				status = IMVS_EC_ALGO_NO_DATA;
				LOGE("get subscribe failed, no data\r\n");
				break;
			}
		}

		if (TxtTransEnable)
		{
			if (VM_M_GetString_Dynamic(hInput, I_FTP_SUB_TXT_INFO, dataCount, strTxtInfo, strTxtVec) != IMVS_EC_OK)
			{
				status = IMVS_EC_ALGO_NO_DATA;
				LOGE("get subscribe failed, no data\r\n");
				break;
			}
			m_pMessageObj->enqueueTextData(strTxtInfo);
		}

		if ((NULL == imageData) || (imageDataLen > (int)(SENSOR_SIZE + BMP_EXTRA_SIZE))
			|| (false == m_pMessageObj->isConnect()))
		{
			LOGE("ftp connect:%d, imageDataLen:%d\r\n", m_pMessageObj->isConnect(), imageDataLen);
			status = IMVS_EC_ALGORITHM_DATA_SIZE;
			break;
		}
		else if (((OK_SAVE_STRATEGY == m_nSaveStrategy) && (sub_status))
						|| ((NG_SAVE_STRATEGY == m_nSaveStrategy) && (0 == sub_status))
						|| (OK_NG_SAVE_STRATEGY == m_nSaveStrategy))
		{
			m_nProcessStatus = sub_status;

			param.image.width = nRealImgWidth;
			param.image.height = nRealImgHeight;
			param.image.step[0] = nRealImgWidth;
			param.image.data[0] = imageData;
			param.image.format = (nRealImgWidth * nRealImgHeight == imageDataLen) 
									? HKA_IMG_MONO_08 : HKA_IMG_RGB_RGB24_P3;

			param.type = m_nTransportType;
			param.usedLen = imageDataLen;
			status = PrepareFileName((char *)param.fileName, sizeof(param.fileName),
									 (char *)param.dirName, sizeof(param.dirName));
			m_pMessageObj->enqueueFtpData(&param);
		}
		else
		{
			LOGI("not save. return. save strategy:%d sub_status:%d\n", m_nSaveStrategy, sub_status);
		}
	}while(0);
	nErrCode = status;
	
	ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
	pOutFrame->setVal("SINGLE_status", (0 == status) ? 1 : 0);
	pOutFrame->setVal("SINGLE_param_status", 0, (0 == status) ? 1 : 0);

	LOGD("ftptrans Process end\n");

	UP_PTIME();
	
	return nErrCode;

}

int CFtptransModule::SetPwd(IN const char* szPwd)
{
	snprintf(m_moduleName, sizeof(m_moduleName), "%s", ALGO_NAME);
	m_pwd = szPwd;
	return CAlgo::Init();
}

int CFtptransModule::GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
{
	if (szParamName == NULL || strlen(szParamName) == 0 || pBuff == NULL || nBuffSize <= 0 || pDataLen == NULL)
	{
		return IMVS_EC_PARAM;
	}

	auto value = m_paramManage->GetParam(szParamName);
	snprintf(pBuff, nBuffSize, "%s", value.c_str());
	*pDataLen = strlen(pBuff);
	
	return IMVS_EC_OK;
}

int CFtptransModule::SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{
	if (szParamName == NULL || strlen(szParamName) == 0 || pData == NULL || nDataLen == 0)
	{
		return IMVS_EC_PARAM;
	}

	int nErrCode = m_paramManage->CheckParam(szParamName, pData);
	if (IMVS_EC_OK != nErrCode)
	{
		LOGE("SetParam %s value %s is not valid\r\n", szParamName, pData);
	}
	else
	{
		LOGI("SetParam %s: %s\r\n", szParamName, pData);
		nErrCode = SetmoduParaHandle(szParamName, pData, nDataLen);
		if (IMVS_EC_OK == nErrCode || IMVS_EC_ALGO_PARAM_NOT_FOUND == (unsigned int)nErrCode)
		{
			// 更新内存参数
			nErrCode = m_paramManage->SetParam(szParamName, pData);
			if (IMVS_EC_OK != nErrCode)
			{
				LOGE("SetParam failed, ret = %d\r\n", nErrCode);
			}
			
			nErrCode = m_paramManage->SaveFile();
			if (IMVS_EC_OK != nErrCode)
			{
				LOGE("SaveFile failed, nRet = %d\r\n", nErrCode);
			}
		}

	}

	return nErrCode;
}

int CFtptransModule::InitAlgoPrivate()
{
	int nErrCode = IMVS_EC_OK;

	do
	{
		if (NULL == m_pMessageObj)
		{
			m_pMessageObj = new FtpClientManager;
			if (NULL == m_pMessageObj)
			{
				LOGE("new Message object failed\n");
				nErrCode = IMVS_EC_FILE_FORMAT;
				break;
			}
		}
		else
		{
			m_pMessageObj->DeInit();
		}

		//设置模块参数
		auto callback = [&](std::string paramName,std::string paramValue)->int
		{
			nErrCode = SetParam(paramName.c_str(), paramValue.c_str(), paramValue.length());
			LOGE("set param %s value %s ret 0x%x\r\n", paramName.c_str(), paramValue.c_str(), nErrCode);
			if (IMVS_EC_OK != nErrCode)
			{
				LOGE("set param failed, nErrCode = 0x%x\n", nErrCode);
				//nErrCode = IMVS_EC_PARAM;
			}
			return IMVS_EC_OK;
		};
		nErrCode = m_paramManage->ForEach(callback);
		if(IMVS_EC_OK != nErrCode)
		{
			LOGE("set param failed, ret = 0x%x\n", nErrCode);
			nErrCode = IMVS_EC_PARAM;
			break;
		}

		//初始化ftp传输客户端
		nErrCode = m_pMessageObj->Init();
		if (IMVS_EC_OK != nErrCode)
		{
			LOGE("ftp init failed, ret %d\r\n", nErrCode);
			nErrCode = IMVS_EC_ALGO_PARAM;
			break;
		}

		m_pMessageObj->setLogId(m_nLogId);
	}while(0);

	return nErrCode;
}

int CFtptransModule::DeInit()
{
	FtpLogManager::getInstance().disable(m_pMessageObj);

	if (m_pMessageObj)
	{
		m_pMessageObj->DeInit();
		delete m_pMessageObj;
		m_pMessageObj = NULL;
	}

	return IMVS_EC_OK;	

}

int CFtptransModule::AlgoPlayCtrlPrivate(eALGO_PLAYCTRL ePlayCtrl)
{
	if (m_pMessageObj != nullptr && ALGO_PLAY_STOP == m_ePlayStatus)
	{
		m_pMessageObj->triggerTextFinalRefresh();
	}

	if (!m_nTransLogEnable)
	{
		return IMVS_EC_OK;
	}

	if (ALGO_PLAY_CONTINUE == m_ePlayStatus)
	{
		FtpLogManager::getInstance().cancelDelayedTransfer();
	}
	else if (ALGO_PLAY_STOP == m_ePlayStatus)
	{
		FtpLogManager::getInstance().triggerLogTransferWithDelay();
	}

	return IMVS_EC_OK;
}

int CFtptransModule::SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{
	if(NULL == szParamName || NULL == pData)
	{
		return IMVS_EC_NULL_PTR;
	}

	int nErrCode = IMVS_EC_OK;
	if (0 == strcmp(szParamName, "HostIp"))
	{
		if (false == CAlgoUtils::CheckIpValid(std::string(pData))){
			return IMVS_EC_COMMU_INVALID_ADDRESS;
		}

		m_pMessageObj->setAddr(pData);
	}
	else if (0 == strcmp(szParamName, "HostPort"))
	{
		m_pMessageObj->setPort(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "UserName"))
	{
		m_pMessageObj->setUsername(pData);
	}
	else if (0 == strcmp(szParamName, "Password"))
	{
		m_pMessageObj->setPassword(pData);
	}
	else if (0 == strcmp(szParamName, "AnonymousLoginsEnable"))
	{
		m_pMessageObj->setAnonymousLogin(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "SaveStrategy"))
	{
		m_nSaveStrategy = (atoi(pData));
	}
	else if (0 == strcmp(szParamName, "FrameIdEnable"))
	{
		m_nFrameIdEnable = (atoi(pData));
	}
	else if (0 == strcmp(szParamName, "TransportType"))
	{
		m_nTransportType = (atoi(pData));
	}
	else if (0 == strcmp(szParamName, "TimeStampEnable"))
	{
		m_nTimestampEnable = (atoi(pData));
	}
	else if (0 == strcmp(szParamName, "ProcessStatusEnable"))
	{
		m_nProcessStatusEnable = (atoi(pData));
	}
	else if (0 == strcmp(szParamName, "StartEndText"))
	{
		m_strStartEnd = std::string(pData);
	}
	else if (0 == strcmp(szParamName, "SeperatorText"))
	{
		m_strSeperator = std::string(pData);
	}
	else if (0 == strcmp(szParamName, "SINGLE_ftp_sub_status"))
	{
		//snprintf(ftp_cli_man.output_condition, sizeof(ftp_cli_man.output_condition), "%s", pData);
	}
	else if (0 == strcmp(szParamName, "FtpLinkCheck"))
	{
		LOGI("FtpLinkCheck enter\n");
		if (!m_pMessageObj->isConnect() || m_pMessageObj->noopCheckAsync())
		{
			nErrCode = SC_EC_ALGO_PARAM_FTP_LINK_FAIL;
		}
		LOGI("FtpLinkCheck level\n");
	}
	else if (0 == strcmp(szParamName, "DirectoryType"))
	{
		m_nDirType = static_cast<eDirectoryOption>(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "FileNameStrategy"))
	{
		m_nFilenameStrategy = static_cast<eFileNameOption>(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "SINGLE_ftp_sub_filename"))
	{
		m_strSubFilename = std::string(pData);
	}
	else if (0 == strcmp(szParamName, "CustomFileName"))
	{
		m_strCustomFilename = std::string(pData);
	}
	else if (0 == strcmp(szParamName, "SINGLE_ftp_sub_dirname"))
	{
		m_strSubscribeDir = std::string(pData);
	}
	else if (0 == strcmp(szParamName, "CustomDirName"))
	{
		m_strCustomDir = std::string(pData);
	}
	else if(0 == strcmp(szParamName, "RootDirectory"))
	{
		if (false == IsStringLegal(pData, FTP_TRANS_ROOT_DIR_REGULAR_EXP))
		{
			LOGE("invalid root directory[%s]\n", pData);
			return SC_EC_MODULE_STR_REGULAR_EXP;
		}
		m_pMessageObj->setRootDir(pData);
	}
	else if (0 == strcmp(szParamName, "LogTransferEnable"))
	{
		if (atoi(pData))
		{
			if (!FtpLogManager::getInstance().enable(m_pMessageObj, m_nLogId))
			{
				LOGE("FtpLogManager is already enabled by another ftpclient instance.\n");
				return IMVS_EC_ALGO_RESOURCE_CREATE;
			}
		}
		else
		{
			FtpLogManager::getInstance().disable(m_pMessageObj);
		}
		m_nTransLogEnable = atoi(pData);
	}
	else if(0 == strcmp(szParamName, FTPTRANS_TXT_TRANS_ENABLE))
	{
		m_pMessageObj->setTextTransEnable(atoi(pData));
	}
	else if (0 == strcmp(szParamName, FTPTRANS_TEXT_FILE_FORMAT))
	{
		m_pMessageObj->setTextFileFormat(atoi(pData));
	}
	else if (0 == strcmp(szParamName, FTPTRANS_TEXT_TIMESTAMP_ENABLE))
	{
		m_pMessageObj->setTextTimestampEnable(atoi(pData));
	}
	else if (0 == strcmp(szParamName, FTPTRANS_TEXT_FILE_NAME))
	{
		m_pMessageObj->setTextFileName(pData);
	}
	else if (0 == strcmp(szParamName, FTPTRANS_TEXT_RETENTION_POLICY))
	{
		m_pMessageObj->setTextRetentionPolicy(atoi(pData));
	}
	else if (0 == strcmp(szParamName, FTPTRANS_TEXT_RETENTION_COUNT))
	{
		m_pMessageObj->setTextRetentionCount(strtoull(pData, nullptr, 10));
	}
	else if (0 == strcmp(szParamName, FTPTRANS_TEXT_RETENTION_TIME_RANGE_SEC))
	{
		m_pMessageObj->setTextRetentionTimeRangeSec(strtoll(pData, nullptr, 10));
	}
	else if (0 == strcmp(szParamName, FTPTRANS_TEXT_REFRESH_STEP))
	{
		m_pMessageObj->setTextRefreshStep(strtoull(pData, nullptr, 10));
	}
	else
	{
		nErrCode = IMVS_EC_ALGO_PARAM_NOT_FOUND;
	}

	return nErrCode;	
}

int CFtptransModule::PrepareFileName(IN char pFileName[], IN int nFileLen, IN char pDirName[], IN int nDirLen)
{
#define	SEPERATOR_PTR (strlen(pFileName) == m_strStartEnd.length() ? "" : m_strSeperator.c_str())

	int nErrCode = IMVS_EC_OK;
	char time_buf[FTP_STATE_MAX_LENGTH] = {0};
	struct osal_rtc cur_rtc = {0};

	if ((NULL == pFileName) || (NULL == pDirName) || (0 == nFileLen) || (0 == nDirLen))
	{
		return IMVS_EC_NULL_PTR;
	}

	pFileName[0] = 0; /* clean previous string, it's save & fast, because we only use snprintf */
	pDirName[0] = 0; /* clean previous string, it's save & fast, because we only use snprintf */
	
	nErrCode = osal_get_systime(&cur_rtc);
	if (nErrCode < 0)
	{
		LOGE("osal_get_systime error ret = %d\n", nErrCode);
		return nErrCode;
	}

	snprintf(time_buf, sizeof(time_buf), "%04u_%02u_%02u_%02u_%02u_%02u", \
			 cur_rtc.year, cur_rtc.month, cur_rtc.day, cur_rtc.hour, cur_rtc.minute, cur_rtc.second);

	if (dirNull == m_nDirType)
	{
	}
	else if (dirDefault == m_nDirType)
	{
		snprintf(pDirName + strlen(pDirName), nDirLen - strlen(pDirName), "%04u_%02u_%02u", cur_rtc.year, cur_rtc.month, cur_rtc.day);
	}
	else if (dirCustom == m_nDirType)
	{
		snprintf(pDirName + strlen(pDirName), nDirLen - strlen(pDirName), "%s", m_strCustomDir.c_str());
	}
	else if (dirSubscribe == m_nDirType)
	{
		snprintf(pDirName + strlen(pDirName), nDirLen - strlen(pDirName), "%s", m_strSubscribeDir.c_str());
	}

	snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), "%s", m_strStartEnd.c_str());

	if (FN_DEFAULT == m_nFilenameStrategy)
	{
    	if (0 != m_nFrameIdEnable)
    	{
    		snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), "%s%u", SEPERATOR_PTR, m_nFrameId);
    	}

    	if (0 != m_nProcessStatusEnable)
    	{
    		snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), "%s%s", SEPERATOR_PTR, (m_nProcessStatus ? "OK" : "NG"));
    	}
	}
	else if (FN_SUB == m_nFilenameStrategy)
	{
		snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), "%s%s", SEPERATOR_PTR, m_strSubFilename.c_str());
	}
	else if (FN_CUSTOM == m_nFilenameStrategy)
	{
		snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), "%s%s", SEPERATOR_PTR, m_strCustomFilename.c_str());
	}

	if (0 != m_nTimestampEnable)
	{
		snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), "%s%s", SEPERATOR_PTR, time_buf);
	}

	if (!m_strStartEnd.empty())
	{
		snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), "%s", m_strStartEnd.c_str());
	}

	snprintf(pFileName + strlen(pFileName), nFileLen - strlen(pFileName), 
			"%s", (TO_JPG == m_nTransportType) ? ".jpg" : ".bmp");

    while (*pFileName) {
        if (strchr(m_szSpecialCharSet, *pFileName))
            *pFileName = '&';
        pFileName++;
    }

	return nErrCode;
}

bool CFtptransModule::IsStringLegal(const char* szFileName, const char* szPattern)
{
	if (NULL == szFileName || NULL == szPattern || *szFileName == '\0')
	{
		return false;
	}

    try {
        std::regex rgx(szPattern);
        if (std::regex_match(szFileName, rgx))
            return true;
		LOGE("the format of '%s' is illegal!\n", szFileName);
    } catch(const std::regex_error& e) {
		LOGE("regex_error caught: %s\n", e.what());
    }

	return false;
}


CAbstractUserModule* CreateModule(void* hModule)
{
	assert(hModule != NULL);

	CAbstractUserModule* pUserModule = new CFtptransModule;

	return pUserModule;
}

void DestroyModule(void* hModule, CAbstractUserModule* pUserModule)
{
	assert(hModule != NULL);
	assert(pUserModule != NULL);

	delete pUserModule;
	pUserModule = NULL;
}
