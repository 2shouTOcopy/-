/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author  zhengxiaoyu
 * @date    2019/10/28
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2019/10/28  |V1.0.0  |zhengxiaoyu         |创建代码文档
 * @warning 
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/time.h>

#include "ethernetip_msg.h"
#include "utils.h"
#include "util_net.h"
#include "thread/ThreadApi.h"
#include "TriggerParamDef.h"
#include "AppParamCommon.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "algo_common.h"

#include "cip_application.h"
#include "framework_service.h"
#include "log/log.h"
#include "IIspSource.h"
#include "ITriggerSource.h"
#include "IImageProcess.h"
#include "calibrateapiwapper.h"
#include "CapacityApi.h"
#include "algoutils.h"
#include "industrial_protocol_debug.h"

#ifndef min
#define min(a, b) ((a)<(b)) ? (a) : (b)
#endif

#define DEBUG_GLOBAL_MDC_STRING "CEthernetipTransModule"

#define HIKROBOT_DEVICE_VENDOR_ID          (1546)
#define HIKROBOT_DEVICE_TYPE               (101)
#define HIKROBOT_DEVICE_SERIAL_NUMBER      (1234)
#define HIKROBOT_DEVICE_MAJOR_REVISION     (1)
#define HIKROBOT_DEVICE_MINOR_REVISION     (1)
#define HIKROBOT_READER_CLASS_CODE         (0xA6)
#define INPUT_ASSEMBLY_NUM                 (13)
#define OUTPUT_ASSEMBLY_NUM                (22) 
#define CONFIG_ASSEMBLY_NUM                (1)
#define HEARTBEAT_INPUT_ONLY_ASSEMBLY_NUM  (152)
#define HEARTBEAT_LISTEN_ONLY_ASSEMBLY_NUM (153)
#define EXPLICT_ASSEMBLY_NUM               (154)

#define EIP_RESULT_DATA_OFFSET             (18)
#define EIP_RESULT_DATA_LEN_BYTES          (2)
#define EIP_RESULT_DATA_BYTES              (480)
#define EIP_RESULT_INPUT_BUFFER_SIZE       (EIP_RESULT_DATA_LEN_BYTES + EIP_RESULT_DATA_BYTES)
#define EIP_USERDATA_OUTPUT_BUFFER_SIZE	   (EIP_RESULT_DATA_LEN_BYTES + EIP_RESULT_DATA_BYTES) 
#define EIP_ASSEMBLY_INPUT_BUFFER_SIZE     (EIP_RESULT_DATA_OFFSET + EIP_RESULT_INPUT_BUFFER_SIZE)
#define EIP_ASSEMBLY_OUTPUT_BUFFER_SIZE    (EIP_RESULT_DATA_OFFSET + EIP_USERDATA_OUTPUT_BUFFER_SIZE)

#define NETWORK_INTERFACE 	               "eth0"
#define EIP_RESULT_TIMEOUT                 (6000)
#define EIP_DEBUG_HEARTBEAT_MS             (2000)

typedef struct
{
	uint32_t length;
	uint8_t *data;
} cip_byte_array;

typedef enum
{
	EIPC_TRIGGER_ENABLE_BIT = 0,
	EIPC_TRIGGER_BIT = 1,
	EIPC_RESULT_ACK_BIT = 2,
	EIPC_EXCUTE_COMMAND_BIT = 8,
	EIPC_CLEAR_ERROR_BIT = 15,
	EIPC_EQUAL_EXCUTE_COMMAND_BIT = 3,
	EIPC_EQUAL_CLEAR_ERROR_BIT = 7,
} eip_control_bits;

typedef enum
{
	EIPS_TRIGGER_READY_BIT = 0,
	EIPS_TRIGGER_ACK_BIT = 1,
	EIPS_ACQUIRING_BIT = 2,
	EIPS_DECODING_BIT = 3,
	EIPS_RESULT_OK_BIT = 8,
	EIPS_RESULT_NG_BIT = 9,
	EIPS_COMMONAND_SUCCESS_BIT = 10,
	EIPS_COMMONAND_FAILED_BIT = 11,
	EIPS_GENERAL_FAULT_BIT = 15,
} eip_status_bits;

#define EIP_SET_BIT(a, b)   ((a) |= (1 << (b)))
#define EIP_CLR_BIT(a, b)   ((a) &= ~(1 << (b)))
#define EIP_CHK_BIT(a, b)   ((a) & ((unsigned int)1 << (b)))

struct eip_assembly_input_event
{
	int16_t useful_data_length;        /* Byte0~Byte1 */
	int32_t input_event;               /* Byte2~Byte5 */
	int16_t reserved1;                 /* Byte6~Byte7 */
	int16_t reserved2;                 /* Byte8~Byte9 */
	int16_t reserved3;                 /* Byte10~Byte11 */
	int16_t trigger_id;                /* Byte12~Byte13 */
	int16_t result_id;                 /* Byte14~Byte15 */
	int16_t reserved4;                 /* Byte16~Byte17 */
	cip_byte_array input_result_array; /* Byte18~... */
};

struct eip_assembly_output_event
{
	int32_t output_event;              /* Byte0~Byte3 */
	int16_t reserved1;                 /* Byte4~Byte5 */
	int16_t reserved2;                 /* Byte6~Byte7 */
	int16_t reserved3;                 /* Byte8~Byte9 */
	int16_t reserved4;                 /* Byte10~Byte11 */
	int16_t reserved5;                 /* Byte12~Byte13 */
	int16_t reserved6;                 /* Byte14~Byte15 */
	int16_t reserved7;                 /* Byte16~Byte17 */
	cip_byte_array output_result_array;/* Byte18~... */
};

static uint8_t assembly_input_data[EIP_ASSEMBLY_INPUT_BUFFER_SIZE]; /* Input */
static uint8_t assembly_output_data[EIP_ASSEMBLY_OUTPUT_BUFFER_SIZE]; /* Output */
static uint8_t assembly_config_data[10]; /* Config */
static uint8_t assembly_explicit_data[32]; /* Explicit */
static uint8_t assembly_input_heartbeat_data[4]; /* input only */
static uint8_t assembly_listen_heartbeat_data[4]; /* output only */
static struct eip_app_cfg_para cfg_data;
static struct eip_identity_object identity_data;
static struct eip_custom_object custom_data;
static struct eip_application_interface app_interface;

