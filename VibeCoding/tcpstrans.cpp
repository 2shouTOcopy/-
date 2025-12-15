#include <unistd.h>

#include "tcpstrans.h"

#include "char_conversion.h"
#include "AppParamCommon.h"
#include "algo_common.h"

#include <net/if.h>
#include <sys/ioctl.h>

#include "log/log.h"
#include "IImageProcess.h"
#include "ITriggerSource.h"
#include "infra/DataBuffer.h"

#define DEBUG_GLOBAL_MDC_STRING "CTcpstransModule"
#define TCPSTRANS_MOUDLE_ENABLE	"AlgoEnable"
#define HEARTBEAT_TRAN_MODE		"HeartbeatTranMode"
#define HEARTBEAT_TEXT			"HeartbeatText"
#define HEARTBEAT_INTERVAL		"HeartbeatInterval"

namespace tcps
{

CTcpstransModule::CTcpstransModule()
    : m_pMessageObj{nullptr}
{
}

CTcpstransModule::~CTcpstransModule()
{
	DeInit();
}

int CTcpstransModule::Process(IN void* hInput, IN void* hOutput)
{
	std::string strValue;
	int nErrCode = IMVS_EC_OK;
	int status = 1;
	int nFrame = 0;
	int nTrigger = 0;
	int nExcludeNum = 0;
	int paramStatus = -1;
	int MoudleEnable = 0;

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
            		int nSchedTransEnable = 0;
                	try
					{
						nSchedTransEnable = m_paramManage->GetParam<int>(PM_SCHEDULED_TRANS_ENABLE);
					}
					catch (const std::invalid_argument& e)
					{
						LOGE("Get Param Filed %s \n\n", e.what());
						return false;
					}
                    if (nSchedTransEnable)
                        return nTrigger != m_pMessageObj->m_nRecordTriggerCount ? true : false;
                }
            }
        }

        return true;
    };

	UP_PSTIME();
	
	LOGI("tcpstrans Process start\n");

	ScFramePtr pImgFrame = VM_M_Get_Frame_ByID(hInput, 0);
	if (!pImgFrame)
	{
		LOGE("get frame by id failed, no data\r\n");
		return IMVS_EC_ALGO_NO_DATA;
	}

	nErrCode = pImgFrame->getIntVal("SINGLE_frame_cnt", nFrame);
	if (IMVS_EC_OK != nErrCode)
	{
		LOGE("get subscribe SINGLE_frame_cnt failed, no data\r\n");
		return IMVS_EC_ALGO_NO_DATA;
	}

	nErrCode = pImgFrame->getIntVal("SINGLE_trigger_cnt", nTrigger);
	if (IMVS_EC_OK != nErrCode)
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
		MoudleEnable = m_paramManage->GetParam<int>(TCPSTRANS_MOUDLE_ENABLE);
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
		m_pMessageObj->SetFrameAddTrigger(nFrame, nTrigger,MoudleEnable);
		ScFramePtr pOutFrame = VM_M_Get_Frame_ByHOutput(hOutput);
		if (MoudleEnable)
		{
			scheduled_fifo_param p{};
			p.exclude_num = nExcludeNum;
			p.len = std::min<uint32_t>(binaryValue->getLen(), MAX_SCHEDULED_PAYLOAD_LEN);
			std::memcpy(p.buf, binaryValue->getBuffer(), p.len);
			paramStatus = m_pMessageObj->EnqueueScheduled(p);
		}

		pOutFrame->setVal("SINGLE_status", status);		
		pOutFrame->setVal("SINGLE_param_status", (0 == paramStatus) ? 1:0);
	}
	else
	{
		nErrCode = IMVS_EC_ALGO_NO_DATA;
		LOGE("get subscribe failed, no data\r\n");
		LOGE("nTrigger:%d, m_nRecord:%d\r\n", nTrigger, m_pMessageObj->m_nRecordTriggerCount);
	}

	LOGI("tcpstrans Process end\n");

	UP_PTIME();
	
	return nErrCode;
}

int CTcpstransModule::SetPwd(IN const char* szPwd)
{
	snprintf(m_moduleName, sizeof(m_moduleName), "%s", ALGO_NAME);
	m_pwd = szPwd;
	return CAlgo::Init();
}

int CTcpstransModule::GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen)
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

