#include <fstream>
#include <system_error>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <assert.h>
#include <memory>

#include "memory.h"
#include "../daemon/utils/utils.h"
#include "../daemon/PluginService.h"
#include "../oconfig/configfile.h"

static constexpr char const *OUTPUT_FILENAME = "mmz_info.txt";

namespace
{
	std::string executeCommandAndGetOutput(const std::string &command)
	{
		std::string data;
		std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
		if (!pipe)
		{
			ERROR("execute command '%s' failed (popen)", command.c_str());
			return "";
		}
		std::array<char, 256> buffer;
		while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
		{
			data += buffer.data();
		}
		if (ferror(pipe.get()))
		{
			ERROR("reading output from command %s", command.c_str());
		}
		return data;
	}

}

CMemoryModule::CMemoryModule()
    : m_bAbsolute(true),
      m_bPercentage(false),
      m_bCommInfo(false)
{

}

int CMemoryModule::config(const std::string& key, const std::string& val)
{
	if      (key == "ValuesAbsolute")        m_bAbsolute  = IS_TRUE(val.c_str());
	else if (key == "ValuesPercentage")      m_bPercentage= IS_TRUE(val.c_str());
	else if (key == "IncludeCommInfo")       m_bCommInfo  = IS_TRUE(val.c_str());
	else return -1;

	return 0;
}

bool CMemoryModule::parseLine(const std::string &line, const char *key_to_match, gauge_t &target_value_ref)
{
	if (line.rfind(key_to_match, 0) == 0)
	{
		size_t colon_pos = line.find(':');
		if (colon_pos == std::string::npos)
		{
			return false;
		}

		// 从冒号后开始查找第一个非空字符作为数值的开始
		size_t value_start_pos = line.find_first_not_of(" \t", colon_pos + 1);
		if (value_start_pos == std::string::npos)
		{
			return false; // 冒号后全是空格或没有内容
		}

		// 从数值开始处查找第一个空白字符作为数值的结束
		size_t value_end_pos = line.find_first_of(" \t", value_start_pos);
		std::string value_str;
		if (value_end_pos == std::string::npos)
		{
			value_str = line.substr(value_start_pos);
		}
		else
		{
			value_str = line.substr(value_start_pos, value_end_pos - value_start_pos);
		}

		if (value_str.empty())
			return false;

		try
		{
			target_value_ref = std::stod(value_str) * 1024.0; // 转换为字节
			return true;
		}
		catch (const std::invalid_argument &ia)
		{
			WARNING("Failed to parse numeric value for key '%s' from substring '%s'. Invalid argument: %s", key_to_match, value_str.c_str(), ia.what());
		}
		catch (const std::out_of_range &oor)
		{
			WARNING("Numeric value out of range for key '%s' from substring '%s'. Out of range: %s", key_to_match, value_str.c_str(), oor.what());
		}
		return false;
	}
	return false;
}

bool CMemoryModule::parseMemInfo(ParsedMemInfo &data_out)
{
	std::ifstream meminfo_file("/proc/meminfo");
	if (!meminfo_file.is_open())
	{
		ERROR("Failed to open /proc/meminfo: %s", strerror(errno));
		return false;
	}

	data_out = ParsedMemInfo{};

	std::string current_line;
	while (std::getline(meminfo_file, current_line))
	{
		if (parseLine(current_line, "MemTotal:", data_out.mem_total))
			continue;
		else if (parseLine(current_line, "MemFree:", data_out.mem_free))
			continue;
		else if (parseLine(current_line, "Buffers:", data_out.mem_buffered))
			continue;
		else if (parseLine(current_line, "Cached:", data_out.mem_cached))
			continue;
		else if (parseLine(current_line, "Slab:", data_out.mem_slab_total))
			continue;
		else if (parseLine(current_line, "SReclaimable:", data_out.mem_slab_reclaimable))
		{
			data_out.detailed_slab_info_present = true; // 标记Slab详细信息存在
			continue;
		}
		else if (parseLine(current_line, "SUnreclaim:", data_out.mem_slab_unreclaimable))
		{
			// SReclaimable 和 SUnreclaim 通常一起出现，所以设置 detailed_slab_info_present 是合理的
			data_out.detailed_slab_info_present = true;
			continue;
		}
		else if (parseLine(current_line, "MemAvailable:", data_out.mem_available))
		{
			data_out.mem_available_info_present = true; // 标记MemAvailable信息存在
			continue;
		}
	}

	if (data_out.mem_total < (data_out.mem_free + data_out.mem_buffered + data_out.mem_cached + data_out.mem_slab_total))
	{
		WARNING("Data sanity check failed: MemTotal (%lf) is less than the sum of Free (%lf), Buffered (%lf), Cached (%lf), and SlabTotal (%lf).",
			data_out.mem_total, data_out.mem_free, data_out.mem_buffered, data_out.mem_cached, data_out.mem_slab_total);
		return false;
	}

	if (data_out.detailed_slab_info_present)
	{
		data_out.mem_used = data_out.mem_total - (data_out.mem_free + data_out.mem_buffered + data_out.mem_cached + data_out.mem_slab_reclaimable);
	}
	else
	{
		data_out.mem_used = data_out.mem_total - (data_out.mem_free + data_out.mem_buffered + data_out.mem_cached + data_out.mem_slab_total);
	}

	if (data_out.mem_used < 0)
	{
		data_out.mem_used = 0;
	}

	return true;
}

