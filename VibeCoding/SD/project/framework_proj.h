/** @file
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   project demo
  *
  * @author  luxianguan
  * @date    2019/08/30
  *
  * @version
  *  date             |version |author          |message
  *  :----            |:----   |:----           |:------
  *  2019/08/30       |V1.0.0  |luxianguan      |creation
  * @warning
  */
#ifndef _FRAMEWORK_PROJ_H
#define _FRAMEWORK_PROJ_H
#include <stdint.h>
#include <stdbool.h>
#include "StorageCommon.h"

#define MAX_MEM_PROJ_NUM    		(5)
#define MAX_FILE_PACHET_LEN 		(0xA00000) 	/* 方案文件大小10m */
#define PROJ_VERSION_190830     	(0x20190830)   
#define PROJ_VERSION_200929     	(0x20200929)   /* SC2000pro V2.0.0 */
#define PROJ_VERSION_210301     	(0x20210301)   /* SC3000  V1.0.0 */
#define PROJ_VERSION_230801     	(0x20230801)   /* SC3000  V3.1.0 */
#define MAX_FNAME_LEN    			(64)
#define MAX_FILE_NAME_LEN 			(64)
#ifndef MAX_ALGO_NAME_LEN
#define MAX_ALGO_NAME_LEN 			(64)
#endif
#define MAX_PASSWD_LEN   			(32)
#define MAX_SWITCH_STR_LEN  		(32)
#define MAX_SWITCH_SUFFIX_LEN   (16)
#define MAX_SWITCH_RET_LEN  		(32)
#define MAX_TRIGGER_STR_LEN  		(32)
#define MAX_DI_LINE_NUM 			(16) 	
#define MIN_DI_LINE_NUM 			(0)
#define MAX_ALGO_PARAM_VALUE_LEN    (128)
#define MAX_IMAGE_SOURCE_LEN        (64)
#define CREATE_PROCEDURE_ID         (10000)
#define CREATE_PROCEDURE_NAME       "hik_proce"
#define SUPPORT_PROJ_VERSION    	PROJ_VERSION_210301
#define PUSH_OK                     (0)
#define PUSH_ERR                    (-1)
#if (defined R349) || (defined R319) || (defined R320)
#define MAX_PROJECT_NUM  			(32)
#elif (defined R316) || (defined R315) || (defined R328) || (defined R328a)
#define MAX_PROJECT_NUM  			(8)
#endif
#define MAX_PROJECT_SWITCH_NUM  	(256)

#ifndef TIME_OUT_S_3
#define TIME_OUT_S_3                (150)
#endif

#define PUSH_RATE_PROJ(type, sts, rate)    do {  \
	struct proj_push_data proj_data = {0};		 \
	proj_data.progress = rate;					 \
	proj_data.status   = sts;					 \
	proj_data.cmd_data = type;				 \
	scfw_execute_project_cb(&proj_data);		 \
} while (0)


#ifndef IMAGE_ALGO_INTER_NAME
#define IMAGE_ALGO_INTER_NAME                      "image"
#endif
#ifndef BASEIMAGE_ALGO_INTER_NAME
#define BASEIMAGE_ALGO_INTER_NAME                  "baseimage"
#endif

#ifndef FORMAT_ALGO_INTER_NAME
#define FORMAT_ALGO_INTER_NAME                     "format"
#endif
#ifndef LOGIC_ALGO_INTER_NAME
#define LOGIC_ALGO_INTER_NAME                      "logic"
#endif
#ifndef SAVEIMG_ALGO_INTER_NAME
#define SAVEIMG_ALGO_INTER_NAME                    "saveimage"
#endif
#ifndef IO_ALGO_INTER_NAME
#define IO_ALGO_INTER_NAME                         "iomodule"
#endif

/**json**/
#define JSON_PROJECT_LIST               "project_list"
#define JSON_PROJECT_NUM                "project_num"
#define JSON_PROJECT_NAME               "project_name"
#define JSON_PROJECT_SWITCH_STR         "switch_str"
#define JSON_PROJECT_SWITCH_SUFFIX_STR  "switch_suffix_str"
#define JSON_PROJECT_SWITCH_RET         "switch_ret"
#define JSON_PROJECT_SWITCH_FAIL        "switch_fail"
#define JSON_PROJECT_SWITCH_SOURCE      "switch_source"
#define JSON_PROJECT_SWITCH_ACTIVE      "switch_active"
#define JSON_PROJECT_FALSE_TIME         "false_time"
#define JSON_PROJECT_DI_ENABLE          "di_enable"
#define JSON_PROJECT_CM_ENABLE          "cm_enable"
#define JSON_PROJECT_IO_INPUT           "io_input"
#define JSON_PROJECT_PASSWD             "passwd"
#define JSON_PROJECT_LAST_USED_PROJ     "last_used_project"
#define JSON_PROJECT_CRAT_TIME          "create_time"
#define JSON_PROJECT_INTER_MS           "interval_ms"
#define JSON_PROJECT_CARM_TRI_SOURCE    "camera_trigger_source"
#define JSON_PROJECT_CARM_TRI_MODE      "camera_trigger_mode"
#define JSON_PROJECT_IMAGE_SOURCE       "algo_image_source"  
#define JSON_PROJECT_DI_LINE_CFG        "line_cfg"
#define JSON_PROJECT_RELOAD             "project_reload"
#define JSON_PROJECT_TRI_COMM           "camera_trigger_comm"

