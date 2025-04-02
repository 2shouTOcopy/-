#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include "plugin.h" // 假设包含了 plugin_read_cb 等定义

/// \brief 回调管理器，用于注册/注销 Collectd 中的各种回调。
///
/// Collectd 中常见的回调类型包括：
/// - Config（简单、或 complex）
/// - Init / Shutdown
/// - Read
/// - Write
/// - Flush
/// - Log
/// - Notification
/// - Missing
/// - CacheEvent
/// 等等。
///
/// 这里演示一种将每类回调存入不同 map 的做法，
/// key = 回调名称, value = 回调信息(含函数指针, user_data等)。
class CCallbackManager
{
public:
    /// 获取单例
    static CCallbackManager &Instance() {
        static CCallbackManager s_instance;
        return s_instance;
    }

    // -----------------------------
    // 1. Init / Shutdown callbacks
    // -----------------------------
    int RegisterInit(const std::string &name, plugin_init_cb cb);
    int UnregisterInit(const std::string &name);

    int RegisterShutdown(const std::string &name, plugin_shutdown_cb cb);
    int UnregisterShutdown(const std::string &name);

    // -----------------------------
    // 2. Read callbacks
    // -----------------------------
    /// \param interval 可在这里存储 read interval
    /// \param ud user_data
    /// \note 如果要兼容 collectd 的 plugin_register_complex_read，需要额外信息
    int RegisterRead(const std::string &name,
                     plugin_read_cb cb,
                     cdtime_t interval,
                     const user_data_t *ud);
    int UnregisterRead(const std::string &name);

    // -----------------------------
    // 3. Write callbacks
    // -----------------------------
    int RegisterWrite(const std::string &name,
                      plugin_write_cb cb,
                      const user_data_t *ud);
    int UnregisterWrite(const std::string &name);

    // -----------------------------
    // 4. Flush callbacks
    // -----------------------------
    int RegisterFlush(const std::string &name,
                      plugin_flush_cb cb,
                      const user_data_t *ud);
    int UnregisterFlush(const std::string &name);

    // -----------------------------
    // 5. Log callbacks
    // -----------------------------
    int RegisterLog(const std::string &name,
                    plugin_log_cb cb,
                    const user_data_t *ud);
    int UnregisterLog(const std::string &name);

    // -----------------------------
    // 6. Notification callbacks
    // -----------------------------
    int RegisterNotification(const std::string &name,
                             plugin_notification_cb cb,
                             const user_data_t *ud);
    int UnregisterNotification(const std::string &name);

    // -----------------------------
    // 7. Missing callbacks
    // -----------------------------
    int RegisterMissing(const std::string &name,
                        plugin_missing_cb cb,
                        const user_data_t *ud);
    int UnregisterMissing(const std::string &name);

    // -----------------------------
    // 8. CacheEvent callbacks
    // -----------------------------
    int RegisterCacheEvent(const std::string &name,
                           plugin_cache_event_cb cb,
                           const user_data_t *ud);
    int UnregisterCacheEvent(const std::string &name);

    // ------------------------------------------------------------------
    // 访问 / 获取回调列表的接口 (给调度层或 Procedure 用)
    // 例如, Procedure::InitAll() 时需要调 init 回调
    // ------------------------------------------------------------------
    std::vector<plugin_init_cb>           GetAllInitCallbacks() const;
    std::vector<plugin_shutdown_cb>       GetAllShutdownCallbacks() const;
    std::vector<plugin_read_cb>           GetAllReadCallbacks(std::vector<std::string> *names = nullptr) const;
    std::vector<plugin_write_cb>          GetAllWriteCallbacks() const;
    std::vector<plugin_flush_cb>          GetAllFlushCallbacks() const;
    std::vector<plugin_log_cb>            GetAllLogCallbacks() const;
    std::vector<plugin_notification_cb>   GetAllNotificationCallbacks() const;
    std::vector<plugin_missing_cb>        GetAllMissingCallbacks() const;
    std::vector<plugin_cache_event_cb>    GetAllCacheEventCallbacks() const;

    // 如果需要更多函数 (比如访问 read interval、user_data) 也可加。

private:
    CCallbackManager() = default;
    ~CCallbackManager();

    // 禁用拷贝
    CCallbackManager(const CCallbackManager &) = delete;
    CCallbackManager &operator=(const CCallbackManager &) = delete;

    // -- 辅助函数，用于清理 user_data
    static void freeUserData(user_data_t &ud);

private:
    mutable std::mutex m_mutex;

    // 对每种回调类型定义一个小 struct，保存函数指针、user_data等。
    struct InitCB   { plugin_init_cb cb;   plugin_ctx_t ctx; };
    struct ShutdownCB { plugin_shutdown_cb cb; plugin_ctx_t ctx; };
    struct ReadCB {
        plugin_read_cb cb;
        plugin_ctx_t ctx;
        cdtime_t interval;
        user_data_t ud;
    };
    struct WriteCB {
        plugin_write_cb cb;
        plugin_ctx_t ctx;
        user_data_t ud;
    };
    struct FlushCB {
        plugin_flush_cb cb;
        plugin_ctx_t ctx;
        user_data_t ud;
    };
    struct LogCB {
        plugin_log_cb cb;
        plugin_ctx_t ctx;
        user_data_t ud;
    };
    struct NotificationCB {
        plugin_notification_cb cb;
        plugin_ctx_t ctx;
        user_data_t ud;
    };
    struct MissingCB {
        plugin_missing_cb cb;
        plugin_ctx_t ctx;
        user_data_t ud;
    };
    struct CacheEventCB {
        plugin_cache_event_cb cb;
        plugin_ctx_t ctx;
        user_data_t ud;
    };

    // 将回调信息按 name 存放
    std::map<std::string, InitCB>         m_inits;
    std::map<std::string, ShutdownCB>     m_shutdowns;
    std::map<std::string, ReadCB>         m_reads;
    std::map<std::string, WriteCB>        m_writes;
    std::map<std::string, FlushCB>        m_flushes;
    std::map<std::string, LogCB>          m_logs;
    std::map<std::string, NotificationCB> m_notifications;
    std::map<std::string, MissingCB>      m_missings;
    std::map<std::string, CacheEventCB>   m_cacheevents;
};