void CMemoryModule::submitMultiMetrics(value_list_t *vl_template, const ParsedMemInfo &d)
{
	std::vector<MetricDataPoint> data_points;

	if (d.detailed_slab_info_present)
	{
		data_points.push_back({"used", d.mem_used});
		data_points.push_back({"buffered", d.mem_buffered});
		data_points.push_back({"cached", d.mem_cached});
		data_points.push_back({"free", d.mem_free});
		data_points.push_back({"slab_unrecl", d.mem_slab_unreclaimable});
		data_points.push_back({"slab_recl", d.mem_slab_reclaimable});
	}
	else
	{
		data_points.push_back({"used", d.mem_used});
		data_points.push_back({"buffered", d.mem_buffered});
		data_points.push_back({"cached", d.mem_cached});
		data_points.push_back({"free", d.mem_free});
		data_points.push_back({"slab", d.mem_slab_total});
	}

	if (m_bAbsolute)
	{
	    PluginService::Instance().dispatchMultivalues(vl_template,
	                                                  false,
	                                                  DS_TYPE_GAUGE,
	                                                  data_points);
	}

	if (m_bPercentage)
	{
	    PluginService::Instance().dispatchMultivalues(vl_template,
	                                                  true, 
	                                                  DS_TYPE_GAUGE,
	                                                  data_points);
	}
}

void CMemoryModule::submitAvailableMetric(gauge_t mem_available_value)
{
	value_list_t vl = VALUE_LIST_INIT;
	value_t val_entry = {.gauge = mem_available_value};

	vl.values = &val_entry;
	vl.values_len = 1;

	sstrncpy(vl.plugin, "memory", sizeof(vl.plugin));
	sstrncpy(vl.type, "memory", sizeof(vl.type));
	sstrncpy(vl.type_instance, "available", sizeof(vl.type_instance));

	INFO("Submitting metric: plugin='%s', type='%s', type_instance='%s', value=%lf",
	     vl.plugin, vl.type, vl.type_instance, mem_available_value);
	PluginService::Instance().dispatchValues(&vl);
}

int CMemoryModule::read()
{
	INFO("Memory read() method called.");
	ParsedMemInfo current_mem_data;

	if (!parseMemInfo(current_mem_data))
	{
		ERROR("Failed to parse memory information from /proc/meminfo.");
		return -1;
	}

	value_list_t vl_template = VALUE_LIST_INIT;
	sstrncpy(vl_template.plugin, "memory", sizeof(vl_template.plugin));
	sstrncpy(vl_template.type, "memory", sizeof(vl_template.type));

	vl_template.time = cdtime();

	submitMultiMetrics(&vl_template, current_mem_data);

	if (current_mem_data.mem_available_info_present)
	{
		submitAvailableMetric(current_mem_data.mem_available);
	}

	return 0;
}

