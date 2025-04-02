#include "CCallbackManager.h"
#include <cassert>
#include <iostream> // for debug log

CCallbackManager::~CCallbackManager()
{
    // 析构时，如果需要释放 user_data 的 data，可以做相应处理。
    // 这里简单演示:
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &kv : m_reads) {
        freeUserData(kv.second.ud);
    }
    for (auto &kv : m_writes) {
        freeUserData(kv.second.ud);
    }
    for (auto &kv : m_flushes) {
        freeUserData(kv.second.ud);
    }
    // ... 其他包含 user_data 的回调类型也同理
}

void CCallbackManager::freeUserData(user_data_t &ud)
{
    if (ud.data != nullptr && ud.free_func != nullptr) {
        ud.free_func(ud.data);
        ud.data = nullptr;
    }
}

// --------------------------------------------------------------------------
// 1. Init
// --------------------------------------------------------------------------
int CCallbackManager::RegisterInit(const std::string &name, plugin_init_cb cb)
{
    if (!cb) // 回调函数指针为空，返回错误
        return -1;

    // 取当前线程上下文
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_inits.find(name);
    if (it != m_inits.end()) {
        // 已有同名 init callback
        // 根据 Collectd 的做法，要么覆盖，要么报 warning
        // 这里简单覆盖
        // std::cerr << "CCallbackManager: override init callback: " << name << "\n";
    }
    InitCB info;
    info.cb  = cb;
    info.ctx = ctx;
    m_inits[name] = info;

    return 0;
}

int CCallbackManager::UnregisterInit(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_inits.find(name);
    if (it == m_inits.end()) {
        // 不存在
        return -1;
    }
    m_inits.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 2. Shutdown
// --------------------------------------------------------------------------
int CCallbackManager::RegisterShutdown(const std::string &name, plugin_shutdown_cb cb)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    ShutdownCB info;
    info.cb  = cb;
    info.ctx = ctx;

    // 同样是否覆盖由需求而定
    m_shutdowns[name] = info;
    return 0;
}

int CCallbackManager::UnregisterShutdown(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_shutdowns.find(name);
    if (it == m_shutdowns.end()) {
        return -1;
    }
    m_shutdowns.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 3. Read
// --------------------------------------------------------------------------
int CCallbackManager::RegisterRead(const std::string &name,
                                   plugin_read_cb cb,
                                   cdtime_t interval,
                                   const user_data_t *ud)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    ReadCB info;
    info.cb       = cb;
    info.ctx      = ctx;
    info.interval = interval;
    if (ud) {
        info.ud = *ud;
    } else {
        info.ud.data = nullptr;
        info.ud.free_func = nullptr;
    }

    m_reads[name] = info;
    return 0;
}

int CCallbackManager::UnregisterRead(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_reads.find(name);
    if (it == m_reads.end()) {
        return -1;
    }
    // free user data
    freeUserData(it->second.ud);

    m_reads.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 4. Write
// --------------------------------------------------------------------------
int CCallbackManager::RegisterWrite(const std::string &name,
                                    plugin_write_cb cb,
                                    const user_data_t *ud)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    WriteCB info;
    info.cb  = cb;
    info.ctx = ctx;
    if (ud) {
        info.ud = *ud;
    } else {
        info.ud.data = nullptr;
        info.ud.free_func = nullptr;
    }
    m_writes[name] = info;
    return 0;
}

int CCallbackManager::UnregisterWrite(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_writes.find(name);
    if (it == m_writes.end())
        return -1;

    freeUserData(it->second.ud);
    m_writes.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 5. Flush
// --------------------------------------------------------------------------
int CCallbackManager::RegisterFlush(const std::string &name,
                                    plugin_flush_cb cb,
                                    const user_data_t *ud)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    FlushCB info;
    info.cb  = cb;
    info.ctx = ctx;
    if (ud) {
        info.ud = *ud;
    } else {
        info.ud.data = nullptr;
        info.ud.free_func = nullptr;
    }
    m_flushes[name] = info;
    return 0;
}

int CCallbackManager::UnregisterFlush(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_flushes.find(name);
    if (it == m_flushes.end())
        return -1;

    freeUserData(it->second.ud);
    m_flushes.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 6. Log
// --------------------------------------------------------------------------
int CCallbackManager::RegisterLog(const std::string &name,
                                  plugin_log_cb cb,
                                  const user_data_t *ud)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    LogCB info;
    info.cb  = cb;
    info.ctx = ctx;
    if (ud) {
        info.ud = *ud;
    } else {
        info.ud.data = nullptr;
        info.ud.free_func = nullptr;
    }
    m_logs[name] = info;
    return 0;
}

