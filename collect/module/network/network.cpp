#include <cstdio>
#include <memory>
#include <fstream>
#include <string>
#include <cassert>
#include <vector>

#include "network.h"
#include "../daemon/PluginService.h"
#include "../daemon/utils/utils.h"
#include "../oconfig/configfile.h"

static constexpr char const *OUTPUT_FILENAME = "network_status.txt";

// 匿名命名空间，用于辅助函数
namespace
{
	// 辅助函数：执行命令并将其输出写入 ofstream
	void executeCommandAndWriteOutput(std::ofstream &ofs, const char *command, const char *errorContext)
	{
		std::unique_ptr<FILE, decltype(&pclose)> pipe(
			popen(command, "r"), pclose);
		if (!pipe)
		{
			if (errorContext && command)
			{
				ERROR("network plugin: %s: Failed to execute command '%s'", errorContext, command);
			}
			else if (command)
			{
				ERROR("network plugin: Failed to execute command '%s'", command);
			}
			else
			{
				ERROR("network plugin: Failed to execute command (unknown)");
			}
			ofs << "[执行命令 " << (command ? command : "未知") << " 出错]\n\n";
		}
		else
		{
			char buf[4096];
			while (fgets(buf, sizeof(buf), pipe.get()) != nullptr)
			{
				ofs << buf;
			}
			if (ferror(pipe.get()))
			{
				if (errorContext && command)
				{
					ERROR("network plugin: %s: Error reading output from command '%s'", errorContext, command);
				}
				else if (command)
				{
					ERROR("network plugin: Error reading output from command '%s'", command);
				}
				else
				{
					ERROR("network plugin: Error reading output from command (unknown)");
				}
				ofs << "[读取命令 " << (command ? command : "未知") << " 输出时出错]\n";
			}
			ofs << "\n";
		}
	}

	// 辅助函数：读取文件内容并将其写入 ofstream
	void readFileAndWriteOutput(std::ofstream &ofs, const char *filepath, const char *errorContext)
	{
		std::ifstream ifs_file(filepath);
		if (ifs_file.is_open())
		{
			ofs << ifs_file.rdbuf();
			ofs << "\n\n";
		}
		else
		{
			if (errorContext && filepath)
			{
				ERROR("network plugin: %s: Failed to read file '%s'", errorContext, filepath);
			}
			else if (filepath)
			{
				ERROR("network plugin: Failed to read file '%s'", filepath);
			}
			else
			{
				ERROR("network plugin: Failed to read file (unknown)");
			}
			ofs << "[读取文件 " << (filepath ? filepath : "未知") << " 出错]\n\n";
		}
	}

} // namespace

int CNetworkModule::config(const std::string &key, const std::string &val)
{
	return 0;
}

int CNetworkModule::flush()
{
	const std::string strDir = ConfigManager::Instance().GetGlobalOption("BaseDir");
	if (strDir.empty())
	{
		ERROR("network plugin: BaseDir is not configured.");
		return -1;
	}
	const std::string outPath = strDir + "/" + OUTPUT_FILENAME;

	std::ofstream ofs(outPath, std::ios::out | std::ios::trunc);
	if (!ofs.is_open())
	{
		ERROR("network plugin: Can't open output file %s", outPath.c_str());
		return -1;
	}

	// 第1部分：接口统计信息 (来自 /proc/net/dev)
	ofs << "=== Interface Statistics (/proc/net/dev) ===\n";
	ofs << "提示：显示网络接口流量（字节、数据包）以及错误/丢弃情况。\n"
	    << "      列通常包括：接收（字节、数据包、错误、丢弃），发送（字节、数据包、错误、丢弃）等。\n";
	readFileAndWriteOutput(ofs, "/proc/net/dev", "Interface Statistics");

	// 第2部分：IP 地址和接口详情 (ip addr show)
	ofs << "=== IP Addresses and Interface Details (ip addr show) ===\n";
	ofs << "提示：显示 IP 地址 (IPv4/IPv6)、MAC 地址、接口状态 (UP/DOWN)、MTU 等。\n"
	    << "  - 'inet': IPv4 地址\n"
	    << "  - 'inet6': IPv6 地址\n"
	    << "  - 'link/ether': MAC 地址\n"
	    << "  - 'state UP/DOWN': 接口的运行状态\n"
	    << "  - 'MTU': 最大传输单元\n";
	executeCommandAndWriteOutput(ofs, "ip addr show", "IP Addresses");

	// 第3部分：内核 IP 路由表 (ip route show)
	ofs << "=== Kernel IP Routing Table (ip route show) ===\n";
	ofs << "提示：显示网络数据包如何路由。查找 'default via [网关IP]' 可找到默认网关。\n";
	executeCommandAndWriteOutput(ofs, "ip route show", "Routing Table");

	// 第4部分：DNS 客户端配置 (/etc/resolv.conf)
	ofs << "=== DNS Client Configuration (/etc/resolv.conf) ===\n";
	ofs << "提示：显示本系统用于解析域名的 DNS 服务器。\n"
	    << "  - 'nameserver': DNS 服务器的 IP 地址\n"
	    << "  - 'search': 域搜索列表\n"
	    << "  - 'options': 各种解析器选项\n";
	readFileAndWriteOutput(ofs, "/etc/resolv.conf", "DNS Configuration");

	// 第5部分：活动连接和监听套接字 (netstat -anp)
	ofs << "=== Active Connections & Listening Sockets (netstat -anp) ===\n";
	ofs << "提示：显示 TCP、UDP 和 UNIX 套接字。'-a' (所有)，'-n' (数字IP/端口)，'-p' (进程ID/程序名)。\n"
	    << "常见的 TCP 状态说明:\n"
	    << "  - LISTEN       : (服务器)正在等待传入连接。\n"
	    << "  - ESTABLISHED  : 活动的数据通信。\n"
	    << "  - SYN_SENT     : (客户端)正在尝试建立连接。\n"
	    << "  - SYN_RECV     : 已收到远端的初始 SYN，连接尚未完全建立。\n"
	    << "  - FIN_WAIT_1   : 套接字已关闭，连接正在终止中。\n"
	    << "  - FIN_WAIT_2   : 连接已关闭，等待远端的 FIN。\n"
	    << "  - TIME_WAIT    : 套接字已关闭，等待处理延迟的数据包。\n"
	    << "  - CLOSE        : 套接字未被使用。\n"
	    << "  - CLOSE_WAIT   : 远端已关闭连接，等待本地应用程序关闭。\n"
	    << "  - LAST_ACK     : 远端已关闭，且套接字也已关闭，等待最后的确认。\n"
	    << "Proto 'unix' 指的是用于本地进程间通信的 Unix 域套接字。\n";
	executeCommandAndWriteOutput(ofs, "netstat -anp", "Active Connections");

	// 第6部分：ARP 缓存 (arp -n)
	ofs << "=== ARP Cache (arp -n) ===\n";
	ofs << "提示：地址解析协议缓存。在本地网络中将 IP 地址映射到 MAC 地址。\n"
	    << "      用于诊断本地网络连接问题。\n";
	executeCommandAndWriteOutput(ofs, "arp -n", "ARP Cache");

	ofs.close();
	if (!ofs)
	{
		ERROR("network plugin: Error occurred during writing or closing file %s", outPath.c_str());
		return -1;
	}
	return 0;
}

CAbstractUserModule *CreateModule()
{
	return new CNetworkModule();
}

void DestroyModule(CAbstractUserModule *pUserModule)
{
	assert(pUserModule != nullptr);
	delete pUserModule;
}