int CMemoryModule::flush()
{
	const std::string strDir = ConfigManager::Instance().GetGlobalOption("BaseDir");
	if (strDir.empty())
	{
		ERROR("memory plugin: DataDir is not configured.");
		return -1;
	}
	const std::string outPath = strDir + "/" + OUTPUT_FILENAME;
	const std::string tmpPath = strDir + "/" + "tmp.txt";
	std::ofstream outFile;

	const std::string command1 = "/mnt/app/toolbox mp_stat 1 mmz_comm_pool ";

	outFile.open(outPath, std::ios::out | std::ios::trunc);
	if (!outFile.is_open())
	{
		ERROR("memory plugin: open output file %s failed!", outPath.c_str());
		return -1;
	}

	std::string command1_output = executeCommandAndGetOutput(command1);
	if (!command1_output.empty())
	{
		outFile << command1_output;
	}
	else
	{
		if (!outFile.good())
		{
			ERROR("memory plugin: Error writing output from command");
			outFile.close();
			remove(outPath.c_str());
			return -1;
		}
	}
	outFile.close();

	const std::string command2 = "/mnt/app/toolbox mp_print 1 mmz_comm_pool " + tmpPath;

	remove(tmpPath.c_str());

	std::unique_ptr<FILE, decltype(&pclose)> pipe_cmd2(
	    popen(command2.c_str(), "r"), pclose);

	if (!pipe_cmd2)
	{
		ERROR("memory plugin: Failed to execute command '%s'", command2.c_str());
		return -1;
	}
	else
	{
		char buffer[256];
		std::string cmd2_diag_output;
		while (fgets(buffer, sizeof(buffer), pipe_cmd2.get()) != nullptr)
		{
			cmd2_diag_output += buffer;
		}

		int cmd2_status = pclose(pipe_cmd2.release()); // 获取 command2 的退出状态

		if (cmd2_status == -1)
		{
			ERROR("memory plugin: command '%s' pclose failed", command2.c_str());
			remove(tmpPath.c_str());
			return -1;
		}
		else if (WIFEXITED(cmd2_status))
		{
			int exit_code = WEXITSTATUS(cmd2_status);
			if (exit_code != 0)
			{
				ERROR("memory plugin: command '%s' exit by status '%d' diag output: %s", 
					command2.c_str(), exit_code, cmd2_diag_output.c_str());
				remove(tmpPath.c_str());
				return -1;
			}

			if (!cmd2_diag_output.empty())
			{
				INFO("Command2 diag output: %s", cmd2_diag_output.c_str());
			}
		}
		else if (WIFSIGNALED(cmd2_status))
		{
			ERROR("memory plugin: command '%s' Terminated By Signal %d. Diagnostic Output: %s", 
				command2.c_str(), WTERMSIG(cmd2_status), cmd2_diag_output.c_str());
			remove(tmpPath.c_str());
			return -1;
		}
		else
		{
			ERROR("memory plugin: command '%s' Terminated Abnormally. Diagnostic Output: %s", 
				command2.c_str(), cmd2_diag_output.c_str());
			remove(tmpPath.c_str());
			return -1;
		}
	}

	std::ifstream tempFile(tmpPath, std::ios::in | std::ios::binary);
	if (!tempFile.is_open())
	{
		ERROR("Memory Plugin: Opening temporary file '%s' failed (for merging)."
			"Command2 may not have generated the file.", tmpPath.c_str());
		remove(tmpPath.c_str());
		return -1;
	}

	outFile.open(outPath, std::ios::out | std::ios::app | std::ios::binary);
	if (!outFile.is_open())
	{
		ERROR("memory plugin: Opening output file '%s' for appended content failed.", outPath.c_str());
		tempFile.close();
		remove(tmpPath.c_str());
		return -1;
	}

	outFile << "\n=== mp_print 1 mmz_comm_pool ===\n";
	outFile << "提示：内存池使用情况信息，用于诊断算子模块内存使用问题。\n";
	outFile << tempFile.rdbuf();               // 高效地流式传输文件内容

	if (tempFile.bad() || !outFile.good())
	{
		ERROR("memory Plugin: An error occurred when reading from '%s' or writing to '%s'.", 
			tmpPath.c_str(), outPath.c_str());
		tempFile.close();
		outFile.close();
		remove(tmpPath.c_str());
		return -1;
	}

	tempFile.close();
	outFile.close();

	// 删除 tmpPath
	if (remove(tmpPath.c_str()) != 0)
	{
		ERROR("memory Plugin: remove '%s' failed", tmpPath.c_str());
	}

	if (m_bCommInfo)
	{
		const std::string commInfoPath = "/proc/hdal/comm/info";

		std::ifstream commInfoFile(commInfoPath, std::ios::in | std::ios::binary);
		if (!commInfoFile.is_open())
		{
			ERROR("Memory Plugin: Opening communication information file '%s' failed.", commInfoPath.c_str());
			return -1;
		}

		outFile.open(outPath, std::ios::out | std::ios::app | std::ios::binary);
		if (!outFile.is_open())
		{
			ERROR("memory plugin: Opening output file '%s' for appending comm info failed.", outPath.c_str());
			commInfoFile.close();
			return -1;
		}

		outFile << "\n=== /proc/hdal/comm/info ===\n";
		outFile << "提示：NT平台MMZ内存使用信息（可选文本）\n";
		outFile << commInfoFile.rdbuf();

		if (commInfoFile.bad() || !outFile.good())
		{
			ERROR("memory plugin: comm info '%s' read or write '%s' failed", commInfoPath.c_str(), outPath.c_str());
			commInfoFile.close();
			outFile.close();
			return -1;
		}

		commInfoFile.close();
		outFile.close();
	}

	return 0;
}


CAbstractUserModule* CreateModule()
{
	return new CMemoryModule();
}

void DestroyModule(CAbstractUserModule* pUserModule)
{
	assert(pUserModule != nullptr);
	delete pUserModule;
}
