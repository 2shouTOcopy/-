#include "RstDispatcher.h"
#include "ModuleLoader.h"
#include "PluginService.h"
#include "utils/common/common.h"
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

/* ===== 内部实现体 ===== */
struct RstDispatcher::Impl
{
    std::deque<std::shared_ptr<value_list_t>> queue;
    std::mutex              mtx;
    std::condition_variable cv;
    std::atomic<bool>       exit{false};
    std::thread             th;

    Impl()
    {
        th = std::thread([this]{
            /* 若有 thread_set_name，可以在此调用 */
            while (!exit.load(std::memory_order_acquire))
            {
                std::shared_ptr<value_list_t> vl;
                {
                    std::unique_lock<std::mutex> lk(mtx);
                    cv.wait(lk, [this]{ return exit || !queue.empty(); });
                    if (exit && queue.empty()) break;
                    vl = std::move(queue.front());
                    queue.pop_front();
                }

                /* 过滤链钩子占位 */

                /* 分发给所有 writer 插件 */
                for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames()) {
                    auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
                    if (mod) mod->write(vl.get());
                }
            }
        });
    }

    ~Impl()
    {
        exit.store(true);
        cv.notify_all();
        if (th.joinable()) th.join();
    }
};

/* ====== RstDispatcher 公共接口 ====== */
RstDispatcher& RstDispatcher::Instance()
{
    static RstDispatcher inst;
    return inst;
}

RstDispatcher::RstDispatcher()  : pImpl_(new Impl) {}
RstDispatcher::~RstDispatcher() = default;

int RstDispatcher::enqueue(const value_list_t *vl)
{
    if (!vl) return EINVAL;
    {
        std::lock_guard<std::mutex> lk(pImpl_->mtx);
        pImpl_->queue.emplace_back(std::make_shared<value_list_t>(*vl));
    }
    pImpl_->cv.notify_one();
    return 0;
}

int RstDispatcher::flushAll(cdtime_t /*timeout*/, const char* /*ident*/)
{
    /* 目前无实际操作，仅占位 */
    return 0;
}

void RstDispatcher::stop()
{
    pImpl_.reset();   // 触发 Impl 析构 -> 线程 join
}