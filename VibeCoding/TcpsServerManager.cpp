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

#include "TcpsServerManager.h"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include "utils.h"
#include "util_net.h"
#include "thread/ThreadApi.h"
#include "algo_common.h"
#include "log/log.h"
#include "ITriggerSource.h"

#define BACKLOG					(5)
#define MSG_RECV_SELECT_TIME	(20)  // ms

namespace tcps
{

void *TcpsTaskWorkerThread(void* argv)
{
	TcpsServerManager* tcps = (TcpsServerManager*)argv;
	tcps->TaskWorker();
	return nullptr;
}

void *ListenTaskThread(void *argv)
{
	TcpsServerManager* tcps = (TcpsServerManager*)argv;
	tcps->ListenTask();
	return nullptr;
}

	TcpsServerManager::TcpsServerManager()
	{
		m_end = false;
		m_listenRun = false;
		m_taskRun = false;
	m_reCreate = false;
	m_conning = false;
	m_moduleEnable = false;
	m_nLogId = 0;
	m_hostIp = 0;
	m_localPort = 0;
	m_preLocalPort = 0;
	m_defaultLocalPort = 0;
	m_sockFd = -1;
	
	m_triggerCnt = 0;
	m_sendCnt = 0;
	m_errorCnt = 0;
	m_lisThread = -1;
		m_taskThread = -1;
		m_taskLoopCallback = nullptr;
		m_connFactory = nullptr;
		memset(&m_serverAddr, 0, sizeof(m_serverAddr));
		memset(&m_clientAddr, 0, sizeof(m_clientAddr));
	}

TcpsServerManager::~TcpsServerManager()
{

}

int TcpsServerManager::Init(const uint16_t localPort, const uint16_t defaultLocalPort, IConnectionFactory *connFactory)
{
	int ret = 0;

	m_localPort = localPort == 0 ? defaultLocalPort : localPort;
	m_defaultLocalPort = defaultLocalPort;
	m_connFactory = connFactory;

	m_conns.resize(MAX_CLIENT_NUM);
	for (int i = 0; i < MAX_CLIENT_NUM && m_conns[i]; i++)
	{
		m_conns[i].reset();
	}

	m_sockFd = -1;

	ret = PrepareServerSocket();
	if (0 != ret)
	{
		LOGE("PrepareServerSocket failed, ret %d\r\n", ret);
		return -1;
	}

	m_end = false;
	m_listenRun = false;
	m_taskRun = false;

	ret = thread_spawn_ex(&m_lisThread, 0, SCHED_POLICY_OTHER, SCHED_PRI_NA, 10 * 1024, ListenTaskThread, this);
	if (ret != 0)
	{
		LOGE("tcp thread creation ret %d failed!\r\n", ret);
		return -1;
	}

	ret = thread_spawn_ex(&m_taskThread, 0, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, TcpsTaskWorkerThread, this);
	if (ret != 0)
	{
		LOGE("client output thread creation ret %d failed!\r\n", ret);
		return -1;
	}
	LOGI("tcp server init ok!\r\n");
	return 0;
}

void TcpsServerManager::DeInit()
{
	int i = 0;

	m_end = true;

	while ((m_listenRun) || (m_taskRun))
	{
		usleep(10000);
	}

	for (i = 0; i < MAX_CLIENT_NUM; i++)
	{
		if (m_conns[i])
		{
			if (m_conns[i]->GetSockFd() > 0)
			{
				m_conns[i]->Close();
			}
			m_conns[i].reset();
		}
	}

	if (m_connFactory)
	{
		delete m_connFactory;
		m_connFactory = nullptr;
	}

	close(m_sockFd);
	m_sockFd = -1;
}

int TcpsServerManager::SetLogId(int nLogId)
{
	m_nLogId = nLogId;
	return IMVS_EC_OK;	
}

bool TcpsServerManager::IsSendQueueEmpty()
{
    std::lock_guard<std::mutex> locker(m_sendQueueMtx);
    return m_sendQueue.empty();
}

	uint64_t TcpsServerManager::GetLastSendTime()
	{
		uint64_t maxTime = 0;

	for (const auto& conn : m_conns)
	{
		if (conn)
		{
			uint64_t sendTime = conn->GetLastSendTime();
			if (sendTime > maxTime)
			{
				maxTime = sendTime;
			}
		}
		}

		return maxTime;
	}

