#include <iostream>
#include <cstdarg>

#include "RstDispatcher.h"
#include "PluginService.h"
#include "ModuleBase.h"
#include "ModuleDef.h"

PluginService &PluginService::Instance()
{
    static PluginService inst;
    return inst;
}

void PluginService::setDirectory(const std::string &dir)
{
    ModuleLoader::Instance().SetDir(dir);
}

int PluginService::load(const char *pluginName, bool global)
{
    if (!pluginName)
        return EINVAL;
    return ModuleLoader::Instance().Load(pluginName, global);
}

bool PluginService::isLoaded(const char *pluginName) const
{
    if (!pluginName)
        return false;
    return ModuleLoader::Instance().IsLoaded(pluginName);
}

int PluginService::initAll()
{
    int status = 0;
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
		std::cerr << "[init] plugin:" << name << "\n";
        if (mod && ((status = mod->init()) != 0))
        {
            std::cerr << "[plugin] init failed: " << name << "err:" << status << "\n";
            return status;
        }
    }
    return 0;
}

int PluginService::readAllOnce()
{
    int status = 0;
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
		std::cerr << "[read] plugin:" << name << "\n";
        if (mod && mod->read() != 0)
        {
            std::cerr << "[plugin] read failed: " << name << "\n";
            status = -1;
        }
    }
    return status;
}

void PluginService::readAll()
{
    // 简单起见，这里直接调用一次性读；你可以改成多线程或定时调度
    readAllOnce();
}

int PluginService::write(const data_set_t *ds, const value_list_t *vl)
{
    if (!vl)
        return EINVAL;
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
        if (mod)
            mod->write(ds, vl);
    }
    return 0;
}

int PluginService::flush(const char *pluginName,
                         cdtime_t timeout,
                         const char *ident)
{
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        if (!pluginName || name == pluginName)
        {
            auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
            if (mod)
                mod->flush();
        }
    }
    return 0;
}

int PluginService::flushAll()
{
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
        if (mod)
            mod->flush();
    }
    return 0;
}


int PluginService::dispatchMissing(const value_list_t *vl)
{
    if (!vl)
        return EINVAL;
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
        if (mod)
            mod->missing();
    }
    return 0;
}

void PluginService::dispatchCacheEvent(enum cache_event_type_e type,
                                       unsigned long mask,
                                       const char *name,
                                       const value_list_t *vl)
{
    for (auto &p : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(p);
        if (mod)
            mod->cache_event();
    }
}

int PluginService::dispatchNotification(const notification_t *notif)
{
    if (!notif)
        return EINVAL;
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
        if (mod)
            mod->notification();
    }
    return 0;
}

int PluginService::shutdownAll()
{
    int status = 0;
    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
        if (mod && mod->shutdown() != 0)
        {
            std::cerr << "[plugin] shutdown failed: " << name << "\n";
            status = -1;
        }
        // 卸载模块
        ModuleLoader::Instance().Unload(name);
    }

	RstDispatcher::Instance().stop();

    return status;
}

void PluginService::log(int level, const char *format, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    std::cerr << buf << "\n";

    for (auto &name : ModuleLoader::Instance().GetLoadedPluginNames())
    {
        auto mod = ModuleLoader::Instance().GetUserModuleImpl(name);
        if (mod)
            mod->logmsg();
    }
}

int PluginService::dispatchValues(const value_list_t *vl)
{
	return RstDispatcher::Instance().enqueue(vl);
}

int PluginService::dispatchMultivalues(const value_list_t* vl_template,
										bool store_percentage_if_gauge,
										int common_store_type,
										const std::vector<MetricDataPoint>& data_points)
{
	return RstDispatcher::Instance().enqueueMultivalues(vl_template, 
		store_percentage_if_gauge, common_store_type, data_points);
}

