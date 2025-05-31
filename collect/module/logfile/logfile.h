#pragma once

#include <string>

#include "ModuleBase.h"

class CLogfileModule final : public CAbstractUserModule
{
public:
	CLogfileModule() = default;
	~CLogfileModule() override = default;

	int config(const std::string &key, const std::string &val);

	int read();

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

