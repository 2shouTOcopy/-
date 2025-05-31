#include <iostream>
#include <cstring>
#include <assert.h>
#include <sys/sysinfo.h>

#include "uptime.h"
#include "../daemon/PluginService.h"

void CUptimeModule::uptimeSubmit(gauge_t value)
{
	value_list_t vl = VALUE_LIST_INIT;
	value_t temp = {.gauge = value};

	vl.values = &temp;
	vl.values_len = 1;

	snprintf(vl.plugin, sizeof(vl.plugin), "uptime", sizeof(vl.plugin));
	snprintf(vl.type, sizeof(vl.type), "uptime", sizeof(vl.type));

	INFO("[uptime] uptime Submit :%lf", value);
	PluginService::Instance().dispatchValues(&vl);
}

time_t CUptimeModule::uptimeGetSys(void)
{
	struct sysinfo info {};
	if (sysinfo(&info) != 0)
	{
		ERROR("uptime plugin: Error calling sysinfo: %s", strerror(errno));
		return -1;
	}
	return static_cast<time_t>(info.uptime);
}

int CUptimeModule::read()
{
	gauge_t uptime;
	time_t elapsed;
	INFO("[uptime] read");

	/* calculate the amount of time elapsed since boot, AKA uptime */
	elapsed = uptimeGetSys();
	if (elapsed < 0)
	{
		return -1;
	}

	uptime = (gauge_t)elapsed;
	uptimeSubmit(uptime);

	return 0;
}

CAbstractUserModule *CreateModule()
{
	return new CUptimeModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	assert(pUserModule != NULL);
	
	delete pUserModule;
	pUserModule = NULL;
}

