#pragma once
#include <string>
#include <unordered_map>

struct CfGlobalOption {
    std::string key;
    std::string value;
    bool fromCli = false; // 是否从 CLI 传入
    std::string def;
};

class CfGlobalConfig {
public:
    void setOption(const std::string& key, const std::string& value, bool fromCli = false) {
        options[key] = {key, value, fromCli, ""};
    }

    std::string getOption(const std::string& key) const {
        auto it = options.find(key);
        return it != options.end() ? it->second.value : "";
    }

    bool hasKey(const std::string& key) const {
        return options.find(key) != options.end();
    }

	int getNum() const {
		return static_cast<int>(options.size());
	}

    bool isSetFromCli(const std::string& key) const {
        return options.find(key) != options.end() && options.at(key).fromCli;
    }

private:
    std::unordered_map<std::string, CfGlobalOption> options;
};

