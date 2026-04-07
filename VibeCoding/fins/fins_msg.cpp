/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author  zhengxiaoyu
 * @date    2019/08/27
 * @Modifier  biying
 * @data    2020.03.23
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2019/08/27  |V1.0.0  |zhengxiaoyu         |创建文档
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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>

#include "fins_msg.h"
#include "fins.h"
#include "utils.h"
#include "thread/ThreadApi.h"
#include "dsp_isp.h"
#include "dev_cfg.h"
#include "TriggerParamDef.h"
#include "algo_common.h"
#include "AppParamCommon.h"
#include "adapter/ScheErrorCodeDefine.h"

#include "framework_service.h"
#include "log/log.h"
#include "ITriggerSource.h"
#include "IIspSource.h"
#include "IImageProcess.h"
#include "algoutils.h"
#include "industrial_protocol_debug.h"
#ifndef min
#define min(a, b) ((a)<(b)) ? (a) : (b)
#endif

typedef enum
{
	FNC_TRIGGER_ENABLE_BIT = 0,
	FNC_TRIGGER_BIT = 1,
	FNC_RESULT_ACK_BIT = 2,
	FNC_EXCUTE_COMMAND_BIT = 8,
	FNC_CLEAR_ERROR_BIT = 15,
} fins_control_bits;

typedef enum
{
	FNS_TRIGGER_READY_BIT = 0,
	FNS_TRIGGER_ACK_BIT = 1,
	FNS_ACQUIRING_BIT = 2,
	FNS_DECODING_BIT = 3,
	FNS_RESULT_OK_BIT = 8,
	FNS_RESULT_NG_BIT = 9,
	FNS_COMMONAND_SUCCESS_BIT = 10,
	FNS_COMMONAND_FAILED_BIT = 11,
	FNS_GENERAL_FAULT_BIT = 15,
} fins_status_bits;

#define FN_SET_BIT(a, b)   ((a) |= (1 << (b)))
#define FN_CLR_BIT(a, b)   ((a) &= ~(1 << (b)))
#define FN_CHK_BIT(a, b)   ((a) & ((unsigned short)1 << (b)))

#define MAX_BUF_SIZE       (2000)
#define MAX_COMMAND_LEN    (128)
#define FINS_DEBUG_HEARTBEAT_MS (2000)

struct fins_ctrl_t
{
	sem_t result_sem;
	pthread_mutex_t mutex_lock;
	struct fins_t *ctx;    
	int need_recreate;    
	int fins_process;    
	int waiting_result;
	int result_ng;
	int trigger_step;    
	int trigger_process_running;
	int trigger_process_end;    
	int message_timeout;
	fins_param_opt *config_param;    
	char result_buf[MAX_BUF_SIZE];    
	short command_buf[MAX_COMMAND_LEN];
};

static struct fins_ctrl_t fins_ctrl;
static ind_proto_debug_ctx_t g_fins_debug_ctx;
static int g_fins_debug_inited = 0;

static const ind_proto_debug_bit_t g_fins_control_bits[] =
{
	{FNC_TRIGGER_ENABLE_BIT, "TRIG_EN"},
	{FNC_TRIGGER_BIT, "TRIG"},
	{FNC_RESULT_ACK_BIT, "RESULT_ACK"},
	{FNC_EXCUTE_COMMAND_BIT, "EXEC_CMD"},
	{FNC_CLEAR_ERROR_BIT, "CLR_ERR"},
};

static const ind_proto_debug_bit_t g_fins_status_bits[] =
{
	{FNS_TRIGGER_READY_BIT, "READY"},
	{FNS_TRIGGER_ACK_BIT, "ACK"},
	{FNS_ACQUIRING_BIT, "ACQ"},
	{FNS_DECODING_BIT, "DEC"},
	{FNS_RESULT_OK_BIT, "OK"},
	{FNS_RESULT_NG_BIT, "NG"},
	{FNS_COMMONAND_SUCCESS_BIT, "CMD_OK"},
	{FNS_COMMONAND_FAILED_BIT, "CMD_NG"},
	{FNS_GENERAL_FAULT_BIT, "FAULT"},
};

