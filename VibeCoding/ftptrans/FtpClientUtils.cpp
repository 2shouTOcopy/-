/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   ftp client utils class
  *
  * @author  zhoufeng20
  * @date    2024/08/27
  *
  * @version
  *  date        |version |author              |message
  *  :----       |:----   |:----               |:------
  *  2024/08/27  |V1.0.0  |zhoufeng20           |创建代码文档
  * @warning 
  */

#include "FtpClientUtils.h"

void FtpClientUtils::checkLastReply(const ftp::replies & replies, const std::string& expected)
{
	// TODO
	//const std::vector<ftp::reply> & reply_list = replies.get_replies();
}

std::string FtpClientUtils::getFilename(std::string path)
{
	size_t last_slash = path.find_last_of("\\/");

	if (last_slash == std::string::npos)
	{
		return path;
	}
	else
	{
		return path.substr(last_slash + 1);
	}
}

std::string FtpClientUtils::extractDirectory(const std::string& response) 
{
	size_t start = response.find('\"');
	size_t end = response.rfind('\"');

	if (start != std::string::npos && end != std::string::npos && start != end)
	{
		return response.substr(start + 1, end - start - 1);
	}
	else
	{
		return "/";
	}
}