static struct eip_assembly_input_event g_assembly_input;
static struct eip_assembly_output_event g_assembly_output;
static uint8_t g_result_buffer[EIP_RESULT_INPUT_BUFFER_SIZE];
static uint8_t g_userdata_buffer[EIP_USERDATA_OUTPUT_BUFFER_SIZE];
static uint8_t g_trigger_step = 1;
static uint16_t g_input_size = 200;
static uint16_t g_output_size = 200;
static uint16_t g_result_byte_swap = 0;
static uint8_t g_waiting_result = 0;
static uint8_t g_module_enable = 0;

static uint8_t g_explicit_result_buffer[EIP_RESULT_INPUT_BUFFER_SIZE];
static CipByteArray g_explicit_result_array;
static uint8_t g_explicit_trigger_step = 1;
static uint16_t g_explicit_output_status = 0;
static uint16_t g_explicit_input_status = 0;
static uint16_t g_explicit_trigger_id = 0;
static uint16_t g_explicit_result_id = 0;
static uint8_t g_explicit_trigger_running = 0;
static uint8_t g_explicit_trigger_end = 1;
static uint8_t g_explicit_waiting_result = 0;

static uint8_t g_result_raw_buffer[EIP_RESULT_INPUT_BUFFER_SIZE];
static uint16_t g_result_raw_len = 0;
static int last_command_excuted = 1;

static ind_proto_debug_ctx_t g_eip_debug_ctx;
static int g_eip_debug_inited = 0;

enum
{
	EIP_DEBUG_WORK_MODE_IMPLICIT = 0,
	EIP_DEBUG_WORK_MODE_EXPLICIT = 1,
};

static const ind_proto_debug_bit_t g_eip_control_bits[] =
{
	{EIPC_TRIGGER_ENABLE_BIT, "TRIG_EN"},
	{EIPC_TRIGGER_BIT, "TRIG"},
	{EIPC_RESULT_ACK_BIT, "RESULT_ACK"},
	{EIPC_EXCUTE_COMMAND_BIT, "EXEC_CMD"},
	{EIPC_EQUAL_EXCUTE_COMMAND_BIT, "EXEC_CMD_EQ"},
	{EIPC_EQUAL_CLEAR_ERROR_BIT, "CLR_ERR_EQ"},
	{EIPC_CLEAR_ERROR_BIT, "CLR_ERR"},
};

static const ind_proto_debug_bit_t g_eip_status_bits[] =
{
	{EIPS_TRIGGER_READY_BIT, "READY"},
	{EIPS_TRIGGER_ACK_BIT, "ACK"},
	{EIPS_ACQUIRING_BIT, "ACQ"},
	{EIPS_DECODING_BIT, "DEC"},
	{EIPS_RESULT_OK_BIT, "OK"},
	{EIPS_RESULT_NG_BIT, "NG"},
	{EIPS_COMMONAND_SUCCESS_BIT, "CMD_OK"},
	{EIPS_COMMONAND_FAILED_BIT, "CMD_NG"},
	{EIPS_GENERAL_FAULT_BIT, "FAULT"},
};

eALGO_PLAYCTRL *sys_run_status;

static char m_szProcedureName[128];
static int m_nLogId = 0;
static int m_nFrame = 0;
static int m_nTrigger = 0;

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

static void eip_debug_log_cb(const char *msg, void *user_data)
{
	(void)user_data;
	LOGI("%s\r\n", msg);
}

static void eip_debug_init_once(void)
{
	if (g_eip_debug_inited)
	{
		return;
	}

	if (ind_proto_debug_init(&g_eip_debug_ctx, "EIP",
		g_eip_control_bits, sizeof(g_eip_control_bits) / sizeof(g_eip_control_bits[0]),
		g_eip_status_bits, sizeof(g_eip_status_bits) / sizeof(g_eip_status_bits[0]),
		EIP_DEBUG_HEARTBEAT_MS,
		eip_debug_log_cb, NULL) == 0)
	{
		g_eip_debug_inited = 1;
	}
}

static void eip_debug_fill_state(uint8_t work_mode, uint8_t step, uint8_t waiting_result,
	uint16_t control_event, uint16_t status_event, int32_t elapsed_ms, const char *reason,
	ind_proto_debug_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	state->work_mode = work_mode;
	state->step = step;
	state->waiting_result = waiting_result;
	state->control_event = control_event;
	state->status_event = status_event;
	state->elapsed_ms = elapsed_ms;
	state->reason = reason;
}

static void eip_debug_record(uint8_t work_mode, uint8_t step, uint8_t waiting_result,
	uint16_t control_event, uint16_t status_event, int32_t elapsed_ms, const char *reason, int force_log)
{
	ind_proto_debug_state_t state = {0};
	if (!g_eip_debug_inited)
	{
		return;
	}
	eip_debug_fill_state(work_mode, step, waiting_result, control_event, status_event, elapsed_ms, reason, &state);
	ind_proto_debug_record(&g_eip_debug_ctx, &state, force_log);
}

static void eip_debug_record_if_changed(uint8_t work_mode,
	uint8_t prev_step, uint8_t curr_step, uint8_t prev_waiting, uint8_t curr_waiting,
	uint16_t prev_control_event, uint16_t curr_control_event,
	uint16_t prev_status_event, uint16_t curr_status_event, int32_t elapsed_ms)
{
	ind_proto_debug_state_t prev_state = {0};
	ind_proto_debug_state_t curr_state = {0};

	if (!g_eip_debug_inited)
	{
		return;
	}

	eip_debug_fill_state(work_mode, prev_step, prev_waiting, prev_control_event, prev_status_event,
		elapsed_ms, "state_change", &prev_state);
	eip_debug_fill_state(work_mode, curr_step, curr_waiting, curr_control_event, curr_status_event,
		elapsed_ms, "state_change", &curr_state);
	ind_proto_debug_record_if_changed(&g_eip_debug_ctx, &prev_state, &curr_state);
}

