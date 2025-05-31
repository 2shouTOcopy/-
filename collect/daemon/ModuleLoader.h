#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <map>
#include <vector>

#include "CommonDef.h"
class CAbstractUserModule;

class ModuleLoader {
public:
	static ModuleLoader& Instance()
	{
		static ModuleLoader s_instance;
		return s_instance;
	}

	void SetDir(const std::string &dir);

	int Load(const std::string &pluginName, bool global);

	int Unload(const std::string &pluginName);

	bool IsLoaded(const std::string &pluginName);

	CAbstractUserModule* GetUserModuleImpl(const std::string &pluginName);

	std::vector<std::string> GetLoadedPluginNames();

private:
	ModuleLoader() = default;
	~ModuleLoader() = default;

	ModuleLoader(const ModuleLoader&) = delete;
	ModuleLoader& operator=(const ModuleLoader&) = delete;

private:
	struct LibInfo
	{
		void				*handle;
		pfnCreateModule 	fnCreateOpt;
		pfnDestroyModule	fnDestroyOpt;
		CAbstractUserModule* pUserModuleImpl;
	};

	using OptLibMap = std::map<std::string, LibInfo>;

	int LoadPluginFile(const std::string &fullPath, bool global,
						const std::string &pluginName);

private:
	std::mutex m_mutex;
	std::string m_pluginDir;
	std::vector<std::string> m_loadPluginNames;
	OptLibMap 	m_openLibs;
};

