#include <fstream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <assert.h>

#include "cpu.h"
#include "../daemon/PluginService.h"
#include "../daemon/utils/utils.h"

/* --------- 工具宏 --------- */
#define RATE_ADD(sum,val)  do{ if(std::isnan(sum)) (sum)=(val); \
                               else if(!std::isnan(val)) (sum)+=(val);}while(0)

const std::array<const char*, CCpuModule::MAX_STATE> CCpuModule::kStateName = {{
        "user", "system", "wait", "nice", "swap", "interrupt",
        "softirq", "steal", "guest", "guest_nice", "idle", "active"
    }};

int CCpuModule::config(const std::string& key, const std::string& val)
{
    if      (key == "ReportByCpu")        m_reportByCpu  = IS_TRUE(val.c_str());
    else if (key == "ReportByState")      m_reportByState= IS_TRUE(val.c_str());
    else if (key == "ValuesPercentage")   m_reportPercent= IS_TRUE(val.c_str());
    else if (key == "ReportNumCpu")       m_reportNumCpu = IS_TRUE(val.c_str());
    else if (key == "ReportGuestState")   m_reportGuest  = IS_TRUE(val.c_str());
    else if (key == "SubtractGuestState") m_subGuest     = IS_TRUE(val.c_str());
    else return -1;

	ERROR(" >>>>>>>>>>>>> [%s -> %s]", key.c_str(), val.c_str());
    return 0;
}

int CCpuModule::init()
{
    /* nothing to initialise on Linux */
    return 0;
}

int CCpuModule::read()
{
    const double now = cdtime();
    std::ifstream fin("/proc/stat");
    if (!fin.is_open()) {
        ERROR("cpu: open /proc/stat fail");
        return -1;
    }

    std::string line;
    while (std::getline(fin,line))
    {
        if (line.compare(0,3,"cpu")!=0 || !isdigit(line[3])) continue;

        std::istringstream iss(line);
        std::string label;  iss>>label;               /* cpuN */
        const size_t cpuIdx = static_cast<size_t>(std::stoi(label.substr(3)));
        ERROR("cpu: cpuIdx:%d", cpuIdx);

        if (cpuIdx >= m_cpus.size()) m_cpus.resize(cpuIdx+1);
        uint64_t v[11]={0};
        for(int i=0;i<11 && iss>>v[i];++i){}

        uint64_t user=v[0], nice=v[1];
        /* 2=system,3=idle,... 按 /proc/stat 顺序 */
        stage(cpuIdx, SYSTEM , v[2], now);
        stage(cpuIdx, IDLE   , v[3], now);
        if(iss.good()){ stage(cpuIdx, WAIT    , v[4], now); }
        if(iss.good()){ stage(cpuIdx, INTERRUPT, v[5], now); }
        if(iss.good()){ stage(cpuIdx, SOFTIRQ , v[6], now); }
        if(iss.good()){ stage(cpuIdx, STEAL   , v[7], now); }

        if (iss.good()) {
            if (m_reportGuest) {
                uint64_t guest=v[8]; stage(cpuIdx,GUEST,guest,now);
                if(m_subGuest && user>=guest) user-=guest;
            }
        }
        if (iss.good()) {
            if (m_reportGuest) {
                uint64_t guestNice=v[9]; stage(cpuIdx,GUEST_NICE,guestNice,now);
                if(m_subGuest && nice>=guestNice) nice-=guestNice;
            }
        }
        stage(cpuIdx, USER , user, now);
        stage(cpuIdx, NICE , nice, now);

        /* mark cpu count */
        if (m_cpuSeen <= cpuIdx) m_cpuSeen = cpuIdx+1;
    }

    /* commit & reset */
	m_reportNumCpu = true;
    if (m_reportNumCpu) submitNumCpu(static_cast<double>(m_cpuSeen));
    if (m_reportByState && m_reportByCpu && !m_reportPercent)
        commitDeriveRaw();
    else
        commitPercentages();
    resetIteration();
    return 0;
}

int CCpuModule::stage(size_t cpu, CpuState st, uint64_t raw, double now)
{
    if (st>=ACTIVE) return EINVAL;
    if (cpu>=m_cpus.size()) m_cpus.resize(cpu+1);

    RateState& rs = m_cpus[cpu].st[st];

    if (rs.lastRaw==0) {          /* 首次读到 */
        rs.lastRaw  = raw;
        rs.hasValue = false;
        return 0;
    }
    if (raw < rs.lastRaw) {       /* 计数回绕 */
        rs.lastRaw  = raw;
        rs.hasValue = false;
        return 0;
    }

    const uint64_t diff = raw - rs.lastRaw;
    rs.lastRaw = raw;

    /* 采样间隔取自模块 interval；注意单位同 collectd (cdtime_t) */
    const double interval = CDTIME_T_TO_DOUBLE(now);
    rs.rate = diff / interval;
    rs.hasValue = true;
    return 0;
}

