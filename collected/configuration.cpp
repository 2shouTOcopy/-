#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem> // C++17 读取目录功能
#include <chrono>

#include "config_global.h"         // CfGlobalConfig
#include "config_callbacks.h"      // CfCallbackRegistry, CfComplexCallbackRegistry, CfValueMapper
#include "oconfig.h"               // OConfigItem

// 全局静态对象，用来模拟原 configfile.c 里的全局变量
static CfGlobalConfig g_global_config;                // 全局选项管理
static CfCallbackRegistry g_callback_registry;        // 简单回调管理
static CfComplexCallbackRegistry g_complex_registry;  // 复杂回调管理
static CfValueMapper g_value_mapper;                  // Key -> 回调函数映射

// ----------------------------------------------------------------------
// 1. 原 configfile.c 中对应的全局选项数组/结构的替代，用 CfGlobalConfig 管理
// ----------------------------------------------------------------------

/* 
 * 例如，原先 collectd 代码中：
 *   static cf_global_option_t cf_global_options[] = { ... };
 *   static int cf_global_options_num = ...
 * 这里可以改成初始化 CfGlobalConfig (g_global_config) 中的默认选项。
 */
static void init_global_options()
{
    // 示例：可仿照原 cf_global_options 初始化
    g_global_config.setOption("BaseDir", "/var/lib/collectd");
    g_global_config.setOption("PIDFile", "/var/run/collectd.pid");
    g_global_config.setOption("Hostname", "");
    g_global_config.setOption("FQDNLookup", "true");
    g_global_config.setOption("Interval", "10");  // 默认10秒
    g_global_config.setOption("Timeout", "2");
    // ...根据需要继续
}

// ----------------------------------------------------------------------
// 2. 简单回调与复杂回调的示例性注册
// ----------------------------------------------------------------------

// 下面模拟 dispatch_value_plugindir / dispatch_loadplugin / dispatch_block_plugin 等
// 在 CfValueMapper 中注册。
static int dispatch_value_plugindir(OConfigItem &ci)
{
    // 例如处理 PluginDir 的值
    if (!ci.values.empty()) {
        std::string dir = ci.values[0].getString();
        std::cout << "[dispatch_value_plugindir] plugin dir: " << dir << std::endl;
        // 实际可以将此值写到全局配置
        g_global_config.setOption("PluginDir", dir);
    }
    return 0;
}

static int dispatch_loadplugin(OConfigItem &ci)
{
    // 例如处理 LoadPlugin 的逻辑
    if (!ci.values.empty()) {
        std::string pluginName = ci.values[0].getString();
        std::cout << "[dispatch_loadplugin] load plugin: " << pluginName << std::endl;
        // 此处可调用插件管理逻辑
        // ...
    }
    return 0;
}

static int dispatch_block_plugin(OConfigItem &ci)
{
    // 例如处理 <Plugin "xx"> ... </Plugin> 块
    std::cout << "[dispatch_block_plugin] plugin block key: " << ci.key << std::endl;
    // 可以遍历 children 做进一步处理
    for (auto &child : ci.children) {
        std::cout << "   child key: " << child->key << std::endl;
        // ...
    }
    return 0;
}

// 此函数在初始化时，将上述函数映射到 CfValueMapper
static void init_value_mapper()
{
    g_value_mapper.addMapping("PluginDir", dispatch_value_plugindir);
    g_value_mapper.addMapping("LoadPlugin", dispatch_loadplugin);
    g_value_mapper.addMapping("Plugin", dispatch_block_plugin);
    // 如果还有其他 key -> 回调函数，可以继续添加
}

// ----------------------------------------------------------------------
// 3. 对应原 configfile.c 中的函数接口示例
//    以下函数为示例性、最小化的伪实现，展示如何利用新的类来完成相同功能
// ----------------------------------------------------------------------

/**
 * cf_search:
 * 原先可能是搜索全局 option / 回调等，这里仅示例如何在 CfValueMapper 和 GlobalConfig 中“搜索” 
 */