static void fins_debug_log_cb(const char *msg, void *user_data)
{
	(void)user_data;
	LOGI("%s\r\n", msg);
}

static void fins_debug_init_once(void)
{
	if (g_fins_debug_inited)
	{
		return;
	}

	if (ind_proto_debug_init(&g_fins_debug_ctx, "FINS",
		g_fins_control_bits, sizeof(g_fins_control_bits) / sizeof(g_fins_control_bits[0]),
		g_fins_status_bits, sizeof(g_fins_status_bits) / sizeof(g_fins_status_bits[0]),
		FINS_DEBUG_HEARTBEAT_MS,
		fins_debug_log_cb, NULL) == 0)
	{
		g_fins_debug_inited = 1;
	}
}

static void fins_fill_debug_state(const struct fins_ctrl_t *fins_c,
	short control_reg,
	short status_reg,
	int elapsed_ms,
	const char *reason,
	ind_proto_debug_state_t *state)
{
	if (fins_c == NULL || state == NULL)
	{
		return;
	}
	state->work_mode = 0;
	state->step = (uint8_t)fins_c->trigger_step;
	state->waiting_result = (uint8_t)fins_c->waiting_result;
	state->control_event = (uint16_t)control_reg;
	state->status_event = (uint16_t)status_reg;
	state->elapsed_ms = elapsed_ms;
	state->reason = reason;
}

/**
 * @brief        16 bit message to size
 * @param[in]    message start 16 bit  
 * @return       len of message to verify
 */

static uint16_t get_ushort_from_message_ni(short *buffer_address)
{
	uint16_t data = buffer_address[0] ;
	return data;
}


int fins_set_server_ip(int ip)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->server_ip = ip;
	fins_c->need_recreate = 1;
	return 0;
}

int fins_set_recreate(int recreate)
{    
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->need_recreate = recreate;
	return 0;
}

int fins_set_server_port(int port)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->server_port = port;
	fins_c->need_recreate = 1;
	return 0;
}

int fins_set_control_poll_interval(int ms)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->control_poll_interval = ms;
	return 0;
}

int fins_set_control_offset(int offset)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->control_offset = offset;
	return 0;
}

int fins_set_status_offset(int offset)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->status_offset = offset;
	return 0;
}

int fins_set_result_offset(int offset)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->result_offset = offset;
	return 0;
}

int fins_set_result_size(int num)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->result_size = num;
	return 0;
}

int fins_set_result_byte_swap(int swap)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->result_byte_swap = swap;
	return 0;
}

int fins_set_result_timeout(int second)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	fins_c->config_param->result_timeout = second;
	return 0;
}

int fins_get_enable()
{    
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	return fins_c->fins_process;
}

int fins_set_debug_level(int level)
{
	fins_debug_init_once();
	if (!g_fins_debug_inited)
	{
		return -1;
	}
	return ind_proto_debug_set_level(&g_fins_debug_ctx, level);
}

int fins_get_debug_level(void)
{
	if (!g_fins_debug_inited)
	{
		return IND_PROTO_DEBUG_LEVEL_OFF;
	}
	return ind_proto_debug_get_level(&g_fins_debug_ctx);
}

int fins_get_debug_info(char *buff, int buff_size, int *data_len)
{
	fins_debug_init_once();
	if (!g_fins_debug_inited)
	{
		return -1;
	}
	return ind_proto_debug_dump(&g_fins_debug_ctx, buff, buff_size, data_len, IND_PROTO_DEBUG_DUMP_MAX_DEFAULT);
}

static int sem_timedwait_millsecs(sem_t *sem, long msecs)
{
	struct timespec ts;
	
	clock_gettime(CLOCK_REALTIME, &ts);	
	long secs = msecs / 1000;
	msecs = msecs % 1000;
	long add = 0;
	msecs = msecs * 1000 * 1000 + ts.tv_nsec;
	add = msecs / (1000 * 1000 * 1000);
	ts.tv_sec += (add + secs);
	ts.tv_nsec = msecs % (1000 * 1000 * 1000);
	return sem_timedwait(sem, &ts);
}

