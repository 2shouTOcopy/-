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

#include "TcpsConnection.h"
#include "util_net.h"
#include "log/log.h"
#include "CommProxy.h"
#include "calibrateapiwapper.h"

namespace tcps
{

IConnection::IConnection()
{
    m_sockFd = -1;
    m_nLogId = 0;
    m_nLastSendTime = 0;
}

int IConnection::Recv()
{
    int len = 0;
    memset(m_recvBuf, 0, sizeof(m_recvBuf));

    len = recv(m_sockFd, m_recvBuf, MAX_READ_BUF_LEN, 0);
    if (len > 0)
    {
        RecvProcess(m_recvBuf, len);
    }
    return len;
}

int IConnection::Send()
{
    int len = 0;

    m_sendQueueMtx.lock();
    if (m_sendQueue.empty())
    {
        m_sendQueueMtx.unlock();
        return 0;
    }

    auto msg = m_sendQueue.front();
    m_sendQueue.pop();
    m_sendQueueMtx.unlock();

    m_nLastSendTime = get_up_time();

    len = send(m_sockFd, msg.buf, msg.len, 0);

    return len;
}

void IConnection::Close()
{
    if (m_sockFd >= 0)
    {
        close(m_sockFd);   
        m_sockFd = -1;
    }
}

void IConnection::SetSockFd(const int sockfd)
{
    m_sockFd = sockfd;
}

int IConnection::GetSockFd()
{
    return m_sockFd;
}

uint64_t IConnection::GetLastSendTime()
{
	return m_nLastSendTime;
}

bool IConnection::IsSendQueueEmpty()
{
    std::lock_guard<std::mutex> locker(m_sendQueueMtx);
    return m_sendQueue.empty();
}

int IConnection::InsertSendMsg(const TcpsMessage &msg)
{
    std::lock_guard<std::mutex> locker(m_sendQueueMtx);
    if (m_sendQueue.size() > MAX_SEND_QUEUE_LEN)
    {
        return -1;
    }

    m_sendQueue.push(msg);
    return 0;
}

int IConnection::SetLogId(int nLogId)
{
    m_nLogId = nLogId;
    return IMVS_EC_OK;
}

int MsgConnection::RecvProcess(char *data, int len)
{
    if (nullptr == data || len < 0)
    {
        return -1;
    }

    auto self = shared_from_this();

    int ret = 0;
	CCommProxy::MessageInfo messageInfo{0};
	snprintf(messageInfo.msg, 128, "%s", data);
	messageInfo.len = strlen(data);
	messageInfo.socket = m_sockFd;
	messageInfo.moduleId = m_nLogId; //LogId其实和模块Id是一样
    messageInfo.cb = [self, this](int errCode, std::string buf, void *user) ->int 
    {
        int ret = 0;
        TcpsMessage msg = {0};
        snprintf((char*)msg.buf, sizeof(msg.buf), "%s", buf.c_str());
        msg.len = strlen((char*)msg.buf);

        ret = self->InsertSendMsg(msg);
        if (0 != ret)
        {
            LOGE("InsertSendMsg faild, ret %d\r\n", ret);
            return -1;
        }
        return 0;        
    };

	ret = CCommProxy::getInstance()->AsynRecv(messageInfo);
    if (0 != ret)
    {
        LOGE("AsynRecv faild, ret %d\r\n", ret);
        return -1;
    }

    return 0;
}

int HbConnection::RecvProcess(char *data, int len)
{
    return 0;
}

std::shared_ptr<IConnection> MsgConnectionFactory::CreateConnection()
{
    return std::make_shared<MsgConnection>();
}

std::shared_ptr<IConnection> HbConnectionFactory::CreateConnection()
{
    return std::make_shared<HbConnection>();
}

}
