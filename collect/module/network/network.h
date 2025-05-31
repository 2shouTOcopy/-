#pragma once

#include "ModuleBase.h"

class CNetworkModule final : public CAbstractUserModule
{
public:
	CNetworkModule() = default;
	~CNetworkModule() override = default;

	int config(const std::string &key, const std::string &val);

	int flush();

private:
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