#ifndef MAX_UPDATE_SLN_LEN
#ifdef SC1000
#define MAX_UPDATE_SLN_LEN (15 << 20)  // 15M
#elif SC2000E
#define MAX_UPDATE_SLN_LEN (35 << 20)  // 35M
#else
#define MAX_UPDATE_SLN_LEN (400 << 20)  // 400M
#endif
#endif
#ifdef SC1000
#define PROJ_MAX_MEM_RATE  90
#endif

/**
 * @brief 通信类型切换参数
 */
struct cm_switch_info
{
	char cm_str[MAX_SWITCH_STR_LEN];            /**< 方案切换字符串 */
	char cm_ret[MAX_SWITCH_RET_LEN];            /**< 方案切换成功返回字符串 */
	char cm_fail[MAX_SWITCH_RET_LEN];           /**< 方案切换失败返回字符串 */
	uint32_t cm_enable;                         /**< 方案通信切换使能 */
    char trigger_cm_str[MAX_SWITCH_STR_LEN];    /**< 方案触发字符串 */
    char cm_suffix_str[MAX_SWITCH_SUFFIX_LEN];  /**< 方案通信切换后缀字符串 */
    uint8_t resv[48];                           /**< 保留 */
};

/**
 * @brief io类型切换参数
 */
struct di_switch_info
{
	uint32_t source;                              /**< 触发源见algo_trigger_source定义 */
	uint32_t active;                              /**< 触发类型0-上升沿 1-下降沿 */
	uint32_t time;                                /**< 滤波时间 */
	uint32_t io_input;                            /**< 被设置输入的IO */
	uint32_t di_enable;                           /**< DI模式切换使能 */
	int32_t camera_trigger_source;				  /**< 相机模块触发源 */
	int32_t camera_trigger_mode;				  /**< 相机模块触发模式 */
	char line_cfg[MAX_DI_LINE_NUM];         	  /**< DI切换各路类型 */
	uint8_t resv[8];                             /**< 保留 */
};

/**
 * @brief 切换参数
 */
struct switch_info
{
	uint32_t mode;
	struct cm_switch_info communication;
	struct di_switch_info digital_io;
};

/**
 * @brief 方案信息
 */
struct project_info
{
	char name[MAX_FNAME_LEN + 4];                 /**< 方案名称 */
	char passwd[MAX_PASSWD_LEN];                  /**< 方案密码 */
	char create_time[MAX_ALGO_PARAM_VALUE_LEN];   /**< 方案创建时间 */
	char image_source[MAX_IMAGE_SOURCE_LEN];      /**< 相机图像源信息 */
	uint32_t interval_ms;                         /**< 方案运行间隔 */
	uint8_t resv[64];                             /**< 保留 */
};

/**
 * @brief 方案头信息
 */ 
struct project_head
{
	uint32_t magic;								/**< 幻数 */
	uint32_t crc;								/**< 校验 */
	uint32_t ver;							    /**< 版本 */
	uint32_t total_size;						/**< 方案文件大小 */
	uint32_t dev_type;							/**< 设备类型 */
	uint32_t language_type;						/**< 设备语言 */
	uint32_t base_image_size;					/**< 基准图长度 */
	struct project_info proj_info;		        /**< 方案信息 */
	struct switch_info swth_info;               /**< 切换信息 */
	uint32_t resv[64];							/**< 保留字段 */
};

/**
 * @brief 切换管理信息
 */ 
struct project_switch_mng
{
	void               *mng_lock;
	uint32_t           project_num;
	char             project_name[MAX_PROJECT_SWITCH_NUM][MAX_FNAME_LEN];  /**< 方案名称 */
	struct switch_info swth_info[MAX_PROJECT_SWITCH_NUM];
};

/**
 * @brief 进度推送数据
 */ 
struct proj_push_data
{
	int cmd_data;           /**< 执行的功能函数*/
	int status;				/**< 推送状态 */
	int progress;			/**< 进度数据信息*/
};

struct button_switch_info
{
	uint32_t active;                              /**< 触发类型0-上升沿 1-下降沿 */
	uint32_t time;                                /**< 滤波时间 */
	uint32_t di_enable;                           /**< DI模式切换使能 */
};

