#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "utils.h"
#include "util_thread.h"
#include <ctype.h> 

#include "wireless/WirelessApi.h"
#include "api/LuaApi.h"
#include "api/EventApi.h"

#include "gvcp_redline.h"
#include "sign_crypt.h"
#include "bsp_secure.h"
#include "log.h"
#include "checkPasswd.h"

#include "SystemData.h"
#include "SystemApi.h"
#include "ToolFunc.h"

//上电从flash读取参数
REDLINE_SOLIDIFY_PARAM_T s_solid_param = {0};
REDLINE_DEVICE_STS_T	 s_device_sts = {0};
REDLINE_COOKIE_UPDATE_T	 s_cookie_sts = {0};

#define DEV_RESET_PWD_RSAPK_PATH	"/mnt/app/public_key1.pem"
#define DEV_UPG_VERIFY_PATH			"/mnt/app/public_key3.pem"

static pthread_t login_lock_thread_t;     /**< 登录锁定线程*/
static pthread_t auth_timeout_thread_t;		//鉴权有效期线程

char active_random[32];				//加密激活等随机串，每次都随机生成，用完就清空，有效时长30min，cookie
unsigned char resetpwd_token[8];			//密码重置口令

extern unsigned int get_sendsdk_device_type();
extern int wire_less_is_connect();
extern void rm_xml(void);
extern int gvcp_currentip_lla();
extern int is_same_c_network(unsigned int peer_ip); 
//extern bool checkPasswd(char* username, char* password);
extern int get_mac_address(unsigned int *macHigh, unsigned int *macLow);


static int32_t get_dbg_timeDiff(struct timeval timeStart, struct timeval timeEnd)
{
	int32_t timediff = timeStart.tv_sec - timeEnd.tv_sec;
	LOGD("timeStart:(%ld), timeEnd:(%ld)!, timediff %d\n",timeStart.tv_sec, timeEnd.tv_sec, timediff);
	return timediff;
};

static void log_hexdump(const char *tag, const void *data, size_t size) 
{
}

bool redline_is_sup_reg_operation()
{
	// 红线设备连接老客户端时，判断是否激活或鉴权，否则不允许连接
	if (!(LOGGED_IN == redline_get_login_sts() || redline_get_auth_sts()))	
	{
		//LOGE("dev not login or auth!\n");
		return false;
	}

	return true;
}

static void redline_gvcp_fill_ack_header(struct gvcp_header *header, unsigned short status,
		enum GVCP_COMMAND ack, unsigned short size, unsigned short ack_id)
{
	header->type_status.ack_status = htons(status);
	header->command = htons(ack);
	header->size = htons(size);
	header->id = htons(ack_id);
}


int redline_get_flash_solid_param()
{
	hal_flash_read(USR_PART, 0, (uint8_t *)&s_solid_param, sizeof(REDLINE_SOLIDIFY_PARAM_T));
	if (s_solid_param.bActiveSts == DEV_INIT_STS)
	{
		memset(&s_solid_param, 0, sizeof(s_solid_param));
		memcpy(s_solid_param.userName[0], "root", 4);
	}
	
	return 0;
}

int redline_set_flash_solid_param()
{
	//写之前先,先全部清空
	REDLINE_SOLIDIFY_PARAM_T temp = {0};
	hal_flash_write(USR_PART, 0, (uint8_t *)&temp, sizeof(REDLINE_SOLIDIFY_PARAM_T));
	
	hal_flash_write(USR_PART, 0, (uint8_t *)&s_solid_param, sizeof(REDLINE_SOLIDIFY_PARAM_T));

	//log_hexdump("USR_SET_FLASH", &s_solid_param, sizeof(REDLINE_SOLIDIFY_PARAM_T));

	return 0;
}

REDLINE_SOLIDIFY_PARAM_T * redline_get_solid_param()
{
	return &s_solid_param;
}

uint8_t redline_get_active_sts()
{
	return s_solid_param.bActiveSts;
}

uint8_t redline_get_lock_sts()
{
	return s_device_sts.lock_sts;
}

uint16_t redline_get_lock_time()
{
	return s_device_sts.lock_times;
}

int redline_get_login_sts()
{
	return s_device_sts.login_sts1;
}

int redline_get_upg_auth_sts()
{
	return s_device_sts.upg_auth;
}

int redline_set_login_sts(int sts)
{
	return s_device_sts.login_sts1 = sts;
}

int redline_get_auth_sts()
{
	return s_device_sts.auth_sts;
}

void str_to_lower(char *str) 
{
    if (str == NULL) 
		return;
    
    for (int i = 0; str[i] != '\0'; i++) 
	{
        str[i] = tolower(str[i]);
    }
}

static int compare_mac(uint16_t macHigh, uint32_t macLow)
{
	unsigned int recvHigh = 0;
	unsigned int recvLow = 0;
	recvHigh = ntohs(macHigh);
	recvLow = ntohl(macLow);
	
	unsigned int myMacHigh = 0;
	unsigned int myMacLow = 0;
	get_mac_address(&myMacHigh, &myMacLow);

	LOGE("cur dev mac high %x, low %x, recv mac hi %x, low %x!\n", 
		myMacHigh, myMacLow, recvHigh, recvLow);

	return (myMacHigh == recvHigh && myMacLow == recvLow) ? 1 : 0;
}

void redline_update_cookie(char *cookie)
{
	LOGI("cookie update %s!\n", cookie);
	strncpy(s_cookie_sts.last_cookie, cookie, sizeof(s_cookie_sts.last_cookie) - 1);
	get_time_now(&s_cookie_sts.update_time, GetTimeMode_LOCAL);
	return;
}

