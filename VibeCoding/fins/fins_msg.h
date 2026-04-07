/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author zhengxiaoyu	
 * @date 2019/10/17
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2019/10/17  |V1.0.0  |zhengxiaoyu         |创建文档
 * @warning 
 */
#ifndef __FINS_MSG_H
#define __FINS_MSG_H

#define MAX_FINS_PAYLOAD_LEN (1280)

#include "CommProxy.h"

typedef struct
{
	int server_ip;
	int server_port;
	int control_poll_interval;
	int control_space;
	int control_offset;
	int control_size;
	int status_space;
	int status_offset;
	int status_size;
	int result_space;
	int result_offset;
	int result_size;
	int result_byte_swap;
	int result_timeout;    
	int ins_space;
	int ins_offset;
	int ins_size;
} fins_param_opt;

#ifdef __cplusplus
extern "C" {
#endif

int fins_msg_init(fins_param_opt *c_param);
int fins_msg_deinit(void);
int fins_set_server_ip(int ip);
int fins_set_server_port(int port);
int fins_set_recreate(int recreate);
int fins_set_control_poll_interval(int ms);
int fins_set_control_offset(int offset);
int fins_set_status_offset(int offset);
int fins_set_result_offset(int offset);
int fins_set_result_size(int num);
int fins_set_result_byte_swap(int swap);
int fins_set_result_timeout(int second);
int fins_get_enable();
int fins_set_debug_level(int level);
int fins_get_debug_level(void);
int fins_get_debug_info(char *buff, int buff_size, int *data_len);
int fins_send_result(const char *result_ptr, unsigned int result_len);

#ifdef __cplusplus
}
#endif

#endif /* __FINS_MSG_H */

