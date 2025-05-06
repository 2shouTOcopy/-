#pragma once
#include "collectd_types.h"     // value_list_t, cdtime_t …

/* 负责把采集到的 value_list_t 异步分发给所有 writer-plugin 的单例 */
class RstDispatcher
{
public:
    static RstDispatcher& Instance();

    /* 把数据入队 – 内部会做深拷贝，立即返回 */
    int  enqueue(const value_list_t *vl);

    /* 预留接口：主动 flush，当前实现为空 */
    int  flushAll(cdtime_t timeout, const char *ident);

    /* 主动停止（PluginService 在 shutdown 时调用） */
    void stop();

private:
    RstDispatcher();
    ~RstDispatcher();

    RstDispatcher(const RstDispatcher&)            = delete;
    RstDispatcher& operator=(const RstDispatcher&) = delete;

    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};