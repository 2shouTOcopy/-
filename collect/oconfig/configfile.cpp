#include <iostream>
#include <cstring>
#include <functional>

#include "configfile.h"

#include "../daemon/ModuleLoader.h"
#include "../daemon/ModuleBase.h"

ConfigManager::ConfigManager()
{
	InitGlobalOptions();
	InitValueMapper();
}

ConfigManager::~ConfigManager()
{
	TypesDbParser::free_datasets(type_datasets_);
}

void ConfigManager::InitGlobalOptions() {
	global_config_.setOption("BaseDir", "/var/lib/collectd");
	global_config_.setOption("PIDFile", "/var/run/collectd.pid");
	global_config_.setOption("TypesDB", "/mnt/data/collect/share/types.db");
	global_config_.setOption("FQDNLookup", "true");
	global_config_.setOption("Interval", "10");
	global_config_.setOption("ReadThreads", "5");
	global_config_.setOption("WriteThreads", "5");
	global_config_.setOption("Timeout", "2");
	global_config_.setOption("AutoLoadPlugin", "false");
	global_config_.setOption("MaxReadInterval", "86400");
}

void ConfigManager::InitValueMapper()
{
	using namespace std::placeholders;
	value_mapper_.addMapping("PluginDir", 
		std::bind(&ConfigManager::DispatchValuePluginDir, this, _1));
	value_mapper_.addMapping("LoadPlugin", 
		std::bind(&ConfigManager::DispatchLoadPlugin, this, _1));
	value_mapper_.addMapping("Plugin", 
		std::bind(&ConfigManager::DispatchBlockPlugin, this, _1));
}

int ConfigManager::DispatchValuePluginDir(OConfigItem& ci)
{
	if (!ci.values.empty())
	{
		std::string dir = ci.values[0].getString();
		std::cout << "[dispatch_value_plugindir] plugin dir => " << dir << std::endl;
		ModuleLoader::Instance().SetDir(dir);
	}
	return 0;
}

int ConfigManager::DispatchLoadPlugin(OConfigItem& ci)
{
	if (ci.values.empty() || ci.values[0].type != OConfigType::STRING)
	{
		std::cerr << "LoadPlugin needs exactly one string argument\n";
		return -1;
	}

	std::string pluginName = ci.values[0].getString();
	std::cout << "[dispatch_loadplugin] load plugin => " << pluginName << std::endl;

	int ret = ModuleLoader::Instance().Load(pluginName, false);
	if (ret != 0)
	{
		std::cerr << "Load plugin failed: " << pluginName << ", ret=" << ret << std::endl;
	}

	bool loaded = ModuleLoader::Instance().IsLoaded(pluginName);
	std::cout << pluginName << " loaded? " << (loaded ? "YES" : "NO") << std::endl;
	return ret;
}

int ConfigManager::DispatchBlockPlugin(OConfigItem& ci)
{
	if (ci.values.empty())
	{
		std::cerr << "[dispatch_block_plugin] Error: Plugin block <" << ci.key << "> is missing plugin name." << std::endl;
		return -1;
	}
	const std::string& plugin_name = ci.values[0].getString();

	std::cout << "[dispatch_block_plugin] Configuring plugin block for: " << plugin_name << std::endl;

	CAbstractUserModule* pluginHandle = ModuleLoader::Instance().GetUserModuleImpl(plugin_name);
	if (!pluginHandle)
	{
		std::cerr << "[dispatch_block_plugin] Error: Plugin '" << plugin_name << "' not loaded or module not found. Cannot configure." << std::endl;
		return -1;
	}

	int ret = 0;
	for (auto& child_config_item : ci.children)
	{
		if (!child_config_item)
		{
			std::cerr << "[dispatch_block_plugin] Warning: Encountered null child config item for plugin '" << plugin_name << "'. Skipping." << std::endl;
			continue;
		}

		const std::string& config_key = child_config_item->key;
		std::string config_value;

		if (child_config_item->values.empty())
		{
			std::cerr << "[dispatch_block_plugin] Warning: Config key '" << config_key
			          << "' for plugin '" << plugin_name << "' has no value. Skipping." << std::endl;
			continue;
		}
		else
		{
			config_value = child_config_item->values[0].getString();
		}

		std::cout << "   Dispatching to plugin '" << plugin_name 
 		          << "': Key='" << config_key << "', Value='" << config_value << "'"
 		          << std::endl;

		if (pluginHandle->config(config_key, config_value) != 0)
		{
			std::cerr << "[dispatch_block_plugin] Error: Plugin '" << plugin_name
			          << "' failed to configure option: " << config_key 
			          << " = " << config_value << std::endl;
			ret = -1;
		}
	}
	return ret;
}

int ConfigManager::FcConfigure(OConfigItem& ci)
{
	std::cout << "[fc_configure] key=" << ci.key << "\n";
	return 0;
}

