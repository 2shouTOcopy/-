#include <unistd.h>
#include <stdexcept>
#include "modbus_trans.h"

#include "utils.h"
#include "thread/ThreadApi.h"
#include "util_net.h"
#include "log/log.h"
#include "AppParamCommon.h"
#include "ITriggerSource.h"
#include "IImageProcess.h"
#include "infra/DataBuffer.h"
#include "outputMode.h"

#define DEBUG_GLOBAL_MDC_STRING "CModbusTransModule"
#define MODBUS_MOUDLE_ENABLE "AlgoEnable"
#define MODBUS_INPUT_MODULE_SIZE "InputAddressQuantity"
#define MODBUS_OUTPUT_MODULE_SIZE "OutputAddressQuantity"
#define MODBUS_DEBUG_LEVEL "ModbusDebugLevel"
#define INDUSTRIAL_DEBUG_LEVEL "IndustrialDebugLevel"

#ifndef INADDR_NONE
#define INADDR_NONE                  (0xFFFFFFFF)
#endif
#ifndef INPUTADDRESSQUANTTITY_KEY
#define INPUTADDRESSQUANTTITY_KEY   (14)
#endif
#ifndef OUTPUTADDRESSQUANTTITY_KEY
#define OUTPUTADDRESSQUANTTITY_KEY  (22)
#endif
#define HOST_IP					"HostIp"

void *ThreadProcessScheduledTrans(IN void* argv)
{
	CModbusTransModule* pUserModule = (CModbusTransModule*)argv;
	if (!pUserModule) return nullptr;

	char szGetString[64] = {0};
	snprintf(szGetString, sizeof(szGetString), "%s_mbs", SCHEDULED_NAME);
	thread_set_name(szGetString);

	SimpleFifoAdapter qadp(&(pUserModule->m_struScheduledQueue));

	// 发送函数
	auto sendFn = [&](const scheduled_fifo_param& p)
	{
		if (p.len <= sizeof(p.buf))
		{
			(void)modbus_send_result((char*)p.buf, (int)p.len);
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

	while (1)
	{
		int mEnable = 0;

		try
		{
			mEnable    = pUserModule->GetParamManagePtr()->GetParam<int>(MODBUS_MOUDLE_ENABLE);
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


CModbusTransModule::CModbusTransModule(void)
{
	initialized = 0;
	m_nRecordTriggerCount = -1;
	m_nThreadScheduledTrans = 0;
	memset(&m_struScheduledFifoParamArray, 0, sizeof(m_struScheduledFifoParamArray));
	memset(&m_struScheduledQueue, 0, sizeof(m_struScheduledQueue));

	modbus_para.iWorkMode = 0;
	modbus_para.iServerIp = 0;
	modbus_para.iServerPort = 0;
	modbus_para.iModuleEnable = 0;
	modbus_para.iSlaveId = 255;
	modbus_para.iCtrlAddrOffset = 0;
	modbus_para.iStatusAddrOffset = 1;
	modbus_para.iInputAddrOffset = 2;
	modbus_para.iOutputAddrOffset = 500;
	modbus_para.iCtrlAddrQuantity = 1;
	modbus_para.iStatusAddrQuantity = 1;
	modbus_para.iInputAddrQuantity = 100;
	modbus_para.iOutputAddrQuantity = 100;
	modbus_para.iCtrlAddrSpaceType = 0;
	modbus_para.iInputAddrSpaceType = 0;
	modbus_para.iOutputAddrSpaceType = 0;
	modbus_para.iStatusAddrSpaceType = 0;
	modbus_para.iIdleTimeoutUsec = 120;
	modbus_para.iMaxConnection = 6;
	modbus_para.iTriggerID = 0;
	modbus_para.iTriggerReadID = 0;
	modbus_para.iTriggerWriteID = 0;
	modbus_para.sys_run_status = NULL;
	modbus_para.iByteOrder = ORDER_BADC;
	modbus_para.iByteOrderEnable = 0;
	modbus_para.iSpacer = SEMICOLON;
	modbus_para.iControlPollInterval = 0;
}

CModbusTransModule::~CModbusTransModule(void)
{
	DeInit();
}

int CModbusTransModule::SetModbusHostIp(const char* szIp)
{
	int nErrCode = IMVS_EC_OK;

	return nErrCode;	
}

int CModbusTransModule::SetModbusPort(int nPort)
{
	int nErrCode = IMVS_EC_OK;

	return nErrCode;	
}
int CModbusTransModule::SetByteOrder(int nType)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iByteOrder = (enum modbus_byte_order)nType;

	return nErrCode;
}


int CModbusTransModule::SetModbusMode(int nMode)
{
	modbus_para.iWorkMode = nMode;

	return IMVS_EC_OK;
}

int CModbusTransModule::SetModbusServerIp(IN const char* szIp)
{
	uint32_t addr = 0;

	if (NULL == szIp)
	{
		return -1;
	}
	
	addr = inet_addr(szIp);
	if (addr == INADDR_NONE)
	{
		return -2;
	}

	modbus_para.iServerIp = ntohl(addr);
	return IMVS_EC_OK;
}

int CModbusTransModule::SetModbusServerPort(int nPort)
{
	modbus_para.iServerPort = nPort;
	return IMVS_EC_OK;
}

int CModbusTransModule::SetModbusSlaveId(int nSlaveId)
{
	modbus_para.iSlaveId = nSlaveId;
	return IMVS_EC_OK;
}

int CModbusTransModule::SetModbusControlPollInterval(int nTimes)
{
	modbus_para.iControlPollInterval = nTimes;
	return IMVS_EC_OK;
}


int CModbusTransModule::SetByteOrderEnable(int nType)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iByteOrderEnable = nType;

	return nErrCode;
}

int CModbusTransModule::SetSpacer(int nSpacer)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iSpacer = (enum modbus_spacer)nSpacer;

	return nErrCode;
}

int CModbusTransModule::SetControlAddressSpaceType(int nType)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iCtrlAddrSpaceType = nType;
	
	return nErrCode;
}

int CModbusTransModule::SetStatusAddressSpaceType(int nType)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iStatusAddrSpaceType = nType;
	
	return nErrCode;
}

