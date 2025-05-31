#pragma once
#include <string>

#include "ModuleBase.h"
#include "IgnoreList.h"

class CDfModule final : public CAbstractUserModule
{
public:
    CDfModule() = default;
    ~CDfModule() override = default;

    int init() override;
    int config(const std::string &key, const std::string &val) override;
    int read() override;
    int shutdown() override;

private:
    void submitValue(const std::string &pluginInstance,
                     const std::string &type,
                     const std::string &typeInstance,
                     gauge_t value);

    IgnoreList m_ilDevice{true};
    IgnoreList m_ilMountPoint{true};
    IgnoreList m_ilFsType{true};
    IgnoreList m_ilErrors{true};

    bool m_bDevice{false};
    bool m_bReportInodes{false};
    bool m_bAbsolute{true};
    bool m_bPercentage{false};
    bool m_bLogOnce{false};
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


