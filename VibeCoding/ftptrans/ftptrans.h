/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   DL Classify algo main class
  *
  * @author  caopengfei5
  * @date    2019/10/30
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2019/10/30  |V1.0.0  |caopengfei5           |创建代码文档
  * @warning 
  */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string>
#include <unordered_map>

#include "VmModuleBase.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "VmTypeDef.h"
#include "algo_common.h"
#include "json_tool.h"
#include "FtpClientManager.h"
#include "FtpLogManager.h"
#include "algo.h"

using namespace std;

#define ALGO_DIR            ALGO_ROOT_DIR"ftptrans/json/"
#define ALGO_ABI_JSON_PATH  ALGO_DIR ALGO_ABI_JSON_NAME
#define ALGO_IO_JSON_PATH   ALGO_DIR ALGO_IO_JSON_NAME
#define ALGO_PM_JSON_PATH   ALGO_DIR ALGO_PM_JSON_NAME
#define ALGO_DISP_JSON_PATH ALGO_DIR ALGO_DISP_JSON_NAME
#define ALGO_NAME           "ftptrans"

#define DESTIN_IP			"DestinIp"
#define DESTIN_PORT			"DestinPort"

// This class is exported from the libftptrans.so
class CFtptransModule : public CAlgo
{
public:
	enum eFtpSaveStrategy
	{
		NO_SAVE_STRATEGY = 0,       /**< do not save */
		OK_SAVE_STRATEGY = 1,       /**< save only for OK(1) status */
		NG_SAVE_STRATEGY = 2,       /**< save only for NG(0) status */
		OK_NG_SAVE_STRATEGY = 3,    /**< always save */
	};

	enum eDirectoryOption
	{
		dirNull      = 0,
		dirDefault   = 1,
		dirCustom    = 2,
		dirSubscribe = 3
	};

	enum eFileNameOption
	{
		FN_DEFAULT = 0,
		FN_SUB,
		FN_CUSTOM
	};

	static constexpr auto FTP_STATE_MAX_LENGTH = 32; 

public:
	CFtptransModule();
	~CFtptransModule();
	
	int Process(IN void* hInput, IN void* hOutput);
	int SetPwd(IN const char* szPwd);
	void UpdatePwdPrivate(const char* szPwd) override{return;};
	int GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen);
	int SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen);
	int AlgoPlayCtrlPrivate(eALGO_PLAYCTRL ePlayCtrl) override;

private:
	int InitAlgoPrivate();
	int InitROI(int logId,std::string moduleName,std::string pwd)override{return IMVS_EC_OK;};
	int DeInit();
	
	int SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen);
	int PrepareFileName(IN char pFileName[], IN int nFileLen, IN char pDirName[], IN int nDirLen);
	bool IsStringLegal(const char* szFileName, const char* szPattern);

	FtpClientManager *m_pMessageObj;

	int m_nSaveStrategy;
	int m_nProcessStatus;
	int m_nTimestampEnable;
	int m_nProcessStatusEnable;

	int m_nFrameIdEnable;
	int m_nFrameId;

	int m_nTransLogEnable;
	int m_nTransportType;
	eDirectoryOption m_nDirType;
	eFileNameOption m_nFilenameStrategy;
	std::string m_strCustomDir;
	std::string m_strSubscribeDir;
	std::string m_strSubFilename;
	std::string m_strCustomFilename;
	std::string m_strStartEnd;
	std::string m_strSeperator;

    static constexpr const char* m_szSpecialCharSet = "\\/:*?\042<>|\011\012\015";
};


/////////////////////////////模块须导出的接口（实现开始）//////////////////////////////////////////
#ifdef __cplusplus
extern "C"
{
#endif

	CAbstractUserModule* CreateModule(void* hModule);
	void DestroyModule(void* hModule, CAbstractUserModule* pUserModule);
	
#ifdef __cplusplus
};
#endif
/////////////////////////////模块须导出的接口（实现结束）//////////////////////////////////////////
