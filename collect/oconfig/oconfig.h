#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdio>

#include "Any.h"

enum class OConfigType {
    STRING,
    NUMBER,
    BOOLEAN
};

class OConfigValue {
public:
    Any value;
    OConfigType type;

    OConfigValue(std::string v) : value(std::move(v)), type(OConfigType::STRING) {}
    OConfigValue(double v) : value(v), type(OConfigType::NUMBER) {}
    OConfigValue(bool v) : value(v), type(OConfigType::BOOLEAN) {}

    // 获取值时提供类型安全的访问
    std::string getString() const {
        return value.AnyCast<std::string>();
    }

    double getNumber() const {
        return value.AnyCast<double>();
    }

    bool getBoolean() const {
        return value.AnyCast<bool>();
    }
};

class OConfigItem {
public:
    std::string key;
    std::vector<OConfigValue> values;
    std::vector<std::unique_ptr<OConfigItem>> children;
    OConfigItem* parent = nullptr;

    OConfigItem(std::string k) : key(std::move(k)) {}

    OConfigItem(const OConfigItem &other)
        : key(other.key), values(other.values), parent(nullptr)
    {
        // 手动逐个克隆 children
        children.reserve(other.children.size());
        for (auto &childPtr : other.children) {
            if (childPtr) {
                // 递归拷贝子节点
                auto cloned = std::make_unique<OConfigItem>(*childPtr);
                cloned->parent = this;
                children.push_back(std::move(cloned));
            }
        }
    }

    void addValue(const OConfigValue& val) {
        values.push_back(val);
    }

    OConfigItem* addChild(const std::string& childKey) {
        children.emplace_back(std::make_unique<OConfigItem>(childKey));
        children.back()->parent = this;
        return children.back().get();
    }
};

class ConfigParser {
public:
    // 创建解析器实例
    static std::unique_ptr<ConfigParser> create();

    // 核心解析接口
    std::unique_ptr<OConfigItem> parseFile(const char* filename);
    std::unique_ptr<OConfigItem> parseString(const char* buffer);

private:
    ConfigParser() = default; // 禁止直接实例化

    // 内部实现方法
    int parseStream(FILE* fh, OConfigItem& root);
    int parseBlock(FILE* fh, OConfigItem& parent, int& lineNum);

    // 工具方法
    static std::string trim(const std::string& s);
    static std::string stripQuotes(const std::string& s);
    static std::vector<std::string> tokenizeLine(const std::string& line);
};

