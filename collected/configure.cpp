#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include <cctype>

// 简单的字符串修剪函数
static inline std::string trim(const std::string &s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) {
        ++start;
    }
    auto end = s.end();
    if (start == s.end()) return "";
    do {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

// 如果字符串两端有引号，则去除
static inline std::string removeQuotes(const std::string &s) {
    std::string result = s;
    if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
        result = result.substr(1, result.size() - 2);
    }
    return result;
}

// 用于存储单个插件的配置
struct PluginConfig {
    std::string name;
    std::map<std::string, std::string> settings;
};

// 全局配置，包含全局键值和各插件的配置块
struct GlobalConfig {
    std::map<std::string, std::string> settings;
    std::map<std::string, PluginConfig> plugins;
};

// 配置文件解析器类
class ConfigParser {
public:
    // 从文件中解析配置，成功返回 true，否则返回 false
    bool parseFile(const std::string &filename, GlobalConfig &config) {
        std::ifstream infile(filename);
        if (!infile.is_open()) {
            std::cerr << "Error: Cannot open file: " << filename << std::endl;
            return false;
        }
        return parseStream(infile, config);
    }
    
    // 从任意输入流中解析配置
    bool parseStream(std::istream &in, GlobalConfig &config) {
        std::string line;
        int lineNumber = 0;
        bool inPluginBlock = false;
        PluginConfig currentPlugin;
        
        while (std::getline(in, line)) {
            ++lineNumber;
            line = trim(line);
            // 跳过空行和注释行（以 # 开头）
            if (line.empty() || line[0] == '#') {
                continue;
            }
            // 如果不在插件块中且检测到以 "<Plugin" 开始的行，进入插件块
            if (!inPluginBlock && line.size() >= 8 && line.substr(0, 7) == "<Plugin") {
                size_t posEnd = line.find('>');
                if (posEnd == std::string::npos) {
                    std::cerr << "Error: Invalid plugin block start at line " << lineNumber << std::endl;
                    return false;
                }
                // 提取插件名（例如 <Plugin logfile> 中的 logfile）
                std::string pluginName = line.substr(7, posEnd - 7);
                pluginName = trim(pluginName);
                pluginName = removeQuotes(pluginName);
                if (pluginName.empty()) {
                    std::cerr << "Error: Plugin name empty at line " << lineNumber << std::endl;
                    return false;
                }
                currentPlugin.name = pluginName;
                currentPlugin.settings.clear();
                inPluginBlock = true;
                continue;
            }
            
            // 如果在插件块中，检测到结束标签 "</Plugin>"
            if (inPluginBlock && line == "</Plugin>") {
                config.plugins[currentPlugin.name] = currentPlugin;
                inPluginBlock = false;
                continue;
            }
            
            // 其它情况均为键值对
            std::string key, value;
            if (!parseKeyValue(line, key, value)) {
                std::cerr << "Warning: Failed to parse line " << lineNumber << ": " << line << std::endl;
                continue;
            }
            // 去除value两端可能存在的引号
            value = removeQuotes(value);
            if (inPluginBlock) {
                currentPlugin.settings[key] = value;
            } else {
                config.settings[key] = value;
            }
        }
        // 如果文件结束时还在插件块内，则视为错误
        if (inPluginBlock) {
            std::cerr << "Error: Plugin block not closed." << std::endl;
            return false;
        }
        return true;
    }
    
private:
    // 解析一行“Key Value”，注意 key 与 value 之间可能有多个空格
    bool parseKeyValue(const std::string &line, std::string &key, std::string &value) {
        std::istringstream iss(line);
        if (!(iss >> key)) {
            return false;
        }
        std::getline(iss, value);
        value = trim(value);
        return true;
    }
};

// 打印解析结果
void printConfig(const GlobalConfig &config) {
    std::cout << "Global Settings:" << std::endl;
    for (auto &kv : config.settings) {
        std::cout << "  " << kv.first << " = " << kv.second << std::endl;
    }
    std::cout << std::endl;
    std::cout << "Plugin Configurations:" << std::endl;
    for (auto &pluginPair : config.plugins) {
        const PluginConfig &pc = pluginPair.second;
        std::cout << "Plugin: " << pc.name << std::endl;
        for (auto &kv : pc.settings) {
            std::cout << "  " << kv.first << " = " << kv.second << std::endl;
        }
        std::cout << std::endl;
    }
}

// 测试 Demo
// 如果命令行参数中给出文件路径，则从该文件解析；否则使用内嵌示例配置
int main(int argc, char* argv[]) {
    GlobalConfig config;
    ConfigParser parser;
    
    if (argc > 1) {
        std::string filename(argv[1]);
        if (!parser.parseFile(filename, config)) {
            std::cerr << "Failed to parse configuration file." << std::endl;
            return 1;
        }
    } else {
        // 内嵌一个 collectd 配置文件示例
        std::string sampleConfig = R"(#
# Config file for collectd(1).
# Please read collectd.conf(5) for a list of options.
# http://collectd.org/
#

##############################################################################
# Global                                                                     #
#----------------------------------------------------------------------------#
# Global settings for the daemon.
##############################################################################

#Hostname    "localhost"
FQDNLookup   false
#BaseDir     "${prefix}/var/lib/collectd"
#PIDFile     "${prefix}/var/run/collectd.pid"
#PluginDir   "${exec_prefix}/lib/collectd"
#TypesDB     "/opt/collectd/share/collectd/types.db"

#----------------------------------------------------------------------------#
# When enabled, plugins are loaded automatically with the default options
# when an appropriate <Plugin ...> block is encountered.
# Disabled by default.
#----------------------------------------------------------------------------#
#AutoLoadPlugin false

#----------------------------------------------------------------------------#
# When enabled, internal statistics are collected, using "collectd" as the
# plugin name.
# Disabled by default.
#----------------------------------------------------------------------------#
#CollectInternalStats false

#----------------------------------------------------------------------------#
# Interval at which to query values. This may be overwritten on a per-plugin
# base by using the 'Interval' option of the LoadPlugin block:
#   <LoadPlugin foo>
#       Interval 60
#   </LoadPlugin>
#----------------------------------------------------------------------------#
#Interval     10

#MaxReadInterval 86400
#Timeout         2
#ReadThreads     5
#WriteThreads    5

# Write queue limit settings
#WriteQueueLimitHigh 1000000
#WriteQueueLimitLow   800000

##############################################################################
# Logging
##############################################################################

#LoadPlugin syslog
LoadPlugin logfile
LoadPlugin cpu
#LoadPlugin cpufreq
#LoadPlugin cpusleep
LoadPlugin csv

<Plugin logfile>
    LogLevel debug
    File "/mnt/data/collect/log"
    Timestamp true
    PrintSeverity false
</Plugin>

<Plugin cpu>
  ReportByCpu true
  ReportByState true
  ValuesPercentage false
  ReportNumCpu false
  ReportGuestState false
  SubtractGuestState false
</Plugin>

<Plugin csv>
    DataDir "/mnt/data/collect/csv"
    StoreRates false
</Plugin>

#<Plugin syslog>
#    LogLevel debug
#</Plugin>
)";
        std::istringstream iss(sampleConfig);
        if (!parser.parseStream(iss, config)) {
            std::cerr << "Failed to parse sample configuration." << std::endl;
            return 1;
        }
    }
    
    printConfig(config);
    return 0;
}