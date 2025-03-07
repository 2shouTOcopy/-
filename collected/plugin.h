#ifndef PLUGIN_HPP
#define PLUGIN_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <memory>

// 假设 cdtime_t 用 double 表示秒数
using cdtime_t = double;

// 定义数据名称的最大长度（参考 collectd 定义）
constexpr size_t DATA_MAX_NAME_LEN = 64;

// 数值类型
using counter_t  = unsigned long long;
using gauge_t    = double;
using derive_t   = int64_t;
using absolute_t = uint64_t;

// value_t 用 union 表示数值
union value_t {
  counter_t counter;
  gauge_t   gauge;
  derive_t  derive;
  absolute_t absolute;
  value_t() : counter(0) {}
};

// value_list_t 用于存储采集到的一组数据
struct value_list_t {
  std::vector<value_t> values;
  cdtime_t time;
  cdtime_t interval;
  std::string host;
  std::string plugin;
  std::string plugin_instance;
  std::string type;
  std::string type_instance;
  // meta 数据（原代码为 meta_data_t*，此处暂用 void* 占位）
  void* meta;
};

// 数据源和数据集定义
struct data_source_t {
  std::string name;
  int type;
  double min;
  double max;
};

struct data_set_t {
  std::string type;
  std::vector<data_source_t> ds;
};

// 通知相关数据结构

enum class NotificationMetaType {
  STRING,
  SIGNED_INT,
  UNSIGNED_INT,
  DOUBLE,
  BOOLEAN
};

struct notification_meta_t {
  std::string name;
  NotificationMetaType type;
  // 为简单起见，此处采用 union 存储值
  union {
    const char* nm_string;
    int64_t nm_signed_int;
    uint64_t nm_unsigned_int;
    double nm_double;
    bool nm_boolean;
  } nm_value;
  // 链表指针，使用智能指针管理链表（链表结构）
  std::unique_ptr<notification_meta_t> next;
};

struct notification_t {
  int severity;
  cdtime_t time;
  std::string message;
  std::string host;
  std::string plugin;
  std::string plugin_instance;
  std::string type;
  std::string type_instance;
  std::unique_ptr<notification_meta_t> meta;
};

// 用户数据结构
struct user_data_t {
  void* data;
  // free 函数采用 std::function 管理
  std::function<void(void*)> free_func;
};

// 缓存事件类型及结构
enum class CacheEventType {
  VALUE_NEW,
  VALUE_UPDATE,
  VALUE_EXPIRED
};

struct cache_event_t {
  CacheEventType type;
  const value_list_t* value_list;
  std::string value_list_name;
  int ret;
};

// 插件上下文，用于保存当前插件相关状态信息
struct plugin_ctx_t {
  std::string name;
  cdtime_t interval;
  cdtime_t flush_interval;
  cdtime_t flush_timeout;
};

// 回调函数类型定义
using plugin_init_cb         = std::function<int(void)>;
using plugin_read_cb         = std::function<int(user_data_t*)>;
using plugin_write_cb        = std::function<int(const data_set_t*, const value_list_t*, user_data_t*)>;
using plugin_flush_cb        = std::function<int(cdtime_t, const std::string&, user_data_t*)>;
using plugin_missing_cb      = std::function<int(const value_list_t*, user_data_t*)>;
using plugin_cache_event_cb  = std::function<int(cache_event_t*, user_data_t*)>;
using plugin_log_cb          = std::function<void(int, const std::string&, user_data_t*)>;
using plugin_shutdown_cb     = std::function<int(void)>;
using plugin_notification_cb = std::function<int(const notification_t&, user_data_t*)>;