void redline_cookie_monitor_thread()
{
	struct product_data *rt = get_product_data();
	struct timeval time_now = {0};    
	set_pthread_name("cookie_monitor");
	set_thread_core_bind(0);

	strcpy(s_cookie_sts.last_cookie, rt->device_configs.SessionCookie);
	get_time_now(&time_now, GetTimeMode_LOCAL);
	get_time_now(&s_cookie_sts.update_time, GetTimeMode_LOCAL);

	//登录状态下比较cookie
	while(1)
	{
		if (s_device_sts.login_sts1 == LOGGED_OUT)
		{
			memset(s_cookie_sts.last_cookie, 0, sizeof(s_cookie_sts.last_cookie));
			sleep(5);
			continue;
		}		

		if (strncmp(s_cookie_sts.last_cookie, rt->device_configs.SessionCookie, 16) == 0 && strlen(rt->device_configs.SessionCookie) == 16)
		{
			//LOGI("cookie same! wait timeout %d\n", rt->device_configs.CookieLifeTime * 60 - get_dbg_timeDiff(time_now, s_cookie_sts.update_time));
			get_time_now(&time_now, GetTimeMode_LOCAL);
			
			if (get_dbg_timeDiff(time_now, s_cookie_sts.update_time) >= rt->device_configs.CookieLifeTime * 60)
			{
				//在登录成功或者鉴权时置0
				s_device_sts.cookie_timeout_sts = 1;

				LOGI("cookie timeout need auth!\n");
			}
		}

		sleep(5);
	}
}

static void readline_recover_lock_info()
{
	//锁定信息恢复初始值
	s_device_sts.lock_sts = 0;
	s_device_sts.login_times = 0;
	s_device_sts.lock_times = 0;

	return;
}

static void redline_lock_time_thread()
{
	struct timeval time_now = {0};        
	set_pthread_name("lock_time");
	set_thread_core_bind(0);
	struct product_data *rt = get_product_data();

	get_time_now(&time_now, GetTimeMode_LOCAL);

	//获取当前时间与锁定开始时间,锁定时间可配，默认30min,增加客户端节点
	while(get_dbg_timeDiff(time_now, s_device_sts.lock_start_time) < (rt->device_configs.LockTime * 60))
	{
		s_device_sts.lock_times = rt->device_configs.LockTime * 60 - get_dbg_timeDiff(time_now, s_device_sts.lock_start_time);
		LOGD("left lock time %ds!\n", s_device_sts.lock_times);
		get_time_now(&time_now, GetTimeMode_LOCAL);
		sleep(1);
	}

	readline_recover_lock_info();
}

static void redline_lock_proc()
{
	int ret = 0;
	struct product_data *rt = get_product_data();

	s_device_sts.login_times++;
	LOGI("login times %d\n", s_device_sts.login_times);	

	//登录尝试次数超过限制，设备锁定
	if (s_device_sts.login_times >= rt->device_configs.LoginTimes)
	{
		LOGE("device logtimes over, locked !\n");
		if (0 == s_device_sts.lock_sts)	
		{	
			get_time_now(&s_device_sts.lock_start_time, GetTimeMode_LOCAL);
			ret = pthreadSpawn(&login_lock_thread_t, 1, 40, 10 * 1024, redline_lock_time_thread, 1, NULL);
			if (ret < 0)
			{
				LOGE("login_lock_thread_t creation failed!\r\n");
				return;
			}
		}
		s_device_sts.lock_sts = 1;
	}

	return;
}

static void redline_dev_factory()
{
	lua_load_userset(1, 0);

	restore_factory_settings();
	LOGI("redline dev factory !!!\n");
	notify_event(EVENT_REBOOT, EventAction_Pulse);
	set_bootdelay(3);
	reboot_device(0);	
	return;
}