static void eip_debug_record_heartbeat(uint8_t work_mode, uint8_t step, uint8_t waiting_result,
	uint16_t control_event, uint16_t status_event, int32_t elapsed_ms)
{
	ind_proto_debug_state_t state = {0};
	if (!g_eip_debug_inited)
	{
		return;
	}
	eip_debug_fill_state(work_mode, step, waiting_result, control_event, status_event, elapsed_ms, "heartbeat", &state);
	ind_proto_debug_record_heartbeat(&g_eip_debug_ctx, &state);
}

static void move_message_n_bytes(int32_t amount_of_bytes_moved, uint8_t **message_runner)
{
	(*message_runner) += amount_of_bytes_moved;
}

static int32_t add_short_to_message(uint16_t data, uint8_t **buffer) 
{
	uint8_t *p = (uint8_t *) *buffer;
	p[0] = (uint8_t) data;
	p[1] = (uint8_t) (data >> 8);
	*buffer += 2;
	return 2;
}

static int32_t add_int_to_message( uint32_t data, uint8_t **buffer)
{
	uint8_t *p = (uint8_t *) *buffer;
	p[0] = (uint8_t) data;
	p[1] = (uint8_t) (data >> 8);
	p[2] = (uint8_t) (data >> 16);
	p[3] = (uint8_t) (data >> 24);
	*buffer += 4;
	return 4;
}

static uint16_t get_ushort_from_message(uint8_t **buffer_address)
{
	uint8_t *buffer = *buffer_address;
	uint16_t data = buffer[0] | buffer[1] << 8;
	*buffer_address += 2;
	return data;
}

static uint32_t get_uint_from_message(uint8_t **buffer_address) 
{
 	uint8_t *buffer = *buffer_address;
	uint32_t data = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
	*buffer_address += 4;
	return data;
}

static void eip_get_uptime(struct timeval *tv)
{
	gettimeofday(tv,NULL);
}

static uint32_t eip_timeval_elapsed(struct timeval *now, struct timeval *old)
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

static int32_t reader_start_read(void) 
{
	return 0;
}

static int32_t reader_stop_read(void)
{
	return 0;
}

static int32_t eip_set_explicit_output_status(int16_t output_status)
{
	g_explicit_output_status = output_status;
	return IMVS_EC_OK;
}

static int eip_trigger_once(void) 
{
	return CAlgoUtils::IndustrialProtocolTriggerOnce();
}

int32_t eip_explicit_trigger_once(void)
{
	eip_trigger_once();
	return 0;
}

static void eip_update_input_data(void)
{
	uint8_t *result_buffer = g_result_raw_buffer;
	uint16_t result_len = g_result_raw_len;
	uint8_t *assembly_data = assembly_input_data;
	uint8_t *g_result_data = g_result_buffer;
	int i = 0;
	
	memset(g_result_buffer, 0, sizeof(g_result_buffer));
	memset(assembly_input_data, 0, sizeof(assembly_input_data));
	
	g_assembly_input.input_result_array.length = EIP_RESULT_DATA_LEN_BYTES + result_len;
	g_assembly_input.useful_data_length = EIP_RESULT_DATA_OFFSET + EIP_RESULT_DATA_LEN_BYTES + result_len;
	add_short_to_message(g_assembly_input.useful_data_length, &assembly_data);
	add_int_to_message(g_assembly_input.input_event, &assembly_data);
	add_short_to_message(g_assembly_input.reserved1, &assembly_data);
	add_short_to_message(g_assembly_input.reserved2, &assembly_data);
	add_short_to_message(g_assembly_input.reserved3, &assembly_data);
	add_short_to_message(g_assembly_input.trigger_id, &assembly_data);
	add_short_to_message(g_assembly_input.result_id, &assembly_data);
	add_short_to_message(g_assembly_input.reserved4, &assembly_data);
	
	if ((result_buffer != NULL) && (result_len > 0))
	{
		add_short_to_message(result_len, &assembly_data);
		add_short_to_message(result_len, &g_result_data);
		
		for (i = 0; i < result_len; i += 2)
		{
			if (g_result_byte_swap)
			{
				add_short_to_message((result_buffer[i] << 8) | result_buffer[i + 1], &assembly_data);
				add_short_to_message((result_buffer[i] << 8) | result_buffer[i + 1], &g_result_data);
			}
			else
			{
				add_short_to_message(result_buffer[i] | (result_buffer[i + 1] << 8), &assembly_data);
				add_short_to_message(result_buffer[i] | (result_buffer[i + 1] << 8), &g_result_data);
			}
		}
	}
}

static void eip_update_explicit_input_data(void)
{
	uint8_t *result_buffer = g_result_raw_buffer;
	uint16_t result_len = g_result_raw_len;
	uint8_t *explicit_result_buffer = g_explicit_result_buffer;
	int i = 0;
	
	g_explicit_result_array.length = 0;
	memset(g_explicit_result_buffer, 0x0, sizeof(g_explicit_result_buffer));
	
	if ((result_buffer != NULL) && (result_len > 0))
	{
		g_explicit_result_array.length = result_len;
		for (i = 0; i < result_len; i += 2)
		{
			if (g_result_byte_swap)
			{
				add_short_to_message((result_buffer[i] << 8) | result_buffer[i + 1], &explicit_result_buffer);
			}
			else
			{
				add_short_to_message(result_buffer[i] | (result_buffer[i + 1] << 8), &explicit_result_buffer);
			}
		}
	}
}

static int32_t get_data_from_application_for_eip(uint32_t instance_number)
{
	uint8_t *assembly_data;
	
	switch(instance_number)
	{
		case INPUT_ASSEMBLY_NUM :
		{
			eip_update_input_data();
		}
		break;
		
		case HEARTBEAT_INPUT_ONLY_ASSEMBLY_NUM :
		{
			assembly_data = assembly_input_heartbeat_data;
			add_short_to_message(0x0002, &assembly_data);
			add_short_to_message(0x0001, &assembly_data);
		}
		break;
		
		default :
			break;
	}
	
	return 0;
}

