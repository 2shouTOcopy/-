#include "plugin.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <list>
#include <mutex>
#include <thread>
#include <chrono>
#include <unordered_map>

namespace collectd {
namespace plugin {

// 全局变量及内部容器（采用 std::mutex 保证线程安全）
static std::string g_plugindir = PLUGINDIR; // 默认插件目录，从宏定义中取得
static std::unordered_map<std::string, bool> g_plugins_loaded; // 已加载插件名称

// 以下各回调列表用 vector 存储，pair.first 为名称，pair.second 为回调函数
static std::mutex g_mutex;
static std::vector<std::pair<std::string, plugin_init_cb>> g_init_callbacks;
static std::vector<std::pair<std::string, std::function<int(void)>>> g_read_callbacks; // 简单 read 回调
static std::vector<std::pair<std::string, plugin_read_cb>> g_complex_read_callbacks;
static std::vector<std::pair<std::string, plugin_write_cb>> g_write_callbacks;
static std::vector<std::pair<std::string, plugin_flush_cb>> g_flush_callbacks;
static std::vector<std::pair<std::string, plugin_missing_cb>> g_missing_callbacks;
static std::vector<std::pair<std::string, plugin_cache_event_cb>> g_cache_event_callbacks;
static std::vector<std::pair<std::string, plugin_shutdown_cb>> g_shutdown_callbacks;
static std::vector<std::pair<std::string, plugin_log_cb>> g_log_callbacks;
static std::vector<std::pair<std::string, plugin_notification_cb>> g_notification_callbacks;

// 数据集注册，用 map 存储，键为数据集类型名
static std::unordered_map<std::string, data_set_t> g_data_sets;

// 关于线程上下文，使用 thread_local 变量
thread_local plugin_ctx_t g_plugin_ctx = {"", 0, 0, 0};

// ------------------------- 基本 API 实现 -------------------------

void set_dir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_plugindir = dir;
}

int load(const std::string& name, bool global) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_plugins_loaded.find(name) != g_plugins_loaded.end())
        return 0; // 已加载

    // 构造目标文件名，形如 name + ".so"
    std::string target = name + ".so";
    DIR* dh = opendir(g_plugindir.c_str());
    if (dh == nullptr) {
        std::fprintf(stderr, "plugin_load: opendir (%s) failed: %s\n", g_plugindir.c_str(), strerror(errno));
        return -1;
    }
    std::string filename;
    struct dirent* de = nullptr;
    int ret = -1;
    while ((de = readdir(dh)) != nullptr) {
        if (strcasecmp(de->d_name, target.c_str()) == 0) {
            filename = g_plugindir + "/" + de->d_name;
            break;
        }
    }
    closedir(dh);
    if (filename.empty()) {
        std::fprintf(stderr, "plugin_load: Could not find plugin \"%s\" in %s\n", name.c_str(), g_plugindir.c_str());
        return -1;
    }
    int flags = RTLD_NOW;
    if (global) flags |= RTLD_GLOBAL;
    void* handle = dlopen(filename.c_str(), flags);
    if (handle == nullptr) {
        std::fprintf(stderr, "plugin_load: dlopen(%s) failed: %s\n", filename.c_str(), dlerror());
        return -1;
    }
    // 调用插件中的 module_register 函数
    using module_register_t = void(*)();
    module_register_t reg = reinterpret_cast<module_register_t>(dlsym(handle, "module_register"));
    if (reg == nullptr) {
        std::fprintf(stderr, "plugin_load: dlsym(module_register) failed: %s\n", dlerror());
        dlclose(handle);
        return -1;
    }
    reg();
    g_plugins_loaded[name] = true;
    std::fprintf(stdout, "plugin_load: plugin \"%s\" successfully loaded.\n", name.c_str());
    return 0;
}

bool is_loaded(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_plugins_loaded.find(name) != g_plugins_loaded.end();
}

int init_all() {
    int ret = 0;
    // 调用所有 init 回调
    for (auto& p : g_init_callbacks) {
        plugin_ctx_t old_ctx = g_plugin_ctx;
        int status = p.second();
        g_plugin_ctx = old_ctx;
        if (status != 0) {
            std::fprintf(stderr, "Initialization of plugin \"%s\" failed with status %d\n", p.first.c_str(), status);
            ret = -1;
        }
    }
    // TODO: 启动写线程、读线程等
    return ret;
}