int gvcp_proc_redline_interchange_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, int gvcp_brodcast)
{
	//获取cmd中的mode模式，1激活；2已激活；
	//获取16字节用户名(明文)，384字节公钥(密文)，数据512字节；
	REDLINE_INTERCHANGE_CMD_T recv_data = {0};
	REDLINE_INTERCHANGE_ACK_T ack_data = {0};
	int dataLen = recv_pkt->header.size;
	int idx = 0;
	char randomStr[32] = {0};

	struct product_data *rt = get_product_data();

	size_t outlen = 0;
	unsigned short status = GEV_STATUS_SUCCESS;
	int ret = 0;
	unsigned short mode = 0;

	memcpy(&recv_data, recv_pkt->data, sizeof(recv_data));

	log_hexdump("USR_GET_FLASH", &s_solid_param, sizeof(REDLINE_SOLIDIFY_PARAM_T));

	mode = ntohs(recv_data.mode);

	LOGI("active sts %d! rsq mode %d\n", s_solid_param.bActiveSts, mode);

	do 
	{
		//已激活状态
		if (s_solid_param.bActiveSts == DEV_ACTIVATED)
		{			
			//广播包，但是mac地址不相等
			if (gvcp_brodcast && !compare_mac(recv_data.macHigh, recv_data.maclow))
			{
				*size = 0;
				return 0;
			}
		
			if (mode != REDLINE_ACTIVE)
			{
				LOGE("device actived, and inter mode not auth!\n");
				status = GEV_STATUS_DEVICE_ALREADY_ACTIVE;
				//*size = GVCP_HEADER_SIZE;
			}

			{
				memcpy(ack_data.inter_data.salt, s_solid_param.salt[0], RANDOM_SALT_STR_LEN);
	
				//读取或者生成盐值，生成随机串A；
				generate_random_string(active_random, RANDOM_SALT_STR_LEN+1);
				LOGI("random string :%s!\n", active_random);

				unsigned char e1[256] = {0};
				ret = generate_e1_encrypt(active_random, s_solid_param.salt[0], e1, 256, &outlen);
				if (ret != 0 && status != GEV_STATUS_SUCCESS)
				{
					status = GEV_STATUS_REDLINE_API_FAILED;
				}


                char devE1[8 + 1];

                memset(devE1, 0, sizeof(devE1));
                if (outlen >= 8)
                {
                    strncpy(devE1, e1 + outlen - 8, 8);
                    memcpy(ack_data.devE1, devE1, 8);
                }
				
				LOGE("new e1 %s; outlen %d! devE1 %s\n", e1, outlen, devE1);
				memcpy(&ack_data.inter_data2.RandomString, active_random, RANDOM_SALT_STR_LEN);
				memcpy(ack_pkt->data, &ack_data, sizeof(REDLINE_INTERCHANGE_ACK_T));
				*size = GVCP_HEADER_SIZE + sizeof(REDLINE_INTERCHANGE_ACK_T);
			}
		}
		else if (s_solid_param.bActiveSts == DEV_DEACTIVATED || s_solid_param.bActiveSts == DEV_INIT_STS)
		{
			if (mode != REDLINE_INACTIVE)
			{
				LOGE("device not actived, and inter mode not active!\n");
				status = GEV_STATUS_ACCESS_DENIED;
				*size = GVCP_HEADER_SIZE;
				break;
			}
			else
			{
				unsigned char rsaPubKey[1024] = {0};
			
				//解码拿到公钥
				base64_decode(recv_data.auth_data.publicKey, rsaPubKey);
				LOGD("pub key decode len %d; get rsa pub key %s!\n", strlen(rsaPubKey), rsaPubKey);
	
				//生成随机串A；
				generate_random_string(randomStr, RANDOM_SALT_STR_LEN+1);
				memset(active_random, 0, sizeof(active_random));
				memcpy(active_random, randomStr, RANDOM_SALT_STR_LEN);
				LOGE("random string :%s! active_random %s, random len %d\n", randomStr, active_random, strlen(randomStr));

	
				//使用RSA公钥对随机串进行加密,会直接进行base64编码输出
				ret = rsa_encrypt_by_key(rsaPubKey, randomStr, strlen(randomStr), ack_data.inter_data2.enRandomString, &outlen);
				if (ret != 0)
				{
					LOGE("rsa encrypt failed! ret %d\n", ret);
					status = GEV_STATUS_REDLINE_API_FAILED;
					*size = GVCP_HEADER_SIZE;
					break;
				}
				LOGE("enlen %d , rsa en string :%s \n", outlen, ack_data.inter_data2.enRandomString);
					
				memcpy(ack_pkt->data, &ack_data, sizeof(REDLINE_INTERCHANGE_ACK_T));
				*size = GVCP_HEADER_SIZE + sizeof(REDLINE_INTERCHANGE_ACK_T);
			}	
		}
	}while(0);

	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_INTERCHANGE_ACK, *size, pkt_id);
	log_hexdump("INTER_ACK", ack_pkt, *size);
	
	return 0;
}

