#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <dirent.h>     // POSIX目录函数(opendir, readdir, closedir)
#include <sys/stat.h>   // stat用来判断是否是目录
#include <errno.h>
#include <cstring>      // strerror
#include <cstdio>       // printf (仅作演示)
#include <cstdlib>      // std::stod in c++11/c++14

#include "config_global.h"         // CfGlobalConfig
#include "config_callbacks.h"      // CfCallbackRegistry, CfComplexCallbackRegistry, CfValueMapper
#include "oconfig.h"               // OConfigItem

// -----------------------
// 全局管理对象 (模拟原 configfile.c 的全局)
// -----------------------
static CfGlobalConfig g_global_config;                // 全局选项管理
static CfCallbackRegistry g_callback_registry;        // 简单回调管理
static CfComplexCallbackRegistry g_complex_registry;  // 复杂回调管理
static CfValueMapper g_value_mapper;                  // Key -> 回调函数映射

// ----------------------------------------------------------------------
// 1. 初始化全局选项 (示例)
// ----------------------------------------------------------------------
static void init_global_options()
{
    // 模拟 collectd 中 cf_global_options 的默认值
    g_global_config.setOption("BaseDir", "/var/lib/collectd");
    g_global_config.setOption("PIDFile", "/var/run/collectd.pid");
    g_global_config.setOption("Hostname", "");
    g_global_config.setOption("FQDNLookup", "true");
    g_global_config.setOption("Interval", "10");  // 默认10秒
    g_global_config.setOption("Timeout", "2");
    // ... 可以继续添加
}

// ----------------------------------------------------------------------
// 2. 示例回调函数：对应 dispatch_value_plugindir, dispatch_loadplugin, 等
// ----------------------------------------------------------------------
static int dispatch_value_plugindir(OConfigItem &ci)
{
    if (!ci.values.empty()) {
        std::string dir = ci.values[0].getString();
        std::cout << "[dispatch_value_plugindir] plugin dir: " << dir << std::endl;
        // 写入全局配置
        g_global_config.setOption("PluginDir", dir);
    }
    return 0;
}

static int dispatch_loadplugin(OConfigItem &ci)
{
    if (!ci.values.empty()) {
        std::string pluginName = ci.values[0].getString();
        std::cout << "[dispatch_loadplugin] load plugin: " << pluginName << std::endl;
        // 在此可以调用真正的插件加载逻辑
        // ...
    }
    return 0;
}

static int dispatch_block_plugin(OConfigItem &ci)
{
    std::cout << "[dispatch_block_plugin] plugin block key: " << ci.key << std::endl;
    for (size_t i = 0; i < ci.children.size(); ++i) {
        OConfigItem *child = ci.children[i].get();
        std::cout << "   child key: " << child->key << std::endl;
        // ...
    }
    return 0;
}

// 将上述回调注册到 CfValueMapper
static void init_value_mapper()
{
    g_value_mapper.addMapping("PluginDir", dispatch_value_plugindir);
    g_value_mapper.addMapping("LoadPlugin", dispatch_loadplugin);
    g_value_mapper.addMapping("Plugin", dispatch_block_plugin);
    // 如还有其它键 -> 回调函数，也可添加
}

// ----------------------------------------------------------------------
// 3. 与原 configfile.c 类似的函数接口示例
// ----------------------------------------------------------------------

static int cf_search(const std::string &key)
{
    // 在 CfValueMapper 里检查
    OConfigItem dummy("dummy");
    bool foundInMapper = g_value_mapper.execute(key, dummy);
    if (foundInMapper)
        return 0;

    // 或检查 GlobalConfig
    std::string val = g_global_config.getOption(key);
    if (!val.empty())
        return 0;

    return -1;
}

static int cf_dispatch_option(const std::string &key, const std::string &value)
{
    // 先尝试在 CfValueMapper 中执行
    OConfigItem ci(key);
    ci.addValue(OConfigValue(value));

    bool executed = g_value_mapper.execute(key, ci);
    if (executed) {
        return 0;
    }

    // 否则视为全局选项
    g_global_config.setOption(key, value);
    return 0;
}

static int dispatch_global_option(const std::string &key, const std::string &value)
{
    g_global_config.setOption(key, value);
    return 0;
}

static int dispatch_value(OConfigItem &ci)
{
    bool ok = g_value_mapper.execute(ci.key, ci);
    if (!ok) {
        // 如果没有回调，就简单地写入 GlobalConfig (仅示例)
        if (!ci.values.empty() && ci.values[0].type == OConfigType::STRING) {
            g_global_config.setOption(ci.key, ci.values[0].getString());
        }
    }
    return 0;
}

