#include <iostream>
#include <string>
#include <vector>
#include <functional>

#include "config_global.h"         // CfGlobalConfig
#include "config_callbacks.h"      // CfCallbackRegistry, CfComplexCallbackRegistry, CfValueMapper
#include "oconfig.h"               // OConfigItem + parse API (oconfig_parse_file_cxx14, etc.)

// ----------------------------------------------------------------------
// 全局对象 (模拟原 configfile.c 全局)
// ----------------------------------------------------------------------
static CfGlobalConfig g_global_config;                
static CfCallbackRegistry g_callback_registry;        
static CfComplexCallbackRegistry g_complex_registry;  
static CfValueMapper g_value_mapper;                  

// ----------------------------------------------------------------------
// 1. 简单回调示例 - 可根据需要在 init_value_mapper() 中注册
// ----------------------------------------------------------------------
static int dispatch_value_plugindir(OConfigItem &ci)
{
    if (!ci.values.empty()) {
        std::string dir = ci.values[0].getString();
        std::cout << "[dispatch_value_plugindir] plugin dir => " << dir << std::endl;
        g_global_config.setOption("PluginDir", dir);
    }
    return 0;
}
static int dispatch_loadplugin(OConfigItem &ci)
{
    if (!ci.values.empty()) {
        std::string pluginName = ci.values[0].getString();
        std::cout << "[dispatch_loadplugin] load plugin => " << pluginName << std::endl;
        // 在此调用插件加载逻辑 ...
    }
    return 0;
}
static int dispatch_block_plugin(OConfigItem &ci)
{
    // 遇到 <Plugin something> 块时，可做更多处理
    std::cout << "[dispatch_block_plugin] plugin block => " << ci.key << std::endl;
    // children 中包含诸如 "LogLevel debug" 等子项
    for (size_t i = 0; i < ci.children.size(); ++i) {
        OConfigItem *child = ci.children[i].get();
        std::cout << "   child: " << child->key << std::endl;
        // ...
    }
    return 0;
}

// ----------------------------------------------------------------------
// 2. 初始化 CfValueMapper: 将特定 key 映射到回调
// ----------------------------------------------------------------------
static void init_value_mapper()
{
    g_value_mapper.addMapping("PluginDir", dispatch_value_plugindir);
    g_value_mapper.addMapping("LoadPlugin", dispatch_loadplugin);
    g_value_mapper.addMapping("Plugin", dispatch_block_plugin);
    // 需要更多映射时，可继续添加
}

// ----------------------------------------------------------------------
// 3. 递归分发 OConfigItem 节点
// ----------------------------------------------------------------------
static int dispatch_value(OConfigItem &ci)
{
    // 若 CfValueMapper 有回调，则执行
    bool executed = g_value_mapper.execute(ci.key, ci);
    if (!executed) {
        // 否则，若有 value，把第一个当作 string 存入全局
        if (!ci.values.empty()) {
            // 这里示例地只处理 [STRING]
            std::string val_str = ci.values[0].getString();
            g_global_config.setOption(ci.key, val_str);
        }
    }
    return 0;
}

// 递归处理 “块” (ci), 对当前节点 dispatch_value, 再处理其 children
static void dispatch_block(OConfigItem &ci)
{
    // 先处理自己
    dispatch_value(ci);

    // 再处理子节点
    for (auto &child : ci.children) {
        dispatch_block(*child);
    }
}

// ----------------------------------------------------------------------
// 4. 公开接口：解析配置文件 -> 生成 OConfigItem -> dispatch
// ----------------------------------------------------------------------
int cf_read(const std::string &path)
{
    // 先做一次 mapper 初始化 (只做一次也可)
    static bool inited = false;
    if (!inited) {
        init_value_mapper();
        inited = true;
    }

    OConfigItem root("Root");
    int status = oconfig_parse_file_cxx14(path.c_str(), &root);
    if (status != 0) {
        std::cerr << "[cf_read] parse failed for: " << path << std::endl;
        return status;
    }
    // 递归分发
    dispatch_block(root);
    return 0;
}

// ----------------------------------------------------------------------
// 5. 与原 configfile.c 类似的接口: cf_dispatch_option, cf_register, ...
// ----------------------------------------------------------------------
int cf_dispatch_option(const std::string &key, const std::string &value)
{
    // 构造临时 OConfigItem
    OConfigItem ci(key);
    ci.addValue(OConfigValue(value));

    bool executed = g_value_mapper.execute(key, ci);
    if (!executed) {
        g_global_config.setOption(key, value);
    }
    return 0;
}

int cf_search(const std::string &key)
{
    // 判断 CfValueMapper 或 CfGlobalConfig 是否有此 key
    OConfigItem dummy("dummy");
    bool found = g_value_mapper.execute(key, dummy);
    if (found) return 0;
    std::string val = g_global_config.getOption(key);
    return val.empty() ? -1 : 0;
}

int cf_register(const std::string &type,
                std::function<int(const std::string &, const std::string &)> cb,
                const std::vector<std::string> &keys,
                plugin_ctx_t ctx)
{
    g_callback_registry.registerCallback(type, cb, keys, ctx);
    return 0;
}

int cf_unregister(const std::string &type)
{
    // CfCallbackRegistry 未实现 remove，自己扩展
    return 0;
}

int cf_register_complex(const std::string &type,
                        std::function<int(OConfigItem &)> cb,
                        plugin_ctx_t ctx)
{
    g_complex_registry.registerComplexCallback(type, cb, ctx);
    return 0;
}

int cf_unregister_complex(const std::string &type)
{
    // CfComplexCallbackRegistry 未实现 remove，自己扩展
    return 0;
}

// ----------------------------------------------------------------------
// 6. 全局选项相关函数
// ----------------------------------------------------------------------
void global_option_set(const std::string &key, const std::string &value)
{
    g_global_config.setOption(key, value);
}
std::string global_option_get(const std::string &key)
{
    return g_global_config.getOption(key);
}

// 若需要一些特殊处理(比如解析字符串为数字):
double global_option_get_time(const std::string &key, double def)
{
    std::string v = g_global_config.getOption(key);
    if (v.empty()) return def;
    try {
        return std::stod(v);
    } catch(...) {
        return def;
    }
}

double cf_get_default_interval()
{
    return global_option_get_time("Interval", 10.0);
}

// ----------------------------------------------------------------------
// 如果需要给C语言调用，则加extern "C"接口
// 这里保留C++函数即可
// ----------------------------------------------------------------------