int CModbusTransModule::SetInputAddressSpaceType(int nType)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iInputAddrSpaceType = nType;
	
	return nErrCode;
}

int CModbusTransModule::SetOutputAddressSpaceType(int nType)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iOutputAddrSpaceType = nType;
	
	return nErrCode;
}

int CModbusTransModule::SetControlAddressOffset(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iCtrlAddrOffset = nNum;
	modbus_set_control_addr(nNum);
	
	return nErrCode;
}

int CModbusTransModule::SetStatusAddressOffset(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iStatusAddrOffset = nNum;
	modbus_set_status_addr(nNum);
	
	return nErrCode;
}

int CModbusTransModule::SetInputAddressOffset(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iInputAddrOffset = nNum;
	modbus_set_input_addr(nNum);

	return nErrCode;
}

int CModbusTransModule::SetOutputAddressOffset(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iOutputAddrOffset = nNum;
	modbus_set_output_addr(nNum);
	
	return nErrCode;
}

int CModbusTransModule::SetControlAddressQuantity(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iCtrlAddrQuantity = nNum;
	
	return nErrCode;
}

int CModbusTransModule::SetStatusAddressQuantity(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iStatusAddrQuantity = nNum;

	return nErrCode;
}

int CModbusTransModule::SetInputAddressQuantity(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iInputAddrQuantity = nNum;
	modbus_set_input_size(nNum);
	
	return nErrCode;
}

int CModbusTransModule::SetOutputAddressQuantity(int nNum)
{
	int nErrCode = IMVS_EC_OK;
	modbus_para.iOutputAddrQuantity = nNum;
	modbus_set_output_size(nNum);
	
	return nErrCode;
}

int CModbusTransModule::SetModuleEnable(int nEnable)
{
	int nErrCode = IMVS_EC_OK;
	int32_t nTriggerCount = 0;
	auto trigInstance = mvsc_idr_app::ITriggerSource::getComponent();
	if(trigInstance == nullptr)
	{
		LOGE("trigger source instance nullptr\n");
		return IMVS_EC_NULL_PTR;
	}
		
	if (trigInstance->GetParam(TRIGGER_COUNT, nTriggerCount))
	{
		LOGE("get trigger count failed\r\n");
	}
	else
	{
		m_nRecordTriggerCount = nTriggerCount;
	}
	
	modbus_para.iModuleEnable = nEnable;

	return nErrCode;
}

