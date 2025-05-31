#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <set>
#include <chrono>
#include "ModuleBase.h"

// 线程信息结构体
struct ThreadInfo
{
	pid_t tid;              // 线程ID
	std::string name;       // 线程名
	char state;             // 线程状态字符
	long nice;              // nice值
	long rt_priority;       // 实时优先级
	long sched_policy;      // 调度策略值
	unsigned long utime;    // 用户模式时钟滴答
	unsigned long stime;    // 内核模式时钟滴答
	double user_time;       // 用户态CPU时间(秒)
	double sys_time;        // 内核态CPU时间(秒)
	double total_time;      // 总CPU时间(秒)
	double cpu_usage;       // CPU使用率百分比
	unsigned long vm_stack_kb; // 栈内存大小(KB)
	int fd_count;           // 打开文件描述符数量

	ThreadInfo() 
		: tid(0), state('?'), nice(0), rt_priority(0), sched_policy(0),
		  utime(0), stime(0), user_time(0.0), sys_time(0.0), 
		  total_time(0.0), cpu_usage(0.0), vm_stack_kb(0), fd_count(0) {}
};

// 线程数据快照结构体
struct ThreadDataSnapshot
{
	std::chrono::steady_clock::time_point timestamp; // 时间戳
	double time_delta_seconds = 0.0;                 // 时间间隔(秒)
	std::vector<ThreadInfo> threads;                 // 线程列表
	std::set<pid_t> current_tids;                    // 当前活跃TID集合
};

class CThreadModule final : public CAbstractUserModule
{
public:
	CThreadModule();
	~CThreadModule() override = default;

	int config(const std::string &key, const std::string &val) override;
	int flush() override;

private:
	long m_nHz;                                        // 时钟频率
	bool m_bIsfirstCall;                               // 首次调用标志
	std::map<pid_t, std::pair<unsigned long, unsigned long>> prev_cpu_times; // 历史CPU时间
	std::chrono::steady_clock::time_point prev_timestamp; // 上次时间戳

	ThreadDataSnapshot collectThreadData();        // 收集线程数据
	void outputThreadReport(                       // 输出线程报告
		const ThreadDataSnapshot& snapshot, 
		std::ostream& os);
};

#ifdef __cplusplus
extern "C" {
#endif
CAbstractUserModule* CreateModule();
void DestroyModule(CAbstractUserModule *pUserModule);
#ifdef __cplusplus
};
#endif
