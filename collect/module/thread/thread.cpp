#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <algorithm>
#include <iomanip>
#include <map>
#include <chrono>
#include <set>
#include <cstring>
#include <thread>
#include <assert.h>

#include "thread.h"
#include "../daemon/PluginService.h"
#include "../daemon/utils/utils.h"
#include "../oconfig/configfile.h"

static constexpr char const *OUTPUT_FILENAME = "thread.txt";
static constexpr char const *TARGET_PROCESS = "m320_app";

// 辅助函数：线程状态转字符串
std::string thread_state_to_string(char state_char)
{
	static const std::map<char, std::string> state_map = {
		{'R', "RUNNING"},
		{'S', "SLEEPING (Interruptible)"},
		{'D', "SLEEPING (Uninterruptible)"},
		{'Z', "ZOMBIE"},
		{'T', "STOPPED"},
		{'t', "TRACING_STOP"},
		{'X', "DEAD"},
		{'I', "IDLE"},
		{'P', "PARKED"}
	};
	auto it = state_map.find(state_char);
	return it != state_map.end() ? it->second : "UNKNOWN";
}

// 辅助函数：调度策略转字符串
std::string sched_policy_to_string(long policy_num)
{
	static const std::map<long, std::string> policy_map = {
		{0, "SCHED_OTHER"},
		{1, "SCHED_FIFO"},
		{2, "SCHED_RR"},
		{3, "SCHED_BATCH"},
		{4, "SCHED_ISO"},
		{5, "SCHED_IDLE"},
		{6, "SCHED_DEADLINE"}
	};
	auto it = policy_map.find(policy_num);
	return it != policy_map.end() ? it->second : "UNKNOWN_POLICY";
}

CThreadModule::CThreadModule() : m_nHz(0), m_bIsfirstCall(true) {}

int CThreadModule::config(const std::string &key, const std::string &val)
{
	return 0;
}

// 收集线程数据但不输出
ThreadDataSnapshot CThreadModule::collectThreadData()
{
	ThreadDataSnapshot snapshot;

	if (m_nHz == 0)
	{
		m_nHz = sysconf(_SC_CLK_TCK);
		if (m_nHz <= 0)
		{
			ERROR("Warning: Using fallback HZ=100");
			m_nHz = 100;
		}
	}

	snapshot.timestamp = std::chrono::steady_clock::now();
	if (!m_bIsfirstCall)
	{
		auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(
			snapshot.timestamp - prev_timestamp);
		snapshot.time_delta_seconds = duration.count();
	}

	pid_t target_pid;
	int pid_count = 1;
	if (get_pid_by_name(TARGET_PROCESS, &target_pid, &pid_count) != 0 || pid_count == 0)
	{
		ERROR("Error: Process '%s' not found", TARGET_PROCESS);
		return snapshot;
	}

	std::string task_dir = "/proc/" + std::to_string(target_pid) + "/task";
	DIR *dir = opendir(task_dir.c_str());
	if (!dir)
	{
		perror(("Error opening: " + task_dir).c_str());
		return snapshot;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		if (entry->d_type != DT_DIR) continue;

		pid_t tid = strtol(entry->d_name, nullptr, 10);
		if (tid <= 0) continue;

		ThreadInfo info;
		info.tid = tid;
		snapshot.current_tids.insert(tid);

		// 获取线程名
		std::string comm_path = task_dir + "/" + entry->d_name + "/comm";
		std::ifstream comm_file(comm_path);
		if (comm_file) std::getline(comm_file, info.name);

		// 解析stat文件
		std::string stat_path = task_dir + "/" + entry->d_name + "/stat";
		std::ifstream stat_file(stat_path);
		if (stat_file)
		{
			std::string line;
			std::getline(stat_file, line);
			std::istringstream ss(line);
			std::vector<std::string> fields{
			    std::istream_iterator<std::string>{ss},
			    std::istream_iterator<std::string>{}
			};

			if (fields.size() > 2) info.state = fields[2][0];
			if (fields.size() > 13) info.utime = std::stoul(fields[13]);
			if (fields.size() > 14) info.stime = std::stoul(fields[14]);
			if (fields.size() > 18) info.nice = std::stol(fields[18]);
			if (fields.size() > 39) info.rt_priority = std::stol(fields[39]);
			if (fields.size() > 40) info.sched_policy = std::stol(fields[40]);
		}

		// 计算CPU时间
		info.user_time = static_cast<double>(info.utime) / m_nHz;
		info.sys_time = static_cast<double>(info.stime) / m_nHz;
		info.total_time = info.user_time + info.sys_time;

		// CPU使用率计算
		if (!m_bIsfirstCall)
		{
			auto prev_it = prev_cpu_times.find(tid);
			if (prev_it != prev_cpu_times.end())
			{
				unsigned long delta = 
				    (info.utime - prev_it->second.first) + 
				    (info.stime - prev_it->second.second);
				info.cpu_usage = (static_cast<double>(delta) / m_nHz) / 
				                 snapshot.time_delta_seconds * 100.0;
			}
		}

		// 获取栈内存
		std::string status_path = task_dir + "/" + entry->d_name + "/status";
		std::ifstream status_file(status_path);
		if (status_file)
		{
			std::string line;
			while (std::getline(status_file, line))
			{
				if (line.compare(0, 6, "VmStk:") == 0)
				{
					info.vm_stack_kb = std::stoul(line.substr(6));
					break;
				}
			}
		}

		// 统计文件描述符
		std::string fd_path = task_dir + "/" + entry->d_name + "/fd";
		DIR *fd_dir = opendir(fd_path.c_str());
		if (fd_dir)
		{
			while ((entry = readdir(fd_dir)) != nullptr)
			{
				if (strcmp(entry->d_name, ".") != 0 && 
					strcmp(entry->d_name, "..") != 0)
				{
					info.fd_count++;
				}
			}
			closedir(fd_dir);
		}

		snapshot.threads.push_back(info);
		prev_cpu_times[tid] = {info.utime, info.stime};
	}
	closedir(dir);

	// 清理不存在的线程
	for (auto it = prev_cpu_times.begin(); it != prev_cpu_times.end(); )
	{
		if (!snapshot.current_tids.count(it->first))
		{
			it = prev_cpu_times.erase(it);
		}
		else
		{
			++it;
		}
	}

	prev_timestamp = snapshot.timestamp;
	if (m_bIsfirstCall) m_bIsfirstCall = false;

	return snapshot;
}