static int fins_write_registers(int reg_space, int reg_offset, int reg_num, short* buffer, int timeout_ms)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	const int type = 0x82;	// DM registers
	int ret = 0;
	
	pthread_mutex_lock(&fins_c->mutex_lock);
	ret = fins_write(fins_c->ctx, type, reg_offset, reg_num, (const unsigned short *)buffer);
	pthread_mutex_unlock(&fins_c->mutex_lock);
	if (ret != reg_num)
	{
		return -1;
	}
	
	return 0;
}

static int fins_read_registers(int reg_space, int reg_offset, int reg_num, short *buffer, int timeout_ms)
{	
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	const int type = 0x82;	// D M registers
	int ret = 0;

	pthread_mutex_lock(&fins_c->mutex_lock);
	ret = fins_read(fins_c->ctx, type, reg_offset, reg_num, (unsigned short *)buffer);
	pthread_mutex_unlock(&fins_c->mutex_lock);
	if (ret != reg_num)
	{
		return -1;
	}
	
	return 0;
}

int fins_send_result(const char *result_ptr, unsigned int result_len)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	unsigned short* result_buf_addr = 0;
	int ret = 0;
	int i = 0;
	
//	printf("%s %d %s\r\n", __func__, result_len, result_ptr);
	
	if ((fins_c->fins_process == 0) || (fins_c->ctx == NULL))
	{
		return -1;
	}
	
	memset(fins_c->result_buf, 0x0, fins_c->config_param->result_size * 2);
	if ((result_ptr != NULL) && (result_len > 0))
	{
		if ((result_len + 2) > (unsigned int)(fins_c->config_param->result_size * 2))
		{
			result_len = fins_c->config_param->result_size * 2 - 2;
		}
		
		if (result_len > 0)
		{
			memcpy(fins_c->result_buf, &result_len, 2);
			result_buf_addr = (unsigned short *)&fins_c->result_buf[2];
			for (i = 0; i < (int)result_len; i += 2)
			{
				if (fins_c->config_param->result_byte_swap)
				{
					*result_buf_addr = (result_ptr[i] << 8) | result_ptr[i + 1];
				}
				else
				{
					*result_buf_addr = result_ptr[i] | (result_ptr[i + 1] << 8);
				}
				result_buf_addr++;
			}
			
			ret = fins_write_registers(fins_c->config_param->result_space, fins_c->config_param->result_offset, 
				fins_c->config_param->result_size, (short int *)fins_c->result_buf, fins_c->message_timeout);
			if (ret < 0)
			{
				fins_c->need_recreate = 1;
				return -1;
			}
		}
		
		fins_c->result_ng = 0;
	}
	else if (result_len == 0)
	{
		ret = fins_write_registers(fins_c->config_param->result_space, fins_c->config_param->result_offset, 
			fins_c->config_param->result_size, (short int *)fins_c->result_buf, fins_c->message_timeout);
		if (ret < 0)
		{
			fins_c->need_recreate = 1;
			return -1;
		}
		
		fins_c->result_ng = 1;
	}
	else
	{
		return -1;
	}
	
	if (fins_c->waiting_result)
	{
		sem_post(&fins_c->result_sem);
	}
	
	return 0; 
}

static int fins_trigger_once(void) 
{
	return CAlgoUtils::IndustrialProtocolTriggerOnce();
}

static int shortbuf_to_string(short *short_buf, int short_len, char *buf, int len)
{
	int ret = 0;
	int i = 0;
	unsigned short temp = 0;
	if (NULL == short_buf || NULL == buf)
	{
		printf("ERROR: buffer is NULL");
		return -1;
	}

	if (short_len <= 0 || len <= 0)
	{
		printf("ERROR: buffer len is error(short_len %d, len %d)", short_len, len);
		return -2;
	}

	for (i = 0; i < short_len; i++)
	{
		temp = short_buf[i];
		if (0 != temp)
		{
			snprintf(buf + strlen(buf), len - strlen(buf), "%c%c",
			(char)(temp & 0x00FF), (char)((temp & 0xFF00) >> 8));
		}
		else
		{
			break;
		}
	}

	printf("len %d, buf: %s\r\n", len, buf);
	return ret;
}

