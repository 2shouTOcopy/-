/**@file    fins_trans.h
 * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief   The part of fins implemented according to the specifications of the
 *          scheduler.
 *
 * @author  mengsanhu
 * @date    2022/10/18
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2022/10/18  |V1.0.0  |mengsanhu           |Initial version
 * @warning 
 */

#include <cassert>
#include <cstring>
#include <cstdlib>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "algoutils.h"
#include "fins_trans.h"
#include "log/log.h"
#include "infra/DataBuffer.h"
#include "utils.h"
#include "thread/ThreadApi.h"
#include "AppParamCommon.h"
#include "ITriggerSource.h"
#include "IImageProcess.h"
#include "util_net.h"
#include "outputMode.h"

#define DEBUG_GLOBAL_MDC_STRING "CFinsTransModule"
#define FINS_MOUDLE_ENABLE    "AlgoEnable"
#define INDUSTRIAL_DEBUG_LEVEL "IndustrialDebugLevel"

#define IPQUAD(ip)   \
		((unsigned char *)&(ip))[3], \
		((unsigned char *)&(ip))[2], \
		((unsigned char *)&(ip))[1], \
		((unsigned char *)&(ip))[0]
		
#define FINS_PROCESS_OK         (1)

CFinsTransModule::ModuleParamOptSet CFinsTransModule::m_stParamOptSet[] = {
	{"AlgoEnable",			PBOOL,	 0, 0, 0, 0, &CFinsTransModule::SetModuleEnable, &CFinsTransModule::GetModuleEnable},
	{"ServerIP",			PSTRING, 0, 0, &CFinsTransModule::SetServerIp, &CFinsTransModule::GetServerIp, 0, 0},
	{"ServerPort",			PINT,	 &CFinsTransModule::SetServerPort, &CFinsTransModule::GetServerPort, 0, 0, 0, 0},
	{"ControlPollInterval",	PINT,	 &CFinsTransModule::SetControlPollInterval, &CFinsTransModule::GetControlPollInterval, 0, 0, 0, 0},
	{"ControlSpace",		PINT,	 &CFinsTransModule::SetControlAddressSpace, &CFinsTransModule::GetControlAddressSpace, 0, 0, 0, 0},
	{"StatusSpace",			PINT,	 &CFinsTransModule::SetStatusAddressSpace, &CFinsTransModule::GetStatusAddressSpace, 0, 0, 0, 0},
	{"ResultSpace",			PINT,	 &CFinsTransModule::SetResultAddressSpace, &CFinsTransModule::GetResultAddressSpace, 0, 0, 0, 0},        
	{"InstructionSpace",	PINT,	 &CFinsTransModule::SetInstructionAddressSpace, &CFinsTransModule::GetInstructionAddressSpace, 0, 0, 0, 0},
	{"ControlOffset",		PINT,	 &CFinsTransModule::SetControlAddressOffset, &CFinsTransModule::GetControlAddressOffset, 0, 0, 0, 0},
	{"StatusOffset", 		PINT,	 &CFinsTransModule::SetStatusAddressOffset, &CFinsTransModule::GetStatusAddressOffset, 0, 0, 0, 0},
	{"ResultOffset", 		PINT,	 &CFinsTransModule::SetResultAddressOffset, &CFinsTransModule::GetResultAddressOffset, 0, 0, 0, 0},        
	{"InstructionOffset",	PINT,	 &CFinsTransModule::SetInstructionAddressOffset, &CFinsTransModule::GetInstructionAddressOffset, 0, 0, 0, 0},
	{"ControlSize",			PINT,	 &CFinsTransModule::SetControlAddressSize, &CFinsTransModule::GetControlAddressSize, 0, 0, 0, 0},
	{"StatusSize",			PINT,	 &CFinsTransModule::SetStatusAddressSize, &CFinsTransModule::GetStatusAddressSize, 0, 0, 0, 0},
	{"ResultSize",			PINT,	 &CFinsTransModule::SetResultAddressSize, &CFinsTransModule::GetResultAddressSize, 0, 0, 0, 0},       
	{"InstructionSize",		PINT,	 &CFinsTransModule::SetInstructionAddressSize, &CFinsTransModule::GetInstructionAddressSize, 0, 0, 0, 0},
	{"ResultTimeout",		PINT,	 &CFinsTransModule::SetResultTimeout, &CFinsTransModule::GetResultTimeout, 0, 0, 0, 0},
	{"ResultByteSwap",		PBOOL,	 0, 0, 0, 0, &CFinsTransModule::SetByteOrderEnable, &CFinsTransModule::GetByteOrderEnable},
	{INDUSTRIAL_DEBUG_LEVEL, PINT, &CFinsTransModule::SetIndustrialDebugLevel, &CFinsTransModule::GetIndustrialDebugLevel, 0, 0, 0, 0}
};