	bool TcpsServerManager::HasClient() const
	{
		for (const auto& conn : m_conns)
		{
			if (conn && conn->GetSockFd() >= 0)
			{
				return true;
			}
		}
		return false;
	}

	int TcpsServerManager::InsertSendMsg(const TcpsMessage &msg)
	{
	    std::lock_guard<std::mutex> locker(m_sendQueueMtx);
	    if (m_sendQueue.size() > MAX_SEND_QUEUE_LEN)
	    {
	        return -1;
	    }
	    m_sendQueue.push(msg);
	    return 0;
	}

	int TcpsServerManager::InsertEmptySendQueue(const TcpsMessage &msg)
	{
	    std::lock_guard<std::mutex> locker(m_sendQueueMtx);
	    if (!m_sendQueue.empty())
	    {
	        return -1;
	    }
	    m_sendQueue.push(msg);

	    return 0;
	}

void TcpsServerManager::RegisterTaskLoopCallback(TaskLoopCallback callback)
{
    m_taskLoopCallback = callback;
}

void TcpsServerManager::SetHostIp(const uint32_t ip)
{
    m_hostIp = ip;
    m_reCreate = true;
}

int TcpsServerManager::SetHostPort(const uint16_t port)
{
    int count = 0;
    int nErrCode = IMVS_EC_OK;

	if (m_localPort != port)
	{
		m_preLocalPort = m_localPort;
		m_localPort = port;
		m_reCreate = true;

		while(m_reCreate)
		{
			// wait 2s
			usleep(4000);
			count++;
			if (count >= 500)
			{
				break;
			}
		}
		if (0 == m_localPort 
			|| port != m_localPort 
			|| count >= 500)
		{
			return -1;
		}
	}
    return nErrCode;
}

void TcpsServerManager::SetModuleEnable(const int enable)
{
	m_moduleEnable = enable;
}

void TcpsServerManager::ListenTask()
{
    int ret = 0;
	int connfd = -1;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in *client_addr = &m_clientAddr;
	int i = 0;
	int on = 1;
	bool addClientOk = false;

	fd_set rset;
	int nready;
	struct timeval timeout;

	struct linger tcp_linger;
	tcp_linger.l_onoff = 1;
	tcp_linger.l_linger = 0;

	thread_set_name("tcp_listen");

	m_listenRun = true;

	while (!m_end)
	{		
		if (m_reCreate)
		{
            //在新建连接的时候，由于m_conns存在多线程竞争问题，先等待任务处理线程暂停正常业务处理
			sleep(2);

            ret = RecreateServerSocket();
            if (ret < 0)
            {
				LOGE("RecreateServerSocket failed, ret %d\r\n", ret);
                m_localPort = m_preLocalPort;

                ret = PrepareServerSocket();
                if (0 != ret)
                {
					LOGE("PrepareServerSocket failed, ret %d\r\n", ret);
                    m_localPort = 0;
                    m_reCreate = false;
					continue;
                }               
            }
			m_reCreate = false;
		}

        if (m_sockFd == -1)
        {
			usleep(10 * 1000);
			m_reCreate = true;
			continue;
        }

		timeout.tv_sec  = 0;
		timeout.tv_usec = 10 * 1000; 

		FD_ZERO(&rset);
		FD_SET(m_sockFd, &rset);

		nready = select(m_sockFd + 1, &rset, NULL, NULL, &timeout);
		if (nready < 0)
		{
			LOGE("select error\r\n");
			usleep(10 * 1000);
            m_reCreate = true;
			continue;
		}
		else if (0 == nready)
		{
			usleep(10 * 1000);
			continue;
		}

		if ((connfd = accept(m_sockFd, (struct sockaddr *)client_addr, (socklen_t *)&addrlen)) < 0)
		{
			LOGE("Accept() failed.\n");
			usleep(10000);
			continue;
		}

		if (-1 == fcntl(connfd, F_SETFD, fcntl(connfd, F_GETFD) | FD_CLOEXEC))
		{
			LOGE("fcntl FD_CLOEXEC\n");
		}

		if (setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0)
		{
			LOGE("error:set socketopt TCP_NODELAY.\r\n");
		}

        //在新建连接的时候，由于m_conns存在多线程竞争问题，先等待任务处理线程暂停正常业务处理
		m_conning = true;
		sleep(2);

		addClientOk = false;
		for (i = 0; i < MAX_CLIENT_NUM; i++)
		{
            if (nullptr == m_conns[i])
            {
                m_conns[i] = m_connFactory->CreateConnection();
				m_conns[i]->SetLogId(m_nLogId);
				m_conns[i]->SetSockFd(connfd);
                addClientOk = true;
                LOGI("add client ok. fd[%d] = %d \r\n", i, connfd);
                break;
            }
		}

		if (false == addClientOk)
		{
			LOGE("add client failed. fd = %d \r\n", connfd);
			if (setsockopt(connfd, SOL_SOCKET, SO_LINGER, (const char *)&tcp_linger, sizeof(struct linger)) != 0)
			{
				LOGE("setsockopt SO_LINGER fail\r\n");
			}

			close(connfd);
			connfd = -1;
		}
		m_conning = false;
	}
	m_listenRun = false;
}

int TcpsServerManager::RecreateServerSocket()
{
	int sockfd = -1;
	int addreuse = 1;

	for (int i = 0; i < MAX_CLIENT_NUM; i++)
	{
		if (m_conns[i])
		{
			if (m_conns[i]->GetSockFd() > 0)
			{
				m_conns[i]->Close();
			}
			m_conns[i].reset();
		}
	}
		
	if (m_sockFd >= 0)
	{
		close(m_sockFd);
		m_sockFd = -1;
	}

	if (m_localPort == 0)
	{
		m_localPort = m_defaultLocalPort;
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		LOGE("tcp_thread: socket failed\r\n");
		return -1;
	}

	if (-1 == fcntl(sockfd, F_SETFD, fcntl(sockfd, F_GETFD) | FD_CLOEXEC))
	{
		LOGE("fcntl FD_CLOEXEC\n");
		close(sockfd);
		sockfd = -1;
		return -2;
	}

	if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&addreuse, sizeof(int)))
	{
		LOGE("setsockopt\n");
		close(sockfd);
		sockfd = -1;
		return -3;
	}

	memset(&m_serverAddr, 0, sizeof(m_serverAddr));

	m_serverAddr.sin_family	= AF_INET;
	m_serverAddr.sin_port = htons(m_localPort);
	m_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (-1 == bind(sockfd, (struct sockaddr*)&m_serverAddr, sizeof(struct sockaddr_in)))
	{
		LOGE("bind\n");
		close(sockfd);
		sockfd = -1;
		return -4;
	}

	if (-1 == listen(sockfd, BACKLOG))
	{
		LOGE("listen\n");
		close(sockfd);
		sockfd = -1;
		return -5;
	}

	m_sockFd = sockfd;

	return 0;
}

