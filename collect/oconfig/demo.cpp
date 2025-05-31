#if 0

// demo.cpp
#include <iostream>
#include "oconfig.h"

// 声明解析函数
extern int oconfig_parse_string_cxx14(const char *buffer, OConfigItem *root);
extern int oconfig_parse_file_cxx14(const char *buffer, OConfigItem *root);

// 递归打印工具
void print_config(const OConfigItem &item, int indent = 0)
{
    std::string prefix(indent, ' ');
    std::cout << prefix << "Key: " << item.key << "\n";

    // 打印 values
    if (!item.values.empty()) {
        std::cout << prefix << "  Values:\n";
        for (auto &val : item.values) {
            if (val.type == OConfigType::STRING) {
                std::cout << prefix << "    [STRING] " << val.getString() << "\n";
            } else if (val.type == OConfigType::NUMBER) {
                std::cout << prefix << "    [NUMBER] " << val.getNumber() << "\n";
            } else if (val.type == OConfigType::BOOLEAN) {
                std::cout << prefix << "    [BOOLEAN] " << (val.getBoolean() ? "true":"false") << "\n";
            }
        }
    }

    // 递归子节点
    if (!item.children.empty()) {
        std::cout << prefix << "  Children:\n";
        for (auto &child : item.children) {
            print_config(*child, indent+2);
        }
    }
}


// 在此写上示例配置内容：与 “上面是配置文件的实例” 中相同
static const char *sample_cfg = R"CONF(
##############################################################################
# Global                                                                     #
#----------------------------------------------------------------------------#
# Global settings for the daemon.                                            #
##############################################################################

#Hostname    "localhost"
FQDNLookup   false
#BaseDir     "${prefix}/var/lib/collectd"
#PIDFile     "${prefix}/var/run/collectd.pid"
#PluginDir   "${exec_prefix}/lib/collectd"
#TypesDB     "/opt/collectd/share/collectd/types.db"

#----------------------------------------------------------------------------#
# When enabled, plugins are loaded automatically with the default options    #
# when an appropriate <Plugin ...> block is encountered.                     #
# Disabled by default.                                                       #
#----------------------------------------------------------------------------#
#AutoLoadPlugin false

MaxReadInterval 86400
Timeout         2
ReadThreads     5
WriteThreads    5


# Limit the size of the write queue. Default is no limit. Setting up a limit is
# recommended for servers handling a high volume of traffic.
#WriteQueueLimitHigh 1000000
#WriteQueueLimitLow   800000

##############################################################################
# Logging                                                                    #
#----------------------------------------------------------------------------#
# Plugins which provide logging functions should be loaded first, so log     #
# messages generated when loading or configuring other plugins can be        #
# accessed.                                                                  #
##############################################################################

#LoadPlugin syslog
LoadPlugin logfile
LoadPlugin log_logstash

<Plugin logfile>
	LogLevel debug
	File "/mnt/data/collect/log"
	Timestamp true
	PrintSeverity false
</Plugin>

<Plugin log_logstash>
	LogLevel debug
	File "${prefix}/var/log/collectd.json.log"
</Plugin>

#<Plugin syslog>
#	LogLevel debug
#</Plugin>
)CONF";

int main()
{
	OConfigItem root("root");
	int status = oconfig_parse_file_cxx14("./collect.conf", &root);
	//int status = oconfig_parse_string_cxx14(sample_cfg, &root);
	if (status != 0) {
		std::cerr << "[Error] parse failed\n";
		return 1;
	}
	std::cout << "==== Parsed ====\n";
	print_config(root, 0);
	return 0;
}

#else 
#if 0
#include <iostream>
#include "configfile.h"
#include "oconfig.h"

int main()
{
    // 1) 读取配置文件
    //    你可以把 /tmp/test.conf 换成你实际的配置路径
    const char cfg_file[] = "./collect.conf";
    int status = ConfigManager::Instance().Read(cfg_file);
    if (status != 0) {
        std::cerr << "Failed to parse: " << cfg_file << std::endl;
        //return 1;
    }

    // 2) 测试: 获取全局选项
    std::cout << "FQDNLookup = " << ConfigManager::Instance().GetGlobalOption("FQDNLookup") << std::endl;
    std::cout << "PluginDir = "  << ConfigManager::Instance().GetGlobalOption("PluginDir")  << std::endl;

    // 3) 测试: 动态分发单个选项(相当于从命令行传入)
    ConfigManager::Instance().DispatchOption("Hostname", "mytestserver");
    std::cout << "Hostname = " << ConfigManager::Instance().GetGlobalOption("Hostname") << std::endl;

    // 4) 也可设置
    ConfigManager::Instance().SetGlobalOption("Interval", "30");
    std::cout << "Interval = " << ConfigManager::Instance().GetGlobalOption("Interval") << std::endl;
    std::cout << "WriteThreads = " << ConfigManager::Instance().GetGlobalOption("WriteThreads") << std::endl;

    return 0;
}
#endif
#endif