int32_t application_data_handle_for_eip(uint32_t instance_number)
{
	uint8_t *output_data = assembly_output_data;
	static int clear_error_excuted = 0;
	int ret = -1;
	uint16_t prev_implicit_control = 0;
	uint16_t prev_implicit_status = 0;
	uint8_t prev_implicit_step = 0;
	uint8_t prev_implicit_waiting = 0;

	if (g_module_enable)
	{
		switch (instance_number)
		{
			case OUTPUT_ASSEMBLY_NUM:
			{
				g_assembly_output.output_event = get_uint_from_message(&output_data);
				move_message_n_bytes(14, &output_data);
				g_assembly_output.output_result_array.length = get_ushort_from_message(&output_data);
				if ((g_assembly_output.output_result_array.length) > 0
					&& (g_assembly_output.output_result_array.length < EIP_USERDATA_OUTPUT_BUFFER_SIZE))
				{
					memcpy(g_assembly_output.output_result_array.data, output_data,
						g_assembly_output.output_result_array.length);
				}

				prev_implicit_control = (uint16_t)g_assembly_output.output_event;
				prev_implicit_status = (uint16_t)g_assembly_input.input_event;
				prev_implicit_step = g_trigger_step;
				prev_implicit_waiting = g_waiting_result;
				
				LOGI("implicit control %x status %x step %d\r\n", 
					g_assembly_output.output_event, 
					g_assembly_input.input_event,
					g_trigger_step);
				
				switch (g_trigger_step)
				{
					case 1 :
					{
						if (EIP_CHK_BIT(g_assembly_output.output_event, EIPC_TRIGGER_ENABLE_BIT))
						{
							if(!CAlgoUtils::IsCommunicationOrSoftwareTrigger())
							{
								LOGI("not support software/communication trigger, please check\r\n");
								break;
							}
							g_assembly_input.input_event = 0;
							EIP_SET_BIT(g_assembly_input.input_event, EIPS_TRIGGER_READY_BIT);
							g_trigger_step++;
							eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
								(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
								"trigger_enable_ack", 0);
						}
					}
					break;
					
					case 2 :
					{
						if (EIP_CHK_BIT(g_assembly_output.output_event, EIPC_TRIGGER_BIT)
							&& EIP_CHK_BIT(g_assembly_input.input_event, EIPS_TRIGGER_READY_BIT))
						{
							EIP_SET_BIT(g_assembly_input.input_event, EIPS_TRIGGER_ACK_BIT);
							EIP_CLR_BIT(g_assembly_input.input_event, EIPS_TRIGGER_READY_BIT);

							g_result_raw_len = 0;
							g_assembly_input.trigger_id++;
							EIP_SET_BIT(g_assembly_input.input_event, EIPS_ACQUIRING_BIT);
							EIP_SET_BIT(g_assembly_input.input_event, EIPS_DECODING_BIT);

							g_waiting_result = 1;
                            eip_trigger_once();
							g_trigger_step++;
							eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
								(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
								"trigger_once", 0);
						}
					}
					break;
					
					case 3 :
					{
						if (EIP_CHK_BIT(g_assembly_output.output_event, EIPC_RESULT_ACK_BIT)
							&& (EIP_CHK_BIT(g_assembly_input.input_event, EIPS_RESULT_OK_BIT)
								|| EIP_CHK_BIT(g_assembly_input.input_event, EIPS_RESULT_NG_BIT)))
						{
							EIP_CLR_BIT(g_assembly_input.input_event, EIPS_TRIGGER_ACK_BIT);
							EIP_CLR_BIT(g_assembly_input.input_event, EIPS_RESULT_OK_BIT);
							EIP_CLR_BIT(g_assembly_input.input_event, EIPS_RESULT_NG_BIT);
							g_trigger_step = 1;
							eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
								(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
								"result_ack", 0);
						}
					}
					break;
					
					default :
					{
						g_trigger_step = 1;
					}
					break;
				}
				
				if ((EIP_CHK_BIT(g_assembly_output.output_event, EIPC_EXCUTE_COMMAND_BIT))
					|| (EIP_CHK_BIT(g_assembly_output.output_event, EIPC_EQUAL_EXCUTE_COMMAND_BIT)))
				{
					if (last_command_excuted == 0)
					{
						last_command_excuted = 1;
						if ((g_assembly_output.output_result_array.length > 0)
							 && (g_assembly_output.output_result_array.length < 128)
							 && (strlen((char *)g_assembly_output.output_result_array.data) > 0)
							 && (strlen((char *)g_assembly_output.output_result_array.data) < 128))
						{
							std::string result;
							CCommProxy::MessageInfo messageInfo{0};

							snprintf(messageInfo.msg, sizeof(messageInfo.msg), "%s", g_assembly_output.output_result_array.data);
							if (strlen((char *)messageInfo.msg) < g_assembly_output.output_result_array.length)
							{
								messageInfo.len = strlen((char *)g_assembly_output.output_result_array.data);
							}
							else
							{
								messageInfo.len = g_assembly_output.output_result_array.length;
							}
							messageInfo.msg[messageInfo.len] = 0;
							messageInfo.moduleId = m_nLogId; //LogId其实和模块Id是一样

							ret = CCommProxy::getInstance()->SyncRecv(messageInfo, result);
							LOGI("Recv return result %s ret %d \r\n", result.c_str(), ret);
							if (0 == ret)
							{
								EIP_SET_BIT(g_assembly_input.input_event, EIPS_COMMONAND_SUCCESS_BIT);
								EIP_CLR_BIT(g_assembly_input.input_event, EIPS_COMMONAND_FAILED_BIT);
								eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
									(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
									"cmd_ok", 0);
							}
							else
							{
								EIP_CLR_BIT(g_assembly_input.input_event, EIPS_COMMONAND_SUCCESS_BIT);
								EIP_SET_BIT(g_assembly_input.input_event, EIPS_COMMONAND_FAILED_BIT);
								eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
									(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
									"cmd_ng", 1);
							}

							ret = ethernetip_send_result(const_cast<char*>(result.c_str()), result.length());
							if (0 != ret)
							{
								LOGE("ethernetip_send_result error %d\r\n", ret);
							}	
						}
						else
						{
							EIP_CLR_BIT(g_assembly_input.input_event, EIPS_COMMONAND_SUCCESS_BIT);
							EIP_SET_BIT(g_assembly_input.input_event, EIPS_COMMONAND_FAILED_BIT);
							eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
								(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
								"cmd_invalid", 1);
						}
					}
				}
				else
				{
					if (last_command_excuted == 1)
					{
						last_command_excuted = 0;
						EIP_CLR_BIT(g_assembly_input.input_event, EIPS_COMMONAND_SUCCESS_BIT);
						EIP_CLR_BIT(g_assembly_input.input_event, EIPS_COMMONAND_FAILED_BIT);
					}
				}
				
				if ((EIP_CHK_BIT(g_assembly_output.output_event, EIPC_CLEAR_ERROR_BIT))
					|| (EIP_CHK_BIT(g_assembly_output.output_event, EIPC_EQUAL_CLEAR_ERROR_BIT)))
				{
					if (clear_error_excuted == 0)
					{
						clear_error_excuted = 1;
						g_assembly_input.input_event = 0;
						g_trigger_step = 1;
						g_waiting_result = 0;
						eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
							(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
							"clear_error", 1);
					}
				}
				else
				{
					clear_error_excuted = 0;
				}

				eip_debug_record_if_changed(EIP_DEBUG_WORK_MODE_IMPLICIT,
					prev_implicit_step, g_trigger_step,
					prev_implicit_waiting, g_waiting_result,
					prev_implicit_control, (uint16_t)g_assembly_output.output_event,
					prev_implicit_status, (uint16_t)g_assembly_input.input_event, 0);
			}
			break;
			
			default:
				break;
		}
	}	
	return 0;
}