static int cf_search(const std::string &key)
{
    // 如果 CfValueMapper 里存在这个 key，就返回 0 表示找到
    OConfigItem dummy("dummy");
    bool foundInMapper = g_value_mapper.execute(key, dummy);
    if (foundInMapper)
        return 0;

    // 或者检查 global_config 里是否存在
    std::string val = g_global_config.getOption(key);
    if (!val.empty())
        return 0;

    // 没找到
    return -1;
}

/**
 * cf_dispatch_option:
 * 原先是尝试把一个 key/value 分发到对应的 handler（可能是全局选项，也可能是回调）
 */
static int cf_dispatch_option(const std::string &key, const std::string &value)
{
    // 先试试在 CfValueMapper 有无对应回调（这里需要稍微改造一下：临时构造一个 OConfigItem）
    OConfigItem ci(key);
    ci.addValue(OConfigValue(value)); // 当做 string

    bool executed = g_value_mapper.execute(key, ci);
    if (executed) {
        return 0;
    }

    // 如果 mapper 没处理，则当成全局选项
    g_global_config.setOption(key, value);
    return 0;
}

/**
 * dispatch_global_option:
 * 原先将 key/value 写入全局，这里直接使用 CfGlobalConfig
 */
static int dispatch_global_option(const std::string &key, const std::string &value)
{
    g_global_config.setOption(key, value);
    return 0;
}

/**
 * dispatch_value:
 * 原先 configfile.c 里有 dispatch_value 之类，这里示例把 OConfigItem 的 key 当映射键
 */
static int dispatch_value(OConfigItem &ci)
{
    // 根据 ci.key 查 CfValueMapper
    bool ok = g_value_mapper.execute(ci.key, ci);
    if (!ok) {
        // 如果 mapper 没有对应处理器，看看是不是要存到 global config
        // 也可能是别的逻辑，这里简单示例
        if (!ci.values.empty() && ci.values[0].type == OConfigType::STRING) {
            g_global_config.setOption(ci.key, ci.values[0].getString());
        }
    }
    return 0;
}

/**
 * dispatch_block:
 * 原先处理大块 `<Block "..."> ... </Block>`，此处仅示例
 */
static int dispatch_block(OConfigItem &parentBlock)
{
    // 可能会遍历子节点做处理
    for (auto &child : parentBlock.children) {
        dispatch_value(*child);
        // 若还有嵌套 children，则递归
        if (!child->children.empty()) {
            dispatch_block(*child);
        }
    }
    return 0;
}

// ----------------------------------------------------------------------
// 4. OConfigItem 的辅助操作
// ----------------------------------------------------------------------

static OConfigItem* cf_ci_replace_child(OConfigItem *parent, OConfigItem *old_child, OConfigItem *new_child)
{
    if (!parent || !old_child || !new_child) return nullptr;

    for (auto &childPtr : parent->children) {
        if (childPtr.get() == old_child) {
            // 替换为 new_child
            childPtr = std::make_unique<OConfigItem>(*new_child);
            childPtr->parent = parent;
            return childPtr.get();
        }
    }
    return nullptr;
}

static void cf_ci_append_children(OConfigItem *dest, OConfigItem *src)
{
    if (!dest || !src) return;
    for (auto &child : src->children) {
        OConfigItem *added = dest->addChild(child->key);
        // 拷贝 values
        added->values = child->values;
        // 如果还有 children，可递归
        cf_ci_append_children(added, child.get());
    }
}

// ----------------------------------------------------------------------
// 5. 文件、目录读取相关 (示例化)
// ----------------------------------------------------------------------
static bool cf_compare_string(const std::string &a, const std::string &b)
{
    return (a == b);
}

// 简化版，递归包含文件
static int cf_include_all(const std::string &pattern)
{
    // 这里仅示例，真正实现可能要做 glob 匹配
    std::cout << "[cf_include_all] pattern: " << pattern << std::endl;
    return 0;
}

