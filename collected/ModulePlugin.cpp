#include "ModuleLoader.h"
#include <iostream>
#include <dlfcn.h>   // dlopen, dlsym, dlclose
#include <dirent.h>  // opendir, readdir
#include <sys/stat.h>
#include <cstring>

// 如果需要用到 Collectd 原本的 log/error 宏，可自行包含相应头文件
// #include "plugin.h" // or relevant logging headers

// --------------------------------------------------------------------------
// 设置插件搜索目录
// --------------------------------------------------------------------------
void ModuleLoader::SetPluginDir(const std::string &dir)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pluginDir = dir;
}

// --------------------------------------------------------------------------
// 加载插件
// --------------------------------------------------------------------------
int ModuleLoader::LoadPlugin(const std::string &pluginName, bool global)
{
    // 若已加载，则直接返回
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_loadedPlugins.find(pluginName) != m_loadedPlugins.end()) {
            // 已加载
            return 0;
        }
    }

    // 需要搜索目录下的 同名.so
    // 假设 suffix 就是 ".so"
    const std::string soName = pluginName + ".so";

    // 打开目录进行检索
    // 这里的实现跟 collectd 原本 plugin_load() 类似
    // 也可以只拼接 path = m_pluginDir + "/" + soName，直接调用 LoadPluginFile
    // 取决于你想要的实现方式（是否在目录下做更多判断）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pluginDir.empty()) {
            // 如果没有指定插件目录，可在这里给出警告或用默认值
            // std::cerr << "ModuleLoader::LoadPlugin: pluginDir not set.\n";
        }
    }

    std::string fullPath = m_pluginDir + "/" + soName;

    // 检查文件是否存在
    struct stat st;
    if (::stat(fullPath.c_str(), &st) != 0) {
        // 文件不存在
        // 可以再尝试 opendir 方式遍历，但这里简单处理
        std::cerr << "[ModuleLoader] " << fullPath << " not found\n";
        return -1;
    }

    // 确实存在，尝试加载
    int status = LoadPluginFile(fullPath, global, pluginName);
    return status;
}

// --------------------------------------------------------------------------
// 实际的 dlopen + module_register
// --------------------------------------------------------------------------
int ModuleLoader::LoadPluginFile(const std::string &fullPath, bool global,
                                 const std::string &pluginName)
{
    int flags = RTLD_NOW;
    if (global)
        flags |= RTLD_GLOBAL;

    void *handle = dlopen(fullPath.c_str(), flags);
    if (!handle) {
        std::cerr << "[ModuleLoader] dlopen(\"" << fullPath << "\") failed: "
                  << dlerror() << std::endl;
        return -1;
    }

    // 查找 module_register 符号
    // collectd 插件一般都需要 module_register()
    void *regSymbol = dlsym(handle, "module_register");
    if (!regSymbol) {
        std::cerr << "[ModuleLoader] Can't find symbol \"module_register\" in \""
                  << fullPath << "\": " << dlerror() << std::endl;
        // 关闭 handle
        dlclose(handle);
        return -2;
    }

    // 转成函数指针并调用
    using RegisterFunc = void (*)();
    RegisterFunc regFunc = reinterpret_cast<RegisterFunc>(regSymbol);
    // 调用插件的注册函数
    regFunc();

    // 如果成功执行到这里，说明已加载
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PluginHandle ph;
        ph.dlHandle = handle;
        m_loadedPlugins[pluginName] = ph;
    }

    std::cerr << "[ModuleLoader] plugin \"" << pluginName
              << "\" loaded successfully.\n";
    return 0;
}

// --------------------------------------------------------------------------
// 判断插件是否已加载
// --------------------------------------------------------------------------
bool ModuleLoader::IsLoaded(const std::string &pluginName) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_loadedPlugins.find(pluginName) != m_loadedPlugins.end());
}

// --------------------------------------------------------------------------
// （可选）卸载插件
// --------------------------------------------------------------------------
int ModuleLoader::UnloadPlugin(const std::string &pluginName)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_loadedPlugins.find(pluginName);
    if (it == m_loadedPlugins.end()) {
        // 未加载
        return -1;
    }

    // 可以先看看是否需要特殊的 plugin shutdown 流程
    // collectd 中在 plugin_shutdown_all() 时会逐个调用
    // 这里仅演示 dlclose
    void *dl = it->second.dlHandle;
    if (dl) {
        dlclose(dl);
    }

    m_loadedPlugins.erase(it);
    std::cerr << "[ModuleLoader] plugin \"" << pluginName
              << "\" unloaded.\n";
    return 0;
}