int CTcpstransModule::SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen)
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
		else
		{
			LOGE("SetParam fail,nErrCode = %d\r\n",nErrCode);
		}
		if (0 == strcmp(szParamName, TCPSTRANS_MOUDLE_ENABLE))
		{
			m_pMessageObj->SetModuleEnable(atoi(pData));
		}
		else if (0 == strcmp(szParamName, PM_SCHEDULED_TRANS_ENABLE))
		{
			m_pMessageObj->SetScheduledTransEnable(atoi(pData));
		}
		else if (0 == strcmp(szParamName, PM_SCHEDULED_TRANS_TIME))
		{
			m_pMessageObj->SetScheduledTransTime(atoi(pData));
		}
		else if (0 == strcmp(szParamName, HEARTBEAT_TRAN_MODE))
		{
			m_pMessageObj->SetHeartbeatTranMode(atoi(pData));
		}
		else if (0 == strcmp(szParamName, HEARTBEAT_TEXT))
		{
			m_pMessageObj->SetHeartbeatText(pData);
		}
		else if (0 == strcmp(szParamName, HEARTBEAT_INTERVAL))
		{
			m_pMessageObj->SetHeartbeatInterval(atoi(pData));
		}
	}
	return nErrCode;
}

int CTcpstransModule::UpParam4All(IN const char* szParamName, IN const char* pData)
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