enum proj_switch_lock
{
	SWITCH_LOCK_HTTPD,
	SWITCH_LOCK_WS,
	SWITCH_LOCK_GVMP,
	SWITCH_LOCK_GVCP,
	SWITCH_LOCK_NUM	
};
	
enum proj_running_status
{
	PR_STATUS_INVALID    = 0, // 无效，未知
	PR_STATUS_INIT       = 1, // 初始化
	PR_STATUS_READY      = 2, // 就绪
	PR_STATUS_FREE       = 3, // 空闲
	PR_STATUS_BUSY       = 4, // 忙碌
};

enum proj_run_mode
{
	PR_MODE_CONTINUOUS    = 0, // 循环运行
	PR_MODE_STOP          = 1, // 停止运行
	PR_MODE_RUNONCE       = 2, // 运行一次
};


typedef int32_t (*http_proj_cb)(struct proj_push_data *data);

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  set project busy state
  * @param[in] busy project is busy or not
  */
void scfw_proj_set_busy_state(bool busy);

/**
  * @brief  get project busy state
  * @return  true-busy  false-not busy
  */
bool scfw_proj_is_busy(void);

/**
  * @brief  get project init state
  * @return  true-busy  false-not busy
  */
int32_t scfw_proj_is_init(void *arg);

/**
  * @brief  get project ready state
  * @return  true-busy  false-not busy
  */
int32_t scfw_proj_is_ready(void *arg);

/**
  * @brief  wait proj init done
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_proj_wait_ready_with_timeout(uint32_t timeout_ms);

/**
  * @brief  project switch trylock
  * @param[in] enum proj_switch_lock index  wait time ms
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_project_switch_trylock_proc(enum proj_switch_lock index, uint32_t ms);
/**
  * @brief  project switch lock
  * @param[in] enum proj_switch_lock
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_project_switch_lock_proc(enum proj_switch_lock index);
/**
  * @brief  project statues callback register
  * @param[in] http_proj_cb   project proj_cb
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_project_switch_unlock_proc(enum proj_switch_lock index);

int32_t scfw_http_register_proj_cb(http_proj_cb proj_cb);

/**
  * @brief  project operation progress push
  * @return 
  */
int32_t scfw_execute_project_cb(struct proj_push_data *proj_data);

/**
  * @brief  load a project to DDR
  * @param[in] proj_name   project name
  * @param[in] passwd      password
  * @param[in] need_push_rate according to the need_push_rate , decide to push the progress rate
  * @param[in] need_push_fail according to the need_push_fail , decide to push the progress fail status
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_load_project(const char *proj_name, const char *passwd, uint32_t type,bool need_push_rate,bool need_push_fail);
int32_t scfw_load_project_by_media(StorageMedia media, const char *proj_name, const char *passwd,
								   uint32_t type,bool need_push_rate,bool need_push_fail);

/**
  * @brief  according to the pr_flag , decide to push the progress rate.
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_load_default_project_with_PR(bool pr_flag);

/**
  * @brief  load a default project,only contains IMAGE module
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_load_default_project(void);

/**
  * @brief  load the lastest used project
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_load_last_used_project(void);

/**
  * @brief  free current project in DDR 
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_free_cur_project(const int is_free_comm);

/**
  * @brief  delete a project in NVM 
  * @param[in] proj_name   project name
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_delete_project(const char *project_name);
int32_t scfw_delete_project_by_media(StorageMedia media, const char *project_name);

/**
  * @brief  delete ALL project in NVM 
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_delete_all_projects(void);

/**
  * @brief  save as a project into ddr
  * @param[in] project_name   dst project name.
  * @param[in] passwd         dst project passwd default :123456.
  * @param[in] src_proj_name  src project name.
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_save_as_project_to_ddr(const char *project_name, const char *passwd, const char *src_proj_name);

/**
  * @brief  save as a project into ddr
  * @param[in] project_name   dst project name.
  * @param[in] passwd         dst project passwd default :123456.
  * @param[in] src_proj_name  src project name.
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_copy_project_to_ddr(const char *dst_proj_name, const char *passwd, const char *src_proj_name);

/**
  * @brief  save a project into path
  * @param[in] proj_info   project information
  * @param[in] swth_info   project switch para etc.
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_save_project_to_path(const char *project_name, const char *passwd, const char *save_path);
/**
  * @brief  upload project file buf from client
  * @param[in] proj_buf   project file buffer
  * @param[in] len        size of project file
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_upload_project(const char *proj_file_path);

/**
  * @brief  upload the display data of according module from client.
  * @param[in] module_id  module id
  * @param[in] buf        display data buffer
  * @param[in] len        size of display data
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_upload_disp_data(uint32_t module_id, char *buf, uint32_t buf_size);

/**
  * @brief  dnload the display data of according module to client.
  * @param[in]  module_id  module id
  * @param[in]  buf        display data buffer
  * @param[out] len        size of display data
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_dnload_disp_data(uint32_t module_id, char *buf, uint32_t buf_size, uint32_t *out_len);

/**
  * @brief  update current projetc name.
  * @param[in] proj_name  project name
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_set_cur_project_name(const char *proj_name);

/**
  * @brief  get the current projetc name
  * @return  current projetc name
  */