static void *eip_explicit_trigger_process(void *args)
{
	static struct timeval eip_implicit_now, eip_implicit_oldtm;
	static struct timeval eip_explicit_now, eip_explicit_oldtm;
	int32_t implicit_elapsed_ms = 0;
	int32_t explicit_elapsed_ms = 0;
	static int explicit_last_command_excuted = 0;
	static int explicit_clear_error_excuted = 0;
	uint16_t prev_implicit_control = 0;
	uint16_t prev_implicit_status = 0;
	uint8_t prev_implicit_step = 0;
	uint8_t prev_implicit_waiting = 0;
	uint16_t prev_explicit_control = 0;
	uint16_t prev_explicit_status = 0;
	uint8_t prev_explicit_step = 0;
	uint8_t prev_explicit_waiting = 0;
	
	(void)args;
	eip_debug_init_once();
	g_explicit_trigger_running = 1;
	g_explicit_trigger_end = 0;
	eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
		g_explicit_output_status, g_explicit_input_status, 0, "thread_start", 1);
	
	while (g_explicit_trigger_running)
	{
		prev_implicit_control = (uint16_t)g_assembly_output.output_event;
		prev_implicit_status = (uint16_t)g_assembly_input.input_event;
		prev_implicit_step = g_trigger_step;
		prev_implicit_waiting = g_waiting_result;
		prev_explicit_control = g_explicit_output_status;
		prev_explicit_status = g_explicit_input_status;
		prev_explicit_step = g_explicit_trigger_step;
		prev_explicit_waiting = g_explicit_waiting_result;

		eip_get_uptime(&eip_implicit_now);
		if (g_waiting_result)
		{
			implicit_elapsed_ms = eip_timeval_elapsed(&eip_implicit_now, &eip_implicit_oldtm);
			if (implicit_elapsed_ms > EIP_RESULT_TIMEOUT)
			{
				g_waiting_result = 0;
				EIP_SET_BIT(g_assembly_input.input_event, EIPS_RESULT_NG_BIT);
				EIP_CLR_BIT(g_assembly_input.input_event, EIPS_ACQUIRING_BIT);
				EIP_CLR_BIT(g_assembly_input.input_event, EIPS_DECODING_BIT);
				eip_implicit_oldtm = eip_implicit_now;
				eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
					(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event,
					implicit_elapsed_ms, "result_timeout", 1);
			}
		}
		else
		{
			eip_implicit_oldtm = eip_implicit_now;
			implicit_elapsed_ms = 0;
		}
		
		eip_get_uptime(&eip_explicit_now);
		if (g_explicit_waiting_result)
		{
			explicit_elapsed_ms = eip_timeval_elapsed(&eip_explicit_now, &eip_explicit_oldtm);
			if (explicit_elapsed_ms > EIP_RESULT_TIMEOUT)
			{
				g_explicit_waiting_result = 0;
				EIP_SET_BIT(g_explicit_input_status, EIPS_RESULT_NG_BIT);
				EIP_CLR_BIT(g_explicit_input_status, EIPS_ACQUIRING_BIT);
				EIP_CLR_BIT(g_explicit_input_status, EIPS_DECODING_BIT);
				eip_explicit_oldtm = eip_explicit_now;
				eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
					g_explicit_output_status, g_explicit_input_status,
					explicit_elapsed_ms, "result_timeout", 1);
			}
		}
		else
		{
			eip_explicit_oldtm = eip_explicit_now;
			explicit_elapsed_ms = 0;
		}
		
		//LOGI("explicit control %x status %x step %d\r\n", 
			//g_explicit_output_status, g_explicit_input_status, g_explicit_trigger_step);
		
		switch (g_explicit_trigger_step)
		{
			case 1:
			{
				if (EIP_CHK_BIT(g_explicit_output_status, EIPC_TRIGGER_ENABLE_BIT))
				{
					if(!CAlgoUtils::IsCommunicationOrSoftwareTrigger())
					{
						LOGI("not support software/communication trigger, please check\r\n");
						break;
					}
					g_explicit_input_status = 0;
					EIP_SET_BIT(g_explicit_input_status, EIPS_TRIGGER_READY_BIT);
					g_explicit_trigger_step++;
					eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
						g_explicit_output_status, g_explicit_input_status, explicit_elapsed_ms, "trigger_enable_ack", 0);
				}
			}
			break;
			
			case 2 :
			{
				if (EIP_CHK_BIT(g_explicit_output_status, EIPC_TRIGGER_BIT)
					&& EIP_CHK_BIT(g_explicit_input_status, EIPS_TRIGGER_READY_BIT))
				{
					EIP_CLR_BIT(g_explicit_input_status, EIPS_TRIGGER_READY_BIT);
					EIP_SET_BIT(g_explicit_input_status, EIPS_TRIGGER_ACK_BIT);
					
					g_result_raw_len = 0;
					g_explicit_result_array.length = 0;
					
					g_explicit_trigger_id++;
					EIP_SET_BIT(g_explicit_input_status, EIPS_ACQUIRING_BIT);
					EIP_SET_BIT(g_explicit_input_status, EIPS_DECODING_BIT);
					
					g_explicit_waiting_result = 1;
					eip_trigger_once();
					g_explicit_trigger_step++;
					eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
						g_explicit_output_status, g_explicit_input_status, explicit_elapsed_ms, "trigger_once", 0);
				}
			}
			break;
			
			case 3:
			{
				if (EIP_CHK_BIT(g_explicit_output_status, EIPC_RESULT_ACK_BIT)
					&& (EIP_CHK_BIT(g_explicit_input_status, EIPS_RESULT_OK_BIT)
						|| EIP_CHK_BIT(g_explicit_input_status, EIPS_RESULT_NG_BIT)))
				{
					EIP_CLR_BIT(g_explicit_input_status, EIPS_TRIGGER_ACK_BIT);
					EIP_CLR_BIT(g_explicit_input_status, EIPS_RESULT_OK_BIT);
					EIP_CLR_BIT(g_explicit_input_status, EIPS_RESULT_NG_BIT);
					g_explicit_trigger_step = 1;
					eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
						g_explicit_output_status, g_explicit_input_status, explicit_elapsed_ms, "result_ack", 0);
				}
			}
			break;
			
			default:
			{
				g_explicit_trigger_step = 1;
			}
			break;
		}
		
		if ((EIP_CHK_BIT(g_explicit_output_status, EIPC_EXCUTE_COMMAND_BIT))
			|| (EIP_CHK_BIT(g_explicit_output_status, EIPC_EQUAL_EXCUTE_COMMAND_BIT)))
		{
			if (explicit_last_command_excuted == 0)
			{
				explicit_last_command_excuted = 1;
				EIP_SET_BIT(g_explicit_input_status, EIPS_COMMONAND_FAILED_BIT);
				eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
					g_explicit_output_status, g_explicit_input_status, explicit_elapsed_ms, "cmd_ng", 1);
			}
		}
		else
		{
			if (explicit_last_command_excuted == 1)
			{
				explicit_last_command_excuted = 0;
				EIP_CLR_BIT(g_explicit_input_status, EIPS_COMMONAND_SUCCESS_BIT);
				EIP_CLR_BIT(g_explicit_input_status, EIPS_COMMONAND_FAILED_BIT);
			}
		}
		
		if ((EIP_CHK_BIT(g_explicit_output_status, EIPC_CLEAR_ERROR_BIT))
			|| (EIP_CHK_BIT(g_explicit_output_status, EIPC_EQUAL_CLEAR_ERROR_BIT)))
		{
			if (explicit_clear_error_excuted == 0)
			{
				explicit_clear_error_excuted = 1;
				g_explicit_input_status = 0;
				g_explicit_trigger_step = 1;
				g_explicit_waiting_result = 0;
				eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
					g_explicit_output_status, g_explicit_input_status, explicit_elapsed_ms, "clear_error", 1);
			}
		}
		else
		{
			explicit_clear_error_excuted = 0;
		}

		eip_debug_record_if_changed(EIP_DEBUG_WORK_MODE_IMPLICIT,
			prev_implicit_step, g_trigger_step,
			prev_implicit_waiting, g_waiting_result,
			prev_implicit_control, (uint16_t)g_assembly_output.output_event,
			prev_implicit_status, (uint16_t)g_assembly_input.input_event, implicit_elapsed_ms);
		eip_debug_record_if_changed(EIP_DEBUG_WORK_MODE_EXPLICIT,
			prev_explicit_step, g_explicit_trigger_step,
			prev_explicit_waiting, g_explicit_waiting_result,
			prev_explicit_control, g_explicit_output_status,
			prev_explicit_status, g_explicit_input_status, explicit_elapsed_ms);

		eip_debug_record_heartbeat(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
			(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, implicit_elapsed_ms);
		eip_debug_record_heartbeat(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
			g_explicit_output_status, g_explicit_input_status, explicit_elapsed_ms);
		
		usleep(10000);
	}
	
	g_explicit_trigger_end = 1;
	eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
		g_explicit_output_status, g_explicit_input_status, 0, "thread_exit", 1);
	return nullptr;
}

