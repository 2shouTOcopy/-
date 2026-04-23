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

#ifndef FTP_CLIENT_UTILS_H
#define FTP_CLIENT_UTILS_H

#include <string>
#include <iostream>
#include <ftp/replies.hpp>

class FtpClientUtils 
{
public:
	void checkLastReply(const ftp::replies & replies, const std::string& expected);

	std::string getFilename(std::string path);

	std::string extractDirectory(const std::string& response);
};

#endif
