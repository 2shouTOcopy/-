#pragma once

#include <string>
#include <vector>

#include "ModuleBase.h"

struct ParsedMemInfo
{
	gauge_t mem_total = 0.0;
	gauge_t mem_used = 0.0;
	gauge_t mem_buffered = 0.0;
	gauge_t mem_cached = 0.0;
	gauge_t mem_free = 0.0;
	gauge_t mem_available = 0.0;
	gauge_t mem_slab_total = 0.0;
	gauge_t mem_slab_reclaimable = 0.0;
	gauge_t mem_slab_unreclaimable = 0.0;

	bool detailed_slab_info_present = false;
	bool mem_available_info_present = false;
};

class CMemoryModule final : public CAbstractUserModule
{
public:
	CMemoryModule();
	~CMemoryModule() override = default;

	int read()                         override;

	int flush()                         override;

	int config(const std::string& key,
 			   const std::string& val)    override;

private:
	bool parseMemInfo(ParsedMemInfo &data_out);

	bool parseLine(const std::string &line, const char *key_to_match, gauge_t &target_value_ref);

	void submitAvailableMetric(gauge_t mem_available_value);

	void submitMultiMetrics(value_list_t *vl_template, const ParsedMemInfo &d);

	bool m_bAbsolute;
	bool m_bPercentage;
	bool m_bCommInfo;
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

