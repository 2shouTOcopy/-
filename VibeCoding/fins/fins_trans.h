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

#include <string>
#include <unordered_map>

#include "VmModuleBase.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "VmTypeDef.h"
#include "algo_common.h"
#include "json_tool.h"
#include "fins_msg.h"
#include "algo.h"

using namespace std;

#define ALGO_DIR            ALGO_ROOT_DIR"fins/json/"
#define ALGO_ABI_JSON_PATH  ALGO_DIR ALGO_ABI_JSON_NAME
#define ALGO_IO_JSON_PATH   ALGO_DIR ALGO_IO_JSON_NAME
#define ALGO_PM_JSON_PATH   ALGO_DIR ALGO_PM_JSON_NAME
#define ALGO_DISP_JSON_PATH ALGO_DIR ALGO_DISP_JSON_NAME
#define ALGO_NAME           "fins"

class CFinsTransModule : public CAlgo
{
public:
    typedef int (CFinsTransModule::*IntParamSet) (int nValue);
    typedef int (CFinsTransModule::*IntParamGet) (int* nValue);
    typedef int (CFinsTransModule::*StrParamSet) (const char* szValue);
    typedef int (CFinsTransModule::*StrParamGet) (char* szValue, int nSize, int* nLen);
    typedef int (CFinsTransModule::*BoolParamSet) (bool bValue);
    typedef int (CFinsTransModule::*BoolParamGet) (bool* bValue);

    typedef enum
    {
    	PVOID = 0,
    	PINT,
    	PFLOAT,
    	PSTRING,
    	PBOOL,
    }PARAMTYPE;

    typedef struct
    {
    	char          szName[32];
    	PARAMTYPE     enType;
    	// int 
    	IntParamSet   IntSetFunc;
    	IntParamGet   IntGetFunc;
    	// str
    	StrParamSet   StrSetFunc;
    	StrParamGet   StrGetFunc;
    	// bool
    	BoolParamSet  BoolSetFunc;	
    	BoolParamGet  BoolGetFunc;
    } ModuleParamOptSet;

public:
	CFinsTransModule();
	~CFinsTransModule();

public:
	int Process(IN void* hInput, IN void* hOutput);
	int SetPwd(IN const char* szPwd);
	void UpdatePwdPrivate(const char* szPwd) override{return;};
	int SetParam(IN const char* szParamName, IN const char* pData, IN int nDataLen);
	int SetProcedureName(IN const char* szProcedureName);
	
	int GetParam(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen);
	bool AlgoNeedDLClose(void);
	int UpParam4All(IN const char* szParamName, IN const char* pData);
	int UpSingleParam(IN const char* pParamName, bool bForce);


public:
	pthread_t m_nThreadScheduledTrans;
	scheduled_fifo_param m_struScheduledFifoParamArray[SCHEDULED_SEND_QUEUE_LEN];
	struct simple_fifo m_struScheduledQueue;
	int m_nRecordTriggerCount;

private:
	int InitAlgoPrivate();
	int InitROI(int logId,std::string moduleName,std::string pwd)override{return IMVS_EC_OK;};
	int DeInit();
	int SetModuleEnable(bool nEnable);
	int SetServerIp(const char* szIp);
	int SetServerPort(int nPort);
	int SetControlPollInterval(int nTimes);
	int SetControlAddressSpace(int nSpace);
	int SetStatusAddressSpace(int nSpace);    
    int SetInstructionAddressSpace(int nSpace);
	int SetResultAddressSpace(int nSpace);
	int SetControlAddressOffset(int nOffset);
	int SetStatusAddressOffset(int nOffset);
	int SetResultAddressOffset(int nOffset);
    int SetInstructionAddressOffset(int nOffset);
	int SetControlAddressSize(int nSize);
	int SetStatusAddressSize(int nSize);
	int SetResultAddressSize(int nSize);    
    int SetInstructionAddressSize(int nSize);
	int SetResultTimeout(int nTimes);
	int SetByteOrderEnable(bool nEnable);
	int SetIndustrialDebugLevel(int nLevel);
	
	int SetmoduParaHandle(IN const char* szParamName, IN const char* pData, IN int nDataLen);

	int GetModuleEnable(bool *pnEnable);
	int GetServerIp(char* szIp, int nBuffSize, int* pDataLen);
	int GetServerPort(int *pnPort);
	int GetControlPollInterval(int *pnTimes);
	int GetControlAddressSpace(int *pnSpace);
	int GetStatusAddressSpace(int *pnSpace);
	int GetResultAddressSpace(int *pnSpace);    
    int GetInstructionAddressSpace(int *pnSpace);
	int GetControlAddressOffset(int *pnOffset);
	int GetStatusAddressOffset(int *pnOffset);
	int GetResultAddressOffset(int *pnOffset);    
    int GetInstructionAddressOffset(int *pnOffset);
	int GetControlAddressSize(int *pnSize);
	int GetStatusAddressSize(int *pnSize);
	int GetResultAddressSize(int *pnSize);    
    int GetInstructionAddressSize(int *pnSize);

	int GetResultTimeout(int *pnTimes);
	int GetByteOrderEnable(bool *pnEnable);
	int GetIndustrialDebugLevel(int *pnLevel);
	int GetmoduParaHandle(IN const char* szParamName, OUT char* pBuff, IN int nBuffSize, OUT int* pDataLen);
		
private:

    bool m_inited;

	static fins_param_opt fins_para;
	static ModuleParamOptSet m_stParamOptSet[];
};


#ifdef __cplusplus
extern "C" {
#endif

	CAbstractUserModule* CreateModule(void* hModule);
	void DestroyModule(void* hModule, CAbstractUserModule* pUserModule);

#ifdef __cplusplus
}
#endif

