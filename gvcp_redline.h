#ifndef _GVCP_REDLINE_H_
#define _GVCP_REDLINE_H_

#include <stdbool.h>
#include <stdio.h>

#include "gvcp_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RANDOM_SALT_STR_LEN			(16)
#define PWD_MAX_LENGTH				(16)
#define PWD_E1_STR_LEN				(32)
#define REDLINE_LOGIN_RETRY_TIME	(5)


/*相机红线固化保存数据*/
typedef struct
{
	uint8_t userName[8][16];		//默认最多支持8个用户，用户0默认为管理员
	uint8_t password[8][16];
	uint8_t salt[8][20];			//8个盐值，跟用户名一一对应
	uint8_t e1[8][68];				//8个e1，跟用户名一一对应
	uint8_t	bActiveSts;				//设备激活状态，激活或者去激活时写入，上电读取
	uint32_t reserved2[32];			//保留参数
}REDLINE_SOLIDIFY_PARAM_T;

typedef struct
{
	int login_sts1;
	int login_times;			//同一个用户名尝试登录次数，若是超过五次失败，锁定时间到了之后恢复
	int auth_sts;				//鉴权标志位，修改IP鉴权完成之后，置为1，有效期3分钟，3分钟之后置0
	int cookie_timeout_sts;		//cookie超时状态，提示需要回复鉴权
	int upg_auth;				//升级鉴权状态
	uint8_t lock_sts;			//锁定状态
	uint8_t reserved1; 
	uint16_t lock_times; 		//剩余锁定时间
	struct timeval lock_start_time;
	struct timeval auth_start_time;
}REDLINE_DEVICE_STS_T;

typedef struct
{
	char last_cookie[32];
	struct timeval update_time;
}REDLINE_COOKIE_UPDATE_T;

/*交换码命令数据*/
typedef struct 
{
	uint16_t mode;
	uint16_t reserved1;
    uint16_t reserved2;
    uint16_t macHigh;
	uint32_t maclow;
    union {
        uint8_t userName[16];   // 16字节用户名
        uint8_t publicKey[896]; // 512传输字节，384实际公钥字节
    } auth_data;  
}REDLINE_INTERCHANGE_CMD_T;

/*交换码回复数据*/
typedef struct 
{
	uint32_t reserved1;
    union {
		uint8_t  salt[16];
		uint8_t  reserved2[16];
    } inter_data;  
    union {
		uint8_t  enRandomString[512];
		uint8_t  RandomString[16];
    } inter_data2;
	uint8_t devE1[8];			//用随机串加盐计算E1，取后8字节
}REDLINE_INTERCHANGE_ACK_T;

/*激活命令数据*/
typedef struct
{
	uint32_t reserved1;
	uint8_t  username[16];
	uint8_t	 enPassword[24];
	uint8_t  reserved2[12];
}REDLINE_ACTIVE_CMD_T;


/*激活回复数据*/
typedef struct
{
	uint32_t reserved1;
	uint8_t  salt[16];
}REDLINE_ACTIVE_ACK_T;


/*恢复激活命令数据*/
typedef struct
{
	uint32_t reserved1;
	uint8_t  enString[64];
}REDLINE_SESTORE_CMD_T;


/*激活回复数据*/
typedef struct
{
	uint32_t reserved1;
}REDLINE_SESTORE_ACK_T;

/*登录命令数据*/
typedef struct
{
	uint16_t Function;
	uint16_t reserved1;
	uint16_t reserved2;
    uint16_t macHigh;
	uint32_t maclow;
	uint8_t  enString[64];
}REDLINE_LOGIN_CMD_T;


/*登录回复数据*/
typedef struct
{
	uint16_t keepAlivePort;
	uint16_t fileAccessPort;
	uint16_t loginFaiedTimes;
	uint16_t remainLogTimes;
	uint8_t reserved1[8];
	uint8_t cookie[16];
	uint8_t devEn[8];		//cookie作为随机串对En进行En计算取后8字节
}REDLINE_LOGIN_ACK_T;

/*修改密码命令数据*/
typedef struct
{
	uint32_t reserved1;
	uint8_t  en1String[64];					//旧密码En计算后的摘要，密文
	uint8_t  newPassword[24];				//新密码经过AES加密且base64编码
}REDLINE_CHANGEPWD_CMD_T;


