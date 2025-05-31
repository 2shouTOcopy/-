#pragma once
#include <vector>
#include <string>

#include "config_global.h"
#include "config_callbacks.h"
#include "oconfig.h"
#include "types_parser.h"

class ConfigManager
{
public:
    static ConfigManager &Instance()
    {
        static ConfigManager instance;
        return instance;
    }

    int Read(const char *filename);
    int DispatchOption(const std::string &key, const std::string &value);
    int Search(const std::string &key);
    int Register(const std::string &type,
                 std::function<int(const std::string &, const std::string &)> cb,
                 const std::vector<std::string> &keys,
                 plugin_ctx_t ctx); // Assuming plugin_ctx_t is defined somewhere
    int Unregister(const std::string &type);
    int RegisterComplex(const std::string &type,
                        std::function<int(OConfigItem &)> cb,
                        plugin_ctx_t ctx);
    int UnregisterComplex(const std::string &type);
    void SetGlobalOption(const std::string &key, const std::string &value);
    std::string GetGlobalOption(const std::string &key);
    double GetGlobalOptionTime(const std::string &key, double def);
    double GetDefaultInterval();

    const std::vector<data_set_t>& GetTypeDataSets() const;

    const data_set_t* GetDataSetByName(const std::string& type_name) const;

    // 删除拷贝构造函数和赋值运算符
    ConfigManager(const ConfigManager &) = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

private:
    ConfigManager();
    ~ConfigManager();

    void InitGlobalOptions();
    void InitValueMapper();

    int DispatchValuePluginDir(OConfigItem &ci);
    int DispatchLoadPlugin(OConfigItem &ci);
    int DispatchBlockPlugin(OConfigItem &ci);
    int FcConfigure(OConfigItem &ci);
    int DispatchGlobalOption(OConfigItem &ci);
    int DispatchBlock(OConfigItem &ci);
    int DispatchValue(OConfigItem &ci);

private:
    CfGlobalConfig global_config_;
    CfCallbackRegistry callback_registry_;
    CfComplexCallbackRegistry complex_registry_;
    CfValueMapper value_mapper_;
    std::vector<data_set_t> type_datasets_;
};