int gvcp_proc_redline_active_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size)
{
	REDLINE_ACTIVE_CMD_T acitve_data = {0}; 
	REDLINE_ACTIVE_ACK_T active_ack = {0};
	char deBasePassword[32] = {0};
	char deAesPassword[32] = {0};
	size_t outlen = 0;
	int status = GEV_STATUS_SUCCESS;
	int ret = -1;

	memcpy(&acitve_data, recv_pkt->data, sizeof(acitve_data));

	LOGD("enPassword %s!\n", acitve_data.enPassword);

	do
	{
		ret = aes128_cbc_decrypt(active_random, active_random, acitve_data.enPassword, strlen(acitve_data.enPassword), deAesPassword, &outlen);
		if (ret != 0)
		{
			LOGE("aes128_cbc_decrypt failed! ret %d!\n", ret);
			status = GEV_STATUS_REDLINE_API_FAILED;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		//LOGE("aes decrypt data %s! outlen %d\n", deAesPassword, outlen);
		deAesPassword[outlen] = '\0';

        //1.3 对密码进行合法性校验，客户端会进行校验，这里再次确认
        if (!checkPasswd("root", deAesPassword))
        {
            LOGE("password check failed!!\n");
            status = GEV_STATUS_PWD_FMT_INVALID;
            *size = GVCP_HEADER_SIZE;
            break;
        }

		//1.4 生成盐值，和密码进行En计算
		//generate_random_string(s_solid_param.salt[0], RANDOM_SALT_STR_LEN+1);
		unsigned char e1[256] = {0};
		unsigned char salt[RANDOM_SALT_STR_LEN+1] = {0};

		generate_random_string(salt, RANDOM_SALT_STR_LEN+1);
		LOGE("salt: %s! salt len %d\n", salt, strlen(salt));
		//LOGE("gene1 password %s!  pwd len %d!\n", deAesPassword, strlen(deAesPassword));

		ret = generate_e1_encrypt(deAesPassword, salt, e1, 256, &outlen);
		if (ret != 0)
		{
			LOGE("generate_e1_encrypt failed! ret %d!\n", ret);
			status = GEV_STATUS_REDLINE_API_FAILED;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		LOGE("e1 %s; outlen %d!\n", e1, outlen);

		memcpy(s_solid_param.userName[0], "root", 4);
		memcpy(s_solid_param.password[0], deAesPassword, strlen(deAesPassword) < PWD_MAX_LENGTH ? strlen(deAesPassword) : PWD_MAX_LENGTH);
		memcpy(s_solid_param.salt[0], salt, strlen(salt));
        memcpy(s_solid_param.e1[0], e1, strnlen(e1, sizeof(s_solid_param.e1[0]) - 1));
		s_solid_param.bActiveSts = 1;		//0未激活，1已激活，FF异常状态

		//LOGE("s_solid_param userName %s password %s\n", s_solid_param.userName[0], s_solid_param.password[0]);

		//1.6 将用户名和密码设置给BSP(Uboot和Kernel)
		ret = bsp_set_passwd(s_solid_param.userName[0], s_solid_param.password[0]);
		if (ret != 0)
		{
			LOGE("set bsp passed failed! ret %d!\n", ret);
			status = GEV_STATUS_BSP_SECURE_FAILED;
			*size = GVCP_HEADER_SIZE;
            memset(&s_solid_param, 0, sizeof(s_solid_param));
            break;
		}	

		redline_set_flash_solid_param();

		memcpy(active_ack.salt, salt, RANDOM_SALT_STR_LEN);
		memcpy(ack_pkt->data, &active_ack, sizeof(REDLINE_ACTIVE_ACK_T));
		*size = GVCP_HEADER_SIZE + sizeof(REDLINE_ACTIVE_ACK_T);
	}while(0);

	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_ACTIVE_ACK, *size, pkt_id);
	log_hexdump("ACT_ACK", ack_pkt, *size);

	return 0;
}

int gvcp_proc_redline_retore_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		 unsigned short pkt_id, int *size)
{
	//仅在lla清空下可执行，并且处于已登录状态；否则无法执行；待实现
	REDLINE_SESTORE_CMD_T restore_cmd = {0}; 
	unsigned char myEn[128] = {0};
	int outEnLen = 0;
	int status = GEV_STATUS_SUCCESS;
	int ret = 0;

	memcpy(&restore_cmd, recv_pkt->data, sizeof(restore_cmd));

	do
	{
		//锁定状态直接回复，不处理
		if (s_device_sts.lock_sts)
		{
			LOGE("device locked, not respond!\n");
			status = GEV_STSTUS_LOCKED_DENIED;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		if (redline_get_active_sts() != DEV_ACTIVATED)
		{
			LOGE("device not active, can't deactive!\n");
			status = GEV_STATUS_DEVICE_NOT_ACTIVE;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		ret = generate_en_encrypt(s_solid_param.e1[0], active_random, myEn, 128, &outEnLen);
		if (ret != 0)
		{
			LOGE("generate en failed! ret %d!\n", ret);
			status = GEV_STATUS_REDLINE_API_FAILED;
			break;
		}

		LOGE("myEn %s! restore_cmd.enString %s!\n", myEn, restore_cmd.enString);

		if (strncmp(myEn, restore_cmd.enString, 64) == 0)
		{
			LOGE("en compare ok!\n");
			//校验成功，恢复未激活，删除uboot、kernel密码
			memset(&s_solid_param, 0, sizeof(s_solid_param));
			strncpy(s_solid_param.userName[0], "root", 4);

			//恢复未激活时，设置给BSP(Uboot和Kernel)的密码也重置掉	
			ret = bsp_reset_passwd();
			if (ret != 0)
			{
				LOGE("bsp reset passwd failed! ret %d!\n", ret);
                status = GEV_STATUS_BSP_SECURE_FAILED;
                break;
			}

			redline_set_flash_solid_param();

			readline_recover_lock_info();

			//恢复未激活时出厂设置
			redline_dev_factory();			
		}
		else
		{
			LOGE("en compare failed!\n");
		
			redline_lock_proc();

			status = GEV_STATUS_PWD_VERIFY_FAILED;
			break;
		}

	}while(0);

	*size = GVCP_HEADER_SIZE;
	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_RESTORE_ACK, *size, pkt_id);

	log_hexdump("RESTORE_ACK", ack_pkt, *size);
	return 0;
}

static void auth_sts_timeout_thread()
{
	struct timeval time_now = {0};        
	set_pthread_name("auth_timeout");
	set_thread_core_bind(0);
	struct product_data *rt = get_product_data();

	get_time_now(&time_now, GetTimeMode_LOCAL);

	//获取当前时间与锁定开始时间,默认3min
	while(get_dbg_timeDiff(time_now, s_device_sts.auth_start_time) < (3 * 60))
	{
		LOGD("left auth use time %ds!\n", 180 - get_dbg_timeDiff(time_now, s_device_sts.auth_start_time));
		get_time_now(&time_now, GetTimeMode_LOCAL);
		sleep(10);
	}

	s_device_sts.auth_sts = 0;
}

int gvcp_proc_redline_login_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, int gvcp_brodcast)
{
	REDLINE_LOGIN_ACK_T login_ack = {0};
	REDLINE_LOGIN_CMD_T login_cmd = {0};
	unsigned char myEn[128] = {0};
	unsigned char myAuthEn[128] = {0};
	int outEnLen = 0;
	int status = GEV_STATUS_SUCCESS;
	int ret = 0;
	unsigned short function = 0;
	struct product_data *rt = get_product_data();

	memcpy(&login_cmd, recv_pkt->data, sizeof(REDLINE_LOGIN_CMD_T));
	function = ntohs(login_cmd.Function);

	LOGE("login sts %d;  lock sts %d! lock time %d! function %d\n", s_device_sts.login_sts1, s_device_sts.lock_sts, s_device_sts.lock_times, function);

	struct timeval now;
	struct timeval start;

	get_time_now(&start, GetTimeMode_LOCAL);

	do
	{				
		//锁定状态直接回复，不处理
		if (s_device_sts.lock_sts)
		{
			status = GEV_STSTUS_LOCKED_DENIED;
			break;
		}
		else
		{
			LOGE("generate en param e1:%s; random string %s! salt %s\n", s_solid_param.e1[0], active_random, s_solid_param.salt[0]);
			
			ret = generate_en_encrypt(s_solid_param.e1[0], active_random, myEn, 128, &outEnLen);
			if (ret != 0)
			{
				LOGE("generate en failed! ret %d!\n", ret);
				status = GEV_STATUS_REDLINE_API_FAILED;
				break;
			}

			get_time_now(&now, GetTimeMode_LOCAL);
			LOGE("auth en cost %dms!\n", get_dbg_timeDiff(now, start));

			LOGE("myEn %s, outlen %d; login En %s!\n", myEn, outEnLen, login_cmd.enString);

			if (strncmp(myEn, login_cmd.enString, 64) == 0)
			{
				LOGE("en compare ok!\n");

				if (function == LOGIN_CMD_LOGIN)
				{
					readline_recover_lock_info();


					s_device_sts.login_sts1 = LOGGED_IN;
					s_device_sts.cookie_timeout_sts = 0;
					memcpy(rt->device_configs.SessionCookie, active_random, RANDOM_SALT_STR_LEN);

					//sc和支持tcp fileaccess的需要生成端口协商；
					login_ack.keepAlivePort = htons(GVCP_PORT);
					login_ack.fileAccessPort = htons(GVCP_PORT);

					memcpy(login_ack.cookie, active_random, RANDOM_SALT_STR_LEN);
					break;
				}
				else if (function == LOGIN_CMD_AUTH)
				{
					if (gvcp_brodcast && !compare_mac(login_cmd.macHigh, login_cmd.maclow))
					{
						*size = 0;
						return 0;
					}
					
					readline_recover_lock_info();

					s_device_sts.auth_sts = 1;
					s_device_sts.cookie_timeout_sts = 0;
					get_time_now(&s_device_sts.auth_start_time, GetTimeMode_LOCAL);

					ret = pthreadSpawn(&auth_timeout_thread_t, 1, 40, 10 * 1024, auth_sts_timeout_thread, 1, NULL);
					if (ret < 0)
					{
						LOGE("login_lock_thread_t creation failed!\r\n");
					}

					break;
				}
			}
			else	//校验失败
			{
				LOGE("en compare failed!\n");

				redline_lock_proc();				
				
				status = GEV_STATUS_PWD_VERIFY_FAILED;
				break;
			}
		}
	}while(0);

	if (function == LOGIN_CMD_AUTH)
	{
		memcpy(myAuthEn, login_cmd.enString, 64);
		myAuthEn[64] = '\0';

		ret = generate_en_encrypt(myAuthEn, active_random, myEn, 128, &outEnLen);	
		if (ret != 0)
		{
			LOGE("generate en failed! ret %d!\n", ret);		
			status = GEV_STATUS_REDLINE_API_FAILED;
		}
	}

    char devEn[8 + 1];

    memset(devEn, 0, sizeof(devEn));
    if (outEnLen >= 8)
    {
        strncpy(devEn, myEn + outEnLen - 8, 8);
        memcpy(login_ack.devEn, devEn, 8);
    }

	LOGE("dev en %s, login ack en %s!\n", devEn, myEn);                                                              
	
	memcpy(ack_pkt->data, &login_ack, sizeof(REDLINE_LOGIN_ACK_T)); 	

	get_time_now(&now, GetTimeMode_LOCAL);
	LOGE("after proc cost %dms!\n", get_dbg_timeDiff(now, start));

	*size = GVCP_HEADER_SIZE + sizeof(REDLINE_LOGIN_ACK_T);
	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_LOGIN_ACK, *size, pkt_id);
	log_hexdump("LOGIN_ACK", ack_pkt, *size);

	return 0;
}