static void *fins_trigger_process(void *args)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	struct in_addr ip_addr;
	short control_reg = 0;
	short status_reg = 0;
	int last_command_excuted = 0;
	int clear_error_excuted = 0;
	int ret = -1;
	uint16_t userdata_size = 0;
	short *userdata_buff_size_addr = NULL;
	short *userdata_buff_addr = NULL;
	short prev_control_reg = 0;
	short prev_status_reg = 0;
	int prev_trigger_step = 0;
	int prev_waiting_result = 0;
	ind_proto_debug_state_t prev_state = {0};
	ind_proto_debug_state_t curr_state = {0};
	
	thread_set_name("fins_trigger_process");
	fins_debug_init_once();
	
	fins_c->trigger_process_running = 1;
	fins_c->trigger_process_end = 0;
	fins_c->ctx = NULL;
	fins_c->waiting_result = 0;
	fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "thread_start", &curr_state);
	ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 1);
	
	while (fins_c->trigger_process_running)
	{
		usleep(fins_c->config_param->control_poll_interval * 1000);
		prev_control_reg = control_reg;
		prev_status_reg = status_reg;
		prev_trigger_step = fins_c->trigger_step;
		prev_waiting_result = fins_c->waiting_result;
		
		if (fins_c->ctx == NULL)
		{
            ip_addr.s_addr = fins_c->config_param->server_ip;
//			printf("server ip %s port %d\r\n", inet_ntoa(ip_addr), fins_c->config_param->server_port);
			fins_c->ctx = fins_new_tcp(inet_ntoa(ip_addr), fins_c->config_param->server_port);
			if (fins_c->ctx == NULL)
			{
				sleep(1);
				continue;
			}
			
			ret = fins_connect(fins_c->ctx);
			if (ret != 0)
			{
				fins_close(fins_c->ctx);
				fins_free(fins_c->ctx);
				fins_c->ctx = NULL;
				sleep(1);
				continue;
			}

			//fins_set_debug(fins_c->ctx, 1);
			
			fins_c->trigger_step = 1;
			fins_c->need_recreate = 0;
		}

		if (fins_c->need_recreate)
		{
			fins_close(fins_c->ctx);
			fins_free(fins_c->ctx);
			fins_c->ctx = NULL;
			sleep(1);
			continue;
		}

        ret = fins_read_registers(fins_c->config_param->control_space, fins_c->config_param->control_offset, 
            1, &control_reg, fins_c->message_timeout);
        if (ret < 0)
        {
            fins_c->need_recreate = 1;
            continue;
        }
        
//        printf("contrl: %#x status: %#x step: %d\r\n",
//            control_reg, status_reg, fins_c->trigger_step);
		
		switch (fins_c->trigger_step)
		{
			case 1:
			{
				if (FN_CHK_BIT(control_reg, FNC_TRIGGER_ENABLE_BIT))
				{
					if(!CAlgoUtils::IsCommunicationOrSoftwareTrigger())
					{
						//printf("not support software/communication trigger, please check\r\n");
						break;
					}
					status_reg = 0;
					FN_SET_BIT(status_reg, FNS_TRIGGER_READY_BIT);
					ret = fins_write_registers(fins_c->config_param->status_space, fins_c->config_param->status_offset, 
						1, &status_reg, fins_c->message_timeout);
					if (ret < 0)
					{
						fins_c->need_recreate = 1;
						continue;
					}
					
					fins_c->trigger_step++;
					fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "trigger_enable_ack", &curr_state);
					ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 0);
				}
			}
			break;
			
			case 2:
			{
				if (FN_CHK_BIT(control_reg, FNC_TRIGGER_BIT)
					&& FN_CHK_BIT(status_reg, FNS_TRIGGER_READY_BIT))
				{
					FN_CLR_BIT(status_reg, FNS_TRIGGER_READY_BIT);
					FN_SET_BIT(status_reg, FNS_TRIGGER_ACK_BIT);
					FN_SET_BIT(status_reg, FNS_ACQUIRING_BIT);
					FN_SET_BIT(status_reg, FNS_DECODING_BIT);
					ret = fins_write_registers(fins_c->config_param->status_space, fins_c->config_param->status_offset, 
						1, &status_reg, fins_c->message_timeout);
					if (ret < 0)
					{
						fins_c->need_recreate = 1;
						continue;
					}
					
					memset(fins_c->result_buf, 0x0, fins_c->config_param->result_size * 2);
					ret = fins_write_registers(fins_c->config_param->result_space, fins_c->config_param->result_offset, 
						fins_c->config_param->result_size, (short int *)fins_c->result_buf, fins_c->message_timeout);
					if (ret < 0)
					{
						fins_c->need_recreate = 1;
						continue;
					}
					
					fins_c->waiting_result = 1;
					fins_trigger_once();
					if (sem_timedwait_millsecs(&fins_c->result_sem, fins_c->config_param->result_timeout * 1000) < 0)
					{
						fins_c->waiting_result = 0;
						//FN_CLR_BIT(status_reg, FNS_TRIGGER_ACK_BIT);
						FN_CLR_BIT(status_reg, FNS_ACQUIRING_BIT);
						FN_CLR_BIT(status_reg, FNS_DECODING_BIT);
						FN_SET_BIT(status_reg, FNS_RESULT_NG_BIT);
						ret = fins_write_registers(fins_c->config_param->status_space, fins_c->config_param->status_offset, 
							1, &status_reg, fins_c->message_timeout);
						if (ret < 0)
						{
							fins_c->need_recreate = 1;
							continue;
						}
						
						clear_error_excuted = 0;
						fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "result_timeout", &curr_state);
						ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 1);
					}
					else
					{
						fins_c->waiting_result = 0;
						//FN_CLR_BIT(status_reg, FNS_TRIGGER_ACK_BIT);
						FN_CLR_BIT(status_reg, FNS_ACQUIRING_BIT);
						FN_CLR_BIT(status_reg, FNS_DECODING_BIT);
						if (fins_c->result_ng)
						{
							FN_SET_BIT(status_reg, FNS_RESULT_NG_BIT);
						}
						else
						{
							FN_SET_BIT(status_reg, FNS_RESULT_OK_BIT);
						}
						ret = fins_write_registers(fins_c->config_param->status_space, fins_c->config_param->status_offset, 
							1, &status_reg, fins_c->message_timeout);
						if (ret < 0)
						{
							fins_c->need_recreate = 1;
							continue;
						}
					}
					
					fins_c->trigger_step++;
					fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "trigger_once", &curr_state);
					ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 0);
				}
			}
			break;
			
			case 3:
			{
				if (FN_CHK_BIT(control_reg, FNC_RESULT_ACK_BIT)
					&& (FN_CHK_BIT(status_reg, FNS_RESULT_OK_BIT)
						|| FN_CHK_BIT(status_reg, FNS_RESULT_NG_BIT)))
				{
					FN_CLR_BIT(status_reg, FNS_TRIGGER_ACK_BIT);
					FN_CLR_BIT(status_reg, FNS_RESULT_OK_BIT);
					FN_CLR_BIT(status_reg, FNS_RESULT_NG_BIT);
					ret = fins_write_registers(fins_c->config_param->status_space, fins_c->config_param->status_offset, 
						1, &status_reg, fins_c->message_timeout);
					if (ret < 0)
					{
						fins_c->need_recreate = 1;
						continue;
					}
					
					fins_c->trigger_step = 1;
					fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "result_ack", &curr_state);
					ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 0);
				}
			}
			break;
			
			default:
			{
				fins_c->trigger_step = 1;
			}
			break;
		}

