#include <iostream>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <algorithm>

#include "ModuleLoader.h"
#include "ModuleBase.h"

void ModuleLoader::SetDir(const std::string &dir)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_pluginDir = dir;
}

int ModuleLoader::Load(const std::string &pluginName, bool global)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_openLibs.find(pluginName) != m_openLibs.end())
		{
			return 0;
		}
	}

	const std::string soName = pluginName + ".so";

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_pluginDir.empty())
		{
			m_pluginDir = DEFAULT_LOAD_SO_PATH;
		}
	}

	std::string fullPath = m_pluginDir + "/" + pluginName + "/" + soName;

	struct stat st;
	if (::stat(fullPath.c_str(), &st) != 0)
	{
		std::cerr << "[ModuleLoader] " << fullPath << " not found\n";
		return -1;
	}

	int status = LoadPluginFile(fullPath, global, pluginName);
	return status;
}

int ModuleLoader::LoadPluginFile(const std::string &fullPath, bool global,
                                 const std::string &pluginName)
{
	int flags = RTLD_NOW;
	if (global)
	{
		flags |= RTLD_GLOBAL;
	}

	auto iter = m_openLibs.find(pluginName);
	if (iter == m_openLibs.end())
	{
		void *handle = dlopen(fullPath.c_str(), flags);
		if (!handle)
		{
			std::cerr << "[ModuleLoader] dlopen(\"" << fullPath << "\") failed: "
					<< dlerror() << std::endl;
			return -1;
		}
		
		LibInfo info = {nullptr, };
		info.handle = handle;
		info.fnCreateOpt = (pfnCreateModule)(dlsym(handle, "CreateModule"));
		info.fnDestroyOpt = (pfnDestroyModule)(dlsym(handle, "DestroyModule"));
		if (info.fnCreateOpt == nullptr || info.fnDestroyOpt == nullptr)
		{
			std::cerr << "[ModuleLoader] Can't find symbol in "
					<< fullPath << ": " << dlerror() << std::endl;
			dlclose(handle);
			return -2;
		}

		info.pUserModuleImpl = info.fnCreateOpt();
		if (info.pUserModuleImpl == NULL)
		{
			std::cerr << "User implemented create module function return null\n";
			return -3;
		}
		
		m_openLibs.insert(OptLibMap::value_type(pluginName, info));
		m_loadPluginNames.push_back(pluginName);
	}
	else
	{
		std::cerr << "[ModuleLoader] plugin \"" << pluginName
					<< "\" is already loaded.\n";
		return 0;
	}

	std::cerr << "[ModuleLoader] plugin \"" << pluginName
				<< "\" loaded successfully.\n";
	return 0;
}

bool ModuleLoader::IsLoaded(const std::string &pluginName)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return (m_openLibs.find(pluginName) != m_openLibs.end());
}

int ModuleLoader::Unload(const std::string &pluginName)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto it = m_openLibs.find(pluginName);
	if (it == m_openLibs.end()) return -1;

	if (it->second.pUserModuleImpl && it->second.fnDestroyOpt)
	{
		it->second.fnDestroyOpt(it->second.pUserModuleImpl);
		const_cast<LibInfo&>(it->second).pUserModuleImpl = nullptr;
	}

	void *dl = it->second.handle;

	m_openLibs.erase(it);
	auto itPlugin = std::find(m_loadPluginNames.begin(), 
								m_loadPluginNames.end(), 
								pluginName);
	if (itPlugin != m_loadPluginNames.end())
	{
		m_loadPluginNames.erase(itPlugin);
	}

	if (dl) 
	{
		if (dlclose(dl) != 0)
		{
			std::cerr << "dlclose failed: " << dlerror() << std::endl;
			return -1;
		}
	}

	std::cerr << "[ModuleLoader] plugin \"" << pluginName
	          << "\" unloaded.\n";
	return 0;
}


CAbstractUserModule* ModuleLoader::GetUserModuleImpl(const std::string &pluginName)
{
	OptLibMap::const_iterator iter = m_openLibs.find(pluginName);
	if (iter != m_openLibs.end())
	{
		const LibInfo& user = iter->second;
		return user.pUserModuleImpl;
	}
	return nullptr;
}

std::vector<std::string> ModuleLoader::GetLoadedPluginNames()
{
	return m_loadPluginNames;
}
