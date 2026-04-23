#ifndef FTP_CLIENT_MONITOR_H
#define FTP_CLIENT_MONITOR_H

#include <iostream>
#include <fstream>
#include <memory>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

/*
 * Save logs in the following format:
 *   -- Connected to localhost on port 2121.
 *   <- 220 FTP server is ready.
 *   -> USER user
 *   <- 331 Username ok, send password.
 *   -> PASS *****
 *   <- 230 Login successful.
 *   ...
 *   -> QUIT
 *   <- 221 Goodbye.
 */

class FtpMonitor : public ftp::observer
{
private:
	static constexpr auto FTP_MONITOR_FILE = "/mnt/cfg/enable_ftp_log";

public:
	explicit FtpMonitor(const std::string &filename)
	: enabled_(false)
	{
		if (0 == access(FTP_MONITOR_FILE, F_OK))
		{
			// 文件存在，则启用日志
			enabled_ = true;
			log_file_.open(filename, std::ofstream::out | std::ofstream::trunc);
			if (!log_file_)
			{
				std::cerr << "Cannot create a log file: " << filename << std::endl;
				enabled_ = false;
			}
		}
	}

	void on_connected(std::string hostname, std::uint16_t port) override
	{
		if (!enabled_)
		{
			return ;
		}
		log_file_ << "[" << current_time_str() << "] "
				  << "-- Connected to " << hostname << " on port " << port << "." << std::endl;
	}

	void on_request(std::string command) override
	{
		if (!enabled_)
		{
			return ;
		}
		log_file_ << "[" << current_time_str() << "] "
				  << "-> ";
		if (command.compare(0, 4, "PASS") == 0)
		{
			log_file_ << "PASS *****";
		}
		else
		{
			log_file_ << command;
		}
		log_file_ << std::endl;
	}

	void on_reply(const ftp::reply & reply) override
	{
		if (!enabled_)
		{
			return ;
		}
		log_file_ << "[" << current_time_str() << "] "
				  << "<- " << reply.get_status_string() << std::endl;
	}
	
	void on_file_list(std::string file_list) override
	{
		if (!enabled_)
		{
			return ;
		}
		log_file_ << "[" << current_time_str() << "] "
				  << "-- File list: " << file_list << std::endl;
	}
	
private:
	static std::string current_time_str()
	{
		auto now = std::chrono::system_clock::now();
		std::time_t now_c = std::chrono::system_clock::to_time_t(now);
		std::tm tm_buf;
		localtime_r(&now_c, &tm_buf);
		std::ostringstream oss;
		oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
		return oss.str();
	}

	std::ofstream log_file_;
	bool enabled_;
};


#endif // FTP_CLIENT_MONITOR_H