int TcpsServerManager::PrepareServerSocket()
{
	int sockfd = -1;
	int addreuse = 1;
	int ret = 0;

	for (int i = 0; i < MAX_CLIENT_NUM; i++)
	{
		if (m_conns[i])
		{
			if (m_conns[i]->GetSockFd() > 0)
			{
				m_conns[i]->Close();
			}
			m_conns[i].reset();		
		}
	}
		
	if (m_sockFd >= 0)
	{
		close(m_sockFd);
		m_sockFd = -1;
	}

	if (m_localPort == 0)
	{
		m_localPort = m_defaultLocalPort;
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		LOGE("tcp_thread: socket failed\r\n");
		sockfd = -1;
		return -1;
	}

	if (-1 == fcntl(sockfd, F_SETFD, fcntl(sockfd, F_GETFD) | FD_CLOEXEC))
	{
		LOGE("fcntl FD_CLOEXEC\n");
		close(sockfd);
		sockfd = -1;
		return -2;
	}

	if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&addreuse, sizeof(int)))
	{
		LOGE("setsockopt\n");
		close(sockfd);
		sockfd = -1;
		return -3;
	}

	memset(&m_serverAddr, 0, sizeof(m_serverAddr));

	while(1)
	{
		m_serverAddr.sin_family	= AF_INET;
		m_serverAddr.sin_port = htons(m_localPort);
		m_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		if (bind(sockfd, (struct sockaddr*)&m_serverAddr, sizeof(struct sockaddr_in)) < 0)
		{
			LOGE("bind\n");
			m_localPort++;
			if (m_localPort > 65535)
			{
				break;
			}
			LOGE("poll legacy error %d, local_port %d\r\n", ret, m_localPort);
			usleep(10000);
		}
		else
		{
			break;
		}
	}

	if (m_localPort > 65535)
	{
		LOGE("port not found\n");
		close(sockfd);
		sockfd = -1;
		return -4;	
	}

	if (-1 == listen(sockfd, BACKLOG))
	{
		LOGE("listen\n");
		close(sockfd);
		sockfd = -1;
		return -5;
	}

	m_sockFd = sockfd;

	return 0;
}

