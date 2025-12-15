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

#ifndef TCPS_CONNECTION_H_
#define TCPS_CONNECTION_H_

#include <stdlib.h>
#include <ctype.h>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include "adapter/ScheErrorCodeDefine.h"

#define MAX_TCP_PAYLOAD_LEN (1280)

namespace tcps
{

struct TcpsMessage
{
    int len;
    uint8_t buf[MAX_TCP_PAYLOAD_LEN];
};

class IConnection : public std::enable_shared_from_this<IConnection>
{
public:
    static constexpr auto MAX_READ_BUF_LEN = 128;
    static constexpr auto MAX_SEND_QUEUE_LEN = 4;
    IConnection();
    virtual ~IConnection() {};
    virtual int RecvProcess(char *data, int len) = 0;

    int Recv();

    int Send();

    void Close();

    int GetSockFd();
    void SetSockFd(const int sockfd);

	bool IsSendQueueEmpty();

    int InsertSendMsg(const TcpsMessage &msg);

    int SetLogId(int nLogId);

	uint64_t GetLastSendTime();

protected:

	int m_sockFd;
    int m_nLogId;

	uint64_t m_nLastSendTime;

    char m_recvBuf[MAX_READ_BUF_LEN+1];
	std::queue<TcpsMessage> m_sendQueue;
    std::mutex m_sendQueueMtx;
};

class MsgConnection : public IConnection
{
public:
    virtual int RecvProcess(char *data, int len);
private:
};

class HbConnection : public IConnection
{
public:
    virtual int RecvProcess(char *data, int len);
private:
};

class IConnectionFactory
{
public:
    virtual ~IConnectionFactory() {};
    virtual std::shared_ptr<IConnection> CreateConnection() = 0;
private:
};

class MsgConnectionFactory : public IConnectionFactory
{
public:
    virtual std::shared_ptr<IConnection> CreateConnection();
private:
};

class HbConnectionFactory : public IConnectionFactory
{
public:
    virtual std::shared_ptr<IConnection> CreateConnection();
private:
};

}

#endif // TCPS_CONNECTION_H_