int gvcp_proc_redline_changepwd_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size)
{
    REDLINE_CHANGEPWD_CMD_T changepwd_cmd = {0};
    unsigned char myEn[128] = {0};
    unsigned char newPwd[32] = {0};
    unsigned char newEnPwd[32] = {0};
    unsigned char tmpBuf[128] = {0};
    int outEnLen = 0;
    int status = GEV_STATUS_SUCCESS;
    int ret = -1;
    size_t outlen = 0;

	memcpy(&changepwd_cmd, recv_pkt->data, sizeof(REDLINE_CHANGEPWD_CMD_T));

	LOGE("login sts %d!\n", s_device_sts.login_sts1);

	do
	{
		//锁定状态直接回复，不处理
		if (s_device_sts.lock_sts)
		{
			LOGE("device locked, not respond!\n");
			status = GEV_STSTUS_LOCKED_DENIED;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		if (s_device_sts.login_sts1 == LOGGED_OUT)
		{
			status = GEV_STATUS_NEED_AUTH;
			break;
		}
		else
		{	
			//修改密码一定是处于登录状态下，若是在登录状态下修改也失败的话，需要退出登录并锁定,暂不实现
			ret = generate_en_encrypt(s_solid_param.e1[0], active_random, myEn, 128, &outEnLen);
			if (ret != 0)
			{
				LOGE("generate en failed! ret %d!\n", ret);
				status = GEV_STATUS_REDLINE_API_FAILED;
				break;
			}

            memset(tmpBuf, 0, sizeof(tmpBuf));
            memcpy(tmpBuf, changepwd_cmd.en1String, sizeof(changepwd_cmd.en1String));
			LOGE("myen %s; random string %s; en1string %s!\n", myEn, active_random, tmpBuf);
			LOGE("save e1 %s! salt %s\n", s_solid_param.e1[0], s_solid_param.salt[0]);
			memcpy(newEnPwd, changepwd_cmd.newPassword, sizeof(changepwd_cmd.newPassword));
			newEnPwd[sizeof(changepwd_cmd.newPassword)] = '\0';

			//1.2 En1和本地En进行比较，验证通过后将En2进行保存
			if (strncmp(myEn, changepwd_cmd.en1String, 64) == 0)
			{			
				//aes解密
				ret = aes128_cbc_decrypt(active_random, active_random, newEnPwd, strlen(newEnPwd), newPwd, &outlen);
				if (ret != 0)
				{
					LOGE("aes128_cbc_decrypt failed! ret %d!\n", ret);
					status = GEV_STATUS_REDLINE_API_FAILED;
					break;
				}

				//LOGE("new pwd %s! outlen %d strlen %d!\n", newPwd, outlen, strlen(newPwd));
				newPwd[outlen] = '\0';
				
				//校验密码合法性
                //1.3 对密码进行合法性校验，客户端会进行校验，这里再次确认
                if (!checkPasswd(s_solid_param.userName[0], newPwd))
                {
                    LOGE("password check failed!!\n");
                    status = GEV_STATUS_PWD_FMT_INVALID;
                    *size = GVCP_HEADER_SIZE;
                    break;
                }

				//生成新盐值
				memset(s_solid_param.salt[0], 0, RANDOM_SALT_STR_LEN);
				memset(s_solid_param.e1[0], 0, PWD_E1_STR_LEN);
				memset(s_solid_param.password[0], 0, 16);

				generate_random_string(s_solid_param.salt[0], RANDOM_SALT_STR_LEN+1);
				
				memcpy(s_solid_param.password[0], newPwd, strnlen(newPwd, PWD_MAX_LENGTH));

				//进行E1计算
				ret = generate_e1_encrypt(newPwd, s_solid_param.salt[0], s_solid_param.e1[0], 128, &outEnLen);
				if (ret != 0)
				{
					LOGE("generate e1 failed! ret %d!\n", ret);
					status = GEV_STATUS_REDLINE_API_FAILED;
					break;
				}

				//密码修改同步给BSP(Uboot和Kernel)
				ret = bsp_set_passwd(s_solid_param.userName[0], s_solid_param.password[0]);
				if (ret != 0)
				{
					LOGE("set bsp passed failed! ret %d!\n", ret);
					status = GEV_STATUS_BSP_SECURE_FAILED;
					break;
				}

				//保存到文件中
				redline_set_flash_solid_param();

				s_device_sts.login_sts1 = LOGGED_OUT;

				readline_recover_lock_info();
			}
			else
			{
				LOGE("compare en failed!\n");

				redline_lock_proc();

				status = GEV_STATUS_PWD_VERIFY_FAILED;
				break;
			}	
		}
	}while(0);

	*size = GVCP_HEADER_SIZE;
	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_CHANGEPWD_ACK, *size, pkt_id);
	log_hexdump("CHANGEPWD", ack_pkt, *size);
	return 0;
}

