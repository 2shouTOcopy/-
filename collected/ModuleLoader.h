#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

/// \brief 封装动态加载插件的逻辑
///
/// - 设置插件搜索路径
/// - 加载插件 (dlopen)
/// - 记录已加载状态
/// - 调用插件中的 module_register()
/// - （可选）卸载插件
class ModuleLoader {
public:
    /// \brief 获取单例
    static ModuleLoader& Instance()
    {
        static ModuleLoader s_instance;
        return s_instance;
    }

    /// \brief 设置搜索插件的路径，例如 /usr/local/lib/collectd
    void SetPluginDir(const std::string &dir);

    /// \brief 加载指定名字的插件
    /// \param pluginName 插件名（不带后缀 .so）
    /// \param global 是否用 RTLD_GLOBAL
    /// \return 0 表示成功，大于 0 或 < 0 表示失败
    int LoadPlugin(const std::string &pluginName, bool global);

    /// \brief 判断插件是否已经加载
    bool IsLoaded(const std::string &pluginName) const;

    /// \brief （可选）卸载插件
    /// \return 0: success, 非 0: error
    int UnloadPlugin(const std::string &pluginName);

private:
    /// 私有构造函数，单例
    ModuleLoader() = default;
    ~ModuleLoader() = default;

    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

private:
    /// 记录一个插件的信息，简单起见只有dlopen的句柄
    struct PluginHandle {
        void *dlHandle;
    };

    /// 辅助函数：实际执行 dlopen + dlsym + plugin_register
    int LoadPluginFile(const std::string &fullPath, bool global,
                       const std::string &pluginName);

private:
    /// 互斥锁保护 m_loadedPlugins 等共享状态
    mutable std::mutex m_mutex;

    /// 插件搜索目录（如 /usr/local/lib/collectd）
    std::string m_pluginDir;

    /// 已加载的插件表
    /// key = pluginName, value = {dlHandle}
    std::unordered_map<std::string, PluginHandle> m_loadedPlugins;
};