static int dispatch_block(OConfigItem &parentBlock)
{
    // 递归分发
    for (size_t i = 0; i < parentBlock.children.size(); ++i) {
        OConfigItem &child = *parentBlock.children[i];
        dispatch_value(child);
        if (!child.children.empty()) {
            dispatch_block(child);
        }
    }
    return 0;
}

// ----------------------------------------------------------------------
// 4. OConfigItem 帮助操作
// ----------------------------------------------------------------------
static OConfigItem* cf_ci_replace_child(OConfigItem *parent, OConfigItem *old_child, OConfigItem *new_child)
{
    if (!parent || !old_child || !new_child) return NULL;

    for (size_t i = 0; i < parent->children.size(); ++i) {
        if (parent->children[i].get() == old_child) {
            parent->children[i] = std::make_unique<OConfigItem>(*new_child);
            parent->children[i]->parent = parent;
            return parent->children[i].get();
        }
    }
    return NULL;
}

static void cf_ci_append_children(OConfigItem *dest, OConfigItem *src)
{
    if (!dest || !src) return;
    for (size_t i = 0; i < src->children.size(); ++i) {
        OConfigItem *child = src->children[i].get();
        OConfigItem *added = dest->addChild(child->key);
        // 拷贝 values
        added->values = child->values;
        // 递归
        cf_ci_append_children(added, child);
    }
}

// ----------------------------------------------------------------------
// 5. 读取文件/目录 (使用 POSIX + C++14)
// ----------------------------------------------------------------------
static bool is_directory(const std::string &path)
{
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode) != 0;
    }
    return false;
}

static bool cf_compare_string(const std::string &a, const std::string &b)
{
    return (a == b);
}

static int cf_read_file(const std::string &file, OConfigItem &root)
{
    std::ifstream ifs(file.c_str());
    if (!ifs.is_open()) {
        std::cerr << "[cf_read_file] cannot open file: " << file << " (" << strerror(errno) << ")\n";
        return -1;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        // 简易的“Key Value”解析
        if (line.empty()) 
            continue;
        std::string::size_type pos = line.find(' ');
        if (pos == std::string::npos) 
            continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        OConfigItem *child = root.addChild(key);
        child->addValue(OConfigValue(val));
    }
    ifs.close();
    return 0;
}

static int cf_read_dir(const std::string &dir, OConfigItem &root)
{
    DIR *dp = opendir(dir.c_str());
    if (!dp) {
        std::cerr << "[cf_read_dir] opendir failed: " << dir << " (" << strerror(errno) << ")\n";
        return -1;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(dp)) != NULL) {
        // 忽略 . 和 ..
        if (cf_compare_string(entry->d_name, ".") || cf_compare_string(entry->d_name, "..")) {
            continue;
        }
        // 构造路径
        std::string path = dir + "/" + entry->d_name;
        // 如果是目录，则可递归或直接跳过；这里为了示例，都当文件处理
        if (is_directory(path)) {
            // 也可调用： cf_read_dir(path, root);
            cf_read_file(path, root);
        } else {
            cf_read_file(path, root);
        }
    }
    closedir(dp);
    return 0;
}

static int cf_read_generic(const std::string &path, OConfigItem &root)
{
    if (is_directory(path)) {
        return cf_read_dir(path, root);
    } else {
        return cf_read_file(path, root);
    }
}

static int cf_include_all(const std::string &pattern)
{
    // 如果需要支持 glob pattern，可使用 fnmatch + opendir 进行遍历匹配
    // 这里仅示例输出
    std::cout << "[cf_include_all] pattern: " << pattern << std::endl;
    return 0;
}

// ----------------------------------------------------------------------
// 6. 对外暴露的核心读取函数
// ----------------------------------------------------------------------
static int cf_read(const std::string &path)
{
    OConfigItem root("Root");
    int status = cf_read_generic(path, root);
    if (status != 0) {
        return status;
    }
    // 递归分发
    dispatch_block(root);
    return 0;
}

// ----------------------------------------------------------------------
// 7. 全局选项接口
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
    double interval = global_option_get_time("Interval", 10.0);
    return interval;
}

// ----------------------------------------------------------------------
// 8. 回调的注册与注销
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
    // 需要在 CfCallbackRegistry 中实现 remove/erase 才能真正卸载
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
    // 同上，需要 CfComplexCallbackRegistry 提供相应卸载功能
    return 0;
}

// ----------------------------------------------------------------------
// main 函数示例（非必须，仅演示用）
// ----------------------------------------------------------------------
int main()
{
    init_global_options();
    init_value_mapper();

    // 读取配置文件/目录 (你可根据实际情况传入有效路径)
    cf_read("/tmp/myconfig");

    // 测试全局选项
    std::cout << "[main] BaseDir=" << global_option_get("BaseDir") << std::endl;
    std::cout << "[main] Interval=" << cf_get_default_interval() << std::endl;

    return 0;
}