int gvcp_proc_redline_getdevinfo_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size)
{
	REDLINE_GETDEVINFO_ACK_T devInfo_ack = {0};
	char device_id[64] = {0};
	unsigned char en_dev_id[1280] = {0};
	unsigned char reset_buf[1280] = {0};
	unsigned char reset_pwd[8] = {0};
	uint32_t rsaVer = 1;
	int ret = 0;
	int dev_id_len = 0;
	size_t outlen = 0;
	int status = GEV_STATUS_SUCCESS;

	struct product_data *rt = get_product_data();

	do
	{
		//锁定状态直接回复，不处理
		if (s_device_sts.lock_sts)
		{
			LOGE("device locked, not respond!\n");
			status = GEV_STSTUS_LOCKED_DENIED;
			*size = GVCP_HEADER_SIZE;
			break;
		}
	
		if (rt->device_configs.ResetPwd == 0)
		{
			LOGE("reset pwd not enable!\n");
			status = GEV_STATUS_NETENV_INVALID;
			*size = GVCP_HEADER_SIZE;
			break;	
		}
	
		devInfo_ack.rsaVer = htonl(23);
		LOGE("serial len %d!\n", strlen(rt->burn_info.serial_num));
	
		ret = generate_encryptstring(DEV_RESET_PWD_RSAPK_PATH, rt->burn_info.serial_num, en_dev_id, &outlen, resetpwd_token);
		if (ret != 0)
		{
			LOGE("generate_encryptstring failed! ret %d\n", ret);
			status = GEV_STATUS_REDLINE_API_FAILED;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		str_to_lower(resetpwd_token);
		LOGE("resetpwd token %s!\n", resetpwd_token);
		LOGE("en_dev_id len %d! en_dev_id :%s \r\n", strlen(en_dev_id), en_dev_id);

		memcpy(devInfo_ack.encryptedString, en_dev_id, 1024);
		memcpy(ack_pkt->data, &devInfo_ack, sizeof(REDLINE_GETDEVINFO_ACK_T));
		*size = GVCP_HEADER_SIZE + sizeof(REDLINE_GETDEVINFO_ACK_T);
	}while(0);


	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_GETDEVINFO_ACK, *size, pkt_id);
	log_hexdump("GETDEVINFO", ack_pkt, *size);

	return 0;
}