//        printf("last_command_excuted: %d\r\n", last_command_excuted);
        
		if (FN_CHK_BIT(control_reg, FNC_EXCUTE_COMMAND_BIT))
		{
			if (last_command_excuted == 0)
			{
				last_command_excuted = 1;

				memset(fins_c->command_buf, 0, sizeof(fins_c->command_buf));
				ret = fins_read_registers(fins_c->config_param->ins_space, 
											fins_c->config_param->ins_offset, 
											fins_c->config_param->ins_size, 
											fins_c->command_buf, 
											fins_c->message_timeout);
//                    fins_c->config_param->ins_space, fins_c->config_param->ins_offset, fins_c->config_param->ins_size);
                                            
				if (ret < 0)
				{
					FN_SET_BIT(status_reg, FNS_COMMONAND_FAILED_BIT);
					ret = fins_write_registers(fins_c->config_param->status_space, 
                        fins_c->config_param->status_offset, 
                        fins_c->config_param->status_size, 
                        &status_reg,
                        fins_c->message_timeout);
                    if (ret != 0)
                    {
                       //printf("fins_write_registers failed, ret = %d\r\n", ret);
                    }
//					printf("write space %d, offset %d, size %d, ret %d", 
//							fins_c->config_param->status_space, 
//							fins_c->config_param->status_offset, 
//							fins_c->config_param->status_size, 
//							ret);
					continue;
				}
				else
				{            
					std::string result;
					CCommProxy::MessageInfo messageInfo{0};        
					userdata_size = get_ushort_from_message_ni(fins_c->command_buf);
					userdata_buff_size_addr = fins_c->command_buf;
					userdata_buff_addr = userdata_buff_size_addr + 1;
					userdata_size = min(userdata_size, sizeof(messageInfo.msg)-1);

					if ((userdata_size > 0)
						&& (userdata_size < 128)
						&& (strlen((char *)userdata_buff_addr) > 0)
						&& (strlen((char *)userdata_buff_addr) < REG_SERVICE_INFO_LEN))
					{
						ret = shortbuf_to_string(userdata_buff_addr, sizeof(fins_c->command_buf)-sizeof(short), 
											messageInfo.msg, sizeof(messageInfo.msg));
						if (strlen((char *)userdata_buff_addr) < userdata_size)
						{
							messageInfo.len = strlen((char *)userdata_buff_addr);
						}
						else
						{
							messageInfo.len = userdata_size;
						}
						messageInfo.msg[messageInfo.len] = 0;
						//messageInfo.moduleId = m_nLogId; //LogId其实和模块Id是一样

						ret = CCommProxy::getInstance()->SyncRecv(messageInfo, result);
						printf("Recv return result %s ret %d \r\n", result.c_str(), ret);
						if (0 == ret)
						{
							FN_SET_BIT(status_reg, FNS_COMMONAND_SUCCESS_BIT);
							fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "cmd_ok", &curr_state);
							ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 0);
						}
						else
						{
							FN_SET_BIT(status_reg, FNS_COMMONAND_FAILED_BIT);
							fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "cmd_ng", &curr_state);
							ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 1);
						}

						ret = fins_send_result(result.c_str(), result.length());
						if (0 != ret)
						{
							printf("fins_send_result error %d\r\n", ret);
						}	
					}		

					ret = fins_write_registers(fins_c->config_param->status_space, 
												fins_c->config_param->status_offset, 
												fins_c->config_param->status_size, 
												&status_reg,
												fins_c->message_timeout);
					if (ret < 0)
					{
//						printf("write space %d, offset %d, size %d, ret %d", 
//								fins_c->config_param->status_space, 
//								fins_c->config_param->status_offset, 
//								fins_c->config_param->status_size, 
//								ret);
						continue;
					}
				}                
			}
		}
		else
		{
			if (last_command_excuted == 1)
			{
				last_command_excuted = 0;
				FN_CLR_BIT(status_reg, FNS_COMMONAND_SUCCESS_BIT);
				FN_CLR_BIT(status_reg, FNS_COMMONAND_FAILED_BIT);
				ret = fins_write_registers(fins_c->config_param->status_space, fins_c->config_param->status_offset, 
					1, &status_reg, fins_c->message_timeout);
				if (ret < 0)
				{
					fins_c->need_recreate = 1;
					continue;
				}
			}
		}
		
		if (FN_CHK_BIT(control_reg, FNC_CLEAR_ERROR_BIT))
		{
			if (clear_error_excuted == 0)
			{
				clear_error_excuted = 1;
				status_reg = 0;
				ret = fins_write_registers(fins_c->config_param->status_space, fins_c->config_param->status_offset, 
					1, &status_reg, fins_c->message_timeout);
				if (ret < 0)
				{
					fins_c->need_recreate = 1;
					continue;
				}
				
				fins_c->waiting_result = 0;
				fins_c->trigger_step = 1;
				fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "clear_error", &curr_state);
				ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 1);
			}
		}
		else
		{
			clear_error_excuted = 0;
		}

		fins_fill_debug_state(fins_c, prev_control_reg, prev_status_reg, 0, "state_change", &prev_state);
		prev_state.step = (uint8_t)prev_trigger_step;
		prev_state.waiting_result = (uint8_t)prev_waiting_result;
		fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "state_change", &curr_state);
		ind_proto_debug_record_if_changed(&g_fins_debug_ctx, &prev_state, &curr_state);

		fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "heartbeat", &curr_state);
		ind_proto_debug_record_heartbeat(&g_fins_debug_ctx, &curr_state);
	}
	
	if (fins_c->ctx != NULL)
	{
		fins_close(fins_c->ctx);
		fins_free(fins_c->ctx);
		fins_c->ctx = NULL;
	}
	
	fins_c->trigger_process_end = 1;
	fins_fill_debug_state(fins_c, control_reg, status_reg, 0, "thread_exit", &curr_state);
	ind_proto_debug_record(&g_fins_debug_ctx, &curr_state, 1);
	return nullptr;
}