void read_all() {
    // 此处简化：依次调用所有 read 回调（简单版与复杂版分开）
    for (auto& p : g_read_callbacks) {
        p.second();
    }
    for (auto& p : g_complex_read_callbacks) {
        user_data_t ud = {nullptr, nullptr};
        p.second(&ud);
    }
}

int read_all_once() {
    int ret = 0;
    for (auto& p : g_read_callbacks) {
        int status = p.second();
        if (status != 0) ret = -1;
    }
    for (auto& p : g_complex_read_callbacks) {
        user_data_t ud = {nullptr, nullptr};
        int status = p.second(&ud);
        if (status != 0) ret = -1;
    }
    return ret;
}

int shutdown_all() {
    int ret = 0;
    for (auto& p : g_shutdown_callbacks) {
        int status = p.second();
        if (status != 0) ret = -1;
    }
    return ret;
}

int write(const data_set_t* ds, const value_list_t* vl, const std::string& pluginName) {
    int status = 0;
    if (pluginName.empty()) {
        // 发送给所有 write 插件
        for (auto& p : g_write_callbacks) {
            int s = p.second(ds, vl, nullptr);
            if (s != 0) status = s;
        }
    } else {
        bool found = false;
        for (auto& p : g_write_callbacks) {
            if (strcasecmp(p.first.c_str(), pluginName.c_str()) == 0) {
                found = true;
                status = p.second(ds, vl, nullptr);
                break;
            }
        }
        if (!found) return -1;
    }
    return status;
}

int flush(const std::string& pluginName, cdtime_t timeout, const std::string& identifier) {
    int status = 0;
    for (auto& p : g_flush_callbacks) {
        if (pluginName.empty() || (p.first == pluginName)) {
            status = p.second(timeout, identifier, nullptr);
        }
    }
    return status;
}

// -------------------- 注册/注销接口 --------------------

int register_config(const std::string& name,
                    std::function<int(const std::string&, const std::string&)> callback,
                    const std::vector<std::string>& /*keys*/) {
    // 此处直接调用配置解析模块，不在本模块内部处理
    // TODO: 将配置回调保存到相应数据结构中（如果需要）
    return 0;
}

int register_complex_config(const std::string& type,
                            std::function<int(void*)> callback) {
    // 同上，直接转发给配置解析模块
    return 0;
}

int register_init(const std::string& name, plugin_init_cb callback) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_init_callbacks.push_back({name, callback});
    return 0;
}

int register_read(const std::string& name, std::function<int(void)> callback) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_read_callbacks.push_back({name, callback});
    return 0;
}

int register_complex_read(const std::string& group, const std::string& name,
                          plugin_read_cb callback, cdtime_t interval,
                          const user_data_t* user_data) {
    // 此处 group、interval、user_data 未做特殊处理，直接保存复杂 read 回调
    std::lock_guard<std::mutex> lock(g_mutex);
    g_complex_read_callbacks.push_back({name, callback});
    return 0;
}

int register_write(const std::string& name, plugin_write_cb callback,
                   const user_data_t* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_write_callbacks.push_back({name, callback});
    return 0;
}

int register_flush(const std::string& name, plugin_flush_cb callback,
                   const user_data_t* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_flush_callbacks.push_back({name, callback});
    return 0;
}

int register_missing(const std::string& name, plugin_missing_cb callback,
                     const user_data_t* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_missing_callbacks.push_back({name, callback});
    return 0;
}

int register_cache_event(const std::string& name, plugin_cache_event_cb callback,
                         const user_data_t* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache_event_callbacks.push_back({name, callback});
    return 0;
}

int register_shutdown(const std::string& name, plugin_shutdown_cb callback) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_shutdown_callbacks.push_back({name, callback});
    return 0;
}

int register_data_set(const data_set_t* ds) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ds == nullptr)
        return -1;
    g_data_sets[ds->type] = *ds;
    return 0;
}

int register_log(const std::string& name, plugin_log_cb callback,
                 const user_data_t* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_log_callbacks.push_back({name, callback});
    return 0;
}