static int32_t eip_stack_param_init(void)
{	
	memset(&g_assembly_input, 0, sizeof(g_assembly_input));
	memset(&g_assembly_output, 0, sizeof(g_assembly_output));
	
	memset(g_result_buffer, 0, sizeof(g_result_buffer));
	g_assembly_input.input_result_array.length = sizeof(g_result_buffer);
	g_assembly_input.input_result_array.data = g_result_buffer;
	memset(g_userdata_buffer, 0, sizeof(g_userdata_buffer));
	g_assembly_output.output_result_array.length = sizeof(g_userdata_buffer);
	g_assembly_output.output_result_array.data = g_userdata_buffer;
	
	memset(g_explicit_result_buffer, 0, sizeof(g_explicit_result_buffer));
	
	memset(assembly_input_data, 0, sizeof(assembly_input_data));
	memset(assembly_output_data, 0, sizeof(assembly_output_data));
	memset(assembly_config_data, 0, sizeof(assembly_config_data));
	memset(assembly_explicit_data, 0, sizeof(assembly_explicit_data));
	memset(assembly_input_heartbeat_data, 0, sizeof(assembly_input_heartbeat_data));
	memset(assembly_listen_heartbeat_data, 0, sizeof(assembly_listen_heartbeat_data));

	identity_data.eip_vendor_id = HIKROBOT_DEVICE_VENDOR_ID;
	identity_data.eip_device_type = HIKROBOT_DEVICE_TYPE;
	if(Capa_GetProtocolInfo() != nullptr)
	{
		identity_data.eip_product_code = Capa_GetProtocolInfo()->ethernetipInfo.deviceProductCode;
	}
	identity_data.eip_serial_number = HIKROBOT_DEVICE_SERIAL_NUMBER;
	if(Capa_GetBusinessInfo() == nullptr || Capa_GetBusinessInfo()->deviceInfo.deviceClass == nullptr)
	{
		identity_data.eip_product_name_length = 0;
	    identity_data.eip_product_name = nullptr;
	}
	else
	{
		const char* deviceClass = Capa_GetBusinessInfo()->deviceInfo.deviceClass;
	    if (deviceClass)
	    {
	        identity_data.eip_product_name_length = strlen(deviceClass);
	        identity_data.eip_product_name = new uint8_t[identity_data.eip_product_name_length + 1];
	        memcpy(identity_data.eip_product_name, deviceClass, identity_data.eip_product_name_length);
	        identity_data.eip_product_name[identity_data.eip_product_name_length] = '\0'; 
	    }
	}
	identity_data.eip_revision.major_revision = HIKROBOT_DEVICE_MAJOR_REVISION;
	identity_data.eip_revision.minor_revision = HIKROBOT_DEVICE_MINOR_REVISION;
	cfg_data.identity_data = &identity_data;
	
	custom_data.class_id = HIKROBOT_READER_CLASS_CODE;
	g_explicit_output_status = 0;
	g_explicit_input_status = 0;
	g_explicit_trigger_id = 0;
	g_explicit_result_id = 0;
	custom_data.output_status = &g_explicit_output_status;
	custom_data.input_status = &g_explicit_input_status;
	custom_data.trigger_id = &g_explicit_trigger_id;
	custom_data.result_id = &g_explicit_result_id;	
	g_explicit_result_array.length = 0;
	g_explicit_result_array.data = g_explicit_result_buffer;
	custom_data.result_array = &g_explicit_result_array;
	cfg_data.custom_data = &custom_data;
	
	cfg_data.eip_assembly_parameter.app_input_assembly_num = INPUT_ASSEMBLY_NUM;
	cfg_data.eip_assembly_parameter.app_output_assembly_num = OUTPUT_ASSEMBLY_NUM;
	cfg_data.eip_assembly_parameter.app_config_assembly_num = CONFIG_ASSEMBLY_NUM;
	cfg_data.eip_assembly_parameter.app_input_heartbeat_assembly_num = HEARTBEAT_INPUT_ONLY_ASSEMBLY_NUM;
	cfg_data.eip_assembly_parameter.app_listen_heartbeat_assembly_num = HEARTBEAT_LISTEN_ONLY_ASSEMBLY_NUM;
	cfg_data.eip_assembly_parameter.app_explict_assembly_num = EXPLICT_ASSEMBLY_NUM;
	
	cfg_data.eip_assembly_parameter.input_assembly_array.length = g_input_size;//sizeof(assembly_input_data);
	cfg_data.eip_assembly_parameter.output_assembly_array.length = g_output_size;//sizeof(assembly_output_data);
	cfg_data.eip_assembly_parameter.config_assembly_array.length = sizeof(assembly_config_data);
	cfg_data.eip_assembly_parameter.explicit_assembly_array.length = sizeof(assembly_explicit_data);
	cfg_data.eip_assembly_parameter.input_heartbeat_assembly_array.length = sizeof(assembly_input_heartbeat_data);
	cfg_data.eip_assembly_parameter.listen_heartbeat_assembly_array.length = sizeof(assembly_listen_heartbeat_data);
	
	cfg_data.eip_assembly_parameter.input_assembly_array.data = assembly_input_data;
	cfg_data.eip_assembly_parameter.output_assembly_array.data = assembly_output_data;
	cfg_data.eip_assembly_parameter.config_assembly_array.data = assembly_config_data;
	cfg_data.eip_assembly_parameter.explicit_assembly_array.data = assembly_explicit_data;
	cfg_data.eip_assembly_parameter.input_heartbeat_assembly_array.data = assembly_input_heartbeat_data;
	cfg_data.eip_assembly_parameter.listen_heartbeat_assembly_array.data = assembly_listen_heartbeat_data;
	
	app_interface.application_data_handle = application_data_handle_for_eip;
	app_interface.get_data_from_application = get_data_from_application_for_eip;
	app_interface.application_start_read = reader_start_read;
	app_interface.application_stop_read = reader_stop_read;
	app_interface.application_trigger_once = eip_explicit_trigger_once;
	app_interface.set_explicit_output_status = eip_set_explicit_output_status;
	cfg_data.app_interface = &app_interface;
	
	cfg_data.eip_exit = 0;
	
	g_trigger_step = 1;
	g_explicit_trigger_step = 1;
	last_command_excuted = 1;
	
	return configure_eip_para(&cfg_data);
}

