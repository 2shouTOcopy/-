/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author  zhengxiaoyu
 * @date    2019/08/27
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2019/08/27  |V1.0.0  |zhengxiaoyu         |创建代码文档
 * @warning 
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <regex>

#include "modbus_msg.h"
#include "api_modbus.h"
#include "utils.h"
#include "util_net.h"
#include "thread/ThreadApi.h"
#include "TriggerParamDef.h"
#include "AppParamCommon.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "algo_common.h"
#include "framework_service.h"
#include "log/log.h"
#include "IIspSource.h"
#include "ITriggerSource.h"
#include "IImageProcess.h"
#include "CommProxy.h"
#include "calibrateapiwapper.h"
#include "algoutils.h"
#include "industrial_protocol_debug.h"

#define DEBUG_GLOBAL_MDC_STRING        "CModbusTransModule"
#define FLOAT_REG_WITH_SEMICOLON       "(-?\\d+)(\\.\\d+)?(;{1})?"
#define FLOAT_REG_WITH_COMMA           "(-?\\d+)(\\.\\d+)?(,{1})?"

#define MODBUS_MAX_CONECTION          (6)
#define MODBUS_MAX_HOLDING_REGS       (65535)
#define MODBUS_RESULT_TIMEOUT         (6000)

#ifndef min
#define min(a, b) ((a)<(b)) ? (a) : (b)
#endif

typedef enum
{
	MBC_TRIGGER_ENABLE_BIT = 0,
	MBC_TRIGGER_BIT = 1,
	MBC_RESULT_ACK_BIT = 2,
	MBC_EXCUTE_COMMAND_BIT = 8,
	MBC_CLEAR_ERROR_BIT = 15,
} MODBUS_CONTROL_BITS;

typedef enum
{
	MBS_TRIGGER_READY_BIT = 0,
	MBS_TRIGGER_ACK_BIT = 1,
	MBS_ACQUIRING_BIT = 2,
	MBS_DECODING_BIT = 3,
	MBS_RESULT_OK_BIT = 8,
	MBS_RESULT_NG_BIT = 9,
	MBS_COMMONAND_SUCCESS_BIT = 10,
	MBS_COMMONAND_FAILED_BIT = 11,
	MBS_GENERAL_FAULT_BIT = 15,
} MODBUS_STATUS_BITS;

#define MB_SET_BIT(a, b)   ((a) |= (1 << (b)))
#define MB_CLR_BIT(a, b)   ((a) &= ~(1 << (b)))
#define MB_CHK_BIT(a, b)   ((a) & ((unsigned short)1 << (b)))

static modbus_para_opt *modbus_para;
static modbus_operator modbus_opt;
static char m_szProcedureName[128];
static uint16_t modbus_control_event = 0;
static uint16_t modbus_status_event = 0;
static uint8_t modbus_trigger_step = 1;
static uint8_t modbus_trigger_exit = 1;
static uint8_t modbus_waiting_result = 0;
static uint16_t modbus_userdata_addr = 500;
static uint16_t modbus_userdata_quantity = 100;
static int m_nLogId = 0;
static int m_nFrame = 0;
static int m_nTrigger = 0;

static int msg_initialized = 0;
static int modbus_process = 0;
static int modus_stack_running = 0;
static int modbus_process_start = 0;
static int modbus_algo_deinit = 0;

enum
{
	MODBUS_DEBUG_LEVEL_OFF = IND_PROTO_DEBUG_LEVEL_OFF,
	MODBUS_DEBUG_LEVEL_ERROR = IND_PROTO_DEBUG_LEVEL_ERROR,
	MODBUS_DEBUG_LEVEL_TRACE = IND_PROTO_DEBUG_LEVEL_TRACE,
	MODBUS_DEBUG_LEVEL_TRACE_HEARTBEAT = IND_PROTO_DEBUG_LEVEL_TRACE_HEARTBEAT,
};

#define MODBUS_DEBUG_HEARTBEAT_MS (2000)

static const ind_proto_debug_bit_t g_modbus_control_bits[] =
{
	{MBC_TRIGGER_ENABLE_BIT, "TRIG_EN"},
	{MBC_TRIGGER_BIT, "TRIG"},
	{MBC_RESULT_ACK_BIT, "RESULT_ACK"},
	{MBC_EXCUTE_COMMAND_BIT, "EXEC_CMD"},
	{MBC_CLEAR_ERROR_BIT, "CLR_ERR"},
};

static const ind_proto_debug_bit_t g_modbus_status_bits[] =
{
	{MBS_TRIGGER_READY_BIT, "READY"},
	{MBS_TRIGGER_ACK_BIT, "ACK"},
	{MBS_ACQUIRING_BIT, "ACQ"},
	{MBS_DECODING_BIT, "DEC"},
	{MBS_RESULT_OK_BIT, "OK"},
	{MBS_RESULT_NG_BIT, "NG"},
	{MBS_COMMONAND_SUCCESS_BIT, "CMD_OK"},
	{MBS_COMMONAND_FAILED_BIT, "CMD_NG"},
	{MBS_GENERAL_FAULT_BIT, "FAULT"},
};

static ind_proto_debug_ctx_t g_modbus_debug_ctx;
static int g_modbus_debug_inited = 0;

extern "C" int32_t scfw_is_commtrigger_string(const char* str);
extern "C" int32_t scfw_make_testament(struct reg_service_info *testament_info);

static uint64_t modbus_debug_now_ms(void)
{
	return ind_proto_debug_now_ms();
}

static void modbus_debug_log_cb(const char *msg, void *user_data)
{
	(void)user_data;
	LOGI("%s\r\n", msg);
}

static void modbus_debug_init_once(void)
{
	if (g_modbus_debug_inited)
	{
		return;
	}

	if (ind_proto_debug_init(&g_modbus_debug_ctx, "MB",
		g_modbus_control_bits, sizeof(g_modbus_control_bits) / sizeof(g_modbus_control_bits[0]),
		g_modbus_status_bits, sizeof(g_modbus_status_bits) / sizeof(g_modbus_status_bits[0]),
		MODBUS_DEBUG_HEARTBEAT_MS,
		modbus_debug_log_cb, NULL) == 0)
	{
		g_modbus_debug_inited = 1;
	}
}