fins_param_opt CFinsTransModule::fins_para = {0};


void *ThreadProcessScheduledTrans(IN void* argv)
{
	CFinsTransModule* pUserModule = (CFinsTransModule*)argv;
	if (!pUserModule) return nullptr;

	char szGetString[MAX_SCHEDULED_PAYLOAD_LEN] = {0};
	snprintf(szGetString, sizeof(szGetString), "%s_fins", SCHEDULED_NAME);
	thread_set_name(szGetString);

	SimpleFifoAdapter qadp(&(pUserModule->m_struScheduledQueue));

	// 发送函数
	auto sendFn = [&](const scheduled_fifo_param& p)
	{
		if (p.len <= sizeof(p.buf))
		{
			fins_send_result((char*)p.buf, (int)p.len);
		}
	};

	// 引擎上下文
	OutputContext ctx{};
	ctx.logId = pUserModule->m_nLogId;
	ctx.queue = &qadp;
	ctx.nowMs = &get_up_time;
	ctx.send = sendFn;
	ctx.mode = OutputMode::kImmediate;
	ctx.scheduledIntervalMs = 0;
	ctx.ngText = SCHEDULED_TRANS_NG_STRING;

	OutputModeEngine engine(ctx);

	while(1)
	{
		int mEnable = 0;

		try
		{
			mEnable    = pUserModule->GetParamManagePtr()->GetParam<int>(FINS_MOUDLE_ENABLE);
		}
		catch (const std::invalid_argument&)
		{
			;
		}

		if (!mEnable)
		{
			sfifo_drain(&(pUserModule->m_struScheduledQueue));
			usleep(MAX_COMMUNICATION_USLEEP_TIME);
			continue;
		}

		// 主循环步进
		engine.tick();
	}

	return nullptr;
}


CFinsTransModule::CFinsTransModule()
{
	m_inited = false;
	m_nRecordTriggerCount = -1;
	m_nThreadScheduledTrans = 0;
	
	memset(&fins_para, 0, sizeof fins_para);
	memset(&m_struScheduledFifoParamArray, 0, sizeof(m_struScheduledFifoParamArray));
	memset(&m_struScheduledQueue, 0, sizeof(m_struScheduledQueue));

}

CFinsTransModule::~CFinsTransModule()
{
	DeInit();
}

