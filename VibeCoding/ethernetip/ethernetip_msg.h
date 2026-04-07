/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author zhengxiaoyu	
 * @date 2019/10/28
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2019/10/28  |V1.0.0  |zhengxiaoyu           |创建代码文档
 * @warning 
 */
#ifndef __ETHERNETIP_MSG_H
#define __ETHERNETIP_MSG_H

#include <stdint.h>

#include "VmModuleBase.h"
#include "CommProxy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_ETHERNETIP_PAYLOAD_LEN 500

int ethernetip_msg_init(void);
int ethernetip_msg_deinit(void);
int ethernetip_set_init_input_size(int size);
int ethernetip_set_init_output_size(int size);
int ethernetip_set_input_size(int size);
int ethernetip_set_output_size(int size);
int ethernetip_set_result_byte_swap(int enable);
int ethernetip_set_module_enable(int enable);
int ethernetip_set_debug_level(int level);
int ethernetip_get_debug_level(void);
int ethernetip_get_debug_info(char *buff, int buff_size, int *data_len);
int ethernetip_send_result(char *result_ptr, int result_len);

void set_frame_and_trigger(int nFrame, int nTrigger, int nLogId);
int set_procedure_name(const char* szProcedureName);

extern eALGO_PLAYCTRL *sys_run_status;

#ifdef __cplusplus
}
#endif

#endif /* __ETHERNETIP_MSG_H */