int CModbusTransModule::GetModbusHostIp(char* szIp, int nBuffSize, int* pDataLen)
{
	uint32_t ip_addr = 0;

	if (modbus_get_ipaddr((char*)NET_INTERFACE, &ip_addr) < 0)
	{
		LOGE("get ip addr failed!\r\n");
		return -1;
	}

	snprintf(szIp, 32, "%d.%d.%d.%d", IPQUAD(ip_addr));

	return IMVS_EC_OK;
}

int CModbusTransModule::GetModbusPort(int* pnPort)
{
	*pnPort = MODBUS_FIXED_PORT;
	return IMVS_EC_OK;
}

int CModbusTransModule::GetModbusMode(int* nMode)
{
	*nMode = modbus_para.iWorkMode;
	return IMVS_EC_OK;
}

int CModbusTransModule::GetModbusServerIp(char* szIp, int nBuffSize, int* pDataLen)
{
	snprintf(szIp, 32, "%d.%d.%d.%d", IPQUAD(modbus_para.iServerIp));
	return IMVS_EC_OK;
}

int CModbusTransModule::GetModbusServerPort(int* nPort)
{
	*nPort = modbus_para.iServerPort;
	return IMVS_EC_OK;
}

int CModbusTransModule::GetModbusSlaveId(int* nSlaveId)
{
	*nSlaveId = modbus_para.iSlaveId;
	return IMVS_EC_OK;
}

int CModbusTransModule::GetModbusControlPollInterval(int* nTimes)
{
	*nTimes = modbus_para.iControlPollInterval;
	return IMVS_EC_OK;
}

int CModbusTransModule::GetByteOrder(int *nType)
{
	int nErrCode = IMVS_EC_OK;
	*nType = modbus_para.iByteOrder;
	
	return nErrCode;
}

int CModbusTransModule::GetMaxConnection(int *nCon)
{
	int nErrCode = IMVS_EC_OK;
	*nCon = modbus_para.iMaxConnection;
	
	return nErrCode;
}

int CModbusTransModule::GetIdleTimeoutUsec(int *nUsec)
{
	int nErrCode = IMVS_EC_OK;
	*nUsec = modbus_para.iIdleTimeoutUsec;
	
	return nErrCode;
}

int CModbusTransModule::GetControlAddressSpaceType(int *nType)
{
	int nErrCode = IMVS_EC_OK;
	*nType = modbus_para.iCtrlAddrSpaceType;
	
	return nErrCode;
}

int CModbusTransModule::GetStatusAddressSpaceType(int *nType)
{
	int nErrCode = IMVS_EC_OK;
	*nType = modbus_para.iStatusAddrSpaceType;

	return nErrCode;
}

int CModbusTransModule::GetInputAddressSpaceType(int *nType)
{
	int nErrCode = IMVS_EC_OK;
	*nType = modbus_para.iInputAddrSpaceType;

	return nErrCode;
}

int CModbusTransModule::GetOutputAddressSpaceType(int *nType)
{
	int nErrCode = IMVS_EC_OK;
	*nType = modbus_para.iOutputAddrSpaceType;

	return nErrCode;
}

int CModbusTransModule::GetControlAddressOffset(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iCtrlAddrOffset;

	return nErrCode;
}

int CModbusTransModule::GetStatusAddressOffset(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iStatusAddrOffset;

	return nErrCode;
}

int CModbusTransModule::GetInputAddressOffset(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iInputAddrOffset;

	return nErrCode;
}

int CModbusTransModule::GetOutputAddressOffset(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iOutputAddrOffset;

	return nErrCode;
}

int CModbusTransModule::GetControlAddressQuantity(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iCtrlAddrQuantity;

	return nErrCode;
}

int CModbusTransModule::GetStatusAddressQuantity(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iStatusAddrQuantity;

	return nErrCode;
}

int CModbusTransModule::GetInputAddressQuantity(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iInputAddrQuantity;

	return nErrCode;
}