static void fins_param_init(fins_param_opt *c_param)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	
	fins_c->trigger_process_running = 1;
	fins_c->trigger_process_end = 0;
	fins_c->waiting_result = 0;    
	fins_c->message_timeout = 500;    
    fins_c->config_param = c_param;
}

static void *fins_task(void *arg)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	pthread_t fins_trigger_thread;
	int fins_process_start = 0;
	int ret = 0;
	thread_set_name("fins_task");
	
	while (1)
	{
		if (fins_c->fins_process)
		{
			if (0 == fins_process_start)
			{
				fins_process_start = 1;
								
				ret = thread_spawn_ex(&fins_trigger_thread, 0, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 1024 * 1024, fins_trigger_process, NULL);
				if (ret != 0)
				{
					printf("create fins_trigger_process failed!, ret = %d\r\n", ret);
					continue;
				}
			}
		}
		else
		{
			if (1 == fins_process_start)
			{
				if (fins_c->trigger_process_running)
				{
					fins_c->trigger_process_running = 0;
					while (!fins_c->trigger_process_end)
					{
						usleep(100 * 1000);
					}
				}
				
				fins_process_start = 0;
			}
		}
		
		usleep(100 * 1000);
	}

	return nullptr;
}

int fins_msg_init(fins_param_opt *c_param)
{	
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	pthread_t fins_thread;
	static int initialized = 0;
	int ret = -1;

    if (c_param == NULL)
    {
		printf("invalid argument\r\n");
		return -1;
    }

	if (initialized)
	{
		fins_c->fins_process = 1;
		return 0;
	}
	
	memset(fins_c, 0x0, sizeof(struct fins_ctrl_t));
	
	ret = sem_init(&fins_c->result_sem, 0, 0);
	if (ret < 0)
	{
		printf("sem_init failed\r\n");
		return -2;
	}
	
	ret = pthread_mutex_init(&fins_c->mutex_lock, NULL);
	if (ret < 0)
	{
		printf("pthread_mutex_init failed\r\n");
		return -3;
	}
    
    fins_param_init(c_param);

    ret = thread_spawn_ex(&fins_thread, 0, SCHED_POLICY_RR, SCHED_PRI_HIPRI_60, 10 * 1024, fins_task, NULL);
	if (ret != 0)
	{
		printf("create fins_task failed, ret = %d\r\n", ret);
		return -4;
	}
	
	initialized = 1;
	fins_c->fins_process = 1;
	
	return 0;
}

int fins_msg_deinit(void)
{
	struct fins_ctrl_t *fins_c = &fins_ctrl;
	
	if (fins_c->fins_process)
	{
		fins_c->fins_process = 0;
		usleep(200 * 1000);
	}
	
	return 0;
}