// 输出线程数据到指定流
void CThreadModule::outputThreadReport(const ThreadDataSnapshot& snapshot, std::ostream& os)
{
	const int name_width = 20, state_width = 30, policy_width = 15;
	os << std::fixed << std::setprecision(2);

	// 表头
	os << "--- Thread Monitor: " << TARGET_PROCESS << " ---\n";
	os << "--------------------------------------------------------------------------------------------------------------------------------------------------------------------\n"
	   << "| " << std::left << std::setw(8) << "TID"
	   << "| " << std::left << std::setw(name_width) << "Name"
	   << "| " << std::left << std::setw(state_width) << "State"
	   << "| " << std::left << std::setw(8) << "Nice"
	   << "| " << std::left << std::setw(8) << "RT Prio"
	   << "| " << std::left << std::setw(policy_width) << "Policy"
	   << "| " << std::left << std::setw(10) << "CPU %"
	   << "| " << std::left << std::setw(10) << "User(s)"
	   << "| " << std::left << std::setw(10) << "Sys(s)"
	   << "| " << std::left << std::setw(10) << "Stack(KB)"
	   << "| " << std::left << std::setw(8) << "FDs"
	   << "|\n"
	   << "--------------------------------------------------------------------------------------------------------------------------------------------------------------------\n";

	// 线程详细信息
	for (const auto& t : snapshot.threads)
	{
		std::string state_str = thread_state_to_string(t.state) + " ('" + t.state + "')";
		os << "| " << std::left << std::setw(8) << t.tid
		   << "| " << std::left << std::setw(name_width) << t.name.substr(0, name_width)
		   << "| " << std::left << std::setw(state_width) << state_str.substr(0, state_width)
		   << "| " << std::right << std::setw(8) << t.nice
		   << "| " << std::right << std::setw(8) << t.rt_priority
		   << "| " << std::left << std::setw(policy_width) << sched_policy_to_string(t.sched_policy).substr(0, policy_width)
		   << "| " << std::right << std::setw(9) << t.cpu_usage << "%"
		   << "| " << std::right << std::setw(10) << t.user_time
		   << "| " << std::right << std::setw(10) << t.sys_time
		   << "| " << std::right << std::setw(10) << t.vm_stack_kb
		   << "| " << std::right << std::setw(8) << t.fd_count
		   << "|\n";
	}
	os << "--------------------------------------------------------------------------------------------------------------------------------------------------------------------\n";

	// 聚合分析
	os << "\n--- Aggregated Analysis ---\n";
	std::map<std::string, int> state_counts;
	for (const auto& t : snapshot.threads)
	{
		state_counts[thread_state_to_string(t.state)]++;
	}

	os << "Thread States:\n";
	for (const auto& [state, count] : state_counts)
	{
		os << " - " << std::left << std::setw(30) << state << ": " << count << "\n";
	}
	os << " - " << std::left << std::setw(30) << "Total Threads" << ": " << snapshot.threads.size() << "\n";

	// CPU热点线程
	auto sorted_threads = snapshot.threads;
	std::sort(sorted_threads.begin(), sorted_threads.end(), 
	    [](const ThreadInfo& a, const ThreadInfo& b) {
	        return a.cpu_usage > b.cpu_usage;
	    });

	os << "\nTop CPU Threads:\n";
	const int top_n = std::min(5, (int)sorted_threads.size());
	for (int i = 0; i < top_n; ++i)
	{
		const auto& t = sorted_threads[i];
		os << " " << i+1 << ". TID:" << std::setw(6) << t.tid
		   << " Name:" << std::setw(name_width) << t.name.substr(0, name_width)
		   << " CPU:" << std::setw(6) << t.cpu_usage << "%\n";
	}

	if (m_bIsfirstCall)
	{
		os << " (CPU usage initialized)\n";
	}
	os << "\n--- Report End ---\n\n";
}

int CThreadModule::flush()
{
	INFO("Collecting thread data for '%s'...", TARGET_PROCESS);
	const std::string strDir = ConfigManager::Instance().GetGlobalOption("BaseDir");
	if (strDir.empty())
	{
		ERROR("thread plugin: DataDir is not configured.");
		return -1;
	}
	const std::string outPath = strDir + "/" + OUTPUT_FILENAME;

	// 第一次收集（初始化CPU计算基准）
	collectThreadData();
	std::this_thread::sleep_for(std::chrono::seconds(1));

	// 第二次收集并输出到文件
	std::ofstream outfile(outPath);
	if (!outfile)
	{
		ERROR("Error: Failed to create %s", outPath.c_str());
		return -1;
	}

	outputThreadReport(collectThreadData(), outfile);
	INFO("Thread report saved to %s", outPath.c_str());
	return 0;
}

CAbstractUserModule *CreateModule()
{
	return new CThreadModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	delete pUserModule;
}
