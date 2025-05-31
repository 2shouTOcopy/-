#include <iostream>
#include <assert.h>
#include <memory>

#include "logfile.h"
#include "../daemon/PluginService.h"
#include "../oconfig/configfile.h"

int CLogfileModule::config(const std::string &key, const std::string &val)
{
	return 0;
}

int CLogfileModule::flush()
{
	const std::string strDir = ConfigManager::Instance().GetGlobalOption("BaseDir");
	if (strDir.empty())
	{
		ERROR("dmesg plugin: BaseDir is not configured.");
		return -1;
	}

	const std::string command = "/mnt/app/toolbox log_record export " + strDir;
	std::unique_ptr<FILE, decltype(&pclose)> pipe(
		popen(command.c_str(), "r"), pclose);
	if (!pipe)
	{
		ERROR("logfile plugin: Failed to execute command '%s'", command.c_str());
	}
	else
	{
		if (ferror(pipe.get()))
		{
			ERROR("logfile plugin: execute command '%s' output failed!", command.c_str());
		}
	}

	return 0;
}

int CLogfileModule::read()
{
	return 0;
}

CAbstractUserModule *CreateModule()
{
	return new CLogfileModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	assert(pUserModule != NULL);
	
	delete pUserModule;
	pUserModule = NULL;
}