int register_notification(const std::string& name, plugin_notification_cb callback,
                          const user_data_t* user_data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notification_callbacks.push_back({name, callback});
    return 0;
}

// 注销函数（实现较为简单，直接从 vector 中查找并删除）
template <typename T>
static int unregister_callback(std::vector<std::pair<std::string, T>>& vec, const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (strcasecmp(it->first.c_str(), name.c_str()) == 0) {
            vec.erase(it);
            return 0;
        }
    }
    return -1;
}

int unregister_config(const std::string& name) {
    // 此处交由配置模块处理，返回 0
    return 0;
}

int unregister_complex_config(const std::string& name) {
    return 0;
}

int unregister_init(const std::string& name) {
    return unregister_callback(g_init_callbacks, name);
}

int unregister_read(const std::string& name) {
    int ret1 = unregister_callback(g_read_callbacks, name);
    int ret2 = unregister_callback(g_complex_read_callbacks, name);
    return (ret1==0 || ret2==0) ? 0 : -1;
}

int unregister_read_group(const std::string& group) {
    // 简化处理：不按 group 注销
    return -1;
}

int unregister_write(const std::string& name) {
    return unregister_callback(g_write_callbacks, name);
}

int unregister_flush(const std::string& name) {
    return unregister_callback(g_flush_callbacks, name);
}

int unregister_missing(const std::string& name) {
    return unregister_callback(g_missing_callbacks, name);
}

int unregister_cache_event(const std::string& name) {
    return unregister_callback(g_cache_event_callbacks, name);
}

int unregister_shutdown(const std::string& name) {
    return unregister_callback(g_shutdown_callbacks, name);
}

int unregister_data_set(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_data_sets.erase(name))
      return 0;
    else
      return -1;
}

int unregister_log(const std::string& name) {
    return unregister_callback(g_log_callbacks, name);
}

int unregister_notification(const std::string& name) {
    return unregister_callback(g_notification_callbacks, name);
}

void log_available_writers() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stdout, "Available write plugins:\n");
    for (auto& p : g_write_callbacks) {
        std::fprintf(stdout, "  %s\n", p.first.c_str());
    }
}

// -------------------- 分发数据接口 --------------------

int dispatch_values(const value_list_t* vl) {
    if (vl == nullptr)
        return -1;
    return write(get_ds(vl->type), vl, "");
}

int dispatch_multivalue(const value_list_t* tmpl, bool store_percentage, int store_type, ...) {
    // 此处较为简单：假设传入的参数为 type_instance/value 对，并依次调用 write
    if (tmpl == nullptr)
        return -1;
    va_list ap;
    va_start(ap, store_type);
    int failed = 0;
    // 复制一个 value_list_t 用于发送
    value_list_t copy = *tmpl;
    // 仅支持单值情况
    value_t value;
    while (true) {
        const char* type_instance = va_arg(ap, const char*);
        if (!type_instance)
            break;
        switch(store_type) {
            case 1: // DS_TYPE_GAUGE
                value.gauge = va_arg(ap, double);
                if (store_percentage && tmpl->values.size() > 0) {
                    // 计算百分比（简单示例）
                    double sum = tmpl->values[0].gauge;
                    value.gauge = sum ? (value.gauge * 100.0 / sum) : 0.0;
                }
                break;
            case 2: // DS_TYPE_DERIVE
                value.derive = va_arg(ap, derive_t);
                break;
            case 3: // DS_TYPE_ABSOLUTE
                value.absolute = va_arg(ap, absolute_t);
                break;
            case 0: // DS_TYPE_COUNTER
            default:
                value.counter = va_arg(ap, counter_t);
                break;
        }
        copy.type_instance = type_instance;
        copy.values = { value };
        int s = write(get_ds(copy.type), &copy, "");
        if (s != 0)
            failed++;
    }
    va_end(ap);
    return failed;
}

int dispatch_missing(const value_list_t* vl) {
    int status = 0;
    for (auto& p : g_missing_callbacks) {
        int s = p.second(vl, nullptr);
        if (s != 0)
            status = s;
    }
    return status;
}