int CModbusTransModule::GetOutputAddressQuantity(int *nNum)
{
	int nErrCode = IMVS_EC_OK;
	*nNum = modbus_para.iOutputAddrQuantity;

	return nErrCode;
}

bool CModbusTransModule::AlgoNeedDLClose(void)
{
	return false;
}

int CModbusTransModule::Process(IN void* hInput, IN void* hOutput)
{
	std::string strValue;
	int nErrCode = IMVS_EC_OK;
	int nFrame = 0;
	int nTrigger = 0;
	int nExcludeNum = 0;
	int paramStatus = -1;
	int MoudleEnable = 0;
	scheduled_fifo_param strParam;

    auto IsTriggerIdValid = [this](int nTrigger)
    {
		auto trigInstance = mvsc_idr_app::ITriggerSource::getComponent();
		if(trigInstance == nullptr)
		{
			LOGE("trigger source instance nullptr\n");
			return false;
		}
		auto imgInstance = mvsc_idr_app::IImageProcess::getComponent();
		if(imgInstance == nullptr)
		{
			LOGE("image process instance nullptr\n");
			return false;
		}

		int TestImageMode;
        if (imgInstance->GetParam(ENABLE_TEST_IMAGE, TestImageMode) == 0)
        {
            if (TestImageMode == 1) return true;

            int TriggerMode;
            if (trigInstance->GetParam(TRIGGER_MODE_PARAM, TriggerMode) == 0)
            {
                if (TriggerMode == 1)
                {
					int nResultMode = 0;
					if (0 != imgInstance->GetParam(RESULT_OUTPUT_MODE, nResultMode))
					{
						LOGE("Get result output mode Filed %s\n");
						return false;
					}
					if (OutputMode::kImmediate != static_cast<OutputMode>(nResultMode))
                        return nTrigger != m_nRecordTriggerCount ? true : false;
                }
            }
        }

        return true;
    };

	UP_PSTIME();
	LOGI("modbustrans Process start\n");

	if (0 == initialized)
	{
		return -1;
	}

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
		MoudleEnable = m_paramManage->GetParam<int>(MODBUS_MOUDLE_ENABLE);
	}
	catch (const std::invalid_argument& e)
	{
		LOGE("Get Param Filed %s \n\n", e.what());
		return IMVS_EC_PARAM;
	}
	
	Infra::DataBufferPtr binaryValue;
	ScFramePtr pFrame = VM_M_Get_Frame_ByHInput(hInput, "SINGLE_obj_binary");
	if (pFrame != nullptr && 0 == pFrame->getBuffer("SINGLE_obj_binary", binaryValue) && IsTriggerIdValid(nTrigger))
	{
		ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
		set_frame_and_trigger(nFrame, nTrigger, m_nLogId);
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

	LOGI("modbustrans Process end\n");
	UP_PTIME();

	return nErrCode;
}

int CModbusTransModule::SetPwd(IN const char* szPwd)
{
	snprintf(m_moduleName, sizeof(m_moduleName), "%s", ALGO_NAME);
	m_pwd = szPwd;
	
	LOGI("SetPwd CModbusTransModule \r\n");
	return CAlgo::Init();
}

