#pragma once

#include <mutex>
#include <string>

#include "ModuleBase.h"

class CCsvModule final : public CAbstractUserModule
{
public:
    CCsvModule() = default;
    ~CCsvModule() override = default;

    int config(const std::string &key,
               const std::string &val) override;
    int write(const data_set_t *ds,
              const value_list_t *vl) override;

private:
    int vlToString(std::string &out,
                   const data_set_t *ds,
                   const value_list_t *vl) const;
    int vlToPath(std::string &path,
                 const value_list_t *vl) const;
    bool touchCsv(const std::string &file,
                  const data_set_t *ds) const;

    /* 配置项 */
    std::string _dataDir = "/mnt/data/collect/csv"; ///< 空表示使用默认路径
    bool _useStdout = false;
    bool _useStderr = false;
    bool _storeRates = false;
    bool _withDate = true;

    mutable std::mutex _ioMtx; ///< 与 flock 配合，防止多线程冲突
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

