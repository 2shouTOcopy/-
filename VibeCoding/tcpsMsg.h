/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author tanpeng7
 * @date 2019/10/10
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2019/10/10  |V1.0.0  |tanpeng7           |创建代码文档
 * @warning 
 */
#ifndef __TCPSMSG_H
#define __TCPSMSG_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "adapter/ScheErrorCodeDefine.h"
#include "simple_fifo.h"
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "CommProxy.h"
#include "outputMode.h"
#include "TcpsServerManager.h"

#ifndef IN
    #define IN
#endif

#ifndef OUT
    #define OUT
#endif

#define TCP_SERVER_PORT				(8192)
#define TCP_SERVER_HEARTBEAT_PORT	(8193)

namespace tcps
{

class CTcpsMessage
{
public:
	CTcpsMessage();
	~CTcpsMessage();

	static constexpr auto MAX_HB_TEXT_LEN = 32;

public:
	int Init(const uint16_t localPort, const uint16_t localHbPort);
	int DeInit();
	int InsertSendMsg(const void *data, const uint32_t len);
	int EnqueueScheduled(const scheduled_fifo_param& param);
	int SetLogId(IN int nLogId);
	int SetProcedureName(IN const char* szProcedureName);
	void SetModuleEnable(IN const int enable);
	void SetFrameAddTrigger(IN int nFrame, IN int nTrigger,IN int MoudleEnable);
	void SetScheduledTransEnable(IN int value);
	void SetScheduledTransTime(IN int value);
	void SetHostIp(const uint32_t ip);
	int SetHostPort(const uint16_t port);
	void SetHostHeartbeatIp(const uint32_t ip);
	void SetHeartbeatTranMode(const int mode);
	int SetHostHeartbeatPort(const uint16_t port);
	void SetHeartbeatText(const char* text);
	void SetHeartbeatInterval(const uint64_t interval);

	int m_nRecordTriggerCount;

	private:
		//消息服务
		TcpsServerManager m_msgServer;
		//心跳服务  
		TcpsServerManager m_hbServer;

	void DispatchQueuedMessages();
	int MsgTaskLoopCallback();
	void InitOutputEngine();

	int m_nLogId;
	int m_nFrame;
	int m_nTrigger;
	int m_nTriggerCount;
	int m_nMoudleEnable;
		int m_nScheduledTransEnable;
		int m_nScheduledTransTime;
		char m_moduleName[128];
		char m_szProcedureName[128];
		int m_nHeartbeatTranMode;
		uint64_t m_nHeartbeatInterval;
		uint64_t m_lastHeartbeatQueuedTimeMs;
		char m_szHeartbeatText[MAX_HB_TEXT_LEN+1];

	DequeAdapter* m_qAdapter = nullptr;
	OutputModeEngine* m_engine = nullptr;

	//定时发送消息队列
	std::deque<scheduled_fifo_param> m_shchedSendQueue;
	std::mutex m_shchedSendQueueMtx;
};

}


#endif /* __TCPSMSG_H */