void dispatch_cache_event(CacheEventType event_type, unsigned long callbacks_mask,
                          const std::string& name, const value_list_t* vl) {
    cache_event_t event { event_type, vl, name, 0 };
    for (auto& p : g_cache_event_callbacks) {
        int s = p.second(&event, nullptr);
        if (s != 0)
            std::fprintf(stderr, "Cache event callback \"%s\" failed with status %d\n", p.first.c_str(), s);
    }
}

int dispatch_notification(const notification_t& notif) {
    int status = 0;
    for (auto& p : g_notification_callbacks) {
        int s = p.second(notif, nullptr);
        if (s != 0)
            status = s;
    }
    return status;
}

// -------------------- 日志接口 --------------------

void log(int level, const std::string& message, ...) {
    // 简单实现：直接将日志输出到 stderr，并调用所有注册的 log 回调
    char buf[1024];
    va_list ap;
    va_start(ap, message);
    vsnprintf(buf, sizeof(buf), message.c_str(), ap);
    va_end(ap);
    std::fprintf(stderr, "%s\n", buf);
    for (auto& p : g_log_callbacks) {
        p.second(level, buf, nullptr);
    }
}

void daemon_log(int level, const std::string& message, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, message);
    vsnprintf(buf, sizeof(buf), message.c_str(), ap);
    va_end(ap);
    // 在 daemon_log 中可添加插件名前缀，此处简单处理
    std::string fullMsg = g_plugin_ctx.name.empty() ? buf : (g_plugin_ctx.name + " plugin: " + buf);
    log(level, fullMsg);
}

int parse_log_severity(const std::string& severity) {
    if (strcasecmp(severity.c_str(), "emerg") == 0 ||
        strcasecmp(severity.c_str(), "alert") == 0 ||
        strcasecmp(severity.c_str(), "crit") == 0 ||
        strcasecmp(severity.c_str(), "err") == 0)
      return 3; // LOG_ERR
    else if (strcasecmp(severity.c_str(), "warning") == 0)
      return 4; // LOG_WARNING
    else if (strcasecmp(severity.c_str(), "notice") == 0)
      return 5; // LOG_NOTICE
    else if (strcasecmp(severity.c_str(), "info") == 0)
      return 6; // LOG_INFO
    else if (strcasecmp(severity.c_str(), "debug") == 0)
      return 7; // LOG_DEBUG
    return -1;
}

int parse_notif_severity(const std::string& severity) {
    if (strcasecmp(severity.c_str(), "FAILURE") == 0)
      return 1; // NOTIF_FAILURE
    else if (severity == "OKAY")
      return 4; // NOTIF_OKAY
    else if (severity == "WARNING" || severity == "WARN")
      return 2; // NOTIF_WARNING
    return -1;
}

const data_set_t* get_ds(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_data_sets.find(name);
    if (it != g_data_sets.end())
      return &(it->second);
    std::fprintf(stderr, "No such dataset registered: %s\n", name.c_str());
    return nullptr;
}

// -------------------- 通知 meta 相关函数 --------------------

int notification_meta_add_string(notification_t& n, const std::string& name, const std::string& value) {
    auto meta = std::make_unique<notification_meta_t>();
    meta->name = name;
    meta->type = NotificationMetaType::STRING;
    meta->nm_value.nm_string = strdup(value.c_str());
    if (!n.meta)
      n.meta = std::move(meta);
    else {
      notification_meta_t* tail = n.meta.get();
      while (tail->next)
        tail = tail->next.get();
      tail->next = std::move(meta);
    }
    return 0;
}

int notification_meta_add_signed_int(notification_t& n, const std::string& name, int64_t value) {
    auto meta = std::make_unique<notification_meta_t>();
    meta->name = name;
    meta->type = NotificationMetaType::SIGNED_INT;
    meta->nm_value.nm_signed_int = value;
    if (!n.meta)
      n.meta = std::move(meta);
    else {
      notification_meta_t* tail = n.meta.get();
      while (tail->next)
        tail = tail->next.get();
      tail->next = std::move(meta);
    }
    return 0;
}

int notification_meta_add_unsigned_int(notification_t& n, const std::string& name, uint64_t value) {
    auto meta = std::make_unique<notification_meta_t>();
    meta->name = name;
    meta->type = NotificationMetaType::UNSIGNED_INT;
    meta->nm_value.nm_unsigned_int = value;
    if (!n.meta)
      n.meta = std::move(meta);
    else {
      notification_meta_t* tail = n.meta.get();
      while (tail->next)
        tail = tail->next.get();
      tail->next = std::move(meta);
    }
    return 0;
}