void CCpuModule::commitDeriveRaw()
{
	ERROR("cpu: commitDeriveRaw:%d", m_cpuSeen);
    for(int st=0; st<ACTIVE; ++st){
        for(size_t c=0;c<m_cpuSeen;++c){
            const RateState& rs = m_cpus[c].st[st];
            if(rs.hasValue) submitDerive(static_cast<int>(c),
                                          static_cast<CpuState>(st),
                                          rs.lastRaw);
        }
    }
}

void CCpuModule::aggregate()
{
    /* 计算各 CPU ACTIVE 率和全局总和 */
    for(size_t c=0;c<m_cpuSeen;++c){
        double activeSum=NAN;
        for(int st=0; st<ACTIVE; ++st){
            const RateState& rs=m_cpus[c].st[st];
            if(!rs.hasValue) continue;
            if(st!=IDLE) RATE_ADD(activeSum, rs.rate);
        }
        m_cpus[c].st[ACTIVE].rate = activeSum;
        m_cpus[c].st[ACTIVE].hasValue = !std::isnan(activeSum);
    }
}

void CCpuModule::commitPercentages()
{
    aggregate();

    /* --- 全局聚合 --- */
    std::array<double, MAX_STATE> global{};
    global.fill(NAN);

    for(size_t c=0;c<m_cpuSeen;++c){
        for(int st=0; st<MAX_STATE; ++st){
            const RateState& rs=m_cpus[c].st[st];
            if(rs.hasValue) RATE_ADD(global[st], rs.rate);
        }
    }

    if(!m_reportByCpu){
        /* 全系统百分比 */
        const double sum = global[ACTIVE]+global[IDLE];
        if (!m_reportByState){
            submitPercent(-1, ACTIVE, 100.0*global[ACTIVE]/sum);
        }else{
            for(int st=0; st<ACTIVE; ++st)
                submitPercent(-1, static_cast<CpuState>(st), 100.0*global[st]/sum);
        }
        return;
    }

    /* --- 每个 CPU --- */
    for(size_t c=0;c<m_cpuSeen;++c){
        const auto& cpu = m_cpus[c];
        const double sum = cpu.st[ACTIVE].rate + cpu.st[IDLE].rate;

        if(!m_reportByState){
            submitPercent((int)c, ACTIVE, 100.0*cpu.st[ACTIVE].rate/sum);
            continue;
        }
        for(int st=0; st<ACTIVE;++st){
            const double pct = 100.0 * cpu.st[st].rate / sum;
            submitPercent((int)c, static_cast<CpuState>(st), pct);
        }
    }
}

void CCpuModule::resetIteration()
{
    for(size_t c=0;c<m_cpuSeen;++c){
        for(auto& rs:m_cpus[c].st) rs.hasValue=false;
    }
    m_cpuSeen = 0;
}

void CCpuModule::submitValue(int cpu, CpuState st, const char *type, value_t val)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values      = &val;
	vl.values_len  = 1;

	snprintf(vl.plugin, sizeof(vl.plugin), "cpu", sizeof(vl.plugin));
	snprintf(vl.type, sizeof(vl.type), type, sizeof(vl.type));
	snprintf(vl.type_instance, sizeof(vl.type_instance), kStateName[st], sizeof(vl.type_instance));

	if (cpu >= 0)
	{
		snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%d", cpu);
	}

	INFO("[%s] type:%s, type_instance:%s, plugin_instance:%s,", 
	vl.plugin, vl.type, vl.type_instance, vl.plugin_instance);

	PluginService::Instance().dispatchValues(&vl);
}

void CCpuModule::submitDerive(int cpu, CpuState st, uint64_t raw)
{
    value_t v{.derive = static_cast<derive_t>(raw)};
    submitValue(cpu, st, "cpu", v);
}

void CCpuModule::submitPercent(int cpu, CpuState st, double pct)
{
    if (std::isnan(pct)) return;
    value_t v{.gauge = pct};
    submitValue(cpu, st, "percent", v);
}

void CCpuModule::submitNumCpu(double n)
{
    value_t v{.gauge = n};
    submitValue(-1, ACTIVE, "count", v);
}

CAbstractUserModule *CreateModule()
{
	return new CCpuModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	assert(pUserModule != NULL);
	
	delete pUserModule;
	pUserModule = NULL;
}

