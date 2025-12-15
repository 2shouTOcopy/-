#include "tcpsMsg.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <algorithm>
#include <unistd.h>
#include <sys/prctl.h>
#include <assert.h>

#include "utils.h"
#include "util_net.h"
#include "thread/ThreadApi.h"
#include "framework_service.h"
#include "AppParamCommon.h"
#include "algo_common.h"
#include "log/log.h"
#include "ITriggerSource.h"
#include "TcpsConnection.h"

#define SECONDS (1000)
#define DEBUG_GLOBAL_MDC_STRING "CTcpstransModule"

namespace tcps
{

CTcpsMessage::CTcpsMessage()
{
	m_nLogId = 0;
	m_nFrame = 0;
	m_nTrigger = 0;
	m_nTriggerCount = 0;
	m_nMoudleEnable = 0;
	m_nRecordTriggerCount = -1;
	m_nScheduledTransEnable = 0;
	m_nScheduledTransTime = 0;
	m_nHeartbeatTranMode = 0;
	m_nHeartbeatInterval = 0;
	m_lastHeartbeatQueuedTimeMs = 0;

	memset(m_moduleName, 0, sizeof(m_moduleName));
	memset(m_szProcedureName, 0, sizeof(m_szProcedureName));
	memset(m_szHeartbeatText, 0, sizeof(m_szHeartbeatText));
}

CTcpsMessage::~CTcpsMessage()
{

}

int CTcpsMessage::MsgTaskLoopCallback()
{
	//按模式分发消息
	DispatchQueuedMessages();

	//心跳包
	uint64_t nowMs = get_up_time();
	uint64_t lastActivityMs = std::max(m_msgServer.GetLastSendTime(), m_hbServer.GetLastSendTime());
	const bool hbPathHasClient =
		(m_nHeartbeatTranMode == 1) ? m_msgServer.HasClient() :
		(m_nHeartbeatTranMode == 2) ? m_hbServer.HasClient() : false;
	if (hbPathHasClient)
	{
		lastActivityMs = std::max(lastActivityMs, m_lastHeartbeatQueuedTimeMs);
	}

	if ((nowMs - lastActivityMs) / SECONDS >= m_nHeartbeatInterval)
	{
		TcpsMessage msg = {0};
		snprintf((char*)msg.buf, sizeof(msg.buf), "%s", m_szHeartbeatText);
		msg.len = strlen((char*)msg.buf);

		switch (m_nHeartbeatTranMode)
		{
		case 1:
			if (0 == m_msgServer.InsertEmptySendQueue(msg))
			{
				if (hbPathHasClient)
				{
					m_lastHeartbeatQueuedTimeMs = nowMs;
				}
			}
			break;
		case 2:
			if (0 == m_hbServer.InsertEmptySendQueue(msg))
			{
				if (hbPathHasClient)
				{
					m_lastHeartbeatQueuedTimeMs = nowMs;
				}
			}
			break;
		}
	}

	return 0;
}

int CTcpsMessage::Init(const uint16_t localPort, const uint16_t localHbPort)
{
	int ret = 0;

	InitOutputEngine();

	ret = m_msgServer.Init(localPort, TCP_SERVER_PORT, new MsgConnectionFactory());
	if (0 != ret)
	{
		LOGI("Msg server init failed, ret %d\r\n", ret);
		return -1;
	}
	m_msgServer.RegisterTaskLoopCallback(std::bind(&CTcpsMessage::MsgTaskLoopCallback, this));

	ret = m_hbServer.Init(localHbPort, TCP_SERVER_HEARTBEAT_PORT, new HbConnectionFactory());
	if (0 != ret)
	{
		LOGI("HB server init failed, ret %d\r\n", ret);
		return -2;
	}

	LOGI("tcp server init ok!\r\n");
	return 0;
}

int CTcpsMessage::DeInit()
{
	m_msgServer.DeInit();
	m_hbServer.DeInit();

	if (m_qAdapter)
	{
		delete m_qAdapter;
		m_qAdapter = nullptr;
	}

	if (m_engine)
	{
		delete m_engine;
		m_engine = nullptr;
	}
	return 0;
}

void CTcpsMessage::InitOutputEngine()
{
	if (!m_qAdapter)
	{
		m_qAdapter = new DequeAdapter(&m_shchedSendQueue, &m_shchedSendQueueMtx);
	}

	auto sendFn = [this](const scheduled_fifo_param& p){
		// 复用原有发送：将统一结构转成 TcpsMessage 再入发送服务
		TcpsMessage msg{};
		size_t n = std::min(sizeof(msg.buf), (size_t)p.len);
		std::memcpy(msg.buf, p.buf, n);
		msg.len = (int)n;
		this->m_msgServer.InsertSendMsg(msg);
	};


	OutputContext ctx{};
	ctx.logId = m_nLogId;
	ctx.queue = m_qAdapter;
	ctx.nowMs = &get_up_time;
	ctx.send = sendFn;
	ctx.mode = OutputMode::kImmediate;
	ctx.scheduledIntervalMs = 0;
	ctx.ngText = SCHEDULED_TRANS_NG_STRING;

	m_engine = new OutputModeEngine(ctx);
}

int CTcpsMessage::EnqueueScheduled(const scheduled_fifo_param& param)
{
	if (0 == param.len)
	{
		return -1;
	}

	m_qAdapter->push_drop_new(param);
	return 0;
}

int CTcpsMessage::SetLogId(IN int nLogId)
{
	m_nLogId = nLogId;

	m_msgServer.SetLogId(m_nLogId);
	m_hbServer.SetLogId(m_nLogId);

	return IMVS_EC_OK;
}

int CTcpsMessage::SetProcedureName(IN const char* szProcedureName)
{
	snprintf(m_szProcedureName, sizeof(m_szProcedureName), "%s", szProcedureName);
	return IMVS_EC_OK;
}

void CTcpsMessage::SetModuleEnable(IN const int enable)
{
	m_nMoudleEnable = enable;
	m_msgServer.SetModuleEnable(enable);
	m_hbServer.SetModuleEnable(enable);
}

void CTcpsMessage::SetFrameAddTrigger(IN int nFrame, IN int nTrigger,IN int MoudleEnable)
{
	m_nFrame = nFrame;
	m_nTrigger = nTrigger;
	m_nMoudleEnable = MoudleEnable;
}

void CTcpsMessage::SetScheduledTransEnable(IN int value)
{
	m_nScheduledTransEnable = value;
}

void CTcpsMessage::SetScheduledTransTime(IN int value)
{
	m_nScheduledTransTime = value;
}

void CTcpsMessage::DispatchQueuedMessages()
{
	if (m_engine)
	{
		m_engine->tick();
		return;
	}
}

void CTcpsMessage::SetHostIp(const uint32_t ip)
{
	m_msgServer.SetHostIp(ip);
}

int CTcpsMessage::SetHostPort(const uint16_t port)
{
	return m_msgServer.SetHostPort(port);
}

void CTcpsMessage::SetHostHeartbeatIp(const uint32_t ip)
{
	m_hbServer.SetHostIp(ip);
}

void CTcpsMessage::SetHeartbeatTranMode(const int mode)
{
	m_nHeartbeatTranMode = mode;
}

int CTcpsMessage::SetHostHeartbeatPort(const uint16_t port)
{
	return m_hbServer.SetHostPort(port);
}

void CTcpsMessage::SetHeartbeatText(const char* text)
{
	snprintf(m_szHeartbeatText, sizeof(m_szHeartbeatText), "%s", text);
}

void CTcpsMessage::SetHeartbeatInterval(const uint64_t interval)
{
	m_nHeartbeatInterval = interval;
}

}