int notification_meta_add_double(notification_t& n, const std::string& name, double value) {
    auto meta = std::make_unique<notification_meta_t>();
    meta->name = name;
    meta->type = NotificationMetaType::DOUBLE;
    meta->nm_value.nm_double = value;
    if (!n.meta)
      n.meta = std::move(meta);
    else {
      notification_meta_t* tail = n.meta.get();
      while (tail->next)
        tail = tail->next.get();
      tail->next = std::move(meta);
    }
    return 0;
}

int notification_meta_add_boolean(notification_t& n, const std::string& name, bool value) {
    auto meta = std::make_unique<notification_meta_t>();
    meta->name = name;
    meta->type = NotificationMetaType::BOOLEAN;
    meta->nm_value.nm_boolean = value;
    if (!n.meta)
      n.meta = std::move(meta);
    else {
      notification_meta_t* tail = n.meta.get();
      while (tail->next)
        tail = tail->next.get();
      tail->next = std::move(meta);
    }
    return 0;
}

int notification_meta_copy(notification_t& dst, const notification_t& src) {
    // 简单实现：遍历 src.meta，并调用相应的添加函数
    notification_meta_t* meta = src.meta.get();
    while (meta) {
        switch (meta->type) {
            case NotificationMetaType::STRING:
                notification_meta_add_string(dst, meta->name, meta->nm_value.nm_string);
                break;
            case NotificationMetaType::SIGNED_INT:
                notification_meta_add_signed_int(dst, meta->name, meta->nm_value.nm_signed_int);
                break;
            case NotificationMetaType::UNSIGNED_INT:
                notification_meta_add_unsigned_int(dst, meta->name, meta->nm_value.nm_unsigned_int);
                break;
            case NotificationMetaType::DOUBLE:
                notification_meta_add_double(dst, meta->name, meta->nm_value.nm_double);
                break;
            case NotificationMetaType::BOOLEAN:
                notification_meta_add_boolean(dst, meta->name, meta->nm_value.nm_boolean);
                break;
        }
        meta = meta->next.get();
    }
    return 0;
}

int notification_meta_free(notification_meta_t* n) {
    // 由于采用 unique_ptr 管理，这里一般不需要手动释放
    return 0;
}

// -------------------- 插件上下文管理 --------------------

void init_ctx() {
    // 初始化线程局部插件上下文
    g_plugin_ctx = {"", cf_get_default_interval(), 0, 0};
}

plugin_ctx_t get_ctx() {
    return g_plugin_ctx;
}

plugin_ctx_t set_ctx(const plugin_ctx_t& ctx) {
    plugin_ctx_t old = g_plugin_ctx;
    g_plugin_ctx = ctx;
    return old;
}

cdtime_t get_interval() {
    if (g_plugin_ctx.interval > 0)
        return g_plugin_ctx.interval;
    // 返回默认值
    return cf_get_default_interval();
}

// -------------------- 线程创建 --------------------

struct PluginThreadWrapper {
    plugin_ctx_t ctx;
    std::function<void(void*)> start_routine;
    void* arg;
};

static void* plugin_thread_start(void* arg) {
    PluginThreadWrapper* wrapper = static_cast<PluginThreadWrapper*>(arg);
    set_ctx(wrapper->ctx);
    wrapper->start_routine(wrapper->arg);
    delete wrapper;
    return nullptr;
}

int thread_create(std::thread& thread, std::function<void(void*)> start_routine,
                  void* arg, const std::string& name) {
    PluginThreadWrapper* wrapper = new PluginThreadWrapper{ get_ctx(), start_routine, arg };
    try {
        thread = std::thread(plugin_thread_start, wrapper);
    } catch (...) {
        delete wrapper;
        return -1;
    }
    // 可调用 pthread_setname_np 给线程命名（省略）
    return 0;
}

// -------------------- 模块注册 --------------------

void module_register() {
    // 每个插件模块需要实现自己的 module_register()
    // 此处为空，作为接口声明
}

} // namespace plugin
} // namespace collectd