void TcpsServerManager::TaskWorker()
{
	int32_t sockfd = -1;
	int32_t maxfd = -1;
	int32_t len = 0;	
	int32_t i = 0;

	fd_set rdSet, wrSet;
	int32_t result = 0;
	struct timeval timeout;

    m_taskRun = true;

    while (!m_end)
    {
        if (m_reCreate || m_conning)
        {
            usleep(100000);
            continue;
        }

		if (!m_moduleEnable)
		{
            usleep(100000);
            continue;			
		}

        if (m_taskLoopCallback)
        {
            m_taskLoopCallback();
        }

		timeout.tv_sec = 0;
		timeout.tv_usec = MSG_RECV_SELECT_TIME * 1000;

		maxfd = -1;
		FD_ZERO(&rdSet);
        FD_ZERO(&wrSet);

        //需要将广播消息插入到所有连接的发送队列中
        m_sendQueueMtx.lock();
		TcpsMessage msg = {0};
		if (!m_sendQueue.empty())
		{
			msg = m_sendQueue.front();
        	m_sendQueue.pop();
		}
        m_sendQueueMtx.unlock();

		for (i = 0; i < MAX_CLIENT_NUM && m_conns[i]; i++)
		{
			sockfd = m_conns[i]->GetSockFd();
            if (sockfd >= 0)
            {
				if (msg.len > 0)
				{
					m_conns[i]->InsertSendMsg(msg);
				}

                if (sockfd > maxfd)
                {
                    maxfd = sockfd;
                }

                FD_SET(sockfd, &rdSet);
                if (false == m_conns[i]->IsSendQueueEmpty()) 
				{
                    FD_SET(sockfd, &wrSet);
                }
            }
		}

        if (maxfd > 0)
        {
            result = select(maxfd + 1, &rdSet, &wrSet, NULL, &timeout);
            if (result > 0)
            {
                for (i = 0; i < MAX_CLIENT_NUM && m_conns[i]; i++)
                {
                    sockfd = m_conns[i]->GetSockFd();
					if (sockfd > 0 && FD_ISSET(sockfd, &rdSet))
                    {
                        len = m_conns[i]->Recv();
                        if (len <= 0)
                        {
                            LOGE("tcp client recv ret %d, sockfd=%d\r\n", len, sockfd);
                            m_conns[i]->Close();
							m_conns[i].reset();
                            continue;
                        }
                    }

                    if (sockfd > 0 && FD_ISSET(sockfd, &wrSet))
                    {
                        len = m_conns[i]->Send();
                    	if (len < 0 && (errno != EINTR && errno != EAGAIN))
				        {
					        m_conns[i]->Close();
                            m_conns[i].reset();
				        }
                    }
                }
            }
            else if (result < 0)
            {
				for (i = 0; i < MAX_CLIENT_NUM && m_conns[i]; i++)
				{
					sockfd = m_conns[i]->GetSockFd();
					if (sockfd > 0 && FD_ISSET(sockfd, &rdSet))
					{
						LOGE("tcp client select error, sockfd=%d\r\n", sockfd);
						m_conns[i]->Close();
						m_conns[i].reset();			
					}
				}
            }
        }
        else
        {
            usleep(MSG_RECV_SELECT_TIME * 1000);
        }
    }
    m_taskRun = false;
}

}