int ConfigManager::DispatchGlobalOption(OConfigItem& ci)
{
	if (ci.values.size() != 1)
	{
	    std::cerr << "Global option needs exactly one argument\n";
	    return -1;
	}

	const auto& val = ci.values[0];
	std::string value;

	switch (val.type)
	{
		case OConfigType::STRING:
			value = val.getString();
			break;
		case OConfigType::NUMBER:
			value = std::to_string(val.getNumber());
			break;
		case OConfigType::BOOLEAN:
			value = val.getBoolean() ? "true" : "false";
			break;
		default:
			std::cerr << "Unknown value type for option: " << ci.key << "\n";
			return -1;
	}

	SetGlobalOption(ci.key, value);
	return 0;
}

int ConfigManager::DispatchBlock(OConfigItem& ci)
{
	const std::string& key = ci.key;

	if (key == "LoadPlugin") return DispatchLoadPlugin(ci);
	if (key == "Plugin") return DispatchBlockPlugin(ci);
	if (key == "Chain") return FcConfigure(ci);

	return 0;
}

int ConfigManager::DispatchValue(OConfigItem& ci)
{
	if (value_mapper_.execute(ci.key, ci))
	{
		return 0;
	}

	if (global_config_.hasKey(ci.key))
	{
		return DispatchGlobalOption(ci);
	}

	std::cerr << "Unhandled configuration option: " << ci.key << "\n";
	return -1;
}

int ConfigManager::Read(const char *filename)
{
	if (!filename)
	{
		std::cerr << "ConfigManager::Read: Invalid filename (nullptr)\n";
		return -1;
	}

	auto oconfig_parser = ConfigParser::create(); 
	auto root = oconfig_parser->parseFile(filename);
	if (root == nullptr)
	{
		std::cerr << "ConfigManager::Read: Main config parse failed: " << filename << "\n";
		return -1;
	}

	if (root->children.empty())
	{
		std::cerr << "ConfigManager::Read: Empty main config file: " << filename << "\n";
		return -1;
	}

	int main_config_ret = 0;
	for (auto &child : root->children)
	{
		if (child->children.empty())
		{
			if (DispatchValue(*child) != 0) 
				main_config_ret = -1;
		}
		else
		{
			if (DispatchBlock(*child) != 0)
				main_config_ret = -1;
		}
	}

	if (main_config_ret != 0)
	{
		std::cerr << "ConfigManager::Read: Errors occurred while processing main configuration." << std::endl;
	}

	std::string types_db_path = global_config_.getOption("TypesDB");
	if (types_db_path.empty())
	{
		std::cerr << "ConfigManager::Read: TypesDB path not configured. Skipping types.db parsing." << std::endl;
	}
	else
	{
	    std::cout << "ConfigManager::Read: Attempting to parse TypesDB from: " << types_db_path << std::endl;
	    if (TypesDbParser::parse_file(types_db_path.c_str(), type_datasets_) != 0)
		{
	        std::cerr << "ConfigManager::Read: Failed to parse TypesDB file: " << types_db_path << std::endl;
	    }
		else
		{
	        std::cout << "ConfigManager::Read: Successfully parsed " << type_datasets_.size() << " data sets from " << types_db_path << std::endl;
	    }
	}

	return main_config_ret;
}

int ConfigManager::DispatchOption(const std::string& key, const std::string& value)
{
	OConfigItem ci(key);
	ci.addValue(OConfigValue(value));

	if (!value_mapper_.execute(key, ci))
	{
		SetGlobalOption(key, value);
	}
	return 0;
}

int ConfigManager::Search(const std::string& key)
{
	OConfigItem dummy("dummy");
	if (value_mapper_.execute(key, dummy)) 
	{
		return 0;
	}
	return global_config_.hasKey(key) ? 0 : -1;
}

int ConfigManager::Register(const std::string& type,
                           std::function<int(const std::string&, const std::string&)> cb,
                           const std::vector<std::string>& keys,
                           plugin_ctx_t ctx)
{
	callback_registry_.registerCallback(type, cb, keys, ctx);
	return 0;
}

int ConfigManager::Unregister(const std::string& type)
{
	// Implementation depends on CfCallbackRegistry
	return 0;
}

int ConfigManager::RegisterComplex(const std::string& type,
                                  std::function<int(OConfigItem&)> cb,
                                  plugin_ctx_t ctx)
{
	complex_registry_.registerComplexCallback(type, cb, ctx);
	return 0;
}

int ConfigManager::UnregisterComplex(const std::string& type)
{
	// Implementation depends on CfComplexCallbackRegistry
	return 0;
}

void ConfigManager::SetGlobalOption(const std::string& key, const std::string& value)
{
	global_config_.setOption(key, value);
}

std::string ConfigManager::GetGlobalOption(const std::string& key)
{
	return global_config_.getOption(key);
}

double ConfigManager::GetGlobalOptionTime(const std::string& key, double def)
{
	try
	{
		return std::stod(GetGlobalOption(key));
	}
	catch (...)
	{
		return def;
	}
}

double ConfigManager::GetDefaultInterval()
{
	return GetGlobalOptionTime("Interval", 10.0);
}

const std::vector<data_set_t>& ConfigManager::GetTypeDataSets() const
{
	return type_datasets_;
}

const data_set_t* ConfigManager::GetDataSetByName(const std::string& type_name) const
{
	for (const auto& ds : type_datasets_)
	{
		if (type_name == ds.type)
		{
			return &ds;
		}
	}
	return nullptr;
}