/*修改密码回复数据*/
typedef struct
{

}REDLINE_CHANGEPWD_ACK_T;


/*获取设备状态命令数据*/
typedef struct
{
	uint32_t reserved1;
}REDLINE_GETDEVINFO_CMD_T;


/*获取设备状态回复数据*/
typedef struct
{
	uint32_t reserved1;
	int32_t rsaVer;
	uint8_t encryptedString[1024];				//加密设备串

}REDLINE_GETDEVINFO_ACK_T;


/*重置密码命令数据*/
typedef struct
{
	uint32_t reserved1;
	uint8_t encryptString[64];					//加密串,密文
	uint8_t ivString[32];						
}REDLINE_RESETPWD_CMD_T;


/*重置密码状态回复数据*/
typedef struct
{

}REDLINE_RESETPWD_ACK_T;


/*升级命令数据*/
typedef struct
{
	uint16_t port;
	uint16_t reserved1;
	uint8_t encryptString[64];					
}REDLINE_UPGRADE_CMD_T;


/*升级状态回复数据*/
typedef struct
{
	uint16_t upgPort;
	uint16_t reserved;
}REDLINE_UPGRADE_ACK_T;


typedef enum
{
	LOGGED_OUT = 0,
	LOGGED_IN,
}REDLINE_DEVICE_LOGIN_STS_E;

typedef enum
{
	DEV_DEACTIVATED	= 0,
	DEV_ACTIVATED,
	DEV_INIT_STS = 0xff,
}REDLINE_DEVICE_ACTIVE_STS_E;

typedef enum
{
	LOGIN_CMD_LOGIN	= 0,		//登录验证
	LOGIN_CMD_AUTH,				//只鉴权不登录
}REDLINE_LOGIN_CMD_TYPE_E;

typedef enum {
    REDLINE_INACTIVE = 1,
    REDLINE_ACTIVE = 2
}REDLINE_INTERCHW_CMD_TYPE_E;

/*获取锁定信息命令数据*/
typedef struct
{
	uint16_t reserved1;
    uint16_t macHigh;			//设备MAC高16位
	uint32_t macLow;			//设备MAC低32位
}REDLINE_LOCK_CMD_T;

/*锁定信息回复数据*/
typedef struct
{
	uint8_t lockSts;			//锁定状态
	uint8_t reserved1; 
	uint16_t lockTime; 			//剩余锁定时间, 单位：s	
	uint16_t loginFaiedTimes;	//登录失败次数
	uint16_t remainLogTimes;	//剩余登录尝试次数
	uint16_t reserved2;	
    uint16_t macHigh;			//设备MAC高16位
	uint32_t macLow;			//设备MAC低32位	
}REDLINE_LOCK_ACK_T;

REDLINE_SOLIDIFY_PARAM_T * redline_get_solid_param();
int redline_set_flash_solid_param();
int redline_get_flash_solid_param();
uint8_t redline_get_active_sts();
uint8_t redline_get_lock_sts();
uint16_t redline_get_lock_time();
int redline_get_login_sts();
int redline_set_login_sts(int sts);
int redline_get_auth_sts();
int redline_get_upg_auth_sts();
const uint8_t * redline_get_init_dropbear_cmd();
void redline_cookie_monitor_thread();
void redline_update_cookie(char *cookie);
bool redline_is_sup_reg_operation();

int gvcp_proc_redline_interchange_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, int gvcp_brodcast);

int gvcp_proc_redline_active_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size);

int gvcp_proc_redline_retore_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size);

int gvcp_proc_redline_login_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, int gvcp_brodcast);

int gvcp_proc_redline_changepwd_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size);

int gvcp_proc_redline_changepIP_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size);

int gvcp_proc_redline_resetpwd_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, ip_addr_t peer_ip);

int gvcp_proc_redline_upg_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size);

int gvcp_proc_redline_getdevinfo_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size);

int gvcp_proc_redline_getlockinfo_cmd(const struct gvcp_packet *recv_pkt, struct gvcp_packet *ack_pkt,
		unsigned short pkt_id, int *size, int gvcp_brodcast);

int gvcp_proc_redline_default_active(void);

#ifdef __cplusplus
}
#endif

#endif
