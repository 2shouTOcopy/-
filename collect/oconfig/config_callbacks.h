#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

#include "oconfig.h"
#include "ModuleDef.h"
#include "CommonDef.h"

struct CfCallback {
    std::string type;
    std::function<int(const std::string&, const std::string&)> callback;
    std::vector<std::string> keys;
    plugin_ctx_t ctx;  // 插件上下文
};

// 可能的存储方式
class CfCallbackRegistry {
public:
    void registerCallback(const std::string& type, std::function<int(const std::string&, const std::string&)> cb,
                          const std::vector<std::string>& keys, plugin_ctx_t ctx) {
        callbacks.emplace_back(CfCallback{type, cb, keys, ctx});
    }

    const std::vector<CfCallback>& getCallbacks() const {
        return callbacks;
    }

private:
    std::vector<CfCallback> callbacks;
};


struct CfComplexCallback {
    std::string type;
    std::function<int(OConfigItem&)> callback;
    plugin_ctx_t ctx;
};

// 可能的存储方式
class CfComplexCallbackRegistry {
public:
    void registerComplexCallback(const std::string& type, std::function<int(OConfigItem&)> cb, plugin_ctx_t ctx) {
        callbacks.emplace_back(CfComplexCallback{type, cb, ctx});
    }

    const std::vector<CfComplexCallback>& getComplexCallbacks() const {
        return callbacks;
    }

private:
    std::vector<CfComplexCallback> callbacks;
};


using CfValueMap = std::unordered_map<std::string, std::function<int(OConfigItem&)>>;

class CfValueMapper {
public:
    void addMapping(const std::string& key, std::function<int(OConfigItem&)> func) {
        valueMap[key] = func;
    }

    bool execute(const std::string& key, OConfigItem& item) {
        if (valueMap.find(key) != valueMap.end()) {
            return valueMap[key](item) == 0;
        }
        return false;
    }

private:
    CfValueMap valueMap;
};