int gvcp_proc_redline_resetpwd_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, ip_addr_t peer_ip)
{
	REDLINE_RESETPWD_CMD_T resetpwd_cmd = {0};
	int status = GEV_STATUS_SUCCESS;
	char my_token_sha[64] = {0};
	char aes_sha_data[128] = {0};
	char aes_key_sha[128] = {0};
	char aes_de_data[128] = {0};
	char new_pwd[16] = {0};
	char enData[128] = {0};
	unsigned char e1[256] = {0};
	unsigned char salt[RANDOM_SALT_STR_LEN+1] = {0};
	size_t outlen = 0;
	int ret = 0;

	memcpy(&resetpwd_cmd, recv_pkt->data, sizeof(REDLINE_RESETPWD_CMD_T));

	do 
	{
		LOGE("random string:%s, token %s!\n", resetpwd_cmd.ivString, resetpwd_token);

		//组装客户端发送的32位随机串+重置口令，执行sha256结果，取前16位作为aes秘钥
		snprintf(aes_sha_data, sizeof(aes_sha_data), "%s%.8s", resetpwd_cmd.ivString, resetpwd_token);
		LOGE("aes sha data:%s! strlen %d\n", aes_sha_data, strlen(aes_sha_data));

		ret = sha_string(aes_sha_data, aes_key_sha, &outlen);
		if (ret != 0)
		{
			LOGE("sha string not ok. ret %d!\n", ret);
			status = GEV_STATUS_REDLINE_API_FAILED;
			break;
		}

		aes_key_sha[16] = '\0';
		LOGE("len %d; aes sha data %s! aes key sha %s\n", strlen(aes_sha_data), aes_sha_data, aes_key_sha);

		memcpy(enData, resetpwd_cmd.encryptString, 64);
		enData[64] = '\0';
		LOGE("encrypt string %s!\n", enData);

		//aes解密得到客户端的随机串D+新密码
		ret = aes128_cbc_decrypt(aes_key_sha, aes_key_sha, enData, strlen(enData), aes_de_data, &outlen);
		if (ret != 0)
		{
			LOGE("aes128_cbc_decrypt failed. ret %d!\n", ret);
			status = GEV_STATUS_REDLINE_API_FAILED;
			break;
		}

		aes_de_data[outlen] = '\0';
		//LOGE("aes de data %s! outlen %d\n", aes_de_data, outlen);

		//校验解密的随机串和客户端发送的随机串，如果相等，将新密码计算E1保存
		if (strncmp(aes_de_data, resetpwd_cmd.ivString, 32) != 0)
		{
			LOGE("verify random string failed!\n");
			status = GEV_STATUS_PWD_VERIFY_FAILED;
			break;
		}

		memset(resetpwd_token, 0, sizeof(resetpwd_token));		//成功之后，清空重置口令，避免口令泄露多次修改，失败不处理

		//校验通过，将新密码做E1计算,进行保存
		memcpy(new_pwd, aes_de_data+32, outlen - 32);
		generate_random_string(salt, RANDOM_SALT_STR_LEN+1);
		//LOGE("salt: %s! salt len %d, new pwd %s, pwd len %d\n", 
			//salt, strlen(salt), new_pwd, strlen(new_pwd));

		if (!checkPasswd(s_solid_param.userName[0], new_pwd))
		{
			LOGE("password check failed!!\n");
			status = GEV_STATUS_PWD_FMT_INVALID;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		ret = generate_e1_encrypt(new_pwd, salt, e1, 256, &outlen);
		if (ret != 0)
		{
			LOGE("generate_e1_encrypt failed! ret %d!\n", ret);
			status = GEV_STATUS_REDLINE_API_FAILED;
			break;
		}

		memcpy(s_solid_param.password[0], new_pwd, strlen(new_pwd));
		memcpy(s_solid_param.salt[0], salt, strlen(salt));
		memcpy(s_solid_param.e1[0], e1, sizeof(s_solid_param.e1[0]));

		//重置密码修改同步给BSP(Uboot和Kernel)
		ret = bsp_reset_passwd();
		if (ret != 0)
		{
			LOGE("reset bsp passed failed! ret %d!\n", ret);
			status = GEV_STATUS_BSP_SECURE_FAILED;
			break;
		}

		redline_set_flash_solid_param();

		readline_recover_lock_info();

	}while(0);

	*size = GVCP_HEADER_SIZE;
	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_GETDEVINFO_ACK, *size, pkt_id);

	return 0;
}