char *scfw_get_cur_project_name(void);

/**
  * @brief  get the current switch name
  * @return  current switch name
  */
char *scfw_get_cur_switch_name(void);

/**
  * @brief  get the  projetc path
  * @return  current projetc name
  */

int32_t scfw_get_project_path_ddr(char *path,const char *project_name, int len);
/**
  * @brief  get the current projetc path
  * @return  current projetc name
  */
int32_t scfw_get_cur_project_path(char * path, int len);

/**
  * @brief  update current projetc name.
  * @param[in] old_proj_name  old project name
  * @param[in] new_proj_name  new project name
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_modify_project_name(const char *old_proj_name, const char *new_proj_name);
int32_t scfw_modify_project_name_by_media(StorageMedia media, const char *old_proj_name,
										  const char *new_proj_name);

/**
  * @brief  update the switch information of given project
  * @param[in] proj_name  project name
  * @param[in] swt_info   the buffer addr of project switch parameter information
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_set_project_switch_info(const char *proj_name, struct switch_info *swt_info);

/**
  * @brief  get the switch information of given project
  * @param[in]  proj_name  project name
  * @param[out] swt_info   the buffer addr of project switch parameter information
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_get_project_switch_info(const char *proj_name, struct switch_info *swt_info);

int32_t scfw_get_project_switch_info_ex(const char *proj_name, struct switch_info *swt_info);

int32_t scfw_get_project_switch_info_by_media(StorageMedia media, const char *proj_name,
											  struct switch_info *swt_info);

/**
  * @brief  get total project num in NVM.
  * @param[out] proj_num   project num buffer addr
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_get_project_num(uint32_t *proj_num);

int32_t scfw_get_project_num_by_media(StorageMedia media, uint32_t *proj_num);

bool scfw_check_is_solution_file_exist(const char *name);

/** 
  * @brief  get the project information of given project index. 
  * @param[in]  proj_idx    project index in project management json file 
  * @param[out] proj_info   project information buffer addr  
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_get_project_info(int proj_idx, struct project_info *proj_info);

int32_t scfw_get_project_info_by_media(StorageMedia media, int proj_idx,
									   struct project_info *proj_info);

/**
 * @brief	switch project
 * @param[in]  project_name
 * @param[in]  restore_type 1:出错不还原  0:出错还原
 * @param[in]  load_type    1:user        0:DI/communication
 * @return       0-执行成功，负数-执行失败
 */
int32_t scfw_switch_project(const char *proj_name, uint32_t restore_type, uint32_t load_type);

/**
 * @brief    switch_mang
 * @param[in]  
 * @return     
 */
struct project_switch_mng *get_proj_switch_mng(void);

/**
 * @brief    get digital io set mask code  
 * @param[in]  
 * @return     
 */
uint32_t scfw_get_di_set_mask(void);

/**
 * @brief    get digital io filter time
 * @param[in]  
 * @return     
 */
int32_t scfw_get_di_filter_time(uint32_t *filter_time_us);

/**
 * @brief    set image source
 * @param[in]  image_source 
 * @return     0:ok  other:error
 */
int32_t scfw_set_image_source(char *image_source);

/**
 * @brief    get image source
 * @param[in]  image_source 
 * @return     0:ok  other:error
 */
int32_t scfw_get_image_source(char *image_source);

/**
 * @brief    get comm enable
 * @param[in]  image_source 
 * @return     0:ok  other:error
 */
int32_t scfw_get_comm_enable(void);

/**
 * @brief    get button switch
 * @param[in]  button switch info 
 * @return     0:ok  other:error
 */
int32_t scfw_get_button_switch(struct button_switch_info *info);

/**
 * @brief	   prepare of load solution finsih
 * @param[in]	   
 * @return	   success: 0; fail:other
 */
int32_t scfw_load_solution_finish_pre(void);

/**
 * @brief      set project param
 * @param[in]  proj_name project name
 * @param[in]  param param name
 * @param[in]  value_buf value buf
 * @param[in]  buf_size value buf size
 * @return     success: 0; fail:other
 */
int32_t scfw_set_project_param(const char *proj_name, const char *param, char *value_buf, uint32_t buf_size);;

void scfw_set_last_used_project_default(void);

#ifdef __cplusplus
}
#endif

#endif
