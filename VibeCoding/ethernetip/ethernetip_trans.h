/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   DL Classify algo main class
  *
  * @author  zhengxiaoyu
  * @date    2019/10/28
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2019/10/28  |V1.0.0  |zhengxiaoyu           |创建代码文档
  * @warning 
  */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string>
#include <unordered_map>
#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include "VmModuleBase.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "VmTypeDef.h"
#include "algo_common.h"
#include "json_tool.h"
#include "net.h"
#include "ethernetip_msg.h"
#include "simple_fifo.h"
#include "algo.h"

using namespace std;

#define ALGO_DIR            ALGO_ROOT_DIR"ethernetip/json/"
#define ALGO_ABI_JSON_PATH  ALGO_DIR ALGO_ABI_JSON_NAME
#define ALGO_IO_JSON_PATH   ALGO_DIR ALGO_IO_JSON_NAME
#define ALGO_PM_JSON_PATH   ALGO_DIR ALGO_PM_JSON_NAME
#define ALGO_DISP_JSON_PATH ALGO_DIR ALGO_DISP_JSON_NAME
#define ALGO_NAME           "ethernetip"
#define NET_INTERFACE	"eth0"

class CEthernetipTransModule : public CAlgo
{
public:
	CEthernetipTransModule(void);
	~CEthernetipTransModule(void);
	
public:
	int Process(IN void* hInput, IN void* hOutput);
	int SetPwd(IN const char* szPwd);
	void UpdatePwdPrivate(const char* szPwd) override{return;};
	int GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen);
	int SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen);

	bool AlgoNeedDLClose(void);
	int SetProcedureName(IN const char* szProcedureName);

	pthread_t m_nThreadScheduledTrans;
	scheduled_fifo_param m_struScheduledFifoParamArray[SCHEDULED_SEND_QUEUE_LEN];
	struct simple_fifo m_struScheduledQueue;
	int m_nRecordTriggerCount;

private:
	int InitAlgoPrivate();
	int InitROI(int logId,std::string moduleName,std::string pwd)override{return IMVS_EC_OK;};
	int DeInit();
	int SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen);
	int GetmoduParaHandle(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen);

	pthread_mutex_t output_lock;
	char msg_out[MAX_ETHERNETIP_PAYLOAD_LEN];

	int initialized;
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
