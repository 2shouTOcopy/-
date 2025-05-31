#pragma once

#include "ModuleBase.h"

class CUptimeModule final : public CAbstractUserModule
{
public:
	CUptimeModule() = default;
	~CUptimeModule() override = default;

	int read();

private:
	void uptimeSubmit(gauge_t value);
	time_t uptimeGetSys(void);
};

#ifdef __cplusplus
extern "C"
{
#endif

	CAbstractUserModule* CreateModule();
	void DestroyModule(CAbstractUserModule *pUserModule);
	
#ifdef __cplusplus
};
#endif

