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
 *  2019/10/17  |V1.0.0  |zhengxiaoyu           |创建代码文档
 * @warning 
 */
#ifndef __MODBUS_MSG_H
#define __MODBUS_MSG_H

#include <stdint.h>

#include "VmModuleBase.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "api_modbus.h"
#include "simple_fifo.h"

#define IPQUAD(ip)   \
		((unsigned char *)&(ip))[3], \
		((unsigned char *)&(ip))[2], \
		((unsigned char *)&(ip))[1], \
		((unsigned char *)&(ip))[0]

#define MAX_MODBUS_PAYLOAD_LEN (1280)
#define MAX_MODBUS_REGULAR_LEN (128)
#ifndef MODBUS_ONCE_WRIE_MAX_REG
#define MODBUS_ONCE_WRIE_MAX_REG (100)
#endif
enum modbus_byte_order
{
	ORDER_BADC = 0,  
	ORDER_ABCD = 1,  
	ORDER_CDAB = 2,  
	ORDER_DCBA = 3,  
};

enum modbus_spacer
{
	SEMICOLON = 0,  //  mean ';'
	COMMA     = 1,  //  mean ','
	BLOCK     = 2,  //  NULL
};

enum modbus_work_mode
{
	MODBUS_SERVER_MODE = 0,
	MODBUS_CLIENT_MODE = 1,
};

typedef struct
{
	int32_t iWorkMode;
	uint32_t iServerIp;
	uint32_t iServerPort;
	uint32_t iSlaveId;
	int iMaxConnection;
	int iIdleTimeoutUsec;
	int iCtrlAddrSpaceType;
	int iStatusAddrSpaceType;
	int iInputAddrSpaceType;
	int iOutputAddrSpaceType;
	int iCtrlAddrOffset;
	int iStatusAddrOffset;
	int iInputAddrOffset;
	int iOutputAddrOffset;
	int iCtrlAddrQuantity;
	int iStatusAddrQuantity;
	int iInputAddrQuantity;
	int iOutputAddrQuantity;
	int iTriggerWriteID;
	int iTriggerReadID;
	uint16_t iTriggerID;
	eALGO_PLAYCTRL *sys_run_status;
	int iByteOrderEnable;
	enum modbus_byte_order iByteOrder;
	enum modbus_spacer iSpacer;
	int iModuleEnable;
	int iControlPollInterval;
}modbus_para_opt;

int init_modbus_msg(modbus_para_opt *para);
int modbus_deinit(void);
int modbus_set_input_size(int reg_num);
int modbus_set_output_size(int reg_num);
int modbus_send_result(char *result_ptr, int result_len);
int modbus_get_ipaddr(char *ifname, unsigned int *ip);
int modbus_set_slaveId(void);
int modbus_set_input_addr(int addr);
int modbus_set_control_addr(int addr);
int modbus_set_status_addr(int addr);
int modbus_set_output_addr(int addr);
int modbus_set_debug_level(int level);
int modbus_get_debug_level(void);
int modbus_get_debug_info(char *buff, int buff_size, int *data_len);

void set_frame_and_trigger(int nFrame, int nTrigger, int nLogId);
int set_procedure_name(const char* szProcedureName);

int character_sequence_handle_for_modbus(int type,char*console_buffer);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_MSG_H */