namespace collectd {
namespace plugin {

// 设置插件目录
void set_dir(const std::string& dir);

// 加载指定名称的插件；global 表示是否以全局方式加载
int load(const std::string& name, bool global);

// 判断指定插件是否已加载
bool is_loaded(const std::string& name);

// 初始化所有插件（调用 init 回调）
int init_all();

// 依次调用所有 read 回调（循环调度）
void read_all();

// 仅调用一次所有 read 回调（用于测试）
int read_all_once();

// 调用所有 shutdown 回调
int shutdown_all();

// 将数据写入（依次调用 write 回调）
// 若 plugin 参数为空，则发送给所有写插件；否则仅调用指定的插件
int write(const data_set_t* ds, const value_list_t* vl, const std::string& plugin = "");

// 调用 flush 回调
int flush(const std::string& plugin, cdtime_t timeout, const std::string& identifier);

// 注册回调函数
int register_config(const std::string& name,
                    std::function<int(const std::string&, const std::string&)> callback,
                    const std::vector<std::string>& keys);

int register_complex_config(const std::string& type,
                            std::function<int(void*)> callback); // oconfig_item_t* 用 void* 替代

int register_init(const std::string& name, plugin_init_cb callback);

int register_read(const std::string& name, std::function<int(void)> callback);

int register_complex_read(const std::string& group, const std::string& name,
                          plugin_read_cb callback, cdtime_t interval,
                          const user_data_t* user_data);

int register_write(const std::string& name, plugin_write_cb callback,
                   const user_data_t* user_data);

int register_flush(const std::string& name, plugin_flush_cb callback,
                   const user_data_t* user_data);

int register_missing(const std::string& name, plugin_missing_cb callback,
                     const user_data_t* user_data);

int register_cache_event(const std::string& name, plugin_cache_event_cb callback,
                         const user_data_t* user_data);

int register_shutdown(const std::string& name, plugin_shutdown_cb callback);

int register_data_set(const data_set_t* ds);

int register_log(const std::string& name, plugin_log_cb callback,
                 const user_data_t* user_data);

int register_notification(const std::string& name, plugin_notification_cb callback,
                          const user_data_t* user_data);

// 注销（unregister）各类回调
int unregister_config(const std::string& name);
int unregister_complex_config(const std::string& name);
int unregister_init(const std::string& name);
int unregister_read(const std::string& name);
int unregister_read_group(const std::string& group);
int unregister_write(const std::string& name);
int unregister_flush(const std::string& name);
int unregister_missing(const std::string& name);
int unregister_cache_event(const std::string& name);
int unregister_shutdown(const std::string& name);
int unregister_data_set(const std::string& name);
int unregister_log(const std::string& name);
int unregister_notification(const std::string& name);

// 输出已注册的所有 write 插件名称
void log_available_writers();

// 分发数据：调用写插件，将采集到的数据发送出去
int dispatch_values(const value_list_t* vl);

// 分发多值（variadic 版本），store_type 为 DS_TYPE_XXX
int dispatch_multivalue(const value_list_t* tmpl, bool store_percentage, int store_type, ...);

// 分发“缺失值”事件
int dispatch_missing(const value_list_t* vl);

// 分发缓存事件
void dispatch_cache_event(CacheEventType event_type, unsigned long callbacks_mask,
                          const std::string& name, const value_list_t* vl);

// 分发通知事件
int dispatch_notification(const notification_t& notif);

// 日志输出接口
void log(int level, const std::string& message, ...);
void daemon_log(int level, const std::string& message, ...);

// 将字符串形式的日志级别解析为整数
int parse_log_severity(const std::string& severity);

// 将字符串形式的通知级别解析为整数
int parse_notif_severity(const std::string& severity);

// 根据类型名称获取数据集定义
const data_set_t* get_ds(const std::string& name);

// 以下为通知 meta 相关操作
int notification_meta_add_string(notification_t& n, const std::string& name, const std::string& value);
int notification_meta_add_signed_int(notification_t& n, const std::string& name, int64_t value);
int notification_meta_add_unsigned_int(notification_t& n, const std::string& name, uint64_t value);
int notification_meta_add_double(notification_t& n, const std::string& name, double value);
int notification_meta_add_boolean(notification_t& n, const std::string& name, bool value);
int notification_meta_copy(notification_t& dst, const notification_t& src);
int notification_meta_free(notification_meta_t* n); // 一般由 unique_ptr 自动管理

// 插件上下文管理（基于线程局部存储）
void init_ctx();
plugin_ctx_t get_ctx();
plugin_ctx_t set_ctx(const plugin_ctx_t& ctx);
cdtime_t get_interval();

// 基于插件上下文创建线程（封装 std::thread）
int thread_create(std::thread& thread, std::function<void(void*)> start_routine,
                  void* arg, const std::string& name);

// 每个插件模块需要实现此函数来注册自己的回调
void module_register();

} // namespace plugin
} // namespace collectd

#endif // PLUGIN_HPP