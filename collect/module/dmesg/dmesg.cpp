#include <cstdio>
#include <fstream>
#include <string>
#include <cassert>

#include "dmesg.h"
#include "../daemon/PluginService.h"
#include "../oconfig/configfile.h"

static constexpr char const *OUTPUT_FILENAME = "dmesg.txt";

int CDmesgModule::config(const std::string &key, const std::string &val)
{
	return 0;
}

int CDmesgModule::flush()
{
	const std::string strDir = ConfigManager::Instance().GetGlobalOption("BaseDir");
	if (strDir.empty())
	{
		ERROR("dmesg plugin: BaseDir is not configured.");
		return -1;
	}
	const std::string outFile = strDir + "/" + OUTPUT_FILENAME;

	FILE *pipe = popen("dmesg", "r");
	if (!pipe)
	{
		ERROR("dmesg popen call failed!");
		return -1;
	}

	std::ofstream ofs(outFile, std::ios::out | std::ios::trunc);
	if (!ofs.is_open())
	{
		ERROR("can't open file:%s", outFile.c_str());
		pclose(pipe);
		return -1;
	}

	char buffer[4096];
	while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
	{
		ofs << buffer;
	}

	pclose(pipe);
	ofs.close();

	return 0;
}

CAbstractUserModule *CreateModule()
{
	return new CDmesgModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	assert(pUserModule != nullptr);
	delete pUserModule;
}