int CModbusTransModule::GetmoduParaHandle(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
{
	int nErrCode = IMVS_EC_OK;

	int nValueType = 0;  // 1-int 2-float 3-bool 4-string
	int nValue = 0;
	float fValue = 0.0f;
	
	LOGI("GetmoduParaHandle CModbusTransModule \r\n");
	if (0 == strcmp(szParamName, "MaxConnection"))
	{
		nErrCode = GetMaxConnection(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "IdleTimeoutUsec"))
	{
		nErrCode = GetIdleTimeoutUsec(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "ControlAddressSpaceType"))
	{
		nErrCode = GetControlAddressSpaceType(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "StatusAddressSpaceType"))
	{
		nErrCode = GetStatusAddressSpaceType(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "InputAddressSpaceType"))
	{
		nErrCode = GetInputAddressSpaceType(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "OutputAddressSpaceType"))
	{
		nErrCode = GetOutputAddressSpaceType(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "ControlAddressOffset"))
	{
		nErrCode = GetControlAddressOffset(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "StatusAddressOffset"))
	{
		nErrCode = GetStatusAddressOffset(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "InputAddressOffset"))
	{
		nErrCode = GetInputAddressOffset(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "OutputAddressOffset"))
	{
		nErrCode = GetOutputAddressOffset(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "ControlAddressQuantity"))
	{
		nErrCode = GetControlAddressQuantity(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "StatusAddressQuantity"))
	{
		nErrCode = GetStatusAddressQuantity(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "InputAddressQuantity"))
	{
		nErrCode = GetInputAddressQuantity(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "OutputAddressQuantity"))
	{
		nErrCode = GetOutputAddressQuantity(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "HostIp"))
	{
		nErrCode = GetModbusHostIp(pBuff, nBuffSize, pDataLen);
		nValueType = 4;
	}
	else if (0 == strcmp(szParamName, "ModbusPort"))
	{
		nErrCode = GetModbusPort(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "ModbusMode"))
	{
		nErrCode = GetModbusMode(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "ServerIp"))
	{
		nErrCode = GetModbusServerIp(pBuff, nBuffSize, pDataLen);
		nValueType = 4;
	}
	else if (0 == strcmp(szParamName, "ServerPort"))
	{
		nErrCode = GetModbusServerPort(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "SlaveId"))
	{
		nErrCode = GetModbusSlaveId(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "PollInterval"))
	{
		nErrCode = GetModbusControlPollInterval(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, "ByteOrder"))
	{
		nErrCode = GetByteOrder(&nValue);
		nValueType = 1;
	}
	else if (0 == strcmp(szParamName, MODBUS_DEBUG_LEVEL)
		|| 0 == strcmp(szParamName, INDUSTRIAL_DEBUG_LEVEL))
	{
		nValue = modbus_get_debug_level();
		nValueType = 1;
		nErrCode = IMVS_EC_OK;
	}
	else
	{
		nErrCode = IMVS_EC_ALGO_PARAM_NOT_FOUND;
	}

	if (nErrCode == IMVS_EC_OK)
	{
		if (nValueType == 1 || nValueType == 3)
		{
			snprintf(pBuff, nBuffSize, "%d", nValue);
		}
		else if (nValueType == 2)
		{
			snprintf(pBuff, nBuffSize, "%f", fValue);
		}
		else
		{
			LOGI("nValueType == 4, nothing to do\r\n");
		}
	}
	
	return nErrCode;	
}

int CModbusTransModule::GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
{
	LOGI("GetParam CModbusTransModule \r\n");

	int nRet = IMVS_EC_OK;
	if (szParamName == NULL || strlen(szParamName) == 0 || pBuff == NULL || nBuffSize <= 0 || pDataLen == NULL)
	{
		return IMVS_EC_PARAM;
	}

	if (strstr(szParamName, ALGO_DEBUG_INFO_PARAM_STR))
	{
		return modbus_get_debug_info(pBuff, nBuffSize, pDataLen);
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

int CModbusTransModule::SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{
	int nErrCode = IMVS_EC_OK;
	
	LOGI("SetmoduParaHandle CModbusTransModule \r\n");
	if (0 == strcmp(szParamName, "ControlAddressSpaceType"))
	{
		nErrCode = SetControlAddressSpaceType(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "StatusAddressSpaceType"))
	{
		nErrCode = SetStatusAddressSpaceType(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "InputAddressSpaceType"))
	{
		nErrCode = SetInputAddressSpaceType(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "OutputAddressSpaceType"))
	{
		nErrCode = SetOutputAddressSpaceType(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "ControlAddressOffset"))
	{
		nErrCode = SetControlAddressOffset(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "StatusAddressOffset"))
	{
		nErrCode = SetStatusAddressOffset(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "InputAddressOffset"))
	{
		nErrCode = SetInputAddressOffset(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "OutputAddressOffset"))
	{
		nErrCode = SetOutputAddressOffset(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "ControlAddressQuantity"))
	{
		nErrCode = SetControlAddressQuantity(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "StatusAddressQuantity"))
	{
		nErrCode = SetStatusAddressQuantity(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "InputAddressQuantity"))
	{
		nErrCode = SetInputAddressQuantity(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "OutputAddressQuantity"))
	{
		nErrCode = SetOutputAddressQuantity(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "HostIp"))
	{
		nErrCode = SetModbusHostIp(pData);
	}
	else if (0 == strcmp(szParamName, "ModbusPort"))
	{
		nErrCode = SetModbusPort(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "ModbusMode"))
	{
		nErrCode = SetModbusMode(atoi(pData));
		if (initialized)
		{
			nErrCode = modbus_deinit();
			if (nErrCode != IMVS_EC_OK)
			{
				LOGE("modbus_deinit %d\n", nErrCode);
			}
			modbus_para.sys_run_status = &m_ePlayStatus;
			nErrCode = init_modbus_msg(&modbus_para);
			if (nErrCode != IMVS_EC_OK)
			{
				LOGE("init_modbus_msg %d\n", nErrCode);
			}
		}
	}
	else if (0 == strcmp(szParamName, "ServerIP"))
	{
		nErrCode = SetModbusServerIp(pData);
		if ((MODBUS_CLIENT_MODE == modbus_para.iWorkMode) && initialized)
		{
			nErrCode = modbus_deinit();
			if (nErrCode != IMVS_EC_OK)
			{
				LOGE("modbus_deinit %d\n", nErrCode);
			}
			modbus_para.sys_run_status = &m_ePlayStatus;
			nErrCode = init_modbus_msg(&modbus_para);
			if (nErrCode != IMVS_EC_OK)
			{
				LOGE("init_modbus_msg %d\n", nErrCode);
			}
		}
	}
	else if (0 == strcmp(szParamName, "ServerPort"))
	{
		nErrCode = SetModbusServerPort(atoi(pData));
		if ((MODBUS_CLIENT_MODE == modbus_para.iWorkMode) && initialized)
		{
			nErrCode = modbus_deinit();
			if (nErrCode != IMVS_EC_OK)
			{
				LOGE("modbus_deinit %d\n", nErrCode);
			}
			modbus_para.sys_run_status = &m_ePlayStatus;
			nErrCode = init_modbus_msg(&modbus_para);
			if (nErrCode != IMVS_EC_OK)
			{
				LOGE("init_modbus_msg %d\n", nErrCode);
			}
		}
	}
	else if (0 == strcmp(szParamName, "SlaveId"))
	{
		nErrCode = SetModbusSlaveId(atoi(pData));
		if ((MODBUS_CLIENT_MODE == modbus_para.iWorkMode) && initialized)
		{
			nErrCode = modbus_set_slaveId();
			if (nErrCode != IMVS_EC_OK)
			{
				nErrCode = IMVS_EC_OK;
				LOGE("modbus_set_slaveId %d\n", nErrCode);
			}
		}
	}
	else if (0 == strcmp(szParamName, "PollInterval"))
	{
		nErrCode = SetModbusControlPollInterval(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "ByteOrder"))
	{
		nErrCode = SetByteOrder(atoi(pData));
	}
	else if (0 == strcmp(szParamName, MODBUS_DEBUG_LEVEL)
		|| 0 == strcmp(szParamName, INDUSTRIAL_DEBUG_LEVEL))
	{
		nErrCode = modbus_set_debug_level(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "ByteOrderEnable"))
	{
		nErrCode = SetByteOrderEnable(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "Spacer"))
	{
		nErrCode = SetSpacer(atoi(pData));
	}
	else if (0 == strcmp(szParamName, MODBUS_MOUDLE_ENABLE))
	{
		nErrCode = SetModuleEnable(atoi(pData));
	}
	else
	{
		nErrCode = IMVS_EC_ALGO_PARAM_NOT_FOUND;
	}

	return nErrCode;	
}

int CModbusTransModule::SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{
	LOGI("SetParam CModbusTransModule \r\n");
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

		//nErrCode = m_pProcessObj->SetParam(&udp_man, szParamName, pData, nDataLen);
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

int CModbusTransModule::UpParam4All(IN const char* szParamName, IN const char* pData)
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

int CModbusTransModule::UpSingleParam(IN const char* pParamName, bool bForce)
{
	if (NULL == pParamName)
	{
		return IMVS_EC_PARAM;
	}
	
	char szParamValue[PARAM_VALUE_LEN] = {0};
	int nDataLen = 0;
	int nErrCode = IMVS_EC_OK;

	if (0 == strcmp(pParamName, "HostIp"))
	{
		nErrCode = GetModbusHostIp(szParamValue, PARAM_VALUE_LEN, &nDataLen);
		if (IMVS_EC_OK != nErrCode)
		{
			LOGE("GetModbusHostIp failed, nErrCode = %d\r\n", nErrCode);
			return IMVS_EC_CAMERA_RUNTIME;
		}
	}

	// 刷新参数值
	nErrCode = UpParam4All(pParamName, szParamValue);
	if (IMVS_EC_OK != nErrCode)
	{
		LOGE("UpParam4All param %s value %s failed, nErrCode = %d\r\n", pParamName, szParamValue, nErrCode);
	}

	return IMVS_EC_OK;
}

int CModbusTransModule::SetProcedureName(IN const char* szProcedureName)
{
	if (szProcedureName == NULL)
	{
		return IMVS_EC_PARAM;
	}

	int nErrCode = set_procedure_name(szProcedureName);

	return nErrCode;
}

int CModbusTransModule::InitAlgoPrivate()
{	
	int nErrCode = IMVS_EC_OK;
	int nRet = 0;

	PARAM_VALUE_INFO pParamValueInfo;
	do
	{

		LOGI("CModbusTransModule init \r\n");
		DeInit();

		std::string ip = m_paramManage->GetParam<std::string>(HOST_IP);
		int len = 0;
		nRet = GetModbusHostIp((char*)ip.c_str(), ip.length(), &len);
		if (IMVS_EC_OK != nRet)
		{
			LOGE("GetModbusHostIp failed, ret %d\r\n", nRet);
		}

		//设置模块参数
		auto callback = [&](std::string paramName,std::string paramValue)->int
		{
			nErrCode = SetParam(paramName.c_str(), paramValue.c_str(), paramValue.length());
			LOGE("set aram %s value %s ret %d\r\n", paramName.c_str(), paramValue.c_str(), nRet);
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
		nRet = thread_spawn_ex(&m_nThreadScheduledTrans, 1, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, ThreadProcessScheduledTrans, this);
		if (IMVS_EC_OK != nRet)
		{
			LOGE("GenerateHeadInfo failed, ret %d\r\n", nRet);
			nErrCode = IMVS_EC_ALGO_HEAD_PARAM_ERROR;
			break;
		}
		
		modbus_para.sys_run_status = &m_ePlayStatus;
		nRet = init_modbus_msg(&modbus_para);
		if (nRet != IMVS_EC_OK)
		{
			LOGE("init_modbus_msg %d\n", nRet);
		}


		GetSingleParam(10, &pParamValueInfo);
		nErrCode = SetInputAddressQuantity(atoi(pParamValueInfo.byParamValue));
		
		GetSingleParam(18, &pParamValueInfo);
		nErrCode = SetOutputAddressQuantity(atoi(pParamValueInfo.byParamValue));
		
		initialized = 1;
	}while(0);

	return nErrCode;
}

int CModbusTransModule::DeInit(void)
{

	LOGI("modbus deinit \r\n");
	int nErrCode = IMVS_EC_OK;
	if(m_nThreadScheduledTrans)
	{
		pthread_cancel(m_nThreadScheduledTrans);
		(void)pthread_join(m_nThreadScheduledTrans, nullptr);
		m_nThreadScheduledTrans = 0;
		LOGI("pthread_cancel m_nThreadScheduledTrans end \r\n");
	}
	if (initialized)
	{
		(void)modbus_deinit();
	}

	initialized = 0;
	return nErrCode;
}

CAbstractUserModule* CreateModule(void* hModule)
{
	assert(hModule != NULL);

	CAbstractUserModule* pUserModule = new CModbusTransModule;

	return pUserModule;
}

void DestroyModule(void* hModule, CAbstractUserModule* pUserModule)
{
	assert(hModule != NULL);
	assert(pUserModule != NULL);
	
	delete pUserModule;
	pUserModule = NULL;
}