// 读单个文件，解析后存入 OConfigItem
static int cf_read_file(const std::string &file, OConfigItem &root)
{
    std::ifstream ifs(file);
    if (!ifs.is_open()) {
        std::cerr << "[cf_read_file] cannot open file: " << file << std::endl;
        return -1;
    }
    // 简化演示：假设文件中每一行 `Key Value`
    std::string line;
    while (std::getline(ifs, line)) {
        // 简陋解析
        if (line.empty()) continue;
        auto pos = line.find(' ');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        OConfigItem *child = root.addChild(key);
        child->addValue(OConfigValue(val));
    }
    return 0;
}

static int cf_read_dir(const std::string &dir, OConfigItem &root)
{
    std::error_code ec;
    for (auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) {
            cf_read_file(entry.path().string(), root);
        }
    }
    return 0;
}

// 可能是对文件或目录做统一处理
static int cf_read_generic(const std::string &path, OConfigItem &root)
{
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return cf_read_dir(path, root);
    } else {
        return cf_read_file(path, root);
    }
}

/**
 * cf_read:
 * 对外暴露的读取入口，可读取一个 config 文件或目录，然后 dispatch
 */
static int cf_read(const std::string &path)
{
    OConfigItem root("Root");
    int status = cf_read_generic(path, root);
    if (status != 0) {
        return status;
    }

    // 读完后，把 root 递归分发
    dispatch_block(root);
    return 0;
}

// ----------------------------------------------------------------------
// 6. 全局选项接口
// ----------------------------------------------------------------------

static void global_option_set(const std::string &key, const std::string &value)
{
    g_global_config.setOption(key, value);
}

static std::string global_option_get(const std::string &key)
{
    return g_global_config.getOption(key);
}

static double global_option_get_time(const std::string &key, double default_val)
{
    // 从 global_config 取值，如果为空或不是数字则用 default_val
    std::string val = g_global_config.getOption(key);
    if (val.empty()) {
        return default_val;
    }
    try {
        return std::stod(val);
    } catch (...) {
        return default_val;
    }
}

static double cf_get_default_interval()
{
    // 原 collectd 里默认 Interval 可能是 10s (或全局选项)
    // 这里做个简单封装
    double interval = global_option_get_time("Interval", 10.0);
    return interval;
}

// ----------------------------------------------------------------------
// 7. 回调注册与注销 (cf_register / cf_unregister 等)
// ----------------------------------------------------------------------

static int cf_register(const std::string &type,
                       std::function<int(const std::string &, const std::string &)> cb,
                       const std::vector<std::string> &keys,
                       plugin_ctx_t ctx)
{
    g_callback_registry.registerCallback(type, cb, keys, ctx);
    return 0;
}

static int cf_unregister(const std::string &type)
{
    // 这里示例中 CfCallbackRegistry 没有提供注销接口
    // 若要支持，需在 CfCallbackRegistry 中添加对应的 remove 方法
    // 这里只是展示如何改写
    return 0;
}

static int cf_register_complex(const std::string &type,
                               std::function<int(OConfigItem &)> cb,
                               plugin_ctx_t ctx)
{
    g_complex_registry.registerComplexCallback(type, cb, ctx);
    return 0;
}

static int cf_unregister_complex(const std::string &type)
{
    // 同样的，示例中 CfComplexCallbackRegistry 没有注销接口
    // 在实际项目中可自行扩展
    return 0;
}

// ----------------------------------------------------------------------
// 主入口示例：
// ----------------------------------------------------------------------
int main()
{
    // 初始化全局选项 & ValueMapper
    init_global_options();
    init_value_mapper();

    // 可以模拟往 CfCallbackRegistry / CfComplexCallbackRegistry 注册一些回调
    // 举例：cf_register("ExampleType", someCallback, {"Key1","Key2"}, ...);

    // 读取配置文件（或目录）
    cf_read("/path/to/config/file_or_dir");

    // 检查 global_option
    std::cout << "Global Option BaseDir = " << global_option_get("BaseDir") << std::endl;
    std::cout << "Global Option Interval = " << cf_get_default_interval() << std::endl;

    return 0;
}