#pragma once

#include <string>

#include "ModuleLoader.h"
#include "ModuleDef.h"

class PluginService
{
public:
    static PluginService &Instance();

    // 插件目录
    void setDirectory(const std::string &dir);

    // 加载 / 查询
    int load(const char *pluginName, bool global);
    bool isLoaded(const char *pluginName) const;

    // 生命周期
    int initAll();
    int readAllOnce();
    void readAll(); // 常规循环里调用

    // 分发接口
    int write(const data_set_t *ds, const value_list_t *vl);
    int flush(const char *pluginName, cdtime_t timeout, const char *ident);
	int flushAll();
    int dispatchMissing(const value_list_t *vl);
    void dispatchCacheEvent(enum cache_event_type_e type,
                            unsigned long mask,
                            const char *name,
                            const value_list_t *vl);
    int dispatchNotification(const notification_t *notif);

    int shutdownAll();

    void log(int level, const char *format, ...);

	int dispatchValues(const value_list_t *vl);
	int dispatchMultivalues(const value_list_t* vl_template,
							bool store_percentage_if_gauge,
							int common_store_type,
							const std::vector<MetricDataPoint>& data_points);

private:
    PluginService() = default;
    ~PluginService() = default;
};

