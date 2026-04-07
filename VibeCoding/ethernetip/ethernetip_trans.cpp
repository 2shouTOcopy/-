#include <unistd.h>
#include <stdexcept>
#include "ethernetip_trans.h"
#include "utils.h"
#include "thread/ThreadApi.h"
#include "util_net.h"
#include "log/log.h"
#include "AppParamCommon.h"
#include "ITriggerSource.h"
#include "IImageProcess.h"
#include "infra/DataBuffer.h"
#include "outputMode.h"

#define DEBUG_GLOBAL_MDC_STRING "CEthernetipTransModule"
#define ETHERNETIP_MOUDLE_ENABLE "AlgoEnable"
#define ETHERNETIP_INPUT_SIZE "EthernetIpInputAssemblySize"
#define ETHERNETIP_OUTPUT_SIZE "EthernetIpOutputAssemblySize"
#define ETHERNETIP_RESULT_BYTE_SWAP "EthernetIpResultByteSwapEnable"
#define INDUSTRIAL_DEBUG_LEVEL "IndustrialDebugLevel"

void *ThreadProcessScheduledTrans(IN void* argv)
{
	CEthernetipTransModule* pUserModule = (CEthernetipTransModule*)argv;
	if (!pUserModule) return nullptr;

	char szGetString[64] = {0};
	snprintf(szGetString, sizeof(szGetString), "%s_eip", SCHEDULED_NAME);
	thread_set_name(szGetString);

	SimpleFifoAdapter qadp(&(pUserModule->m_struScheduledQueue));

	// 发送函数
	auto sendFn = [&](const scheduled_fifo_param& p)
	{
		if (p.len <= sizeof(p.buf))
		{
			ethernetip_send_result((char*)p.buf, (int)p.len);
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
			mEnable    = pUserModule->GetParamManagePtr()->GetParam<int>(ETHERNETIP_MOUDLE_ENABLE);
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

CEthernetipTransModule::CEthernetipTransModule(void)
{
	initialized = 0;
	m_nRecordTriggerCount = -1;
	m_nThreadScheduledTrans = 0;
	memset(&m_struScheduledFifoParamArray, 0, sizeof(m_struScheduledFifoParamArray));
	memset(&m_struScheduledQueue, 0, sizeof(m_struScheduledQueue));
}

CEthernetipTransModule::~CEthernetipTransModule(void)
{
	DeInit();
}

bool CEthernetipTransModule::AlgoNeedDLClose(void)
{
	return false;
}

int CEthernetipTransModule::Process(IN void* hInput, IN void* hOutput)
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

	if (0 == initialized)
	{
		return -1;
	}
	UP_PSTIME();

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
		MoudleEnable = m_paramManage->GetParam<int>(ETHERNETIP_MOUDLE_ENABLE);
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

	UP_PTIME();

	return nErrCode;
}

int CEthernetipTransModule::SetPwd(IN const char* szPwd)
{
	snprintf(m_moduleName, sizeof(m_moduleName), "%s", ALGO_NAME);
	m_pwd = szPwd;
	return CAlgo::Init();
}

int CEthernetipTransModule::GetmoduParaHandle(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
{
	if (0 == strcmp(szParamName, INDUSTRIAL_DEBUG_LEVEL))
	{
		snprintf(pBuff, nBuffSize, "%d", ethernetip_get_debug_level());
		*pDataLen = strlen(pBuff);
		return IMVS_EC_OK;
	}
	return IMVS_EC_ALGO_PARAM_NOT_FOUND;	
}

int CEthernetipTransModule::GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
{
	int nRet = IMVS_EC_OK;
	if (szParamName == NULL || strlen(szParamName) == 0 || pBuff == NULL || nBuffSize <= 0 || pDataLen == NULL)
	{
		return IMVS_EC_PARAM;
	}

	if (strstr(szParamName, ALGO_DEBUG_INFO_PARAM_STR))
	{
		return (ethernetip_get_debug_info(pBuff, nBuffSize, pDataLen) == 0) ? IMVS_EC_OK : IMVS_EC_SYSTEM_INNER_ERR;
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

int CEthernetipTransModule::SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{
	int nErrCode = IMVS_EC_OK;
	int32_t nTriggerCount = 0;
	
	if (0 == strcmp(szParamName, ETHERNETIP_INPUT_SIZE))
	{
		nErrCode = ethernetip_set_input_size(atoi(pData));
		if (nErrCode < 0)
		{
			nErrCode = IMVS_EC_ALGO_PARAM_NOT_VALID;
		}
	}
	else if (0 == strcmp(szParamName, ETHERNETIP_OUTPUT_SIZE))
	{
		nErrCode = ethernetip_set_output_size(atoi(pData));
		if (nErrCode < 0)
		{
			nErrCode = IMVS_EC_ALGO_PARAM_NOT_VALID;
		}
	}
	else if (0 == strcmp(szParamName, ETHERNETIP_RESULT_BYTE_SWAP))
	{
		nErrCode = ethernetip_set_result_byte_swap(atoi(pData));
		if (nErrCode < 0)
		{
			nErrCode = IMVS_EC_ALGO_PARAM_NOT_VALID;
		}
	}
	else if (0 == strcmp(szParamName, INDUSTRIAL_DEBUG_LEVEL))
	{
		nErrCode = ethernetip_set_debug_level(atoi(pData));
		if (nErrCode < 0)
		{
			nErrCode = IMVS_EC_ALGO_PARAM_NOT_VALID;
		}
	}
	else if (0 == strcmp(szParamName, ETHERNETIP_MOUDLE_ENABLE))
	{
		auto trigInstance = mvsc_idr_app::ITriggerSource::getComponent();
		if(trigInstance == nullptr)
		{
			LOGE("trigger source instance nullptr\n");
			return IMVS_EC_NULL_PTR;
		}

		nErrCode = ethernetip_set_module_enable(atoi(pData));
		if (trigInstance->GetParam(TRIGGER_COUNT, nTriggerCount))
		{
			LOGE("get trigger count failed\r\n");
		}
		else
		{
			m_nRecordTriggerCount = nTriggerCount;
		}
	}
	else
	{
		nErrCode = IMVS_EC_ALGO_PARAM_NOT_FOUND;
	}
	
	return nErrCode;	
}

int CEthernetipTransModule::SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen)
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
		LOGD("SetParam %s: %s\r\n", szParamName, pData);

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

int CEthernetipTransModule::SetProcedureName(IN const char* szProcedureName)
{
	if (szProcedureName == NULL)
	{
		return IMVS_EC_PARAM;
	}

	int nErrCode = set_procedure_name(szProcedureName);

	return nErrCode;
}

int CEthernetipTransModule::InitAlgoPrivate()
{	
	int nErrCode = IMVS_EC_OK;
	int nRet = 0;

	PARAM_VALUE_INFO pParamValueInfo;

	LOGI("CEthernetipTransModule init start  \r\n");
	do
	{
		//GetHostIp(m_struModuleParamInfo.ArrParamCell[0].unCellValue.stBaseCell.value.strValue, 0, NULL);
		
        if(DeInit())
		{
			LOGE("DeInit Failed And Return \n\n");
			return IMVS_EC_SYSTEM_INNER_ERR;		
		}

		GetSingleParam(2, &pParamValueInfo);
		LOGD("%s %s \r\n", pParamValueInfo.byParamName, pParamValueInfo.byParamValue);
		ethernetip_set_init_input_size(atoi(pParamValueInfo.byParamValue));
		
		GetSingleParam(3, &pParamValueInfo);
		LOGD("%s %s \r\n", pParamValueInfo.byParamName, pParamValueInfo.byParamValue);
		ethernetip_set_init_output_size(atoi(pParamValueInfo.byParamValue));
		
		GetSingleParam(4, &pParamValueInfo);
		LOGD("%s %s \r\n", pParamValueInfo.byParamName, pParamValueInfo.byParamValue);
		ethernetip_set_result_byte_swap(atoi(pParamValueInfo.byParamValue));
		
		int MoudleEnable = 0;
		try
		{
			MoudleEnable = m_paramManage->GetParam<int>(ETHERNETIP_MOUDLE_ENABLE);
		}
		catch (const std::invalid_argument& e)
		{
			LOGE("Get Param Filed %s \n\n", e.what());
			return IMVS_EC_PARAM;
		}
		std::string value = std::to_string(MoudleEnable);
		//设置算子参数
		nRet = SetParam(ETHERNETIP_MOUDLE_ENABLE, value.c_str(), value.length());
		if (IMVS_EC_OK != nRet)
		{
			LOGE("SetParam PROFINET_MOUDLE_ENABLE failed, nRet %d\r\n", nRet);
			nErrCode = IMVS_EC_ALGO_HEAD_PARAM_ERROR;
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
		
		LOGI(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>%s %d, will go into init_eip_msg\r\n",__func__,__LINE__);
		sys_run_status = &m_ePlayStatus;
		if (ethernetip_msg_init() < 0)
		{
			nErrCode = IMVS_EC_MODULE_CREATE_ALG_MODULE_FAILED;
			break;
		}
		else
		{
			initialized = 1;
		}
	}while(0);
	LOGI("CEthernetipTransModule init end,%d  \r\n", nErrCode);

	return nErrCode;
}

int CEthernetipTransModule::DeInit(void)
{
	int nErrCode = IMVS_EC_OK;
	if (m_nThreadScheduledTrans)
	{
		pthread_cancel(m_nThreadScheduledTrans);
		(void)pthread_join(m_nThreadScheduledTrans, nullptr);
		m_nThreadScheduledTrans = 0;
		LOGI("pthread_cancel m_nThreadScheduledTrans end \r\n");
		ethernetip_msg_deinit();
	}
	LOGI("deinit CEthernetipTransModule \r\n");
	return nErrCode;
}

CAbstractUserModule* CreateModule(void* hModule)
{
	assert(hModule != NULL);

	CAbstractUserModule* pUserModule = new CEthernetipTransModule;

	return pUserModule;
}

void DestroyModule(void* hModule, CAbstractUserModule* pUserModule)
{
	assert(hModule != NULL);
	assert(pUserModule != NULL);
	
	delete pUserModule;
	pUserModule = NULL;
}
