/** @file
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   
  *
  * @author  zhaojianli6
  * @date    2025/3/20
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2025/3/20   |V1.0.0  |zhaojianli6          |creation
  * @warning
  */

#ifndef TCPS_SERVER_MANAGER_H_
#define TCPS_SERVER_MANAGER_H_

#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <cstdint>
#include <functional>
#include <queue>
#include <mutex>
#include "TcpsConnection.h"

namespace tcps
{

class TcpsServerManager
{
public:
	static constexpr auto MAX_CLIENT_NUM = 5;
	static constexpr auto MAX_SEND_QUEUE_LEN = 4;

	using TaskLoopCallback = std::function<int()>;

	TcpsServerManager();
	~TcpsServerManager();

	int Init(const uint16_t localPort, 
		const uint16_t defaultLocalPort, IConnectionFactory *connFactory);

	void DeInit();

	int SetLogId(int nLogId);

	bool IsSendQueueEmpty();

	int InsertSendMsg(const TcpsMessage &msg);

	int InsertEmptySendQueue(const TcpsMessage &msg);

	void RegisterTaskLoopCallback(TaskLoopCallback callback);

	void SetHostIp(const uint32_t ip);

	int SetHostPort(const uint16_t port);

		void SetModuleEnable(const int enable);

		uint64_t GetLastSendTime();

		bool HasClient() const;

		void ListenTask();
		void TaskWorker();
	private:

	int PrepareServerSocket();
	int RecreateServerSocket();

	bool m_end;
	bool m_listenRun;
	bool m_taskRun;
	bool m_reCreate;
	bool m_conning;

	int m_moduleEnable;

	int m_nLogId;
	uint32_t m_hostIp;
	uint16_t m_localPort;
	uint16_t m_preLocalPort;
	uint16_t m_defaultLocalPort;
	int32_t m_sockFd;
	struct sockaddr_in m_serverAddr;
	struct sockaddr_in m_clientAddr;
	
	uint32_t m_triggerCnt;
	uint32_t m_sendCnt;
	uint32_t m_errorCnt;

	pthread_t m_lisThread;
	pthread_t m_taskThread;

	TaskLoopCallback m_taskLoopCallback;

	IConnectionFactory *m_connFactory;
		std::vector<std::shared_ptr<IConnection> > m_conns;

		std::queue<TcpsMessage> m_sendQueue;
		std::mutex m_sendQueueMtx;
	};
	 
	}

#endif // TCP_SERVER_MANAGER_H_