static void modbus_debug_record_event(const char *reason, int32_t elapsed_ms, int force_log)
{
	ind_proto_debug_state_t state = {0};

	if (!g_modbus_debug_inited)
	{
		return;
	}

	state.work_mode = (uint8_t)modbus_opt.work_mode;
	state.step = modbus_trigger_step;
	state.waiting_result = modbus_waiting_result;
	state.control_event = modbus_control_event;
	state.status_event = modbus_status_event;
	state.elapsed_ms = elapsed_ms;
	state.reason = reason;
	ind_proto_debug_record(&g_modbus_debug_ctx, &state, force_log);
}

static void modbus_debug_record_if_changed(uint16_t prev_ctrl, uint16_t prev_status,
	uint8_t prev_step, uint8_t prev_waiting, int32_t elapsed_ms, const char *reason)
{
	ind_proto_debug_state_t prev_state = {0};
	ind_proto_debug_state_t curr_state = {0};

	if (!g_modbus_debug_inited)
	{
		return;
	}

	prev_state.work_mode = (uint8_t)modbus_opt.work_mode;
	prev_state.step = prev_step;
	prev_state.waiting_result = prev_waiting;
	prev_state.control_event = prev_ctrl;
	prev_state.status_event = prev_status;
	prev_state.elapsed_ms = elapsed_ms;
	prev_state.reason = reason;

	curr_state.work_mode = (uint8_t)modbus_opt.work_mode;
	curr_state.step = modbus_trigger_step;
	curr_state.waiting_result = modbus_waiting_result;
	curr_state.control_event = modbus_control_event;
	curr_state.status_event = modbus_status_event;
	curr_state.elapsed_ms = elapsed_ms;
	curr_state.reason = reason;

	ind_proto_debug_record_if_changed(&g_modbus_debug_ctx, &prev_state, &curr_state);
}

int modbus_set_slaveId(void)
{
	modbus_opt.slave_id = modbus_para->iSlaveId;
	return cfg_lib_modbus_slave_id(modbus_opt.slave_id);
}
int modbus_get_ipaddr(char *ifname, unsigned int *ip)
{
	struct ifreq ifr;
	struct sockaddr_in *ptr;
	struct in_addr addr_temp;
	int s;
	
	if ((NULL == ifname) || (NULL == ip))
	{
		return -1;
	}
	
	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) 
	{
		return -1;
	}

    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
	
	if (ioctl(s, SIOCGIFADDR, &ifr) < 0) 
	{
		LOGE("get_ipaddr ioctl error and errno=%d\n", errno);
		close(s);
		return -1;
	}
	
	ptr = (struct sockaddr_in *)&ifr.ifr_ifru.ifru_netmask;
	addr_temp = ptr->sin_addr;
	*ip = ntohl(addr_temp.s_addr);
	close(s);
	
	return 0;
}

static uint16_t get_ushort_from_message_ni(uint8_t **buffer_address)
{
	const uint8_t *buffer = *buffer_address;
	uint16_t data = buffer[0] | buffer[1] << 8;
	return data;
}

static int32_t add_ushort_to_message_ni(uint16_t data, uint8_t **buffer) 
{
	uint8_t *p = (uint8_t *) *buffer;
	p[0] = (uint8_t) data;
	p[1] = (uint8_t) (data >> 8);
	return 2;
}

static int32_t add_ushort_to_message(uint16_t data, uint8_t **buffer) 
{
	uint8_t *p = (uint8_t *) *buffer;
	p[0] = (uint8_t) data;
	p[1] = (uint8_t) (data >> 8);
	*buffer += 2;
	return 2;
}

static int32_t add_short_p_to_message(char *data, uint8_t **buffer)
{
	uint8_t *p = (uint8_t *) *buffer;
	p[1] = (uint8_t) *(data);
	data++;
	p[0] = (uint8_t) *(data);
	*buffer += 2;
	return 2;
}

static int32_t add_short_to_message_x(char *data, uint8_t **buffer)
{
	uint8_t *p = (uint8_t *) *buffer;
	p[0] = (uint8_t) *(data);
	data++;
	p[1] = (uint8_t) *(data);
	*buffer += 2;
	return 2;
}

static void modbus_get_uptime(struct timeval *tv)
{
	gettimeofday(tv,NULL);
}

static uint32_t modbus_timeval_elapsed(struct timeval *now, struct timeval *old)
{
	uint32_t diff = 0;
	
	if ((NULL == now) || (NULL == old))
	{
		LOGE("%s: NULL pointer\r\n", __FUNCTION__);
		return (uint32_t) -1;
	}
	
	if (now->tv_sec < old->tv_sec)
	{
		LOGE("%s: Invalid param\r\n", __FUNCTION__);
		return (uint32_t) -1;
	}
	
	if (now->tv_usec >= old->tv_usec)
	{
		diff = (now->tv_sec - old->tv_sec) * 1000000 + (now->tv_usec - old->tv_usec);
	}
	else
	{
		if (now->tv_sec == old->tv_sec)
		{
			diff = 1000000 + now->tv_usec - old->tv_usec;
		}
		else
		{
			diff = (now->tv_sec - old->tv_sec - 1) * 1000000 + (1000000 + now->tv_usec - old->tv_usec);
		}
	}
	
	return diff / 1000;
}

static int modbus_trigger_once(void) 
{
	return CAlgoUtils::IndustrialProtocolTriggerOnce();
}

int modbus_set_input_addr(int addr)
{
	modbus_opt.result_addr = addr;
	return 0;
}

int modbus_set_control_addr(int addr)
{
	modbus_opt.ctrl_addr = addr;
	return 0;
}

int modbus_set_status_addr(int addr)
{
	modbus_opt.status_addr = addr;
	return 0;
}

int modbus_set_input_size(int reg_num)
{
	modbus_opt.result_quantity = reg_num;
	return 0;
}

int modbus_set_output_addr(int addr)
{
	modbus_userdata_addr = addr;
	return 0;
}

int modbus_set_output_size(int reg_num)
{
	modbus_userdata_quantity = reg_num;
	return 0;
}

int modbus_set_debug_level(int level)
{
	int new_level = MODBUS_DEBUG_LEVEL_OFF;

	modbus_debug_init_once();
	if (!g_modbus_debug_inited)
	{
		return IMVS_EC_SYSTEM_INNER_ERR;
	}

	new_level = ind_proto_debug_set_level(&g_modbus_debug_ctx, level);
	LOGI("modbus debug level set to %d\r\n", new_level);
	return IMVS_EC_OK;
}

int modbus_get_debug_level(void)
{
	if (!g_modbus_debug_inited)
	{
		return MODBUS_DEBUG_LEVEL_OFF;
	}
	return ind_proto_debug_get_level(&g_modbus_debug_ctx);
}