int ethernetip_msg_init(void)
{
	pthread_t eip_stack_thread;
	pthread_t eip_trigger_thread;
	int ret = 0;
	
	ret = eip_stack_param_init();
	if (ret < 0)
	{
		LOGE("eip_stack_param_init failed! ret = %d\r\n", ret);
		return -1;
	}
	
	ret = thread_spawn_ex(&eip_stack_thread, 0, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, (start_routine)eip_init, (void*)NETWORK_INTERFACE);
	if (ret != 0)
	{
		LOGE("create eip_stack_thread failed!, ret = %d\r\n", ret);
		return -1;
	}
	
	ret = thread_spawn_ex(&eip_trigger_thread, 0, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, eip_explicit_trigger_process, NULL);
	if (ret != 0)
	{
		LOGE("create eip_explicit_trigger_process failed!, ret = %d\r\n", ret);
		return -1;
	}
	
	sleep(1);
	return 0;
}

int ethernetip_msg_deinit(void)
{
	if (g_explicit_trigger_running == 1)
	{
		g_explicit_trigger_running = 0;
		while (g_explicit_trigger_end != 1)
		{
			usleep(10 * 1000);
		}
	}

	if(identity_data.eip_product_name != nullptr)
	{
		delete[] identity_data.eip_product_name;
		identity_data.eip_product_name = nullptr;
	}
	
	cfg_stack_run_status(STACK_END);
	while (!cfg_data.eip_exit)
	{
		usleep(10 * 1000);
	}
	
	return 0;
}

