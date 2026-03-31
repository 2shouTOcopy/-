/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   DL Classify algo main class
  *
  * @author  zhengxiaoyu
  * @date    2019/10/17
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2019/10/17  |V1.0.0  |zhengxiaoyu           |创建代码文档
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
#include "modbus_msg.h"
#include "simple_fifo.h"
#include "algo.h"

using namespace std;

#define ALGO_DIR            ALGO_ROOT_DIR"modbus/json/"
#define ALGO_ABI_JSON_PATH  ALGO_DIR ALGO_ABI_JSON_NAME
#define ALGO_IO_JSON_PATH   ALGO_DIR ALGO_IO_JSON_NAME
#define ALGO_PM_JSON_PATH   ALGO_DIR ALGO_PM_JSON_NAME
#define ALGO_DISP_JSON_PATH ALGO_DIR ALGO_DISP_JSON_NAME
#define ALGO_NAME           "modbus"
#define NET_INTERFACE       "eth0"
#define MODBUS_FIXED_PORT   (502)
#define MODBUS_RES_STRING_LEN (1024)

class CModbusTransModule : public CAlgo
{
public:
	CModbusTransModule(void);
	~CModbusTransModule(void);
	
public:
	int Process(IN void* hInput, IN void* hOutput);
	int SetPwd(IN const char* szPwd);
	void UpdatePwdPrivate(const char* szPwd) override{return;};
	int GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen);
	int SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen);

	bool AlgoNeedDLClose(void);
	int SetProcedureName(IN const char* szProcedureName);
	int UpSingleParam(IN const char* pParamName, bool bForce);
	int UpParam4All(IN const char* szParamName, IN const char* pData);

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
	int SetModbusHostIp(const char* szIp);
	int SetModbusPort(int nPort);
	int SetByteOrder(int nType);
	int SetModbusMode(int nMode);
	int SetModbusServerIp(const char* szIp);
	int SetModbusServerPort(int nPort);
	int SetModbusSlaveId(int nSlaveId);
	int SetModbusControlPollInterval(int nTimes);
	int SetByteOrderEnable(int nType);
	int SetSpacer(int nSpacer);
	int SetControlAddressSpaceType(int nType);
	int SetStatusAddressSpaceType(int nType);
	int SetInputAddressSpaceType(int nType);
	int SetOutputAddressSpaceType(int nType);
	int SetControlAddressOffset(int nType);
	int SetStatusAddressOffset(int nType);
	int SetInputAddressOffset(int nType);
	int SetOutputAddressOffset(int nType);
	int SetControlAddressQuantity(int nType);
	int SetStatusAddressQuantity(int nType);
	int SetInputAddressQuantity(int nType);
	int SetOutputAddressQuantity(int nType);
	int SetModuleEnable(int nEnable);
	int GetModbusHostIp(char* szIp, int nBuffSize, int* pDataLen);
	int GetModbusPort(int* pnPort);
	int GetModbusMode(int* nMode);
	int GetModbusServerIp(char* szIp, int nBuffSize, int* pDataLen);
	int GetModbusServerPort(int* nPort);
	int GetModbusSlaveId(int* nSlaveId);
	int GetModbusControlPollInterval(int* nTimes);
	int GetByteOrder(int *nType);
	int GetMaxConnection(int *nCon);
	int GetIdleTimeoutUsec(int *nUsec);
	int GetControlAddressSpaceType(int *nType);
	int GetStatusAddressSpaceType(int *nType);
	int GetInputAddressSpaceType(int *nType);
	int GetOutputAddressSpaceType(int *nType);
	int GetControlAddressOffset(int *nType);
	int GetStatusAddressOffset(int *nType);
	int GetInputAddressOffset(int *nType);
	int GetOutputAddressOffset(int *nType);
	int GetControlAddressQuantity(int *nType);
	int GetStatusAddressQuantity(int *nType);
	int GetInputAddressQuantity(int *nType);
	int GetOutputAddressQuantity(int *nType);

	pthread_mutex_t output_lock;
	char msg_out[MAX_MODBUS_PAYLOAD_LEN];

	int initialized;
	modbus_para_opt modbus_para;
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