int modbus_get_debug_info(char *buff, int buff_size, int *data_len)
{
	if (buff == NULL || buff_size <= 0 || data_len == NULL)
	{
		return IMVS_EC_PARAM;
	}

	modbus_debug_init_once();
	if (!g_modbus_debug_inited)
	{
		return IMVS_EC_SYSTEM_INNER_ERR;
	}
	return (ind_proto_debug_dump(&g_modbus_debug_ctx, buff, buff_size, data_len, IND_PROTO_DEBUG_DUMP_MAX_DEFAULT) == 0)
		? IMVS_EC_OK : IMVS_EC_SYSTEM_INNER_ERR;
}

int modbus_deinit(void)
{
	modbus_algo_deinit = 1;  //算子释放信号
	while (!modbus_trigger_exit)   //触发线程退出
	{
		usleep(10000);
	}

	modbus_opt.deinit = 1;  //重连没有连上就不会初始化,强制置位，等触发函数结束再释放底层资源

	while (!modbus_opt.modbus_exit)  //底层库退出
	{
		usleep(10000);
	}

	while (modus_stack_running)  //modbus_stack_process重连线程退出
	{
		usleep(10000);
	}

	modbus_process = 0;  //modbus_task线程标志位

	while (modbus_process_start)  //modbus_task退出
	{
		usleep(10000);
	}

	deinit_lib_modbus();  //释放底层库

	msg_initialized = 0;  //重置初始化标志位
	LOGI("modbus_msg_deinit: Modbus deinitialized, msg_initialized flag reset.\n");

	return 0;
}

int set_procedure_name(IN const char* szProcedureName)
{
	snprintf(m_szProcedureName, sizeof(m_szProcedureName), "%s", szProcedureName);
	return 0;
}

void set_frame_and_trigger(int nFrame, int nTrigger, int nLogId)
{
	m_nLogId = nLogId;
	m_nFrame = nFrame;
	m_nTrigger = nTrigger;
}

int transform_string(char *buf, const char * pattern, char *trans_buf, int *match_num)
{
	if (NULL == buf || NULL == pattern || NULL == trans_buf || 0 == strlen(buf))
	{
		return -1;
	}

    try {
        std::regex rgx(pattern);
        std::cmatch cm;
        const char *s = buf;
        int n = 0;
        while (std::regex_search(s, cm, rgx)) {
            float f = std::stof(cm.str());
			memcpy(trans_buf+n*4, &f, sizeof f); /* there is a portability issue */
            s += cm.position() + cm.length();
            n++;
			LOGI("n:%d buf:%s f:%lf trans_buf:%s\n", n, buf, f, trans_buf);
        }
		*match_num = n;
    } catch(const std::regex_error& e) {
        LOGE("regex_error caught: %s\n", e.what());
        return -2;
    } catch(const std::invalid_argument& e) {
        LOGE("invalid_argument caught: %s\n", e.what());
        return -3;
    } catch(const std::out_of_range& e) {
        LOGE("out_of_range caught: %s\n", e.what());
        return -4;
    }

	return 0;
}

int character_sequence_handle_for_modbus(int type, char*console_buffer, int nMatchCnt)
{
	int32_t i = 0;
	char  temp_data = 0;
	char  temp_data1= 0;

	switch(type)
	{
		case ORDER_CDAB: 
			for (i = 0; i < nMatchCnt; i++)
			{
				temp_data = console_buffer[i*4];
				console_buffer[i*4] = console_buffer[i*4+1];
				console_buffer[i*4+1] = temp_data;
				temp_data = console_buffer[i*4+2];
				console_buffer[i*4+2] = console_buffer[i*4+3];
				console_buffer[i*4+3] = temp_data;
			}
			break;

		case ORDER_DCBA: 
			break;

		case ORDER_BADC: 
			for (i = 0; i < nMatchCnt; i++)
			{
				temp_data = console_buffer[i*4];
				temp_data1 = console_buffer[i*4+1];
				console_buffer[i*4] = console_buffer[i*4+2];
				console_buffer[i*4+1] = console_buffer[i*4+3];
				console_buffer[i*4+2] = temp_data;
				console_buffer[i*4+3] = temp_data1;
			}
			break;

		case ORDER_ABCD: 
			for (i = 0; i < nMatchCnt; i++)
			{
				temp_data = console_buffer[i*4];
				temp_data1 = console_buffer[i*4+1];
				console_buffer[i*4] = console_buffer[i*4+3];
				console_buffer[i*4+1] = console_buffer[i*4+2];
				console_buffer[i*4+2] = temp_data1;
				console_buffer[i*4+3] = temp_data;
			}
			break;
		
		default:
			break;
	}

	return 0;
}