int ethernetip_send_result(char *result_ptr, int result_len)
{	
	LOGI("frame_cnt:%d trigger_cnt:%d send msg is %s\n", m_nFrame, m_nTrigger, result_ptr);
	eip_debug_init_once();
	
	if (result_ptr != NULL && result_len > 0)
	{
		if (result_len > g_input_size - EIP_RESULT_DATA_OFFSET - EIP_RESULT_DATA_LEN_BYTES)
		{
			result_len = g_input_size - EIP_RESULT_DATA_OFFSET - EIP_RESULT_DATA_LEN_BYTES;
		}
		
		g_result_raw_len = result_len;
		memset(g_result_raw_buffer, 0, sizeof(g_result_raw_buffer));
		if (result_len > 0)
		{
			memcpy(g_result_raw_buffer, result_ptr, result_len);
		}
		
		eip_update_input_data();
		if (g_waiting_result)
		{
			g_waiting_result = 0;
			g_assembly_input.result_id++;
			EIP_SET_BIT(g_assembly_input.input_event, EIPS_RESULT_OK_BIT);
			//EIP_CLR_BIT(g_assembly_input.input_event, EIPS_TRIGGER_ACK_BIT);
			EIP_CLR_BIT(g_assembly_input.input_event, EIPS_ACQUIRING_BIT);
			EIP_CLR_BIT(g_assembly_input.input_event, EIPS_DECODING_BIT);
			eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
				(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
				"result_ok", 0);
		}
		
		eip_update_explicit_input_data();
		if (g_explicit_waiting_result)
		{
			g_explicit_waiting_result = 0;
			g_explicit_result_id++;
			EIP_SET_BIT(g_explicit_input_status, EIPS_RESULT_OK_BIT);
			//EIP_CLR_BIT(g_explicit_input_status, EIPS_TRIGGER_ACK_BIT);
			EIP_CLR_BIT(g_explicit_input_status, EIPS_ACQUIRING_BIT);
			EIP_CLR_BIT(g_explicit_input_status, EIPS_DECODING_BIT);
			eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
				g_explicit_output_status, g_explicit_input_status, 0, "result_ok", 0);
		}
	}
	else if (result_len == 0)
	{
		g_result_raw_len = result_len;
		memset(g_result_raw_buffer, 0, sizeof(g_result_raw_buffer));
		
		eip_update_input_data();
		if (g_waiting_result)
		{
			g_waiting_result = 0;
			EIP_SET_BIT(g_assembly_input.input_event, EIPS_RESULT_NG_BIT);
			//EIP_CLR_BIT(g_assembly_input.input_event, EIPS_TRIGGER_ACK_BIT);
			EIP_CLR_BIT(g_assembly_input.input_event, EIPS_ACQUIRING_BIT);
			EIP_CLR_BIT(g_assembly_input.input_event, EIPS_DECODING_BIT);
			eip_debug_record(EIP_DEBUG_WORK_MODE_IMPLICIT, g_trigger_step, g_waiting_result,
				(uint16_t)g_assembly_output.output_event, (uint16_t)g_assembly_input.input_event, 0,
				"result_ng", 1);
		}
		
		eip_update_explicit_input_data();
		if (g_explicit_waiting_result)
		{
			g_explicit_waiting_result = 0;
			EIP_SET_BIT(g_explicit_input_status, EIPS_RESULT_NG_BIT);
			//EIP_CLR_BIT(g_explicit_input_status, EIPS_TRIGGER_ACK_BIT);
			EIP_CLR_BIT(g_explicit_input_status, EIPS_ACQUIRING_BIT);
			EIP_CLR_BIT(g_explicit_input_status, EIPS_DECODING_BIT);
			eip_debug_record(EIP_DEBUG_WORK_MODE_EXPLICIT, g_explicit_trigger_step, g_explicit_waiting_result,
				g_explicit_output_status, g_explicit_input_status, 0, "result_ng", 1);
		}
	}
	else
	{
		return -1;
	}
	
	return 0;
}

int ethernetip_set_init_input_size(int size)
{
	g_input_size = size;
	return 0;
}

int ethernetip_set_init_output_size(int size)
{
	g_output_size = size;
	return 0;
}

int ethernetip_set_input_size(int size)
{
	ethernetip_msg_deinit();
	g_input_size = size;
	ethernetip_msg_init();
	return 0;
}

int ethernetip_set_output_size(int size)
{
	ethernetip_msg_deinit();
	g_output_size = size;
	ethernetip_msg_init();
	return 0;
}

int ethernetip_set_result_byte_swap(int enable)
{
	g_result_byte_swap = enable;
	return 0;
}
int ethernetip_set_module_enable(int enable)
{
	g_module_enable = (uint8_t)enable;
	return 0;
}

int ethernetip_set_debug_level(int level)
{
	eip_debug_init_once();
	if (!g_eip_debug_inited)
	{
		return -1;
	}
	(void)ind_proto_debug_set_level(&g_eip_debug_ctx, level);
	return 0;
}

int ethernetip_get_debug_level(void)
{
	if (!g_eip_debug_inited)
	{
		return IND_PROTO_DEBUG_LEVEL_OFF;
	}
	return ind_proto_debug_get_level(&g_eip_debug_ctx);
}

int ethernetip_get_debug_info(char *buff, int buff_size, int *data_len)
{
	eip_debug_init_once();
	if (!g_eip_debug_inited)
	{
		return -1;
	}
	return ind_proto_debug_dump(&g_eip_debug_ctx, buff, buff_size, data_len, IND_PROTO_DEBUG_DUMP_MAX_DEFAULT);
}