int gvcp_proc_redline_upg_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size)
{
	//REDLINE_UPGRADE_ACK_T upg_ack = {0};
	REDLINE_UPGRADE_CMD_T upg_cmd = {0};
	unsigned char myEn[128] = {0};
	int outEnLen = 0;
	int status = GEV_STATUS_SUCCESS;
	int ret = 0;

	memcpy(&upg_cmd, recv_pkt->data, sizeof(REDLINE_UPGRADE_CMD_T));

	LOGE("active sts %d!\n", s_solid_param.bActiveSts);
	
	//锁定状态直接回复，不处理
	do
	{
		//锁定状态直接回复，不处理
		if (s_device_sts.lock_sts)
		{
			LOGE("device locked, not respond!\n");
			status = GEV_STSTUS_LOCKED_DENIED;
			*size = GVCP_HEADER_SIZE;
			break;
		}

		if (s_solid_param.bActiveSts != DEV_ACTIVATED)
		{
			status = GEV_STATUS_DEVICE_NOT_ACTIVE;
			*size = GVCP_HEADER_SIZE;
			break;
		}
		else
		{
			LOGE("generate en param e1:%s; random string %s!\n", s_solid_param.e1[0], active_random);
			
			ret = generate_en_encrypt(s_solid_param.e1[0], active_random, myEn, 128, &outEnLen);
			if (ret != 0)
			{
				LOGE("generate en failed! ret %d!\n", ret);
				status = GEV_STATUS_REDLINE_API_FAILED;
				break;
			}

			LOGE("myEn %s, outlen %d; upg En %s!\n", myEn, outEnLen, upg_cmd.encryptString);

			if (strncmp(myEn, upg_cmd.encryptString, 64) == 0)
			{
				LOGE("en compare ok! upg port is %d!\n", ntohs(upg_cmd.port));
				//升级鉴权状态
				s_device_sts.upg_auth = 1;

				readline_recover_lock_info();
			}
			else	//校验失败
			{
				LOGE("en compare failed!\n");
				
				redline_lock_proc();

				status = GEV_STATUS_PWD_VERIFY_FAILED;
				break;
			}
		}
	}while(0);

	*size = GVCP_HEADER_SIZE;
	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_UPGRADE_ACK, *size, pkt_id);
	log_hexdump("UPG_ACK", ack_pkt, *size);

	return 0;
}

int gvcp_proc_redline_getlockinfo_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, int gvcp_brodcast)
{
	REDLINE_LOCK_ACK_T lock_ack = {0};
	REDLINE_LOCK_CMD_T lock_cmd = {0};
	int status = GEV_STATUS_SUCCESS;
	struct product_data *rt = get_product_data();

	memcpy(&lock_cmd, recv_pkt->data, sizeof(REDLINE_LOCK_CMD_T));

	LOGI("lock sts %d, lock sts %d, lock time %d, faied times %d, remain times %d\n", 
		s_device_sts.login_sts1, s_device_sts.lock_sts, s_device_sts.lock_times, s_device_sts.login_times, 
		rt->device_configs.LoginTimes - s_device_sts.login_times);

	do
	{				
		if (gvcp_brodcast && !compare_mac(lock_cmd.macHigh, lock_cmd.macLow))
		{
			LOGE("mac address not match!\n");
			*size = 0;
			return 0;
		}

		lock_ack.lockSts = htons(s_device_sts.lock_sts);
		lock_ack.lockTime = htons(s_device_sts.lock_times);
		lock_ack.loginFaiedTimes = htons(s_device_sts.login_times);
		lock_ack.remainLogTimes = htons(rt->device_configs.LoginTimes - s_device_sts.login_times);	
		lock_ack.macHigh = lock_cmd.macHigh;
		lock_ack.macLow = lock_cmd.macLow;	
	}while(0);       

	memcpy(ack_pkt->data, &lock_ack, sizeof(REDLINE_LOCK_ACK_T)); 	

	*size = GVCP_HEADER_SIZE + sizeof(REDLINE_LOCK_ACK_T);
	redline_gvcp_fill_ack_header(&ack_pkt->header, status, GVCP_COMMAND_GETLOCKINFO_ACK, *size, pkt_id);
	log_hexdump("LOCK_ACK", ack_pkt, *size);

	return 0;
}

int gvcp_proc_redline_default_active(void)
{
#ifdef __PACK_OFFLINE
    do
    {
        if (s_solid_param.bActiveSts == DEV_DEACTIVATED)
        {
            //1.1 生成盐值，和密码进行En计算
            int ret = -1;
            size_t outlen = 0;
            unsigned char e1[256] = {0};
            unsigned char salt[RANDOM_SALT_STR_LEN+1] = {0};

            generate_random_string(salt, RANDOM_SALT_STR_LEN+1);
            LOGE("salt: %s! salt len %d\n", salt, strlen(salt));

            ret = generate_e1_encrypt("produce0", salt, e1, 256, &outlen);
            if (ret != 0)
            {
                LOGE("generate_e1_encrypt failed! ret %d!\n", ret);
                return -1;
            }

            LOGE("e1 %s; outlen %d!\n", e1, outlen);

            memset(&s_solid_param, 0, sizeof(s_solid_param));
            memcpy(s_solid_param.userName[0], "root", 4);
            memcpy(s_solid_param.password[0], "produce0", strlen("produce0"));
            memcpy(s_solid_param.salt[0], salt, strlen(salt));
            memcpy(s_solid_param.e1[0], e1, strlen(e1));
            s_solid_param.bActiveSts = 1; //0未激活，1已激活，FF异常状态

            //1.2 将用户名和密码设置给BSP(Uboot和Kernel)
            ret = bsp_set_passwd(s_solid_param.userName[0], s_solid_param.password[0]);
            if (ret != 0)
            {
                LOGE("set bsp passed failed! ret %d!\n", ret);
                return -1;
            }

            redline_set_flash_solid_param();
        }
    }
    while(0);
#endif

    return 0;
}

