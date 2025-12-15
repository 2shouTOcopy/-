/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   DL Classify algo main class
  *
  * @author  tanpeng7
  * @date    2019/10/09
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2019/10/09  |V1.0.0  |tanpeng7           |创建代码文档
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
#include "tcpsMsg.h"
#include "algo.h"


using namespace std;

#define ALGO_DIR            ALGO_ROOT_DIR"tcpstrans/json/"
#define ALGO_ABI_JSON_PATH  ALGO_DIR ALGO_ABI_JSON_NAME
#define ALGO_IO_JSON_PATH   ALGO_DIR ALGO_IO_JSON_NAME
#define ALGO_PM_JSON_PATH   ALGO_DIR ALGO_PM_JSON_NAME
#define ALGO_DISP_JSON_PATH ALGO_DIR ALGO_DISP_JSON_NAME
#define ALGO_NAME           "tcpstrans"

#define HOST_IP				"HostIp"
#define	HOST_PORT			"HostPort"
#define HOST_HEARTBEAT_PORT	"HostHeartbeatPort"
#define	NET_INTERFACE		"eth0"
#define MAX_TCP_PAYLOAD_LEN (1280)

#define IPQUAD(ip)   \
		((unsigned char *)&(ip))[3], \
		((unsigned char *)&(ip))[2], \
		((unsigned char *)&(ip))[1], \
		((unsigned char *)&(ip))[0]


namespace tcps
{

// This class is exported from the libtcpstrans.so
class CTcpstransModule : public CAlgo
{
public:
	CTcpstransModule();
	~CTcpstransModule();
	
public:
	int Process(IN void* hInput, IN void* hOutput);
	int SetPwd(IN const char* szPwd);
	void UpdatePwdPrivate(const char* szPwd) override{return;};
	int GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen);
	int SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen);

	int SetProcedureName(IN const char* szProcedureName);
	int UpParam4All(IN const char* szParamName, IN const char* pData);
	int UpSingleParam(IN const char* pParamName, bool bForce);
	
private:
	int InitAlgoPrivate();
	int InitROI(int logId,std::string moduleName,std::string pwd)override{return IMVS_EC_OK;};
	int DeInit();

	int SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen);
	int SetTcpHostIp(IN const char* szIp);
	int SetTcpHostPost(IN int nPort);
	int SetTcpHostHeartbeatPost(IN int nPort);
	int GetHostIp(OUT char* szIp);
	int get_ipaddr(IN char *ifname, OUT unsigned int *ip);

	CTcpsMessage* m_pMessageObj;
};

}


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