int CTcpstransModule::UpSingleParam(IN const char* pParamName, bool bForce)
{
	if (NULL == pParamName)
	{
		return IMVS_EC_PARAM;
	}
	
	char szParamValue[PARAM_VALUE_LEN] = {0};
	int nErrCode = IMVS_EC_OK;

	if (0 == strcmp(pParamName, "HostIp"))
	{
		nErrCode = GetHostIp(szParamValue);
		if (IMVS_EC_OK != nErrCode)
		{
			LOGE("GetHostIp failed, nErrCode = %d\r\n", nErrCode);
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

int CTcpstransModule::InitAlgoPrivate()
{
	int nErrCode = IMVS_EC_OK;
	char strValue[256] = {0};
	int value = 0;
	int localPort = 0;
	int localHbPort = 0;

	LOGI("tcpstrans Init start\n");
	do
	{	
		if (NULL == m_pMessageObj)
		{
			m_pMessageObj = new tcps::CTcpsMessage;
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
		
		std::string ip = m_paramManage->GetParam<std::string>(HOST_IP);
		GetHostIp((char*)ip.c_str());

		m_pMessageObj->SetLogId(m_nLogId);

		localPort = m_paramManage->GetParam<int>(HOST_PORT);
		localHbPort = m_paramManage->GetParam<int>(HOST_HEARTBEAT_PORT);

		nErrCode = m_pMessageObj->Init(localPort, localHbPort);
	
		sprintf(strValue, "%d", localPort);
		SetParam("HostPort", strValue, strlen(strValue));

		sprintf(strValue, "%d", localHbPort);
		SetParam("HostHeartbeatPort", strValue, strlen(strValue));

		value = m_paramManage->GetParam<int>(TCPSTRANS_MOUDLE_ENABLE);
		m_pMessageObj->SetModuleEnable(value);

		value = m_paramManage->GetParam<int>(PM_SCHEDULED_TRANS_ENABLE);
		m_pMessageObj->SetScheduledTransEnable(value);

		value = m_paramManage->GetParam<int>(PM_SCHEDULED_TRANS_TIME);
		m_pMessageObj->SetScheduledTransTime(value);

		value = m_paramManage->GetParam<int>(HEARTBEAT_TRAN_MODE);
		m_pMessageObj->SetHeartbeatTranMode(value);

		std::string text = m_paramManage->GetParam<std::string>(HEARTBEAT_TEXT);
		m_pMessageObj->SetHeartbeatText(text.c_str());

		value = m_paramManage->GetParam<int>(HEARTBEAT_INTERVAL);
		m_pMessageObj->SetHeartbeatInterval(value);
		
		LOGI("local_port:%d, local_hb_port:%d\n", localPort, localHbPort);
		LOGI("tcpstrans Init end\n");
	} while(0);

	return nErrCode;
}

int CTcpstransModule::DeInit()
{
	int nErrCode = IMVS_EC_OK;

	if (m_pMessageObj)
	{
		m_pMessageObj->DeInit();
		
		delete m_pMessageObj;
		m_pMessageObj = NULL;
	}

	return nErrCode;	
}

int CTcpstransModule::SetProcedureName(IN const char* szProcedureName)
{
	if (szProcedureName == NULL)
	{
		return IMVS_EC_PARAM;
	}

	int nErrCode = m_pMessageObj->SetProcedureName(szProcedureName);

	return nErrCode;
}

int CTcpstransModule::SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen)
{
	if(szParamName == NULL || pData == NULL)
	{
		return IMVS_EC_NULL_PTR;
	}

	int nErrCode = IMVS_EC_OK;
	if (0 == strcmp(szParamName, "HostIp"))
	{
		nErrCode = SetTcpHostIp(pData);
	}
	else if (0 == strcmp(szParamName, "HostPort"))
	{
		nErrCode = SetTcpHostPost(atoi(pData));
	}
	else if (0 == strcmp(szParamName, "HostHeartbeatPort"))
	{
		nErrCode = SetTcpHostHeartbeatPost(atoi(pData));
	}
	else
	{
		nErrCode = IMVS_EC_ALGO_PARAM_NOT_FOUND;
	}

	return nErrCode;	
}

int CTcpstransModule::SetTcpHostIp(IN const char* szIp)
{
	int nErrCode = IMVS_EC_OK;
	uint32_t addr = 0;

	if (NULL == szIp)
	{
		return IMVS_EC_NULL_PTR;
	}
	LOGI("%s\n", szIp);
	addr = inet_addr(szIp);
	if (addr == INADDR_NONE)
	{
		return -2;
	}

	m_pMessageObj->SetHostIp(ntohl(addr));

	return nErrCode;	
}

int CTcpstransModule::SetTcpHostPost(IN int nPort)
{
	int nErrCode = IMVS_EC_OK;
	
	nErrCode = m_pMessageObj->SetHostPort(nPort);

	return nErrCode;
}

int CTcpstransModule::SetTcpHostHeartbeatPost(IN int nPort)
{
	int nErrCode = IMVS_EC_OK;
	
	nErrCode = m_pMessageObj->SetHostHeartbeatPort(nPort);

	return nErrCode;	
}

int CTcpstransModule::GetHostIp(OUT char* szIp)
{

	if(NULL == szIp)
	{
		return IMVS_EC_NULL_PTR;
	}
	uint32_t ip_addr = 0;	

	if (get_ipaddr((char*)NET_INTERFACE, &ip_addr) < 0)
	{
		LOGE("get ip addr failed!\r\n");
		return -1;
	}

	snprintf(szIp, 32, "%d.%d.%d.%d", IPQUAD(ip_addr));

	LOGI("ip_addr is %s\r\n", szIp);
	return IMVS_EC_OK;	
}

int CTcpstransModule::get_ipaddr(IN char *ifname, OUT unsigned int *ip)
{
	int s;
	struct ifreq ifr;
	struct sockaddr_in *ptr;
	struct in_addr addr_temp;

	if ((NULL == ifname) || (NULL == ip))
	{
		return -1;
	}
	
	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) 
	{
		return -1;
	}

	strncpy(ifr.ifr_name, ifname, 15);
	ifr.ifr_name[15] = '\0';

	if (ioctl(s, SIOCGIFADDR, &ifr) < 0) 
	{
		LOGD("get_ipaddr ioctl error and errno=%d\n", errno);
		close(s);
		return -1;
	}

	ptr = (struct sockaddr_in *)&ifr.ifr_ifru.ifru_netmask;
	addr_temp = ptr->sin_addr;
	*ip = ntohl(addr_temp.s_addr);
	close(s);
	
	return 0;
}

}

CAbstractUserModule* CreateModule(void* hModule)
{
	assert(hModule != NULL);

	CAbstractUserModule* pUserModule = new tcps::CTcpstransModule;

	//printf("[ DLClassifyModule ] CreateModule, hModule = 0x%x, pUserModule = 0x%x \n", hModule, pUserModule);

	return pUserModule;
}

void DestroyModule(void* hModule, CAbstractUserModule* pUserModule)
{
	assert(hModule != NULL);
	assert(pUserModule != NULL);
	
	//printf("\n[ DLClassifyModule ] DestroyModule, hModule = 0x%x\n", hModule);

	delete pUserModule;
	pUserModule = NULL;
}