int CFinsTransModule::Process(IN void* hInput, IN void* hOutput)
{
	std::string strValue;
	int nErrCode = IMVS_EC_OK;
	int nFrame = 0;
	int nTrigger = 0;
	int nExcludeNum = 0;
	int paramStatus = -1;
	int MoudleEnable = 0;

	UP_PSTIME();
	LOGI("finstrans Process start\n");
	scheduled_fifo_param strParam = {0};

	ScFramePtr pImgFrame = VM_M_Get_Frame_ByID(hInput, 0);
	if (!pImgFrame)
	{
		LOGE("get frame by id failed, no data\r\n");
		return IMVS_EC_ALGO_NO_DATA;
	}
	
	nErrCode = pImgFrame->getIntVal("SINGLE_frame_cnt", nFrame);
	if (0 != nErrCode)
	{
		LOGE("get subscribe SINGLE_frame_cnt failed, no data\r\n");
		return IMVS_EC_ALGO_NO_DATA;
	}

	nErrCode = pImgFrame->getIntVal("SINGLE_trigger_cnt", nTrigger);
	if (0 != nErrCode)
	{
		LOGE("get subscribe SINGLE_trigger_cnt failed, no data\r\n");
		return IMVS_EC_ALGO_NO_DATA;
	}

	nErrCode = pImgFrame->getIntVal("SINGLE_exclusion_cnt", nExcludeNum);
	if (0 != nErrCode)
	{
		LOGE("get subscribe SINGLE_exclusion_cnt failed, no data\r\n");
		return IMVS_EC_ALGO_NO_DATA;
	}
	
	try
	{
		MoudleEnable = m_paramManage->GetParam<int>(FINS_MOUDLE_ENABLE);
	}
	catch (const std::invalid_argument& e)
	{
		LOGE("Get Param Filed %s \n\n", e.what());
		return IMVS_EC_PARAM;
	}
	
	Infra::DataBufferPtr binaryValue;
	ScFramePtr pFrame = VM_M_Get_Frame_ByHInput(hInput, "SINGLE_obj_binary");
	if (pFrame != nullptr && 0 == pFrame->getBuffer("SINGLE_obj_binary", binaryValue))
	{
		ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
		if (MoudleEnable)
		{

			memset(strParam.buf, 0, sizeof(strParam.buf));
			strParam.len = binaryValue->getLen();
			strParam.exclude_num = nExcludeNum;
			memcpy(strParam.buf, binaryValue->getBuffer(), strParam.len);	
			paramStatus = add_send_request(&strParam, &(m_struScheduledQueue));
		}

		pOutFrame->setVal("SINGLE_status", (0 == paramStatus) ? 1 : 0);
		pOutFrame->setVal("SINGLE_param_status", 0, (0 == paramStatus) ? 1 : 0);
	}
	else
	{
		nErrCode = IMVS_EC_ALGO_NO_DATA;
		LOGE("get subscribe failed, no data\r\n");
	}

	LOGI("finstrans Process end\n");
	UP_PTIME();

	return nErrCode;
}

int CFinsTransModule::SetPwd(IN const char* szPwd)
{
	snprintf(m_moduleName, sizeof(m_moduleName), "%s", ALGO_NAME);
	m_pwd = szPwd;
	
	LOGI("SetPwd CFinsTransModule \r\n");
	return CAlgo::Init();
}