int modbus_send_result(char *result_ptr, int result_len)
{
	char strRexbuf[MAX_MODBUS_REGULAR_LEN] = {0};
	char strTransBuf[MAX_MODBUS_PAYLOAD_LEN] = {0};
	uint16_t tmp_result_buf[MAX_MODBUS_PAYLOAD_LEN / 2] = {0};
	int32_t nMatchCnt = 0;
	uint8_t *status_buf_addr = NULL;
	uint8_t *result_buf_addr = NULL;
	int32_t ret = 0;
	int32_t i = 0;
	int32_t write_cnt = 0;
	int32_t write_rem = 0;
	LOGI("work_mode:%d trigger_cnt: %d result_len %d, recv msg %s\r\n", modbus_opt.work_mode, m_nTrigger, result_len, result_ptr);

	if (MODBUS_SERVER_MODE == modbus_opt.work_mode)
	{
		status_buf_addr = (uint8_t *)get_modbus_buffer_addr_space(ADDR_SPACE_HOLDING_REGISTER) + modbus_opt.status_addr * 2;
		result_buf_addr = (uint8_t *)get_modbus_buffer_addr_space(ADDR_SPACE_HOLDING_REGISTER) + modbus_opt.result_addr * 2;
		if ((status_buf_addr == NULL)
			|| (result_buf_addr == NULL))
		{
			return -1;
		}
		if ((result_ptr != NULL) && (result_len > 0))
		{
			memset(result_buf_addr, 0x0, modbus_opt.result_quantity * 2);
			
			if (result_len > modbus_opt.result_quantity * 2 - 2)
			{
				result_len = modbus_opt.result_quantity * 2 - 2;
			}
			
			if ((modbus_para != NULL)
				&& modbus_para->iByteOrderEnable)
			{
				if (SEMICOLON == modbus_para->iSpacer)
				{
					snprintf(strRexbuf, MAX_MODBUS_REGULAR_LEN, "%s", FLOAT_REG_WITH_SEMICOLON);
				}
				else if(COMMA == modbus_para->iSpacer)
				{
					snprintf(strRexbuf, MAX_MODBUS_REGULAR_LEN, "%s", FLOAT_REG_WITH_COMMA);
				}
				
				if ((ret = transform_string(result_ptr, strRexbuf, strTransBuf, &nMatchCnt)) != 0)
				{
					LOGE("transform err : %d\n", ret);
					return ret;
				}
				
				character_sequence_handle_for_modbus(modbus_para->iByteOrder, strTransBuf, nMatchCnt);
				
				result_len = nMatchCnt * sizeof(float);
				add_ushort_to_message((uint16_t)result_len, &result_buf_addr);

				LOGI("result_len:%d nMatchCnt:%d buf:%s\n", result_len, nMatchCnt, strTransBuf);

				for (i = 0; i < result_len; i += 2)
				{
					add_short_p_to_message(&strTransBuf[i], &result_buf_addr);
				}
			}
			else
			{
				LOGI("work_mode:%d trigger_cnt: %d result_len %d, recv msg %s\r\n", modbus_opt.work_mode, m_nTrigger, result_len, result_ptr);
				add_ushort_to_message((uint16_t)result_len, &result_buf_addr);
				for (i = 0; i < result_len; i += 2)
				{
					add_short_to_message_x(&result_ptr[i], &result_buf_addr);
				}
			}
			
			if (modbus_waiting_result)
			{
				modbus_waiting_result = 0;
				MB_SET_BIT(modbus_status_event, MBS_RESULT_OK_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_DECODING_BIT);
				add_ushort_to_message_ni(modbus_status_event, &status_buf_addr);
			}
		}
		else
		{
			if (modbus_waiting_result)
			{
				modbus_waiting_result = 0;
				MB_SET_BIT(modbus_status_event, MBS_RESULT_NG_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_DECODING_BIT);
				add_ushort_to_message_ni(modbus_status_event, &status_buf_addr);
			}
		}
	}
	else if (MODBUS_CLIENT_MODE == modbus_opt.work_mode)
	{
		if ((result_ptr != NULL) && (result_len > 0))
		{
			memset(tmp_result_buf, 0x0, sizeof(tmp_result_buf));
			
			if (result_len > modbus_opt.result_quantity * 2 - 2)
			{
				result_len = modbus_opt.result_quantity * 2 - 2;
			}
			
			if ((modbus_para != NULL)
				&& modbus_para->iByteOrderEnable)
			{
				if (SEMICOLON == modbus_para->iSpacer)
				{
					snprintf(strRexbuf, MAX_MODBUS_REGULAR_LEN, "%s", FLOAT_REG_WITH_SEMICOLON);
				}
				else if(COMMA == modbus_para->iSpacer)
				{
					snprintf(strRexbuf, MAX_MODBUS_REGULAR_LEN, "%s", FLOAT_REG_WITH_COMMA);
				}
				
				if ((ret = transform_string(result_ptr, strRexbuf, strTransBuf, &nMatchCnt)) != 0)
				{
					LOGE("transform err : %d\n", ret);
					return ret;
				}
				
				character_sequence_handle_for_modbus(modbus_para->iByteOrder, strTransBuf, nMatchCnt);
				
				result_len = nMatchCnt * sizeof(float);
				lib_modbus_write_registers(modbus_opt.result_addr, 1, (uint16_t *)&result_len);
				for (i = 0; i < result_len; i += 2)
				{
					tmp_result_buf[i/2] = strTransBuf[i + 1] | (strTransBuf[i] << 8);
				}
			}
			else
			{
				ret = lib_modbus_write_registers(modbus_opt.result_addr, 1, (uint16_t *)&result_len);
				if (ret < 0)
				{
					LOGE("[%s]%d ret %d\r\n", __func__, __LINE__, ret);
				}
				for (i = 0; i < result_len; i += 2)
				{
					tmp_result_buf[i/2] = result_ptr[i] | (result_ptr[i + 1] << 8);
					
				}
			}

			write_cnt = (result_len % 2 ? result_len / 2 + 1 : result_len / 2) / MODBUS_ONCE_WRIE_MAX_REG;
			write_rem = (result_len % 2 ? result_len / 2 + 1 : result_len / 2) % MODBUS_ONCE_WRIE_MAX_REG;

			for (i = 0; i < write_cnt; i++)
			{
				ret = lib_modbus_write_registers(modbus_opt.result_addr + 1 + i * MODBUS_ONCE_WRIE_MAX_REG,
					MODBUS_ONCE_WRIE_MAX_REG, tmp_result_buf + i * MODBUS_ONCE_WRIE_MAX_REG);
				if (ret < 0)
				{
					LOGE("[%s]%d ret %d\r\n", __func__, __LINE__, ret);
				}
			}
			if(write_rem)
			{
				ret = lib_modbus_write_registers(modbus_opt.result_addr + 1 + write_cnt * MODBUS_ONCE_WRIE_MAX_REG,
					write_rem, tmp_result_buf + write_cnt * MODBUS_ONCE_WRIE_MAX_REG);
				if (ret < 0)
				{
					LOGE("[%s]%d ret %d\r\n", __func__, __LINE__, ret);
				}
			}
			if (modbus_waiting_result)
			{
				modbus_waiting_result = 0;
				MB_SET_BIT(modbus_status_event, MBS_RESULT_OK_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_DECODING_BIT);
				lib_modbus_write_registers(modbus_opt.status_addr, 1, (uint16_t *)&modbus_status_event);
			}
		}
		else if (0 == result_len)
		{
			memset(tmp_result_buf, 0x0, sizeof(tmp_result_buf));
			ret = lib_modbus_write_registers(modbus_opt.result_addr, 1, (uint16_t *)&result_len);
			if (ret < 0)
			{
				LOGE("[%s]%d ret %d\r\n", __func__, __LINE__, ret);
			}
			write_cnt = (result_len % 2 ? result_len / 2 + 1 : result_len / 2) / MODBUS_ONCE_WRIE_MAX_REG;
			write_rem = (result_len % 2 ? result_len / 2 + 1 : result_len / 2) % MODBUS_ONCE_WRIE_MAX_REG;

			for (i = 0; i < write_cnt; i++)
			{
				ret = lib_modbus_write_registers(modbus_opt.result_addr + 1 + i * MODBUS_ONCE_WRIE_MAX_REG,
					MODBUS_ONCE_WRIE_MAX_REG, tmp_result_buf + i * MODBUS_ONCE_WRIE_MAX_REG);
				if (ret < 0)
				{
					LOGE("[%s]%d ret %d\r\n", __func__, __LINE__, ret);
				}
			}
			if(write_rem)
			{
				ret = lib_modbus_write_registers(modbus_opt.result_addr + 1 + write_cnt * MODBUS_ONCE_WRIE_MAX_REG,
					write_rem, tmp_result_buf + write_cnt * MODBUS_ONCE_WRIE_MAX_REG);
				if (ret < 0)
				{
					LOGE("[%s]%d ret %d\r\n", __func__, __LINE__, ret);
				}
			}
			
			if (modbus_waiting_result)
			{
				modbus_waiting_result = 0;
				MB_SET_BIT(modbus_status_event, MBS_RESULT_NG_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
				MB_CLR_BIT(modbus_status_event, MBS_DECODING_BIT);
				lib_modbus_write_registers(modbus_opt.status_addr, 1, (uint16_t *)&modbus_status_event);
			}
		}
		else
		{
			return -1;
		}
	}
	else
	{
		//not handle
	}
	
	return 0;
}

int modbus_write_registers_callback(void)
{
	uint8_t *control_buf_addr = NULL;
	int ret = IMVS_EC_OK;
	
	control_buf_addr = (uint8_t *)get_modbus_buffer_addr_space(ADDR_SPACE_HOLDING_REGISTER) + modbus_opt.ctrl_addr * 2;
	if (control_buf_addr != NULL)
	{
		if (modbus_para->iModuleEnable)
		{
			modbus_control_event = get_ushort_from_message_ni(&control_buf_addr);
		}
	}
	else
	{
		ret = IMVS_EC_NULL_PTR;
	}
	
	return ret;
}

static void *modbus_trigger_process(void *args)
{
	static struct timeval trigger_time_now, trigger_time_oldtm;
	int32_t trigger_elapsed_ms = 0;
	uint8_t *status_buf_addr = NULL;
	uint8_t *result_buf_addr = NULL;
	uint8_t *userdata_buff_size_addr = NULL;
	uint8_t *userdata_buff_addr = NULL;
	int last_command_excuted = 1;
	int clear_error_excuted = 0;
	uint16_t userdata_size = 0;
	int ret = 0;
	uint16_t prev_control_event = 0;
	uint16_t prev_status_event = 0;
	uint8_t prev_trigger_step = 0;
	uint8_t prev_waiting_result = 0;
	uint64_t last_heartbeat_ms = 0;
	char thread_name[16] = {0};
	if (modbus_opt.work_mode)
	{
		snprintf(thread_name, sizeof(thread_name), "modbus_client");
		prctl(PR_SET_NAME, thread_name, 0, 0, 0);
	}
	else
	{
		snprintf(thread_name, sizeof(thread_name), "modbus_sever");
		prctl(PR_SET_NAME, thread_name, 0, 0, 0);
	}
	
	modbus_trigger_exit = 0;
	modbus_debug_record_event("thread_start", 0, 1);

	while (!modbus_algo_deinit) //仅在算子销毁时退出，适配client重连，否则重连后无法收发消息
	{
		prev_control_event = modbus_control_event;
		prev_status_event = modbus_status_event;
		prev_trigger_step = modbus_trigger_step;
		prev_waiting_result = modbus_waiting_result;

		if (!modbus_opt.inited || !modbus_para->iModuleEnable)
		{
			usleep(100000);
			continue;
		}
								
		if (MODBUS_SERVER_MODE == modbus_opt.work_mode)
		{
			result_buf_addr = (uint8_t *)get_modbus_buffer_addr_space(ADDR_SPACE_HOLDING_REGISTER) + modbus_opt.result_addr * 2;
			status_buf_addr = (uint8_t *)get_modbus_buffer_addr_space(ADDR_SPACE_HOLDING_REGISTER) + modbus_opt.status_addr * 2;
			userdata_buff_size_addr = (uint8_t *)get_modbus_buffer_addr_space(ADDR_SPACE_HOLDING_REGISTER) + modbus_userdata_addr * 2;
			userdata_buff_addr = userdata_buff_size_addr + 2;
			if ((result_buf_addr == NULL) 
				|| (status_buf_addr == NULL) 
				|| (userdata_buff_size_addr == NULL))
			{
				usleep(100000);
				continue;
			}
			
			switch (modbus_trigger_step)
			{
				case 1:
					{
						if (MB_CHK_BIT(modbus_control_event, MBC_TRIGGER_ENABLE_BIT))
						{
							if ((modbus_para != NULL)
								&& (*(modbus_para->sys_run_status) == ALGO_PLAY_CONTINUE))
							{
								//LOGE("PLC send Trigger Enable\r\n");
								if(!CAlgoUtils::IsCommunicationOrSoftwareTrigger())
								{
									LOGI("not support software/communication trigger, please check\r\n");
									break;
								}
									MB_SET_BIT(modbus_status_event, MBS_TRIGGER_READY_BIT);
									add_ushort_to_message_ni(modbus_status_event, &status_buf_addr);
									modbus_trigger_step++;
									modbus_debug_record_event("trigger_enable_ack", trigger_elapsed_ms, 0);
								}
							}
						}
						break;
				case 2:
					{
						if (MB_CHK_BIT(modbus_control_event, MBC_TRIGGER_BIT)
							&& MB_CHK_BIT(modbus_status_event, MBS_TRIGGER_READY_BIT))
						{
							//LOGE("PLC send Trigger\r\n");
							MB_CLR_BIT(modbus_status_event, MBS_TRIGGER_READY_BIT);
							MB_SET_BIT(modbus_status_event, MBS_TRIGGER_ACK_BIT);
							add_ushort_to_message_ni(modbus_status_event, &status_buf_addr);
							
							memset(result_buf_addr, 0x0, modbus_opt.result_quantity * 2);
							
							MB_SET_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
							MB_SET_BIT(modbus_status_event, MBS_DECODING_BIT);
							add_ushort_to_message_ni(modbus_status_event, &status_buf_addr);
							
							modbus_waiting_result = 1;
							modbus_trigger_once();
							modbus_trigger_step++;
							modbus_debug_record_event("trigger_once", trigger_elapsed_ms, 0);
						}
					}
					break;
				case 3:
					{
						if (MB_CHK_BIT(modbus_control_event, MBC_RESULT_ACK_BIT)
							&& (MB_CHK_BIT(modbus_status_event, MBS_RESULT_OK_BIT)
							|| MB_CHK_BIT(modbus_status_event, MBS_RESULT_NG_BIT)))
						{
							//LOGE("PLC send Result Ack\r\n");
							MB_CLR_BIT(modbus_status_event, MBS_TRIGGER_ACK_BIT);
							MB_CLR_BIT(modbus_status_event, MBS_RESULT_OK_BIT);
							MB_CLR_BIT(modbus_status_event, MBS_RESULT_NG_BIT);
							add_ushort_to_message_ni(modbus_status_event, &status_buf_addr);
							modbus_trigger_step = 1;
							modbus_debug_record_event("result_ack", trigger_elapsed_ms, 0);
						}
					}
					break;
				default:
					{
						modbus_trigger_step = 1;
						}
					break;
			}
			
			modbus_get_uptime(&trigger_time_now);
			if (modbus_waiting_result)
			{
				trigger_elapsed_ms = modbus_timeval_elapsed(&trigger_time_now, &trigger_time_oldtm);
				if (trigger_elapsed_ms > MODBUS_RESULT_TIMEOUT)
				{				
					modbus_waiting_result = 0;
					MB_SET_BIT(modbus_status_event, MBS_RESULT_NG_BIT);
					MB_CLR_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
						MB_CLR_BIT(modbus_status_event, MBS_DECODING_BIT);
						add_ushort_to_message_ni(modbus_status_event, &status_buf_addr);
						trigger_time_oldtm = trigger_time_now;
						modbus_debug_record_event("result_timeout", trigger_elapsed_ms, 1);
					}
				}
				else
			{
				trigger_time_oldtm = trigger_time_now;
			}
			
			if (MB_CHK_BIT(modbus_control_event, MBC_EXCUTE_COMMAND_BIT))
			{
				if (last_command_excuted == 0)
				{
					last_command_excuted = 1;
					
					userdata_size = get_ushort_from_message_ni(&userdata_buff_size_addr);
					if (userdata_size > modbus_userdata_quantity * 2)
					{
						userdata_size = modbus_userdata_quantity * 2;
					}
					
					if ((userdata_size > 0)
						&& (userdata_size < 128)
						&& (strlen((char *)userdata_buff_addr) > 0)
						&& (strlen((char *)userdata_buff_addr) < 128))
					{
						std::string result;
						CCommProxy::MessageInfo messageInfo{0};
						messageInfo.moduleId = m_nLogId; //LogId其实和模块Id是一样

						snprintf(messageInfo.msg, sizeof(messageInfo.msg), "%s", (char *)userdata_buff_addr);
						if (strlen((char *)userdata_buff_addr) < userdata_size)
						{
							messageInfo.len = strlen((char *)userdata_buff_addr);
						}
						else
						{
							messageInfo.len = userdata_size;
						}
						messageInfo.msg[messageInfo.len] = 0;

						ret = CCommProxy::getInstance()->SyncRecv(messageInfo, result);
						LOGI("Recv return result %s ret %d \r\n", result.c_str(), ret);
						if (0 == ret)
						{
							MB_SET_BIT(modbus_status_event, MBS_COMMONAND_SUCCESS_BIT);
							add_ushort_to_message(modbus_status_event, &status_buf_addr);
							modbus_debug_record_event("cmd_ok", trigger_elapsed_ms, 0);
						}
						else
						{
							MB_SET_BIT(modbus_status_event, MBS_COMMONAND_FAILED_BIT);
							add_ushort_to_message(modbus_status_event, &status_buf_addr);
							modbus_debug_record_event("cmd_ng", trigger_elapsed_ms, 1);
						}

						ret = modbus_send_result(const_cast<char*>(result.c_str()), result.length());
						if (0 != ret)
						{
							LOGE("modbus_send_result error %d\r\n", ret);
						}
					}
					else
					{
						MB_SET_BIT(modbus_status_event, MBS_COMMONAND_FAILED_BIT);
						add_ushort_to_message(modbus_status_event, &status_buf_addr);
						modbus_debug_record_event("cmd_invalid", trigger_elapsed_ms, 1);
					}
				}
			}
			else
			{
				if (last_command_excuted == 1)
				{
					last_command_excuted = 0;
					MB_CLR_BIT(modbus_status_event, MBS_COMMONAND_SUCCESS_BIT);
					MB_CLR_BIT(modbus_status_event, MBS_COMMONAND_FAILED_BIT);
					add_ushort_to_message(modbus_status_event, &status_buf_addr);
				}
			}
			
			if (MB_CHK_BIT(modbus_control_event, MBC_CLEAR_ERROR_BIT))
			{
				if (clear_error_excuted == 0)
				{
					clear_error_excuted = 1;
					modbus_trigger_step = 1;
					modbus_status_event = 0;				
					modbus_waiting_result = 0;
					add_ushort_to_message(modbus_status_event, &status_buf_addr);
					modbus_debug_record_event("clear_error", trigger_elapsed_ms, 1);
				}
			}
			else
			{
				clear_error_excuted = 0;
			}

			modbus_debug_record_if_changed(prev_control_event, prev_status_event,
				prev_trigger_step, prev_waiting_result, trigger_elapsed_ms, "state_change");
			if (modbus_get_debug_level() >= MODBUS_DEBUG_LEVEL_TRACE_HEARTBEAT)
			{
				uint64_t now_ms = modbus_debug_now_ms();
				if ((last_heartbeat_ms == 0) || (now_ms - last_heartbeat_ms >= MODBUS_DEBUG_HEARTBEAT_MS))
				{
					last_heartbeat_ms = now_ms;
					modbus_debug_record_event("heartbeat", trigger_elapsed_ms, 0);
				}
			}
			
			usleep(10000);
		}
		else if (MODBUS_CLIENT_MODE == modbus_opt.work_mode)
		{
			usleep(modbus_para->iControlPollInterval * 1000);

			ret = lib_modbus_read_registers(modbus_opt.ctrl_addr, 1, &modbus_control_event);
			if (ret < 0)
			{
				usleep(100000);
				continue;
			}
			switch (modbus_trigger_step)
			{
				case 1:
					{
						if (MB_CHK_BIT(modbus_control_event, MBC_TRIGGER_ENABLE_BIT))
						{
							if ((modbus_para != NULL)
								&& (*(modbus_para->sys_run_status) == ALGO_PLAY_CONTINUE))
							{
								MB_SET_BIT(modbus_status_event, MBS_TRIGGER_READY_BIT);
								lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
								modbus_trigger_step++;
								modbus_debug_record_event("trigger_enable_ack", trigger_elapsed_ms, 0);
							}
						}
					}
					break;
				case 2:
					{
						if (MB_CHK_BIT(modbus_control_event, MBC_TRIGGER_BIT)
							&& MB_CHK_BIT(modbus_status_event, MBS_TRIGGER_READY_BIT))
						{
							MB_CLR_BIT(modbus_status_event, MBS_TRIGGER_READY_BIT);
							MB_SET_BIT(modbus_status_event, MBS_TRIGGER_ACK_BIT);
							lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
							
							//memset(result_buf_addr, 0x0, modbus_opt.result_quantity * 2);
							
							MB_SET_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
							MB_SET_BIT(modbus_status_event, MBS_DECODING_BIT);
							lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
							
							modbus_waiting_result = 1;
							modbus_trigger_once();
							modbus_trigger_step++;
							modbus_debug_record_event("trigger_once", trigger_elapsed_ms, 0);
						}
					}
					break;
				case 3:
					{
						if (MB_CHK_BIT(modbus_control_event, MBC_RESULT_ACK_BIT)
							&& (MB_CHK_BIT(modbus_status_event, MBS_RESULT_OK_BIT)
							|| MB_CHK_BIT(modbus_status_event, MBS_RESULT_NG_BIT)))
						{
							MB_CLR_BIT(modbus_status_event, MBS_TRIGGER_ACK_BIT);
							MB_CLR_BIT(modbus_status_event, MBS_RESULT_OK_BIT);
							MB_CLR_BIT(modbus_status_event, MBS_RESULT_NG_BIT);
							lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
							modbus_trigger_step = 1;
							modbus_debug_record_event("result_ack", trigger_elapsed_ms, 0);
						}
					}
					break;
				default:
					{
						modbus_trigger_step = 1;
					}
					break;
			}
			
			modbus_get_uptime(&trigger_time_now);
			if (modbus_waiting_result)
			{
				trigger_elapsed_ms = modbus_timeval_elapsed(&trigger_time_now, &trigger_time_oldtm);
				if (trigger_elapsed_ms > (MODBUS_RESULT_TIMEOUT))
				{				
					modbus_waiting_result = 0;
					MB_SET_BIT(modbus_status_event, MBS_RESULT_NG_BIT);
					MB_CLR_BIT(modbus_status_event, MBS_ACQUIRING_BIT);
						MB_CLR_BIT(modbus_status_event, MBS_DECODING_BIT);
						lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
						trigger_time_oldtm = trigger_time_now;
						modbus_debug_record_event("result_timeout", trigger_elapsed_ms, 1);
					}
				}
				else
			{
				trigger_time_oldtm = trigger_time_now;
			}
			
			if (MB_CHK_BIT(modbus_control_event, MBC_EXCUTE_COMMAND_BIT))
			{
				if (last_command_excuted == 0)
				{
					std::string result;
					CCommProxy::MessageInfo messageInfo{0};
					last_command_excuted = 1;

					ret = lib_modbus_read_registers(modbus_userdata_addr, 1, &userdata_size);
					if (ret < 0 || userdata_size == 0)
					{
						modbus_debug_record_event("cmd_read_size_fail", trigger_elapsed_ms, 1);
						//leep(100000);
						continue;
					}
					if (userdata_size > modbus_userdata_quantity * 2)
					{
						userdata_size = modbus_userdata_quantity * 2;
					}
					userdata_size = min(userdata_size, sizeof(messageInfo.msg)-1);

					ret = lib_modbus_read_registers(modbus_userdata_addr + 1,
					(userdata_size % 2 ? userdata_size / 2 + 1 : userdata_size / 2), (uint16_t *)messageInfo.msg);
					if (ret < 0)
					{
						modbus_debug_record_event("cmd_read_payload_fail", trigger_elapsed_ms, 1);
						//leep(100000);
						continue;
					}

					if ((userdata_size > 0)
						&& (userdata_size < 128)
						&& (strlen(messageInfo.msg) > 0)
						&& (strlen(messageInfo.msg) < 128))
					{
						messageInfo.len = min(strlen(messageInfo.msg), userdata_size);
						messageInfo.msg[messageInfo.len] = 0;
						messageInfo.moduleId = m_nLogId; //LogId其实和模块Id是一样

						ret = CCommProxy::getInstance()->SyncRecv(messageInfo, result);
						LOGI("Recv return result %s ret %d \r\n", result.c_str(), ret);
						if (0 == ret)
						{
							MB_SET_BIT(modbus_status_event, MBS_COMMONAND_SUCCESS_BIT);
							lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
							modbus_debug_record_event("cmd_ok", trigger_elapsed_ms, 0);
						}
						else
						{
							MB_SET_BIT(modbus_status_event, MBS_COMMONAND_FAILED_BIT);
							lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
							modbus_debug_record_event("cmd_ng", trigger_elapsed_ms, 1);
						}

						ret = modbus_send_result(const_cast<char*>(result.c_str()), result.length());
						if (0 != ret)
						{
							LOGE("modbus_send_result error %d\r\n", ret);
						}
					}
					else
					{
						MB_SET_BIT(modbus_status_event, MBS_COMMONAND_FAILED_BIT);
						lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
						modbus_debug_record_event("cmd_invalid", trigger_elapsed_ms, 1);
					}
				}
			}
			else
			{
				if (last_command_excuted == 1)
				{
					last_command_excuted = 0;
					MB_CLR_BIT(modbus_status_event, MBS_COMMONAND_SUCCESS_BIT);
					MB_CLR_BIT(modbus_status_event, MBS_COMMONAND_FAILED_BIT);
					lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
				}
			}
			
			if (MB_CHK_BIT(modbus_control_event, MBC_CLEAR_ERROR_BIT))
			{
				if (clear_error_excuted == 0)
				{
					clear_error_excuted = 1;
					modbus_trigger_step = 1;
					modbus_status_event = 0;
					modbus_waiting_result = 0;
					lib_modbus_write_registers(modbus_opt.status_addr, 1, &modbus_status_event);
					modbus_debug_record_event("clear_error", trigger_elapsed_ms, 1);
				}
			}
			else
			{
				clear_error_excuted = 0;
			}

			modbus_debug_record_if_changed(prev_control_event, prev_status_event,
				prev_trigger_step, prev_waiting_result, trigger_elapsed_ms, "state_change");
			if (modbus_get_debug_level() >= MODBUS_DEBUG_LEVEL_TRACE_HEARTBEAT)
			{
				uint64_t now_ms = modbus_debug_now_ms();
				if ((last_heartbeat_ms == 0) || (now_ms - last_heartbeat_ms >= MODBUS_DEBUG_HEARTBEAT_MS))
				{
					last_heartbeat_ms = now_ms;
					modbus_debug_record_event("heartbeat", trigger_elapsed_ms, 0);
				}
			}
		}
		else
		{
			//not handle
		}
		
	}
	LOGI("Modbus_trigger_process EXIT\r\n");
	modbus_debug_record_event("thread_exit", 0, 1);
	
	modbus_trigger_exit = 1;
	return nullptr;
}

static void modbus_stack_process(void *arg)
{
	modbus_para_opt *para = (modbus_para_opt *)arg;
	char thread_name[32] = {0};
	int ret = 0;
	
	if (para == NULL)
	{
		LOGE("init_lib_modbus param is NULL!\r\n");
		return ;
	}

	snprintf(thread_name, sizeof(thread_name), "modbus_stack_process");
	prctl(PR_SET_NAME, thread_name, 0, 0, 0);
	modus_stack_running = 1;
	
	modbus_para = para;
	modbus_opt.work_mode = modbus_para->iWorkMode;
	modbus_opt.communication_mode = 0;
	modbus_opt.server_ip = modbus_para->iServerIp;
	modbus_opt.server_port = modbus_para->iServerPort;
	modbus_opt.max_connection = MODBUS_MAX_CONECTION;
	modbus_opt.idle_timeout_sec = 1;
	modbus_opt.idle_timeout_usec = 0;
	modbus_opt.max_coil_regs = 0;
	modbus_opt.max_discrete_input_regs = 0;	
	modbus_opt.max_input_regs = 0;
	modbus_opt.max_holding_regs = MODBUS_MAX_HOLDING_REGS;
	modbus_opt.ctrl_addr = modbus_para->iCtrlAddrOffset;
	modbus_opt.status_addr = modbus_para->iStatusAddrOffset;
	modbus_opt.result_addr = modbus_para->iInputAddrOffset;
	modbus_userdata_addr = modbus_para->iOutputAddrOffset;
	modbus_opt.ctrl_quantity = modbus_para->iCtrlAddrQuantity;
	modbus_opt.status_quantity = modbus_para->iStatusAddrQuantity;
	modbus_opt.result_quantity = modbus_para->iInputAddrQuantity;
	modbus_userdata_quantity = modbus_para->iOutputAddrQuantity;
	modbus_opt.app_data_handle_function = modbus_write_registers_callback;
	modbus_opt.deinit = 0;
	modbus_opt.inited = 0;
	modbus_opt.modbus_exit = 0;
	modbus_opt.slave_id = modbus_para->iSlaveId;
		
	init_lib_modbus_params(&modbus_opt);

	while (!modbus_opt.deinit)
	{
		modbus_opt.inited = 0;
		modbus_opt.modbus_exit = 0;
		LOGI("[prt param]: modbus_opt.slave_id  =%d, modbus_opt.server_ip = %d%\r\n", modbus_opt.slave_id, modbus_opt.server_ip);
		ret = init_lib_modbus();
		if (ret < 0)
		{
			LOGI("init_lib_modbus creation ret %d failed!\r\n", ret);
			deinit_lib_modbus();  
			sleep(1);
		}
		LOGI("modbus_opt.deinit = %d \r\n", modbus_opt.deinit);
	}

	while (!modbus_opt.modbus_exit)
	{
		usleep(10 * 1000);
	}
	
	modbus_opt.inited = 0;
	modus_stack_running = 0;
	LOGI("Exit modbus_stack_process\r\n");

}

static void modbus_task(void *arg)
{
	modbus_process_start = 0;
	pthread_t modbus_stack_thread;
	pthread_t modbus_trigger_thread;
	int ret = 0;
	char thread_name[16] = {0};
	snprintf(thread_name, sizeof(thread_name), "modbus_task");
	prctl(PR_SET_NAME, thread_name, 0, 0, 0);
	LOGE("modbus_task created \r\n");
	
	while (1)
	{
		if (modbus_process)
		{
			if (0 == modbus_process_start)
			{
				modbus_process_start = 1;
				ret = thread_spawn_ex(&modbus_stack_thread, 0, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, (start_routine)modbus_stack_process, (void*)arg);
				if (ret != 0)
				{
					LOGE("modbus_stack_process creation ret %d failed!\r\n", ret);
					continue;;
				}
				
				modbus_control_event = 0;
				modbus_status_event = 0;
				modbus_trigger_step = 1;
				modbus_trigger_exit = 1;
				modbus_algo_deinit = 0;

				ret = thread_spawn_ex(&modbus_trigger_thread, 0, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, (start_routine)modbus_trigger_process, NULL);
				if (ret != 0)
				{
					LOGE("modbus_trigger_process creation ret %d failed!\r\n", ret);
					continue;;
				}
			}
		}
		else
		{
			if (1 == modbus_process_start)
			{
				while (!modbus_trigger_exit)
				{
					usleep(10 * 1000);
				}
				
				while (modus_stack_running)
				{
					usleep(10 * 1000);
				}
				break;
			}
		}

		usleep(100 * 1000);
	}
	modbus_process_start = 0;
	LOGI("Exit modbus_task\r\n");
}


int init_modbus_msg(modbus_para_opt *para)
{
	pthread_t modbus_thread;
	int ret = -1;

	modbus_debug_init_once();
	
	if (msg_initialized)
	{
		modbus_process = 1;
		LOGI("init_modbus_msg already msg_initialized\r\n");
		return 0;
	}
	

	ret = thread_spawn_ex(&modbus_thread, 0, SCHED_POLICY_RR, SCHED_PRI_LOW_40, 10 * 1024, (start_routine)modbus_task, (void*)para);
	if (ret != 0)
	{
		LOGE("create modbus_task failed, ret = %d\r\n", ret);
		return -1;
	}
	
	msg_initialized = 1;
	modbus_process = 1;
	LOGI("Quit init_modbus_msg\r\n");
	
	return 0;
}
