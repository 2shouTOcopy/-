#pragma once
#include "ModuleBase.h"           // 你的 CAbstractUserModule 基类
#include <array>
#include <vector>
#include <cmath>

class CCpuModule final : public CAbstractUserModule
{
public:
    CCpuModule() = default;
    ~CCpuModule() override = default;

    /* -------- 基类接口 -------- */
    int  config (const std::string& key,
                 const std::string& val)    override;
    int  init   ()                           override;
    int  read   ()                           override;

private:
    /* -------- 与 collectd 同名常量 -------- */
    enum CpuState : int {
        USER = 0, SYSTEM, WAIT, NICE, SWAP, INTERRUPT,
        SOFTIRQ, STEAL, GUEST, GUEST_NICE, IDLE, ACTIVE,
        MAX_STATE
    };
    static constexpr std::array<const char*, MAX_STATE> kStateName {{
        "user","system","wait","nice","swap","interrupt",
        "softirq","steal","guest","guest_nice","idle","active"}};

    struct RateState {
        uint64_t  lastRaw   {0};
        double    rate      {NAN};
        bool      hasValue  {false};
    };
    struct PerCpu {
        std::array<RateState, MAX_STATE> st {};
    };

    /* -------- 内部辅助 -------- */
    int   stage(size_t cpuIdx, CpuState s, uint64_t raw, double now);
    void  aggregate            ();
    void  commitPercentages    ();
    void  commitDeriveRaw      ();
    void  resetIteration       ();

    /* -------- 成员数据 -------- */
    bool  m_reportByCpu   {true};
    bool  m_reportByState {true};
    bool  m_reportPercent {false};
    bool  m_reportNumCpu  {false};
    bool  m_reportGuest   {false};
    bool  m_subGuest      {true};

    std::vector<PerCpu> m_cpus;      ///< 动态按需扩展
    size_t              m_cpuSeen {0};

    /* dispatch* 接口由基类或 utils 提供 */
    void submitDerive (int cpu, CpuState st, uint64_t v);
    void submitPercent(int cpu, CpuState st, double   v);
    void submitNumCpu (double v);
};