int CFinsTransModule::SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{	
	LOGI("SetParam CFinsTransModule \r\n");

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
			nErrCode = m_paramManage->SetParam(szParamName, pData);
			if (IMVS_EC_OK != nErrCode)
			{
				LOGE("UpParam2Map failed, ret = %d\r\n", nErrCode);
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

int CFinsTransModule::SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{
	int nErrCode = IMVS_EC_OK;
	int n = ARRAY_SIZE(CFinsTransModule::m_stParamOptSet);

	for (int i = 0; i < n; i++)
	{
		if (0 == strncmp(szParamName, CFinsTransModule::m_stParamOptSet[i].szName, 32))
		{
			if (PINT == CFinsTransModule::m_stParamOptSet[i].enType)
			{
				nErrCode = (this->*m_stParamOptSet[i].IntSetFunc)(atoi(pData));
			}
			else if (PSTRING == CFinsTransModule::m_stParamOptSet[i].enType)
			{
				nErrCode = (this->*m_stParamOptSet[i].StrSetFunc)(pData);
			}
			else if (PBOOL == CFinsTransModule::m_stParamOptSet[i].enType)
			{
				nErrCode = (this->*m_stParamOptSet[i].BoolSetFunc)(atoi(pData));
			}
			else
			{
				LOGI("Unknown param set opt type %d\r\n", CFinsTransModule::m_stParamOptSet[i].enType);
				nErrCode = IMVS_EC_ALGO_PARAM_NOT_FOUND;
			}
		}
	}

	return nErrCode;	
}

int CFinsTransModule::SetModuleEnable(bool nEnable)
{
    if (!m_inited) return IMVS_EC_OK;
    return (nEnable ? fins_msg_init(&fins_para) : fins_msg_deinit());
}

int CFinsTransModule::SetServerIp(IN const char* szIp)
{
	if (NULL == szIp)
	{
		return -1;
	}

	if (false == CAlgoUtils::CheckIpValid(std::string(szIp))){
		return IMVS_EC_COMMU_INVALID_ADDRESS;
	}

    struct in_addr in;
	if (0 == inet_aton(szIp, &in))
	{
		return -2;
	}

	fins_para.server_ip = in.s_addr;
	return fins_set_recreate(1);	
}

int CFinsTransModule::SetServerPort(int nPort)
{
	int nErrCode = IMVS_EC_OK;
	fins_para.server_port = nPort;
	nErrCode = fins_set_recreate(1);
	
	return nErrCode;
}

int CFinsTransModule::SetControlPollInterval(int nTimes)
{
	fins_para.control_poll_interval = nTimes;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetControlAddressSpace(int nSpace)
{
	fins_para.control_space = nSpace;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetStatusAddressSpace(int nSpace)
{
	fins_para.status_space = nSpace;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetInstructionAddressSpace(int nSpace)
{
	fins_para.ins_space = nSpace;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetResultAddressSpace(int nSpace)
{
	fins_para.result_space = nSpace;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetControlAddressOffset(int nOffset)
{
	fins_para.control_offset = nOffset;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetStatusAddressOffset(int nOffset)
{
	fins_para.status_offset = nOffset;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetResultAddressOffset(int nOffset)
{
	fins_para.result_offset = nOffset;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetInstructionAddressOffset(int nOffset)
{
	fins_para.ins_offset = nOffset;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetControlAddressSize(int nSize)
{
	fins_para.control_size = nSize;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetStatusAddressSize(int nSize)
{
	fins_para.status_size = nSize;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetResultAddressSize(int nSize)
{
	fins_para.result_size = nSize;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetInstructionAddressSize(int nSize)
{
	fins_para.ins_size = nSize;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetResultTimeout(int nTimes)
{
	fins_para.result_timeout = nTimes;
	return IMVS_EC_OK;
}

int CFinsTransModule::SetByteOrderEnable(bool nEnable)
{
	fins_para.result_byte_swap = (nEnable ? 1 : 0);
	return IMVS_EC_OK;
}

int CFinsTransModule::SetIndustrialDebugLevel(int nLevel)
{
	(void)fins_set_debug_level(nLevel);
	return IMVS_EC_OK;
}

int CFinsTransModule::SetProcedureName(IN const char* szProcedureName)
{
	return IMVS_EC_OK;
}


int CFinsTransModule::GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
{
	LOGI("GetParam CFinsTransModule \r\n");

	int nRet = IMVS_EC_OK;
	if (szParamName == NULL || strlen(szParamName) == 0 || pBuff == NULL || nBuffSize <= 0 || pDataLen == NULL)
	{
		return IMVS_EC_PARAM;
	}

	if (strstr(szParamName, ALGO_DEBUG_INFO_PARAM_STR))
	{
		return (fins_get_debug_info(pBuff, nBuffSize, pDataLen) == 0) ? IMVS_EC_OK : IMVS_EC_SYSTEM_INNER_ERR;
	}

	nRet = GetmoduParaHandle(szParamName, pBuff, nBuffSize, pDataLen);
	if (IMVS_EC_ALGO_PARAM_NOT_FOUND == (unsigned int)nRet)
	{
		auto value = m_paramManage->GetParam(szParamName);
		snprintf(pBuff, nBuffSize, "%s", value.c_str());
		*pDataLen = strlen(pBuff);
	}
	
	return IMVS_EC_OK;
}


int CFinsTransModule::GetmoduParaHandle(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
{
	int nErrCode = IMVS_EC_OK;
	int nOptNum = ARRAY_SIZE(CFinsTransModule::m_stParamOptSet);
	int i = 0;
	int nValue = 0;
	bool bValue = 0;
	PARAMTYPE nValueType = PVOID;  // 1-int 2-float 3-string 4-bool 
	
	LOGI("GetmoduParaHandle %s \r\n", szParamName);

	for (i = 0;i < nOptNum;i++)
	{
		if (0 == strncmp(szParamName, CFinsTransModule::m_stParamOptSet[i].szName, 32))
		{
			if (PINT == CFinsTransModule::m_stParamOptSet[i].enType)
			{
				nErrCode = (this->*m_stParamOptSet[i].IntGetFunc)(&nValue);
				nValueType = PINT;
			}
			else if (PSTRING == CFinsTransModule::m_stParamOptSet[i].enType)
			{
				nErrCode = (this->*m_stParamOptSet[i].StrGetFunc)(pBuff, nBuffSize, pDataLen);
			}
			else if (PBOOL == CFinsTransModule::m_stParamOptSet[i].enType)
			{
				nErrCode = (this->*m_stParamOptSet[i].BoolGetFunc)(&bValue);
				nValueType = PBOOL;
			}
			else
			{
				nErrCode = IMVS_EC_ALGO_PARAM_NOT_FOUND;
				LOGI("Unknown param set opt type %d\r\n", CFinsTransModule::m_stParamOptSet[i].enType);
			}
			break;
		}
	}

	if (nErrCode == IMVS_EC_OK)
	{
		if (PINT == nValueType || PBOOL == nValueType)
		{
			snprintf(pBuff, nBuffSize, "%d", nValue);
		}
		else
		{
			LOGI("nValueType = %d, nothing to do\r\n", nValueType);
		}
		*pDataLen = strlen(pBuff);
	}
	
	return nErrCode;	
}

int CFinsTransModule::GetModuleEnable(bool *pnEnable)
{
	*pnEnable = (fins_get_enable() ? true : false);
    return IMVS_EC_OK;
}

int CFinsTransModule::GetServerIp(char* szIp, int nBuffSize, int* pDataLen)
{
	snprintf(szIp, 32, "%d.%d.%d.%d", IPQUAD(fins_para.server_ip));
	*pDataLen = strlen(szIp);
	
	return IMVS_EC_OK;
}

int CFinsTransModule::GetServerPort(int *pnPort)
{
	*pnPort = fins_para.server_port;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetControlPollInterval(int *pnTimes)
{
	*pnTimes = fins_para.control_poll_interval;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetControlAddressSpace(int *pnSpace)
{
	*pnSpace = fins_para.control_space;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetStatusAddressSpace(int *pnSpace)
{
	*pnSpace = fins_para.status_space;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetResultAddressSpace(int *pnSpace)
{
	*pnSpace = fins_para.result_space;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetInstructionAddressSpace(int *pnSpace)
{
	*pnSpace = fins_para.ins_space;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetControlAddressOffset(int *pnOffset)
{
	*pnOffset = fins_para.control_offset;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetStatusAddressOffset(int *pnOffset)
{
	*pnOffset = fins_para.status_offset;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetResultAddressOffset(int *pnOffset)
{
	*pnOffset = fins_para.result_offset;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetInstructionAddressOffset(int *pnOffset)
{
	*pnOffset = fins_para.ins_offset;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetControlAddressSize(int *pnSize)
{
	*pnSize = fins_para.control_size;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetStatusAddressSize(int *pnSize)
{
	*pnSize = fins_para.status_size;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetResultAddressSize(int *pnSize)
{
	*pnSize = fins_para.result_size;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetInstructionAddressSize(int *pnSize)
{
	*pnSize = fins_para.ins_size;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetResultTimeout(int *pnTimes)
{
	*pnTimes = fins_para.result_timeout;
	return IMVS_EC_OK;
}

int CFinsTransModule::GetByteOrderEnable(bool *pnEnable)
{
	*pnEnable = (fins_para.result_byte_swap ? true : false);
	return IMVS_EC_OK;
}

int CFinsTransModule::GetIndustrialDebugLevel(int *pnLevel)
{
	if (pnLevel == NULL)
	{
		return IMVS_EC_PARAM;
	}
	*pnLevel = fins_get_debug_level();
	return IMVS_EC_OK;
}

bool CFinsTransModule::AlgoNeedDLClose(void)
{
	return false;
}

int CFinsTransModule::UpParam4All(IN const char* szParamName, IN const char* pData)
{
	if (NULL == szParamName || NULL == pData)
	{
		return IMVS_EC_PARAM;
	}
	int nErrCode = IMVS_EC_OK;
	int nRet = -1; 
	LOGD("UpParam4All param %s value %s start\r\n", szParamName, pData);

	// 更新内存参数
	nRet = m_paramManage->SetParam(szParamName, pData);
	if (IMVS_EC_OK != nRet)
	{
		nErrCode = nRet;
		LOGE("SetParam failed, nRet = %d\r\n", nRet);
	}
	
	nRet = m_paramManage->SaveFile();
	if (IMVS_EC_OK != nRet)
	{
		nErrCode = nRet;
		LOGE("SaveFile failed, nRet = %d\r\n", nRet);
	}

	LOGD("UpParam4All param %s value %s end\r\n", szParamName, pData);

	return nErrCode;
}

int CFinsTransModule::UpSingleParam(IN const char* pParamName, bool bForce)
{
	if (NULL == pParamName)
	{
		return IMVS_EC_PARAM;
	}
	
	char szParamValue[PARAM_VALUE_LEN] = {0};
	int nErrCode = IMVS_EC_OK;
	
	nErrCode = UpParam4All(pParamName, szParamValue);
	if (IMVS_EC_OK != nErrCode)
	{
		LOGE("UpParam4All param %s value %s failed, nErrCode = %d\r\n", pParamName, szParamValue, nErrCode);
	}

	return IMVS_EC_OK;
}

int CFinsTransModule::InitAlgoPrivate()
{	
	int nErrCode = IMVS_EC_OK;

	do
	{
        if(DeInit())
		{
			LOGE("DeInit Failed And Return \n\n");
			return IMVS_EC_SYSTEM_INNER_ERR;		
		}

		//设置模块参数
		auto callback = [&](std::string paramName,std::string paramValue)->int
		{
			nErrCode = SetParam(paramName.c_str(), paramValue.c_str(), paramValue.length());
			LOGE("set aram %s value %s ret %d\r\n", paramName.c_str(), paramValue.c_str(), nErrCode);
			if (IMVS_EC_OK != nErrCode)
			{
				LOGE("set param failed, nErrCode = 0x%x\n", nErrCode);
				nErrCode = IMVS_EC_PARAM;
			}
			return nErrCode;
		};
		nErrCode = m_paramManage->ForEach(callback);
		if(IMVS_EC_OK != nErrCode)
		{
			LOGE("set param failed, ret = 0x%x\n", nErrCode);
			nErrCode = IMVS_EC_PARAM;
			break;
		}

		sfifo_init(&(m_struScheduledQueue), sizeof(scheduled_fifo_param),
		SCHEDULED_SEND_QUEUE_LEN, (void*)(m_struScheduledFifoParamArray));
		//定时输出线程
		int nRet = thread_spawn_ex(&m_nThreadScheduledTrans, 1, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, ThreadProcessScheduledTrans, this);
		if (IMVS_EC_OK != nRet)
		{
			LOGE("GenerateHeadInfo failed, ret %d\r\n", nRet);
			nErrCode = IMVS_EC_ALGO_HEAD_PARAM_ERROR;
			break;
		}

        m_inited = true;

		int MoudleEnable = 0;
		try
		{
			MoudleEnable = m_paramManage->GetParam<int>(FINS_MOUDLE_ENABLE);
		}
		catch (const std::invalid_argument& e)
		{
			LOGE("Get Param Filed %s \n\n", e.what());
			return IMVS_EC_PARAM;
		}

        if (MoudleEnable)
        {
            fins_msg_init(&fins_para);
        }
	}while(0);

	return nErrCode;
}

int CFinsTransModule::DeInit(void)
{
	int nErrCode = IMVS_EC_OK;
	LOGI("deinit CFinsTransModule \r\n");
    
	if (m_nThreadScheduledTrans)
	{
		pthread_cancel(m_nThreadScheduledTrans);
		(void)pthread_join(m_nThreadScheduledTrans, nullptr);
		m_nThreadScheduledTrans = 0;
		LOGI("pthread_cancel  m_nThreadScheduledTrans end \r\n");
	}

	fins_msg_deinit();
	m_inited = false;

	return nErrCode;
}

CAbstractUserModule* CreateModule(void* hModule)
{
	assert(hModule != NULL);
	CAbstractUserModule* pUserModule = new CFinsTransModule;
	return pUserModule;
}

void DestroyModule(void* hModule, CAbstractUserModule* pUserModule)
{
	assert(hModule != NULL);
	assert(pUserModule != NULL);
	delete pUserModule;
	pUserModule = NULL;
}
