#pragma once
#include <vector>
#include <memory>

#include "ModuleDef.h"

/* 负责把采集到的 value_list_t 异步分发给所有 writer-plugin 的单例 */
class RstDispatcher
{
public:
    static RstDispatcher& Instance();

    /* 把数据入队 – 内部会做深拷贝，立即返回 */
    int  enqueue(const value_list_t *vl);

    int enqueueMultivalues(const value_list_t* vl_template,
                           bool store_percentage_if_gauge,
                           int common_store_type,
                           const std::vector<MetricDataPoint>& data_points);

    /* 预留接口：主动 flush，当前实现为空 */
    int  flushAll(cdtime_t timeout, const char *ident);

    /* 主动停止（PluginService 在 shutdown 时调用） */
    void stop();

private:
    RstDispatcher();
    ~RstDispatcher();

    RstDispatcher(const RstDispatcher&)            = delete;
    RstDispatcher& operator=(const RstDispatcher&) = delete;

    static std::shared_ptr<value_list_t> vl_clone(const value_list_t *src);

    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