int CCallbackManager::UnregisterLog(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_logs.find(name);
    if (it == m_logs.end())
        return -1;

    freeUserData(it->second.ud);
    m_logs.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 7. Notification
// --------------------------------------------------------------------------
int CCallbackManager::RegisterNotification(const std::string &name,
                                           plugin_notification_cb cb,
                                           const user_data_t *ud)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    NotificationCB info;
    info.cb  = cb;
    info.ctx = ctx;
    if (ud) {
        info.ud = *ud;
    } else {
        info.ud.data = nullptr;
        info.ud.free_func = nullptr;
    }
    m_notifications[name] = info;
    return 0;
}

int CCallbackManager::UnregisterNotification(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_notifications.find(name);
    if (it == m_notifications.end())
        return -1;

    freeUserData(it->second.ud);
    m_notifications.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 8. Missing
// --------------------------------------------------------------------------
int CCallbackManager::RegisterMissing(const std::string &name,
                                      plugin_missing_cb cb,
                                      const user_data_t *ud)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    MissingCB info;
    info.cb  = cb;
    info.ctx = ctx;
    if (ud) {
        info.ud = *ud;
    } else {
        info.ud.data = nullptr;
        info.ud.free_func = nullptr;
    }
    m_missings[name] = info;
    return 0;
}

int CCallbackManager::UnregisterMissing(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_missings.find(name);
    if (it == m_missings.end())
        return -1;

    freeUserData(it->second.ud);
    m_missings.erase(it);
    return 0;
}

// --------------------------------------------------------------------------
// 9. CacheEvent
// --------------------------------------------------------------------------
int CCallbackManager::RegisterCacheEvent(const std::string &name,
                                         plugin_cache_event_cb cb,
                                         const user_data_t *ud)
{
    if (!cb)
        return -1;
    plugin_ctx_t ctx = plugin_get_ctx();

    std::lock_guard<std::mutex> lock(m_mutex);
    CacheEventCB info;
    info.cb  = cb;
    info.ctx = ctx;
    if (ud) {
        info.ud = *ud;
    } else {
        info.ud.data = nullptr;
        info.ud.free_func = nullptr;
    }
    m_cacheevents[name] = info;
    return 0;
}

int CCallbackManager::UnregisterCacheEvent(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cacheevents.find(name);
    if (it == m_cacheevents.end())
        return -1;

    freeUserData(it->second.ud);
    m_cacheevents.erase(it);
    return 0;
}


// --------------------------------------------------------------------------
// 获取回调函数列表
// --------------------------------------------------------------------------
std::vector<plugin_init_cb> CCallbackManager::GetAllInitCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_init_cb> result;
    result.reserve(m_inits.size());
    for (auto const &kv : m_inits) {
        result.push_back(kv.second.cb);
    }
    return result;
}

std::vector<plugin_shutdown_cb> CCallbackManager::GetAllShutdownCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_shutdown_cb> result;
    result.reserve(m_shutdowns.size());
    for (auto const &kv : m_shutdowns) {
        result.push_back(kv.second.cb);
    }
    return result;
}

std::vector<plugin_read_cb> CCallbackManager::GetAllReadCallbacks(std::vector<std::string> *names) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_read_cb> result;
    result.reserve(m_reads.size());
    for (auto const &kv : m_reads) {
        result.push_back(kv.second.cb);
        if (names) {
            names->push_back(kv.first);
        }
    }
    return result;
}

std::vector<plugin_write_cb> CCallbackManager::GetAllWriteCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_write_cb> result;
    result.reserve(m_writes.size());
    for (auto const &kv : m_writes) {
        result.push_back(kv.second.cb);
    }
    return result;
}

std::vector<plugin_flush_cb> CCallbackManager::GetAllFlushCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_flush_cb> result;
    result.reserve(m_flushes.size());
    for (auto const &kv : m_flushes) {
        result.push_back(kv.second.cb);
    }
    return result;
}

std::vector<plugin_log_cb> CCallbackManager::GetAllLogCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_log_cb> result;
    result.reserve(m_logs.size());
    for (auto const &kv : m_logs) {
        result.push_back(kv.second.cb);
    }
    return result;
}

std::vector<plugin_notification_cb> CCallbackManager::GetAllNotificationCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_notification_cb> result;
    result.reserve(m_notifications.size());
    for (auto const &kv : m_notifications) {
        result.push_back(kv.second.cb);
    }
    return result;
}

std::vector<plugin_missing_cb> CCallbackManager::GetAllMissingCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_missing_cb> result;
    result.reserve(m_missings.size());
    for (auto const &kv : m_missings) {
        result.push_back(kv.second.cb);
    }
    return result;
}

std::vector<plugin_cache_event_cb> CCallbackManager::GetAllCacheEventCallbacks() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<plugin_cache_event_cb> result;
    result.reserve(m_cacheevents.size());
    for (auto const &kv : m_cacheevents) {
        result.push_back(kv.second.cb);
    }
    return result;
}