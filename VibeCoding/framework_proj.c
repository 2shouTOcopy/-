/** @file
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   project manager module
  *
  * @author  luxianguan
  * @date    2019/08/30
  *
  * @version
  *  date             |version |author          |message
  *  2019/08/30       |V1.0.0  |luxianguan      |creation
  * @warning
  */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "dev_cfg.h"
#include "type_defs.h"
#include "communication_common_depend.h"
#include "mm.h"
#include "utils.h"
#include "cjson/cJSON.h"
#include "framework_proj.h"
#include "framework_mng.h"
#include "framework_sub.h"
#include "sche_adapter/VmModuleDef.h"
#include "sche_adapter/VmModuleFrame.h"
#include "isp_process.h"
#include "AppParamCommon.h"
#include "AppParamApi.h"
#include "IoParamDef.h"
#include "osal_dir.h"
#include "osal_cond.h"


#ifndef PRINTF_LOCAL
#include "log/log.h"
#else
#define LOGE printf
#define LOGI printf
#define LOGP printf
#endif

#include "osal_debug.h"
#include "osal_errno.h"
#include "osal_file.h"
#include "osal_heap.h"
#include "osal_dir.h"
#include "osal_cap.h"
#include "osal_mutex.h"
#include "osal_sema.h"
#include "osal_misc.h"
#include "sc_errno.h"
#include "thread/ThreadApi.h"
#include "slnar.h"
#include "oem_user_define.h"

#include "EmmcApi.h"
#include "peripheralapi.h"

#define LEAVE(err, exit)     do { error = err; goto exit;}while(0)
#ifdef R349
	#define PJ_MALLOC(n)	 (n == 0 ? MMZmemAllocHigh(1, 4) : MMZmemAllocHigh(n, 4))
	#define PJ_FREE(p)	 	 MMZmemFree((void**)&p)
#elif defined R316
	#define PJ_MALLOC(n)	 (n == 0 ? MMZmemAllocHigh(1, 4) : MMZmemAllocHigh(n, 4))
	#define PJ_FREE(p)		 MMZmemFree((void**)&p)
#else
	#define PJ_MALLOC(n)	 osal_malloc(n, 4)
	#define PJ_FREE(p)		 osal_free(p)
#endif
	

#define PROJ_DIR             "/mnt/data/project_dir/"
#define DEFAULT_PROJ_DIR     "/mnt/data/project_dir/Untitled"
#define DEVSLN_DIR           "/mnt/data/project/"
#define DEVSLN_BASE_IMG_DIR  "/mnt/data/project/base_image/"
#define CRC_ERR_DIR          "/mnt/data/crc"
#define ROOT_DIR             "/"
#define ALGO_SUB_FNAME       "algo_sub.txt"
#define ALGO_CONNECT_FNAME   "algo_connect.txt"
#define DEFAULT_PROJ_NAME    "Untitled"
#define DEFAULT_PASSWD       "123456"
#define PROJ_MNG_FNAME       "/mnt/data/project/project_mng.json"
#define DEFAULT_DISP         "/home/vms_so/so/"
#define DEFAULT_DISP_DIR     "/json/"
#define DEFAULT_IMAGE_SOURCE "0@JPEGIMAGE_jpg_image"
#define PROJ_IMAGE_ALGO_NAME "image"
#define BASE_IMAGE_ALGO_NAME "baseimage"
#define BASE_IMAGE_FILE_NAME "base_image.jpg"

#define MAX_PRO_CB_NUM	 (2)                /**< 最大event callback个数 */

/* callback section */
#define WS_CMD_SPRP             3    /* 保存方案 */
#define WS_CMD_LPRP             2    /* WEB加载方案 */
#define WS_CMD_UPRP             11   /* 上传方案 */
#define WS_CMD_DCLP             17	 /* DI LOAD */
#define WS_CMD_CPRP             21   /* 新建方案 */
#define WS_CMD_EDRP             22   /* 修改方案名称 */
#define BUTTON_TRIG_NUM         15
#define SAVE_IMAGE_SOURCE       1   
#define UNSAVE_IMAGE_SOURCE     0
#define PROJ_CAMERA_MODULE_ID   0
#define PROJ_BASE_IMAGE_MODULE_ID   1

#define MAX_LOCK_WAIT_TIME      (30 * 1000)
#define SOLUTION_DIR            (256)
#define DI_HIGH                 (1)
#define DI_LOW                  (0)
#define NO_SELECT               (-1)
#define DI_LOAD_PROJ 			(0)
#define USER_LOAD_PROJ 			(1)
#define LAST_LOAD_PROJ 			(2)
#define BUTTON_LOAD_PROJ 	    (3)

static cJSON *algo_proj_mng_root = NULL;
static char cur_project_name[MAX_ALGO_NAME_LEN + 1] = {0}; /* 存放当前运行的方案 */
static char cur_switch_name[MAX_ALGO_NAME_LEN + 1] = {0}; /* 存放正在切换的方案名 */
static char cur_algo_image_source[MAX_IMAGE_SOURCE_LEN] = {0};
static char last_project_name[MAX_FNAME_LEN + 1] = {0};    /* 存放上一次下载的方案名 */
struct project_switch_mng proj_switch_mng;
struct button_switch_info proj_button_info = {0};
static http_proj_cb project_cb[MAX_PRO_CB_NUM];
static uint32_t di_set_mask_data_last = 0;
static uint32_t di_filter_time_us[MAX_DI_LINE_NUM] = {0};
static void *proj_switch_mutex_lock[SWITCH_LOCK_NUM] = {0};
static atomic_bool proj_busy = ATOMIC_VAR_INIT(0);
static enum proj_running_status proj_status = PR_STATUS_INVALID;
static struct osal_cond *proj_cond = NULL;

int32_t modify_emmc_sln_file_info(const char *proj_name, const struct project_info *proj_info,
								  const struct switch_info *swth_info);
int32_t modify_algo_project_info(const char *proj_name, const struct project_info *proj_info,
								 const struct switch_info *swth_info);
int32_t modify_emmc_sln_switch_info(const char *proj_name, const struct switch_info *swth_info);
extern struct module *get_created_module(int32_t module_id);

extern int vm_get_module_param(int module_id, const char * param, char * value);

extern int32_t update_procedure_name();
extern int32_t update_pwd();
extern void comif_update_module_common_param(void);
extern int32_t comif_check_sln_param_with_operate(int32_t operate_type, uint32_t* prj_max_num, const char* src_name, const char* dst_name);
extern int32_t comif_get_device_commparam_info(const char *name, IN char *param_buf, IN uint32_t buf_len);
extern int32_t set_trigger_status(TRIGGER_STATUS status);
extern int32_t set_run_status(SCHEME_RUN_STATUS status);

static void scfw_convert_project_base_info(const struct project_info *from, SLNAR_SLN_BASE_INFO_T *to);
static void scfw_convert_project_base_info_r(const SLNAR_SLN_BASE_INFO_T *from, struct project_info *to);
static void scfw_convert_project_switch_info(const struct switch_info *from, SLNAR_SLN_SW_INFO_T *to);
static void scfw_convert_project_switch_info_r(const SLNAR_SLN_SW_INFO_T *from, struct switch_info *to);

static int scfw_slnar_header_hook(SLNAR_T* slnar, SLNAR_DEV_INFO_T* devinfo, SLNAR_SLN_INFO_T* slninfo)
{
	if (NULL == devinfo)
	{
		return -1;
	}

    if (devinfo->devtype != get_dev_type_from_env())
    {
        LOGE("devtype mismatch, devtype:%#x, sln-devtype:%#x\n",
            get_dev_type_from_env(), devinfo->devtype);                
        return -2;
    }

	return 0;
}

static int scfw_slnar_header_hook_save_baseimage(SLNAR_T* slnar, SLNAR_DEV_INFO_T* devinfo, SLNAR_SLN_INFO_T* slninfo)
{
	if (NULL == slnar  || NULL == slninfo)
	{
		return -1;
	}
	char file_path[256] = {0};
	snprintf(file_path, sizeof(file_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, slninfo->baseinfo.name);

	if (SLNAR_EC_OK != slnar_set_baseimage_path(slnar, file_path))
	{
		return -2;    		
	}

	return 0;
}

static char *get_cur_rtc(void)
{
	int ret = 0;
	static char time_string[64] = {0};
	struct osal_rtc cur_rtc;

	ret = osal_get_systime(&cur_rtc);
	if (ret < 0)
	{
		LOGE("osal_get_systime error ret = %d\n", ret);
	}

	snprintf(time_string, sizeof(time_string), "%04d-%02d-%02d %02d:%02d:%02d",
			 cur_rtc.year, cur_rtc.month, cur_rtc.day, cur_rtc.hour, cur_rtc.minute, cur_rtc.second);
	return time_string;
}

struct project_switch_mng *get_proj_switch_mng(void)
{
	return &proj_switch_mng;
}

void scfw_proj_set_busy_state(bool busy)
{
	atomic_store(&proj_busy, busy?1:0);
}

bool scfw_proj_is_busy(void)
{
	return atomic_load(&proj_busy);
}

int32_t scfw_proj_is_init(void *arg)
{
	return (proj_status == PR_STATUS_INIT) ? 1 : 0;
}

int32_t scfw_proj_is_ready(void *arg)
{
	return (proj_status == PR_STATUS_READY) ? 1 : 0;
}

int32_t scfw_proj_wait_ready_with_timeout(uint32_t timeout_ms)
{
	return osal_cond_timed_obtain(proj_cond, scfw_proj_is_init, NULL, timeout_ms);
}

int32_t scfw_proj_switch_mutex_init(void)
{
	int32_t error = 0;
	int32_t i = 0; 

	for (i = 0; i < SWITCH_LOCK_NUM; i++)
	{
		if (osal_mutex_create(&(proj_switch_mutex_lock[i])) < 0)
		{
			LOGE("init proj_switch_mutex %d failed\r\n", i);
			LEAVE(-1, out);
		}
	}

	if ((error = osal_cond_create((void **)&proj_cond)) < 0)
	{
		LOGE("init proj cond mutex %d failed\r\n", error);
		LEAVE(-2, out);
	}
	proj_status = PR_STATUS_INIT;

out:	
	return error;
}

int32_t scfw_project_switch_trylock_proc(enum proj_switch_lock index, uint32_t ms)
{
	int32_t error = 0;
	
	if (index >= SWITCH_LOCK_NUM)
	{
		LOGE("index error\n");
		LEAVE(-1, out);
	}

	if (NULL == proj_switch_mutex_lock[index])
	{
		LOGE("error proj switch mutex lock can destory\r\n");
		LEAVE(-2, out);
	}
	
	if (osal_mutex_timed_lock(proj_switch_mutex_lock[index], ms) < 0)
	{
		LOGE("osal mutex try lock idx [%d] failed\r\n", index);
		LEAVE(-3, out);
	}
out:
	
	return error;
}

int32_t scfw_project_switch_lock_proc(enum proj_switch_lock index)
{
	int32_t error = 0;
	
	if (index >= SWITCH_LOCK_NUM)
	{
		LOGE("index error\n");
		LEAVE(-1, out);
	}

	if (NULL == proj_switch_mutex_lock[index])
	{
		LOGE("error proj switch mutex lock can destory\r\n");
		LEAVE(-2, out);
	}
	
	if (osal_mutex_lock(proj_switch_mutex_lock[index]) < 0)
	{
		LOGE("osal mutex lock failed\r\n");
		LEAVE(-3, out);
	}

out:
	
	return error;
}

int32_t scfw_project_switch_unlock_proc(enum proj_switch_lock index)
{
	int32_t error = SC_EC_OK;
	
	if (index >= SWITCH_LOCK_NUM)
	{
		LOGE("index error\n");
		LEAVE(-1, out);
	}

	if (NULL == proj_switch_mutex_lock[index])
	{
		LOGE("error proj switch mutex lock can destory\r\n");
		LEAVE(-2, out);
	}
	
	if (osal_mutex_unlock(proj_switch_mutex_lock[index]) < 0)
	{
		LOGE("osal mutex unlock failed\r\n");
		LEAVE(-3, out);
	}

out:
	
	return error;
}

int32_t scfw_get_button_switch(struct button_switch_info *info)
{
	if (NULL == info)
	{
		LOGE("button switch info point null\n");
		return -1;
	}

	*info = proj_button_info;
	return 0;
}

int32_t scfw_get_comm_enable(void)
{
	int32_t comm_enable = 0;
	int32_t i = 0;
	
	for (i = 0; i < proj_switch_mng.project_num; i++)
	{
		if (proj_switch_mng.swth_info[i].communication.cm_enable)
		{
			comm_enable = 1;
			break;
		}
	}
	
	return comm_enable;
}

int32_t scfw_get_di_filter_time(uint32_t *filter_time_us)
{

	if (NULL == filter_time_us)
	{
		return -1;
	}

	memcpy(filter_time_us, di_filter_time_us, sizeof(di_filter_time_us));

	return 0;
}

uint32_t scfw_get_di_set_mask(void)
{
	return di_set_mask_data_last;
}

static int32_t update_Procedure(void)
{
	int ret = 0;
	CREATE_PROCEDURE_PARAM procedure = {0};

	if (VM_M_IsProcedureExist(CREATE_PROCEDURE_ID))
	{
		if ((ret = VM_M_DeleteProcedure(CREATE_PROCEDURE_ID)) < 0)
		{
			LOGE("VM_M_DeleteProcedure error:%d\n", ret);
			return ret;
		}
	}

	procedure.nProcedureId = CREATE_PROCEDURE_ID;
	snprintf(procedure.byProcedureName, sizeof(procedure.byProcedureName), "%s", CREATE_PROCEDURE_NAME);

	if ((ret = VM_M_CreateProcedure(&procedure)) != 0)
	{
		LOGE("VM_M_CreateProcedure error:%d\n", ret);
		return ret;
	}

	return ret;
}

static int32_t save_algo_project_mng_into_file(cJSON *proj_mng)
{
	int error = 0;
	int fsize = 0;
	int ret = 0;
	char *file_path = PROJ_MNG_FNAME;
	char *cfg_str = NULL;

	if (NULL == proj_mng)
	{
		LEAVE(-1, out);
	}

	cfg_str = cJSON_Print(proj_mng);
	if (NULL == cfg_str)
	{
		LEAVE(-2, out);
	}

	fsize = strlen(cfg_str);

	if ((ret = osal_save_file(file_path, cfg_str, fsize, FILE_OPT_OVERWRITE)) != fsize)
	{
		LOGE("save file %s error: errno = %d\n", file_path, ret);
		LEAVE(-3, out);
	}

out:

	if (cfg_str)
	{
		osal_free(cfg_str);
	}

	return error;
}

static int delete_project_obj(cJSON *project_mng, const char *project_name)
{
	int i = 0;
	int error = 0;
	cJSON *proj_array = NULL;
	cJSON *proj_obj = NULL;
	cJSON *num_obj = NULL;
	cJSON *name_obj = NULL;

	if ((NULL == project_mng) || (NULL == project_name))
	{
		LEAVE(-1, out);
	}

	if ((proj_array = cJSON_GetObjectItem(project_mng, JSON_PROJECT_LIST)) == NULL)
	{
		LEAVE(-2, out);
	}

	if ((num_obj = cJSON_GetObjectItem(project_mng, JSON_PROJECT_NUM)) == NULL)
	{
		LEAVE(-3, out);
	}

	for (i = 0; i < num_obj->valueint; i++)
	{
		proj_obj = cJSON_GetArrayItem(proj_array, i);
		if (proj_obj)
		{
			if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME)) == NULL)
					|| (NULL == name_obj->valuestring))
			{
				LEAVE(-4, out);
			}

			if (0 == strncmp(name_obj->valuestring, project_name, MAX_FNAME_LEN))
			{
				LOGI("[%s] line%d found %s: %s\n", __func__, __LINE__, project_name, name_obj->valuestring);
				cJSON_DeleteItemFromArray(proj_array, i);
				LEAVE(0, out);
			}
		}
		else
		{
			LOGE("invalid proj array,idx %d\n", i);
		}
	}

out:
	return error;
}

int32_t scfw_execute_project_cb(struct proj_push_data *proj_data)
{
	int32_t i = 0;
	int32_t error = 0;
	
	if (NULL == proj_data)
	{
		LEAVE(-1, out);
	}

	for (i = 0; i < MAX_PRO_CB_NUM; i++)
	{
		if (project_cb[i])
		{
			project_cb[i](proj_data);
		}
	}
	
out:
	return error;
}

int32_t scfw_http_register_proj_cb(http_proj_cb proj_cb)
{
	for (int32_t i = 0; i < MAX_PRO_CB_NUM; i++)
	{
		if (NULL == project_cb[i])
		{
			project_cb[i] = proj_cb;
			return 0;
		}
	}

	LOGE("event callback reached max:%d\n", MAX_PRO_CB_NUM);
	return -1;
}

char *scfw_get_cur_project_name(void)
{
	if (0 == strlen(cur_project_name))
	{
		strncpy(cur_project_name, DEFAULT_PROJ_NAME, sizeof(cur_project_name));
		cur_project_name[sizeof(cur_project_name) - 1] = '\0';
	}

	return cur_project_name;
}

char *scfw_get_cur_switch_name(void)
{
	if (0 == strlen(cur_switch_name))
	{
		snprintf(cur_switch_name, sizeof(cur_switch_name), "%s", DEFAULT_PROJ_NAME);
	}

	return cur_switch_name;
}

int32_t scfw_set_cur_project_name(const char *project_name)
{
	int error = 0;
	int ret = 0;
	char src_path[MAX_ALGO_NAME_LEN * 2] = {0};
	char cur_path[MAX_ALGO_NAME_LEN * 2] = {0};

	if (NULL == project_name)
	{
		LEAVE(-1, out);
	}

	if (0 != strncmp(cur_project_name, project_name, MAX_FNAME_LEN))
	{
		snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, project_name);
		snprintf(cur_path, sizeof(cur_path), "%s%s", PROJ_DIR, cur_project_name);

		if ((ret = osal_move(cur_path, src_path)) < 0)
		{
			LOGE("osal_move %s %s error ret = %d\n", cur_path, src_path, ret);
			LEAVE(-2, out);
		}

		if (0 == strncmp(cur_project_name, DEFAULT_PROJ_NAME, MAX_FNAME_LEN))
		{
			snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, DEFAULT_PROJ_NAME);

			if ((ret = osal_create_dir(src_path)) < 0)
			{
				LOGE("osal_create_dir %s error ret = %d\n", src_path, ret);
				LEAVE(-3, out);
			}
		}

		strncpy(cur_project_name, project_name, sizeof(cur_project_name));
		cur_project_name[sizeof(cur_project_name) - 1] = '\0';
	}

out:
	return error;
}
/**
  * @brief  update current projetc name.
  * @param[in] proj_name  project name
  * @return  0 on success; < 0 on failure
  */
static int update_cur_project_name(const char *project_name)
{

	if (NULL == project_name)
	{
		return -1;
	}

	if (0 != strncmp(cur_project_name, project_name, MAX_FNAME_LEN))
	{
		strncpy(cur_project_name, project_name, sizeof(cur_project_name));
		cur_project_name[sizeof(cur_project_name) - 1] = '\0';
	}

	return 0;
}
/**
  * @brief  update current projetc name.
  * @param[in] proj_name  project name
  * @return  0 on success; < 0 on failure
  */
static int update_last_used_project_name(const char *project_name)
{
	cJSON *tmp_obj = NULL;
	int32_t error = 0;
	int32_t ret = 0;

	if (NULL == project_name)
	{
		return -1;
	}

	if (((tmp_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_LAST_USED_PROJ)) == NULL)
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-2, out);
	}

	if (0 != strncmp(tmp_obj->valuestring, project_name, MAX_FNAME_LEN))
	{
		cJSON_ReplaceItemInObject(algo_proj_mng_root,
								  JSON_PROJECT_LAST_USED_PROJ,
								  cJSON_CreateString(project_name));

		/* write the json str into file */
		if ((ret = save_algo_project_mng_into_file(algo_proj_mng_root)) < 0)
		{
			LOGE("[%s] save_algo_scheme_mng_into_file:%d\n", __func__, ret);
			LEAVE(-3, out);
		}
	}

out:

	return error;
}
int32_t scfw_get_cur_project_path(char *path, int len)
{
	char dir_path[MAX_ALGO_NAME_LEN * 2] = {0};
	int path_len = 0;

	if (NULL == path)
	{
		return -1;
	}

	/* get require len */
	path_len = snprintf(dir_path, sizeof(dir_path), "%s%s/", PROJ_DIR, scfw_get_cur_project_name());
	if (path_len >= len)
	{
		return -2;
	}

	strncpy(path, dir_path, len);
	path[len - 1] = '\0';

	return 0;
}

int32_t scfw_is_need_recreate_procedure(void)
{
	int ret = 0;
	CREATE_PROCEDURE_PARAM procedure = {0};

	if (VM_M_IsProcedureExist(CREATE_PROCEDURE_ID))
	{
		if (is_comm_global_dev())
		{
			return 0;
		}
		
		if ((ret = VM_M_DeleteProcedure(CREATE_PROCEDURE_ID)) < 0)
		{
			LOGE("VM_M_DeleteProcedure error:%d\n", ret);
			return ret;
		}
	}

	procedure.nProcedureId = CREATE_PROCEDURE_ID;
	snprintf(procedure.byProcedureName, sizeof(procedure.byProcedureName), "%s", CREATE_PROCEDURE_NAME);
	
	if ((ret = VM_M_CreateProcedure(&procedure)) != 0)
	{
		LOGE("VM_M_CreateProcedure error:%d\n", ret);
		return ret;
	}

	return ret;
}

/**
  * @brief  free current project in DDR unload cur
  * @return  0 on success; < 0 on failure
  */
/*释放方案资源的时候不需要新建procedure
 *调用该接口用于切换需要手动创建procedure
 */
int32_t scfw_free_cur_project(const int is_free_comm)
{
	int ret = 0;
	char fpath[MAX_ALGO_NAME_LEN * 2] = {0};
	struct module_name_info name_info = {0};
	uint32_t dir_num = 0;
	void *dir = NULL;
	struct osal_dir_info dir_info = {0};
	uint32_t project_num_max = MAX_PROJECT_NUM;
	
	if(0 == oem_get_user_define_by_type(OEM_STYLE_PROJECT_NUM, fpath, sizeof(fpath)))
	{
		project_num_max = atoi(fpath) < MAX_PROJECT_SWITCH_NUM ? atoi(fpath) : MAX_PROJECT_SWITCH_NUM;
	}

	if (0 == scfw_get_created_module_name_info(0, &name_info))
	{
		if ((ret = scfw_delete_module_tree(is_free_comm)) < 0)
		{
			LOGE("scfw_delete_module_tree error:%d\n", ret);
			return ret;
		}
	}

	//全局通信不需要删除流程
	if (!is_comm_global_dev() && VM_M_IsProcedureExist(CREATE_PROCEDURE_ID))
	{
		if ((ret = VM_M_DeleteProcedure(CREATE_PROCEDURE_ID)) < 0)
		{
			LOGE("VM_M_DeleteProcedure error:%d\n", ret);
			return ret;
		}
	}

	if ((ret = osal_get_dir_num(PROJ_DIR, 0, &dir_num)) < 0)
	{
		LOGE("osal_get_dir_num error:%d\n", ret);
		return ret;
	}
	LOGI("osal_get_dir_num:%d\n", dir_num);
	
	/* 因为有默认方案目录 */
	if (dir_num > project_num_max + 1)
	{
		if ((ret = osal_opendir(&dir, PROJ_DIR)) < 0)
		{
			printf("osal_opendir ret = %d\n", ret);
			return -1;
		}

		while (OSAL_NOT_EXIST != osal_readdir(dir, PROJ_DIR, &dir_info))
		{
			if ((OSAL_NODE_DIR == dir_info.type)
				&& ((strncmp(dir_info.name, DEFAULT_PROJ_NAME, MAX_FNAME_LEN))))
			{
				LOGI("file_name:%s\n", dir_info.name);
				snprintf(fpath, sizeof(fpath), "%s%s", PROJ_DIR, dir_info.name);
				if ((ret = osal_remove_dir(fpath)) < 0)
				{
					LOGE("osal_remove_dir %s error\n", fpath);
					break;
				}
			}
		}

		osal_closedir(dir);
	}
#if 0
	snprintf(fpath, sizeof(fpath), "%s%s", PROJ_DIR, cur_project_name);

	if ((ret = osal_remove_dir(fpath)) < 0)
	{
		LOGE("osal_remove_dir %s error\n", fpath);
		return ret;
	}

	if (0 == strncmp(cur_project_name, DEFAULT_PROJ_NAME, MAX_FNAME_LEN))
	{
		snprintf(fpath, sizeof(fpath), "%s%s", PROJ_DIR, DEFAULT_PROJ_NAME);

		if ((ret = osal_create_dir(fpath)) < 0)
		{
			LOGE("osal_create_dir %s error ret = %d\n", fpath, ret);
			return ret;
		}
	}
#endif
	return ret;
}
/*提供给mng用于组合目录*/
int32_t scfw_get_project_path_ddr(char *path, const char *project_name, int len)
{
	char dir_path[MAX_ALGO_NAME_LEN * 2] = {0};
	int path_len = 0;

	if ((NULL == path) || (NULL == project_name))
	{
		return -1;
	}

	path_len = snprintf(dir_path, sizeof(dir_path), "%s%s/", PROJ_DIR, project_name);
	if (path_len >= len)
	{
		return -2;
	}

	strncpy(path, dir_path, len);
	path[len - 1] = '\0';

	return 0;
}

static cJSON *get_project_obj(cJSON *project_mng, const char *project_name)
{
	int i = 0;
	int error = 0;
	cJSON *proj_array = NULL;
	cJSON *proj_obj = NULL;
	cJSON *num_obj = NULL;
	cJSON *name_obj = NULL;

	if ((NULL == project_mng) || (NULL == project_name))
	{
		LEAVE(-1, out);
	}

	if ((proj_array = cJSON_GetObjectItem(project_mng, JSON_PROJECT_LIST)) == NULL)
	{
		LEAVE(-2, out);
	}

	if ((num_obj = cJSON_GetObjectItem(project_mng, JSON_PROJECT_NUM)) == NULL)
	{
		LEAVE(-3, out);
	}

	for (i = 0; i < num_obj->valueint; i++)/* 循环轮询数据*/
	{
		proj_obj = cJSON_GetArrayItem(proj_array, i);/*数组里面每个{}的节点头获取出来*/
		if (proj_obj)
		{
			if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME)) == NULL) || (NULL == name_obj->valuestring))
			{
				LEAVE(-4, out);
			}

			if (0 == strncmp(name_obj->valuestring, project_name, MAX_FNAME_LEN))
			{
				LOGI("[%s] line%d found %s: %s\n", __func__, __LINE__, project_name, name_obj->valuestring);
				LEAVE(0, out);
			}
		}
		else
		{
			LOGE("invalid proj array,idx %d\n", i);
		}
	}

	if (i == num_obj->valueint)
	{
		LEAVE(-5, out);
	}

out:

	if (error < 0)
	{
		LOGE("[%s] \"%s\" not FOUND !,error = %d\n", __func__, project_name, error);
		return NULL;
	}

	return proj_obj;
}
/**
 * @brief      通过索引获取方案管理中的方案信息
 * @param[in]  project_mng 方案管理的根节点
 * @param[in]  index 要寻找的方案的索引值
 * @return      成功:0;失败:其他值
 */
static cJSON *get_project_index(cJSON *project_mng, const int32_t index)
{
	int32_t i = 0;
	int32_t error = 0;
	cJSON *proj_array = NULL;
	cJSON *proj_obj = NULL;
	cJSON *num_obj = NULL;
	cJSON *name_obj = NULL;

	if (NULL == project_mng)
	{
		LEAVE(-1, out);
	}

	if ((proj_array = cJSON_GetObjectItem(project_mng, JSON_PROJECT_LIST)) == NULL)
	{
		LEAVE(-2, out);
	}

	if ((num_obj = cJSON_GetObjectItem(project_mng, JSON_PROJECT_NUM)) == NULL)
	{
		LEAVE(-3, out);
	}

	if (index < 0 || index >= num_obj->valueint)
	{
		LEAVE(-4, out);
	}

	proj_obj = cJSON_GetArrayItem(proj_array, index);
	if (proj_obj)
	{
		if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME)) == NULL)
				|| (NULL == name_obj->valuestring))
		{
			LEAVE(-5, out);
		}
	}
	else
	{
		LOGE("invalid proj array,idx %d\n", i);
	}

out:

	if (error < 0)
	{
		LOGE("[%s] index:\"%d\" not FOUND !\n", __func__, index);
		return NULL;
	}

	return proj_obj;
}

static int32_t load_algo_project_mng_from_file(cJSON **algo_proj_mng)
{
	int error = 0;
	int fsize = 0;
	int32_t gsize = 0;
	char *file_path = PROJ_MNG_FNAME;
	char *mng_file_buf = NULL;

	if (NULL == algo_proj_mng)
	{
		LEAVE(-1, out);
	}

	gsize = osal_get_file_size(file_path);
	if (gsize < 0)
	{
		LOGE("get file size failed\n");
		LEAVE(-2, out);
	}

	mng_file_buf = (char *)malloc(gsize + 1);
	if (!mng_file_buf)
	{
		LOGE("malloc failed, size=%d\n", gsize);
		LEAVE(-3, out);
	}
	memset(mng_file_buf, 0, gsize + 1);

	fsize = osal_load_file(file_path, 0, mng_file_buf, gsize);
	if (fsize < 0)
	{
		LOGE("osal_load_file failed\n");
		LEAVE(-4, out);
	}

	mng_file_buf[fsize] = '\0';

	*algo_proj_mng = cJSON_Parse((const char *)mng_file_buf);
	if (NULL == *algo_proj_mng)
	{
		LOGE("PARSE cfg file %s error:%s !\n", file_path, mng_file_buf);
		LEAVE(-5, out);
	}

out:
	if (mng_file_buf)
	{
		free(mng_file_buf);
		mng_file_buf = NULL;
	}
	return error;
}

enum sln_file_ext
{
	SLN_FILE_NONE = 0,
	SLN_FILE_SLN,
	SLN_FILE_SCSLN,
};

struct sln_file_entry
{
	char filename[OSAL_NAME_MAXLEN];
	char name[MAX_FNAME_LEN + 1];
	uint8_t ext_type;
};

static enum sln_file_ext scfw_parse_sln_filename(const char *filename, char *name, size_t name_len)
{
	size_t len = 0;
	size_t ext_len = 0;
	const char *ext = NULL;

	if (filename == NULL || name == NULL || name_len == 0)
	{
		return SLN_FILE_NONE;
	}

	len = strlen(filename);
	if (len > strlen(".sln") && strcmp(filename + len - strlen(".sln"), ".sln") == 0)
	{
		ext = ".sln";
		ext_len = strlen(ext);
	}
	else if (len > strlen(".scsln") && strcmp(filename + len - strlen(".scsln"), ".scsln") == 0)
	{
		ext = ".scsln";
		ext_len = strlen(ext);
	}
	else
	{
		return SLN_FILE_NONE;
	}

	if (len <= ext_len)
	{
		return SLN_FILE_NONE;
	}

	if (len - ext_len >= name_len)
	{
		LOGE("[%s] sln name too long: %s\n", __func__, filename);
		return SLN_FILE_NONE;
	}

	strncpy(name, filename, len - ext_len);
	name[len - ext_len] = '\0';

	return (ext_len == strlen(".sln")) ? SLN_FILE_SLN : SLN_FILE_SCSLN;
}

static int32_t scfw_find_sln_entry(const struct sln_file_entry *entries, uint32_t entry_num, const char *name)
{
	if (entries == NULL || name == NULL)
	{
		return -1;
	}

	for (uint32_t i = 0; i < entry_num; i++)
	{
		if (0 == strncmp(entries[i].name, name, MAX_FNAME_LEN))
		{
			return (int32_t)i;
		}
	}

	return -1;
}

static bool scfw_project_name_in_entries(const struct sln_file_entry *entries, uint32_t entry_num, const char *name)
{
	if (entries == NULL || name == NULL)
	{
		return false;
	}

	for (uint32_t i = 0; i < entry_num; i++)
	{
		if (0 == strncmp(entries[i].name, name, MAX_FNAME_LEN))
		{
			return true;
		}
	}

	return false;
}

static bool scfw_project_name_in_list(char names[][MAX_FNAME_LEN + 1], uint32_t name_num, const char *name)
{
	if (names == NULL || name == NULL)
	{
		return false;
	}

	for (uint32_t i = 0; i < name_num; i++)
	{
		if (0 == strncmp(names[i], name, MAX_FNAME_LEN))
		{
			return true;
		}
	}

	return false;
}

static int32_t scfw_collect_sln_files(struct sln_file_entry *entries, uint32_t *entry_num)
{
	int32_t error = 0;
	void *dir = NULL;
	struct osal_dir_info dir_info = {0};

	if (entries == NULL || entry_num == NULL)
	{
		LEAVE(-1, out);
	}

	*entry_num = 0;
	if (osal_opendir(&dir, DEVSLN_DIR) < 0)
	{
		LEAVE(-2, out);
	}

	while (OSAL_NOT_EXIST != osal_readdir(dir, DEVSLN_DIR, &dir_info))
	{
		char name[MAX_FNAME_LEN + 1] = {0};
		enum sln_file_ext ext_type = SLN_FILE_NONE;
		int32_t idx = -1;

		if (dir_info.type != OSAL_NODE_FILE)
		{
			continue;
		}

		ext_type = scfw_parse_sln_filename(dir_info.name, name, sizeof(name));
		if (ext_type == SLN_FILE_NONE)
		{
			continue;
		}

		idx = scfw_find_sln_entry(entries, *entry_num, name);
		if (idx >= 0)
		{
			if (entries[idx].ext_type == SLN_FILE_SCSLN && ext_type == SLN_FILE_SLN)
			{
				strncpy(entries[idx].filename, dir_info.name, sizeof(entries[idx].filename));
				entries[idx].filename[sizeof(entries[idx].filename) - 1] = '\0';
				entries[idx].ext_type = ext_type;
			}
			continue;
		}

		if (*entry_num >= MAX_PROJECT_SWITCH_NUM)
		{
			LOGE("[%s] sln file num exceed max:%u\n", __func__, MAX_PROJECT_SWITCH_NUM);
			break;
		}

		strncpy(entries[*entry_num].filename, dir_info.name, sizeof(entries[*entry_num].filename));
		entries[*entry_num].filename[sizeof(entries[*entry_num].filename) - 1] = '\0';
		strncpy(entries[*entry_num].name, name, sizeof(entries[*entry_num].name));
		entries[*entry_num].name[sizeof(entries[*entry_num].name) - 1] = '\0';
		entries[*entry_num].ext_type = ext_type;
		(*entry_num)++;
	}

out:
	if (dir)
	{
		osal_closedir(dir);
	}
	return error;
}

static int32_t scfw_collect_proj_names_from_mng(cJSON *root,
												char names[][MAX_FNAME_LEN + 1],
												uint32_t *name_num)
{
	int32_t error = 0;
	cJSON *list_obj = NULL;
	cJSON *num_obj = NULL;
	int32_t list_size = 0;
	int32_t num = 0;

	if (root == NULL || names == NULL || name_num == NULL)
	{
		LEAVE(-1, out);
	}

	*name_num = 0;
	list_obj = cJSON_GetObjectItem(root, JSON_PROJECT_LIST);
	num_obj = cJSON_GetObjectItem(root, JSON_PROJECT_NUM);
	if (list_obj == NULL || num_obj == NULL)
	{
		LEAVE(-2, out);
	}

	list_size = cJSON_GetArraySize(list_obj);
	num = num_obj->valueint;
	if (num > list_size)
	{
		num = list_size;
	}

	for (int32_t i = 0; i < num && *name_num < MAX_PROJECT_SWITCH_NUM; i++)
	{
		cJSON *proj_obj = cJSON_GetArrayItem(list_obj, i);
		cJSON *name_obj = NULL;

		if (proj_obj == NULL)
		{
			continue;
		}

		name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME);
		if (name_obj == NULL || name_obj->valuestring == NULL)
		{
			continue;
		}

		strncpy(names[*name_num], name_obj->valuestring, MAX_FNAME_LEN);
		names[*name_num][MAX_FNAME_LEN] = '\0';
		(*name_num)++;
	}

out:
	return error;
}

static void scfw_fill_default_proj_info(struct project_info *proj_info, const char *name)
{
	if (proj_info == NULL || name == NULL)
	{
		return;
	}

	memset(proj_info, 0, sizeof(*proj_info));
	snprintf(proj_info->name, sizeof(proj_info->name), "%s", name);
	snprintf(proj_info->passwd, sizeof(proj_info->passwd), "%s", DEFAULT_PASSWD);
	snprintf(proj_info->create_time, sizeof(proj_info->create_time), "%s", get_cur_rtc());
	snprintf(proj_info->image_source, sizeof(proj_info->image_source), "%s", DEFAULT_IMAGE_SOURCE);
	proj_info->interval_ms = 0;
}

static void scfw_fill_default_switch_info(struct switch_info *swth_info)
{
	if (swth_info == NULL)
	{
		return;
	}

	memset(swth_info, 0, sizeof(*swth_info));
	snprintf(swth_info->digital_io.line_cfg, sizeof(swth_info->digital_io.line_cfg), "*****");
}

static int32_t scfw_read_sln_header_info(const char *file_path,
										 struct project_info *proj_info,
										 struct switch_info *swth_info)
{
	int32_t error = 0;
	SLNAR_T *s = NULL;
	SLNAR_SLN_BASE_INFO_T baseinfo = {0};
	SLNAR_SLN_SW_INFO_T swinfo = {0};
	int r = SLNAR_EC_OK;

	if (file_path == NULL || proj_info == NULL || swth_info == NULL)
	{
		LEAVE(-1, out);
	}

	if ((s = slnar_read_new()) == NULL)
	{
		LOGE("[%s] slnar_read_new failed\n", __func__);
		LEAVE(-2, out);
	}

	slnar_set_archive_path(s, file_path);
	if ((r = slnar_read_header(s)) != SLNAR_EC_OK)
	{
		LOGE("[%s] slnar_read_header failed, errno:%d, %s\n",
			__func__, slnar_errno(s), slnar_errstr(s));
		LEAVE(-3, out);
	}

	slnar_get_sln_baseinfo(s, &baseinfo);
	slnar_get_sln_swinfo(s, &swinfo);
	scfw_convert_project_base_info_r(&baseinfo, proj_info);
	scfw_convert_project_switch_info_r(&swinfo, swth_info);

out:
	if (s != NULL)
	{
		slnar_read_free(s);
	}
	return error;
}

static int32_t scfw_sync_project_mng_with_sln_files(void)
{
	int32_t error = 0;
	int32_t ret = 0;
	cJSON *file_root = NULL;
	cJSON *new_root = NULL;
	cJSON *last_obj = NULL;
	struct sln_file_entry entries[MAX_PROJECT_SWITCH_NUM] = {0};
	char mng_names[MAX_PROJECT_SWITCH_NUM][MAX_FNAME_LEN + 1] = {{0}};
	char last_used_name[MAX_FNAME_LEN + 1] = {0};
	uint32_t entry_num = 0;
	uint32_t mng_num = 0;
	bool need_update = false;
	const char *last_used = NULL;
	cJSON *old_root = algo_proj_mng_root;

	if (algo_proj_mng_root != NULL)
	{
		LEAVE(0, out);
	}

	if (osal_is_dir_exist(DEVSLN_DIR))
	{
		if ((ret = scfw_collect_sln_files(entries, &entry_num)) < 0)
		{
			LOGE("[%s] collect sln files failed:%d\n", __func__, ret);
			LEAVE(-1, out);
		}
	}

	if (osal_is_file_exist(PROJ_MNG_FNAME))
	{
		if (load_algo_project_mng_from_file(&file_root) < 0)
		{
			need_update = true;
		}
		else
		{
			last_obj = cJSON_GetObjectItem(file_root, JSON_PROJECT_LAST_USED_PROJ);
			if (last_obj && last_obj->valuestring)
			{
				strncpy(last_used_name, last_obj->valuestring, MAX_FNAME_LEN);
				last_used_name[MAX_FNAME_LEN] = '\0';
			}

			if (scfw_collect_proj_names_from_mng(file_root, mng_names, &mng_num) < 0)
			{
				need_update = true;
			}
			else
			{
				if (mng_num != entry_num)
				{
					need_update = true;
				}
				if (!need_update)
				{
					for (uint32_t i = 0; i < entry_num; i++)
					{
						if (!scfw_project_name_in_list(mng_names, mng_num, entries[i].name))
						{
							need_update = true;
							break;
						}
					}
				}
				if (!need_update)
				{
					for (uint32_t i = 0; i < mng_num; i++)
					{
						if (!scfw_project_name_in_entries(entries, entry_num, mng_names[i]))
						{
							need_update = true;
							break;
						}
					}
				}
			}
		}
	}
	else if (entry_num > 0)
	{
		need_update = true;
	}

	if (!need_update)
	{
		LEAVE(0, out);
	}

	if (last_used_name[0] && scfw_project_name_in_entries(entries, entry_num, last_used_name))
	{
		last_used = last_used_name;
	}
	else if (scfw_project_name_in_entries(entries, entry_num, DEFAULT_PROJ_NAME))
	{
		last_used = DEFAULT_PROJ_NAME;
	}
	else if (entry_num > 0)
	{
		last_used = entries[0].name;
	}
	else
	{
		last_used = DEFAULT_PROJ_NAME;
	}

	new_root = cJSON_CreateObject();
	if (new_root == NULL)
	{
		LEAVE(-2, out);
	}

	cJSON_AddItemToObject(new_root, JSON_PROJECT_LAST_USED_PROJ, cJSON_CreateString(last_used));
	cJSON_AddItemToObject(new_root, JSON_PROJECT_NUM, cJSON_CreateNumber(0));
	cJSON_AddItemToObject(new_root, JSON_PROJECT_LIST, cJSON_CreateArray());

	algo_proj_mng_root = new_root;
	for (uint32_t i = 0; i < entry_num; i++)
	{
		struct project_info proj_info = {0};
		struct switch_info swth_info = {0};
		char file_path[MAX_FILE_NAME_LEN * 4] = {0};

		snprintf(file_path, sizeof(file_path), "%s%s", DEVSLN_DIR, entries[i].filename);
		if (scfw_read_sln_header_info(file_path, &proj_info, &swth_info) < 0)
		{
			scfw_fill_default_proj_info(&proj_info, entries[i].name);
			scfw_fill_default_switch_info(&swth_info);
		}
		else
		{
			strncpy(proj_info.name, entries[i].name, sizeof(proj_info.name));
			proj_info.name[sizeof(proj_info.name) - 1] = '\0';
		}

		if ((ret = add_proj_to_proj_mng_json(&proj_info, &swth_info,
											 proj_info.image_source, UNSAVE_IMAGE_SOURCE)) < 0)
		{
			LOGE("[%s] add proj %s failed:%d\n", __func__, entries[i].name, ret);
			continue;
		}
	}

	if ((ret = save_algo_project_mng_into_file(new_root)) < 0)
	{
		LOGE("[%s] save project mng failed:%d\n", __func__, ret);
		LEAVE(-3, out);
	}

	LOGI("[%s] project mng updated, num:%u\n", __func__, entry_num);

out:
	if (file_root)
	{
		cJSON_Delete(file_root);
	}
	if (algo_proj_mng_root && algo_proj_mng_root == new_root)
	{
		cJSON_Delete(algo_proj_mng_root);
	}
	algo_proj_mng_root = old_root;
	return error;
}

static int clean_default_project(void)
{
	int ret = 0;
	char file_path[MAX_FILE_NAME_LEN * 2] = {0};

	scfw_get_project_path_ddr(file_path, DEFAULT_PROJ_NAME, sizeof(file_path));
	LOGI("rm dir %s\n", file_path);

	if ((ret = osal_remove_dir(file_path)) != 0)
	{
		LOGE("[%s]mkdir %s error:%d\n", __func__, file_path, ret);
	}

	if ((ret = osal_create_dir(file_path)) != 0)
	{
		LOGW("[%s]mkdir %s error:%d\n", __func__, file_path, ret);
	}

	return 0;
}

/**
 * @brief      更新方案通信切换管理信息
 * @return
 */
static int32_t update_project_switch_mng(void)
{
	int i = 0;
	int error = 0;
	cJSON *proj_array = NULL;
	cJSON *proj_obj = NULL;
	cJSON *num_obj = NULL;
	cJSON *name_obj = NULL;
	uint32_t project_num = 0;
	uint32_t need_unlock = 0;
	uint32_t update_mask = 0;
	struct button_switch_info update_switch_info = {0};
	IMG_PROC_ERROR_CODE_E io_ctrl_code;
	uint32_t project_num_max = MAX_PROJECT_NUM;
	char param_buf[MAX_ALGO_PARAM_VALUE_LEN] = {0};

	if(0 == oem_get_user_define_by_type(OEM_STYLE_PROJECT_NUM, param_buf, sizeof(param_buf)))
	{
		project_num_max = atoi(param_buf) < MAX_PROJECT_SWITCH_NUM ? atoi(param_buf) : MAX_PROJECT_SWITCH_NUM;
	}


	if (NULL == algo_proj_mng_root)
	{
		LEAVE(0, out);
	}

	if ((proj_array = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_LIST)) == NULL)
	{
		LEAVE(-1, out);
	}

	if ((num_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_NUM)) == NULL)
	{
		LEAVE(-2, out);
	}

	if (NULL == proj_switch_mng.mng_lock)
	{
		if (osal_mutex_create(&(proj_switch_mng.mng_lock)) < 0)
		{
			LOGE("init project switch mng lock failed\r\n");
			LEAVE(-3, out);
		}

	}

	if (osal_mutex_timed_lock(proj_switch_mng.mng_lock, 100) < 0)
	{
		LOGE("try lock project switch mng lock failed\r\n");
		LEAVE(-4, out);
	}

	need_unlock = 1;
	project_num = (num_obj->valueint > project_num_max) ? project_num_max : num_obj->valueint;
	proj_switch_mng.project_num = 0;

	for (i = 0; i < project_num; i++)
	{
		proj_obj = cJSON_GetArrayItem(proj_array, i);
		if (proj_obj)
		{
			if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME)) == NULL)
					|| (NULL == name_obj->valuestring))
			{
				LEAVE(-5, out);
			}
			snprintf(proj_switch_mng.project_name[i], MAX_FNAME_LEN, "%s", name_obj->valuestring);

			if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_STR)) == NULL)
					|| (NULL == name_obj->valuestring))
			{
				LEAVE(-6, out);
			}
			snprintf(proj_switch_mng.swth_info[i].communication.cm_str, MAX_SWITCH_STR_LEN, "%s", name_obj->valuestring);

			if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_SUFFIX_STR)) == NULL))
			{
				snprintf(proj_switch_mng.swth_info[i].communication.cm_suffix_str, MAX_SWITCH_SUFFIX_LEN, "%s", "");
			}
			else
			{
				if (!cJSON_IsString(name_obj) || NULL == name_obj->valuestring)
				{
					LEAVE(-7, out);
				}
				snprintf(proj_switch_mng.swth_info[i].communication.cm_suffix_str, MAX_SWITCH_SUFFIX_LEN, "%s", name_obj->valuestring);
			}

			if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_RET)) == NULL)
					|| (NULL == name_obj->valuestring))
			{
				LEAVE(-8, out);
			}

			snprintf(proj_switch_mng.swth_info[i].communication.cm_ret, MAX_SWITCH_STR_LEN, "%s", name_obj->valuestring);

			if (((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_FAIL)) == NULL))
			{
				snprintf(proj_switch_mng.swth_info[i].communication.cm_fail, MAX_SWITCH_RET_LEN, "%s", "fail");
			}
			else
			{
				if (!cJSON_IsString(name_obj) || NULL == name_obj->valuestring)
				{
					LEAVE(-9, out);
				}
				snprintf(proj_switch_mng.swth_info[i].communication.cm_fail, MAX_SWITCH_RET_LEN, "%s", name_obj->valuestring);
			}

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_SOURCE)) == NULL)
			{
				LEAVE(-10, out);
			}

			proj_switch_mng.swth_info[i].digital_io.source = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_ACTIVE)) == NULL)
			{
				LEAVE(-11, out);
			}

			proj_switch_mng.swth_info[i].digital_io.active = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_FALSE_TIME)) == NULL)
			{
				LEAVE(-12, out);
			}

			proj_switch_mng.swth_info[i].digital_io.time = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_DI_ENABLE)) == NULL)
			{
				LEAVE(-13, out);
			}

			proj_switch_mng.swth_info[i].digital_io.di_enable = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CM_ENABLE)) == NULL)
			{
				LEAVE(-14, out);
			}

			proj_switch_mng.swth_info[i].communication.cm_enable = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_IO_INPUT)) == NULL)
			{
				LEAVE(-15, out);
			}

			proj_switch_mng.swth_info[i].digital_io.io_input = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CARM_TRI_SOURCE)) == NULL)
			{
				LEAVE(-16, out);
			}

			proj_switch_mng.swth_info[i].digital_io.camera_trigger_source = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CARM_TRI_MODE)) == NULL)
			{
				LEAVE(-17, out);
			}

			proj_switch_mng.swth_info[i].digital_io.camera_trigger_mode = name_obj->valueint;

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_DI_LINE_CFG)) == NULL)
			{
				LEAVE(-18, out);
			}

			snprintf(proj_switch_mng.swth_info[i].digital_io.line_cfg, MAX_DI_LINE_NUM, "%s", name_obj->valuestring);

			if ((name_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_TRI_COMM)) == NULL)
			{
				snprintf(proj_switch_mng.swth_info[i].communication.trigger_cm_str, MAX_SWITCH_STR_LEN, "%s", "");
			}
			else
			{
				snprintf(proj_switch_mng.swth_info[i].communication.trigger_cm_str, MAX_SWITCH_STR_LEN, "%s", name_obj->valuestring);
			}


			proj_switch_mng.project_num++;
		}
		else
		{
			LOGE("invalid proj array,idx %d\n", i);
		}
	}

	i = proj_switch_mng.project_num;

	while (--i >= 0)
	{
		LOGD("proj_mng[%d] name %s\r\n", i, proj_switch_mng.project_name[i]);
		LOGD("proj_mng[%d] mode %d\r\n", i, proj_switch_mng.swth_info[i].mode);
		LOGD("proj_mng[%d] communication str %s\r\n", i, proj_switch_mng.swth_info[i].communication.cm_str);
		LOGD("proj_mng[%d] communication ret %s\r\n", i, proj_switch_mng.swth_info[i].communication.cm_ret);
		LOGD("proj_mng[%d] communication fail %s\r\n", i, proj_switch_mng.swth_info[i].communication.cm_fail);
		LOGD("proj_mng[%d] digital io source %d\r\n", i, proj_switch_mng.swth_info[i].digital_io.source);
		LOGD("proj_mng[%d] digital io active %d\r\n", i, proj_switch_mng.swth_info[i].digital_io.active);
		LOGD("proj_mng[%d] digital io time %d\r\n", i, proj_switch_mng.swth_info[i].digital_io.time);
		LOGD("proj_mng[%d] digital io io_input %d\r\n", i, proj_switch_mng.swth_info[i].digital_io.io_input);
		LOGD("proj_mng[%d] digital io camera_trigger_source %d\r\n", i,
			 proj_switch_mng.swth_info[i].digital_io.camera_trigger_source);
		LOGD("proj_mng[%d] digital io camera_trigger_mode %d\r\n", i,
			 proj_switch_mng.swth_info[i].digital_io.camera_trigger_mode);
		LOGD("proj_mng[%d] digital io line cfg %s\r\n", i,
			 proj_switch_mng.swth_info[i].digital_io.line_cfg);
		LOGD("proj_mng[%d] communication trigger_cm_str %s\r\n", i,
			 proj_switch_mng.swth_info[i].communication.trigger_cm_str);
	}

	di_set_mask_data_last = update_mask;
	proj_button_info = update_switch_info;
	LOGD("proj_button_info.active = %d\n", proj_button_info.active);
	LOGD("proj_button_info.di_enable = %d\n", proj_button_info.di_enable);
	LOGD("proj_button_info.time = %d\n", proj_button_info.time);
out:

	if (need_unlock)
	{
		osal_mutex_unlock(proj_switch_mng.mng_lock);
	}

	if (error < 0)
	{
		LOGE("[%s] update project switch mng err[%d] !\n", __func__, error);
		return 0;
	}

	return error;
}

int delete_saved_project(const char *project_name, const char *passwd)
{
	int ret = 0;
	char file_path[MAX_FILE_NAME_LEN * 2] = {0};
	cJSON *proj_obj = NULL;
	cJSON *passwd_obj = NULL;
	cJSON *tmp_obj = NULL;

	if ((NULL == project_name) || (NULL == passwd) || (strlen(project_name) >= MAX_FILE_NAME_LEN))
	{
		return -1;
	}

	/*当algo_proj_mng_root为空且project_mng.json存在时，读取project_mng.json更新algo_proj_mng_root*/
	if ((NULL == algo_proj_mng_root) && (osal_is_file_exist(PROJ_MNG_FNAME)))
	{
		if (load_algo_project_mng_from_file(&algo_proj_mng_root) < 0)
		{
			return -2;
		}
	}

	if ((NULL != algo_proj_mng_root) && ((proj_obj = get_project_obj(algo_proj_mng_root, project_name)) != NULL))
	{
		if (passwd)
		{
			if ((passwd_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)) == NULL)
			{
				return -3;
			}

			if (0 != strncmp(passwd_obj->valuestring, passwd, MAX_FNAME_LEN))
			{
				return -4;
			}
		}

		if ((tmp_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_NUM)) == NULL)
		{
			return -5;
		}

		if (tmp_obj->valuedouble > 1)
		{
			if ((ret = delete_project_obj(algo_proj_mng_root, project_name)) < 0)
			{
				return -6;
			}

			tmp_obj->valuedouble -= 1;
			tmp_obj->valueint -= 1;

			/* write the json str into file */
			if (save_algo_project_mng_into_file(algo_proj_mng_root) < 0)
			{
				return -7;
			}

			if (update_project_switch_mng() < 0)
			{
				return -8;
			}
		}
		else
		{
			if (osal_remove_file(PROJ_MNG_FNAME) < 0)
			{
				LOGE("[%s] osal_remove_file:%s err !\n", __func__, PROJ_MNG_FNAME);
			}
			cJSON_Delete(algo_proj_mng_root);
			algo_proj_mng_root = NULL;
		}
	}

	/* delete project.sln file */
	memset(file_path, '\0', sizeof(file_path));
	snprintf(file_path, sizeof(file_path), "%s%s.sln", DEVSLN_DIR, project_name);
	if (osal_remove_file(file_path) < 0)
	{
		LOGE("osal_remove_dir fail:ret %s\r\n", file_path);
	}

	return 0;
}

static int32_t has_same_io_switch_src(const char *proj_name, cJSON *proj_array, const struct switch_info *swth_info)
{
	if (proj_name == NULL || proj_array == NULL || swth_info == NULL)
	{
		return -1;
	}
	
	if (!swth_info->digital_io.di_enable)
	{
		return -1;
	}

	for (int i = 0; i < cJSON_GetArraySize(proj_array); ++i)
	{
		cJSON *item = cJSON_GetArrayItem(proj_array, i);
		if (cJSON_GetObjectItem(item, "di_enable")->valueint
			&& 0 == strcmp(cJSON_GetObjectItem(item, "line_cfg")->valuestring, swth_info->digital_io.line_cfg))
		{
			LOGI("has same io switch src. cur_info:[%s,%s] same_info:[%s,%s]\n", proj_name, swth_info->digital_io.line_cfg,
				cJSON_GetObjectItem(item, "project_name")->valuestring, cJSON_GetObjectItem(item, "line_cfg")->valuestring);
			return 0;
		}
	}

	return -1;
}

static int32_t add_proj_to_proj_mng_json(const struct project_info *proj_info, const struct switch_info *swth_info, const char *image_source, uint32_t type)
{
	int error = 0;
	int ret = 0;
	cJSON *proj_obj = NULL;
	cJSON *proj_array = NULL;
	cJSON *tmp_obj = NULL;
	int has_same_di = -1;

	if (NULL == proj_info || NULL == swth_info)
	{
		LEAVE(-1, out);
	}

	if (NULL == algo_proj_mng_root)
	{
		if ((ret = load_algo_project_mng_from_file(&algo_proj_mng_root)) < 0)
		{
			LOGE("ERROR load algo project mang from file error ret = %d\n", ret);
		}

		if (NULL == algo_proj_mng_root)
		{
			if ((algo_proj_mng_root = cJSON_CreateObject()) == NULL)
			{
				LEAVE(-2, out);
			}

			cJSON_AddItemToObject(algo_proj_mng_root,
								  JSON_PROJECT_LAST_USED_PROJ,
								  cJSON_CreateString(DEFAULT_PROJ_NAME));
			cJSON_AddItemToObject(algo_proj_mng_root,
								  JSON_PROJECT_NUM,
								  cJSON_CreateNumber(0));

			cJSON_AddItemToObject(algo_proj_mng_root,
								  JSON_PROJECT_LIST,
								  cJSON_CreateArray());
		}
	}
	
	/* is the project already exist ? */
	if ((proj_obj = get_project_obj(algo_proj_mng_root, proj_info->name)) == NULL)
	{
		/* not found, create a new proj_obj in array "algo_project_list" */
		if ((proj_array = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_LIST)) == NULL)
		{
			LEAVE(-4, out);
		}

		if ((proj_obj = cJSON_CreateObject()) == NULL)
		{
			LEAVE(-5, out);
		}

		//校验相同IO触发源的方案，如果有相同，则关闭新增的方案IO切换源
		has_same_di = has_same_io_switch_src(proj_info->name, proj_array, swth_info);

		cJSON_AddItemToArray(proj_array, proj_obj);
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_NAME,
							  cJSON_CreateString(proj_info->name));
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_PASSWD,
							  cJSON_CreateString(proj_info->passwd));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_CRAT_TIME,
							  cJSON_CreateString(proj_info->create_time));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_INTER_MS,
							  cJSON_CreateNumber(proj_info->interval_ms));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_SWITCH_STR,
							  cJSON_CreateString(swth_info->communication.cm_str));
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_SWITCH_SUFFIX_STR,
							  cJSON_CreateString(swth_info->communication.cm_suffix_str));
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_SWITCH_RET,
							  cJSON_CreateString(swth_info->communication.cm_ret));
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_SWITCH_FAIL,
							  cJSON_CreateString(swth_info->communication.cm_fail));
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_SWITCH_SOURCE,
							  cJSON_CreateNumber(swth_info->digital_io.source));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_SWITCH_ACTIVE,
							  cJSON_CreateNumber(swth_info->digital_io.active));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_FALSE_TIME,
							  cJSON_CreateNumber(swth_info->digital_io.time));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_CM_ENABLE,
							  cJSON_CreateNumber(swth_info->communication.cm_enable));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_DI_ENABLE,
							  cJSON_CreateNumber(has_same_di == 0 ? 0 : swth_info->digital_io.di_enable));	

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_IO_INPUT,
							  cJSON_CreateNumber(swth_info->digital_io.io_input));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_CARM_TRI_SOURCE,
							  cJSON_CreateNumber(swth_info->digital_io.camera_trigger_source));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_CARM_TRI_MODE,
							  cJSON_CreateNumber(swth_info->digital_io.camera_trigger_mode));
		
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_IMAGE_SOURCE,
							  cJSON_CreateString(image_source));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_DI_LINE_CFG,
							  cJSON_CreateString(has_same_di == 0 ? "*****" : swth_info->digital_io.line_cfg));

		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_TRI_COMM,
							  cJSON_CreateString(swth_info->communication.trigger_cm_str));


		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_RELOAD,
							  cJSON_CreateNumber(true));

		if ((tmp_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_NUM)) == NULL)
		{
			LEAVE(-6, out);
		}

		tmp_obj->valuedouble += 1;
		tmp_obj->valueint += 1;
	}
	else
	{
		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)) == NULL) || (NULL == tmp_obj->valuestring))
		{
			LEAVE(-7, out);
		}

		/* replace the valuestring of node "passwd" if chaneged */
		if (0 != strncmp(tmp_obj->valuestring, proj_info->passwd, MAX_FNAME_LEN))
		{
			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_PASSWD, cJSON_CreateString(proj_info->passwd));
		}

		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CRAT_TIME)) == NULL)
				|| (NULL == tmp_obj->valuestring))
		{
			LEAVE(-8, out);
		}

		/* replace the valuestring of node "create_time" if chaneged */
		if (0 != strncmp(tmp_obj->valuestring, proj_info->create_time, MAX_FNAME_LEN))
		{
			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_CRAT_TIME, cJSON_CreateString(proj_info->create_time));
		}

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_INTER_MS)) == NULL)
		{
			LEAVE(-9, out);
		}
	
		tmp_obj->valuedouble = proj_info->interval_ms;
		tmp_obj->valueint = proj_info->interval_ms;

		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_STR)) == NULL)
				|| (NULL == tmp_obj->valuestring))
		{
			LEAVE(-10, out);
		}

		if (0 != strncmp(tmp_obj->valuestring,
						 swth_info->communication.cm_str, MAX_SWITCH_STR_LEN))
		{
			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_SWITCH_STR, cJSON_CreateString(swth_info->communication.cm_str));
		}

		//字段不存在就添加，兼容老版本方案
		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_SUFFIX_STR)) == NULL)
		{
			cJSON_AddItemToObject(proj_obj, JSON_PROJECT_SWITCH_SUFFIX_STR, cJSON_CreateString(swth_info->communication.cm_suffix_str));
		}
		else
		{
			if (NULL == tmp_obj->valuestring)
			{
				LEAVE(-11, out);
			}

			if (0 != strncmp(tmp_obj->valuestring, swth_info->communication.cm_suffix_str, MAX_SWITCH_SUFFIX_LEN))
			{
				cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_SWITCH_SUFFIX_STR, cJSON_CreateString(swth_info->communication.cm_suffix_str));
			}
		}

		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_RET)) == NULL)
				|| (NULL == tmp_obj->valuestring))
		{
			LEAVE(-12, out);
		}

		if (0 != strncmp(tmp_obj->valuestring,
						 swth_info->communication.cm_ret, MAX_SWITCH_RET_LEN))
		{
			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_SWITCH_RET, cJSON_CreateString(swth_info->communication.cm_ret));
		}

		//字段不存在就添加，兼容老版本方案
		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_FAIL)) == NULL)
		{
			cJSON_AddItemToObject(proj_obj, JSON_PROJECT_SWITCH_FAIL, cJSON_CreateString(swth_info->communication.cm_fail));
		}
		else
		{
			if (NULL == tmp_obj->valuestring)
			{
				LEAVE(-13, out);
			}

			if (0 != strncmp(tmp_obj->valuestring, swth_info->communication.cm_fail, MAX_SWITCH_RET_LEN))
			{
				cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_SWITCH_FAIL, cJSON_CreateString(swth_info->communication.cm_fail));
			}
		}

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_SOURCE)) == NULL)
		{
			LEAVE(-14, out);
		}

		tmp_obj->valuedouble = swth_info->digital_io.source;
		tmp_obj->valueint = swth_info->digital_io.source;

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_ACTIVE)) == NULL)
		{
			LEAVE(-15, out);
		}

		tmp_obj->valuedouble = swth_info->digital_io.active;
		tmp_obj->valueint = swth_info->digital_io.active;

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_FALSE_TIME)) == NULL)
		{
			LEAVE(-16, out);
		}

		tmp_obj->valuedouble = swth_info->digital_io.time;
		tmp_obj->valueint = swth_info->digital_io.time;

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CM_ENABLE)) == NULL)
		{
			LEAVE(-17, out);
		}

		tmp_obj->valuedouble = swth_info->communication.cm_enable;
		tmp_obj->valueint = swth_info->communication.cm_enable;

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_DI_ENABLE)) == NULL)
		{
			LEAVE(-18, out);
		}

		tmp_obj->valuedouble = swth_info->digital_io.di_enable;
		tmp_obj->valueint = swth_info->digital_io.di_enable;

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_IO_INPUT)) == NULL)
		{
			LEAVE(-19, out);
		}

		tmp_obj->valuedouble = swth_info->digital_io.io_input;
		tmp_obj->valueint = swth_info->digital_io.io_input;

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CARM_TRI_SOURCE)) == NULL)
		{
			LEAVE(-20, out);
		}

		tmp_obj->valuedouble = swth_info->digital_io.camera_trigger_source;
		tmp_obj->valueint = swth_info->digital_io.camera_trigger_source;

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CARM_TRI_MODE)) == NULL)
		{
			LEAVE(-21, out);
		}

		tmp_obj->valuedouble = swth_info->digital_io.camera_trigger_mode;
		tmp_obj->valueint = swth_info->digital_io.camera_trigger_mode;
		
		if (type)
		{
			if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_IMAGE_SOURCE)) == NULL)
					|| (NULL == tmp_obj->valuestring))
			{
				LEAVE(-22, out);
			}
		
		
			if (0 != strncmp(tmp_obj->valuestring,
							 image_source, MAX_IMAGE_SOURCE_LEN))
			{
				cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_IMAGE_SOURCE, cJSON_CreateString((char *)image_source));
			}
		}
		
		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_DI_LINE_CFG)) == NULL)
				|| (NULL == tmp_obj->valuestring))
		{
			LEAVE(-23, out);
		}

		if (0 != strncmp(tmp_obj->valuestring,
						 swth_info->digital_io.line_cfg, MAX_DI_LINE_NUM))
		{
			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_DI_LINE_CFG, cJSON_CreateString(swth_info->digital_io.line_cfg));
		}

		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_TRI_COMM)) == NULL))
		{
			cJSON_AddItemToObject(proj_obj, JSON_PROJECT_TRI_COMM, cJSON_CreateString(swth_info->communication.trigger_cm_str));
		}
		else
		{
			if (NULL == tmp_obj->valuestring)
			{
			    LEAVE(-24, out);
			}
		    if (0 != strncmp(tmp_obj->valuestring,
			    swth_info->communication.trigger_cm_str, MAX_TRIGGER_STR_LEN))
		    {
			    cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_TRI_COMM, cJSON_CreateString(swth_info->communication.trigger_cm_str));
		    }
		}

		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_RELOAD)) == NULL)
		{
			cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_RELOAD,
							  cJSON_CreateNumber(true));
		}
		else
		{
			tmp_obj->valueint = true;
		}
			
	}

out:
	return (error);
}

int32_t scfw_copy_project_to_ddr(const char *dst_proj_name, const char *passwd, const char *src_proj_name)
{
	int32_t ret = 0;
	int32_t error = 0;
	char dst_path[MAX_ALGO_NAME_LEN * 2] = {0}; /*目标路径*/
	char src_path[MAX_ALGO_NAME_LEN * 2] = {0}; /*源路径*/
	char base_image_path[MAX_ALGO_NAME_LEN * 2] = {0};
	struct project_info proj_info = {0};				/**< 方案信息 */
	struct switch_info swth_info = {0};				/**< 切换信息 */    
    SLNAR_DEV_INFO_T devinfo = {0};
    SLNAR_SLN_BASE_INFO_T baseinfo = {0};    
    SLNAR_SLN_SW_INFO_T swinfo = {0};

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 0);
	
	if ((NULL == dst_proj_name) || (NULL == passwd) || (NULL == src_proj_name))
	{
		LEAVE(-1, out);
	}
	snprintf(src_path, sizeof(src_path), "%s%s.sln", DEVSLN_DIR, src_proj_name);
	
	if (false == osal_is_file_exist(src_path))
	{
		LOGE("path %s is not found \n", src_path);
		LEAVE(-2, out);
	}

	snprintf(dst_path, sizeof(dst_path), "%s%s.sln", DEVSLN_DIR, dst_proj_name);

    if (osal_copy(src_path, dst_path) < 0)
    {
		LOGE("osal_copy failed, src:%s, dst:%s\n", src_path, dst_path);
		LEAVE(-3, out);
    }
    
    snprintf(base_image_path, sizeof(base_image_path),
        "%s%s.jpg", DEVSLN_BASE_IMG_DIR, dst_proj_name);

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 40);

    {
        SLNAR_T *s = NULL;
        int r = SLNAR_EC_OK;

        if ((s = slnar_read_new()) == NULL)
        {
            LOGE("slnar_read_new failed\n");
            LEAVE(-4, out);
        }
        slnar_set_archive_path(s, dst_path);              
        slnar_set_baseimage_path(s, base_image_path);
        if ((r = slnar_read_header(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_read_header failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
			slnar_read_free(s);     
            LEAVE(-5, out);
        }

        slnar_get_dev_info(s, &devinfo);
        slnar_get_sln_baseinfo(s, &baseinfo);
        slnar_get_sln_swinfo(s, &swinfo);
        slnar_read_free(s);
    }

    if (devinfo.devtype != get_dev_type_from_env())
    {
        LOGE("devtype mismatch, devtype:%#x, sln-devtype:%#x\n",
            get_dev_type_from_env(), devinfo.devtype);                
        LEAVE(-6, out);
    }

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 60);

    snprintf(baseinfo.name, sizeof baseinfo.name, "%s", dst_proj_name);
	snprintf(baseinfo.ctime, sizeof baseinfo.ctime, "%s", get_cur_rtc());

    scfw_convert_project_base_info_r(&baseinfo, &proj_info);    
    scfw_convert_project_switch_info_r(&swinfo, &swth_info);

	{
        SLNAR_T *s = NULL;
        int r = SLNAR_EC_OK;

        if ((s = slnar_modify_new()) == NULL)
        {
            LOGE("slnar_modify_new failed\n");
            LEAVE(-7, out);
        }
        slnar_set_archive_path(s, dst_path);              
        slnar_modify_set_flags(s, SLNAR_MODIFIER_FLAG_SLN_BASE_INFO);
        slnar_set_sln_baseinfo(s, &baseinfo);
        if ((r = slnar_modify_header(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_modify_header failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
            slnar_modify_free(s);            
            LEAVE(-8, out);
        }

        slnar_modify_free(s);
    }

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 80);

	/* web set switch info ,set cur algo image source to json*/
	if (0 == strlen(cur_algo_image_source))
	{
		strncpy(cur_algo_image_source, DEFAULT_IMAGE_SOURCE, sizeof(cur_algo_image_source));
		cur_algo_image_source[sizeof(cur_algo_image_source) - 1] = '\0';
	}

	/* change json root */
	if ((ret = add_proj_to_proj_mng_json(&proj_info, &swth_info, cur_algo_image_source, SAVE_IMAGE_SOURCE)) < 0)
	{
		LOGE("[%s] add_proj_9to_proj_mng_json:%d\n", __func__, ret);
		LEAVE(-31, out);
	}

	/* 写入数据到文件中 */
	if ((ret = save_algo_project_mng_into_file(algo_proj_mng_root)) < 0)
	{
		LOGE("[%s] save_algo_project_mng_into_file:%d\n", __func__, ret);
		LEAVE(-32, out);
	}

	if (update_project_switch_mng() < 0)
	{
		LEAVE(-33, out);
	}

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 90);

out:
    if (error != 0)
	{
		if ((ret = osal_remove_file(dst_path)) < 0)
		{
			LOGW("osal_remove_file fail:%s error:%d\n", dst_path, ret);
		}

		if ((ret = osal_remove_file(base_image_path)) < 0)
		{
			LOGW("osal_remove_file fail:%s error:%d\n", base_image_path, ret);
		}
	}

	return error;
}

/**
 * @brief      保存方案到ddr里面
 *             
 * @param[in]  
 * @param[in]  
 * @return
 *
 */
int32_t scfw_save_as_project_to_ddr(const char *project_name, const char *passwd, const char *src_proj_name)
{
	int32_t ret = 0;
	int32_t error = 0;
	int32_t language = 0;
	char file_path[MAX_ALGO_NAME_LEN * 2] = {0};/*方案文件路径 */
	char dst_path[MAX_ALGO_NAME_LEN * 2] = {0}; /*目标路径*/
	char src_path[MAX_ALGO_NAME_LEN * 2] = {0}; /*目标路径*/
	char base_image_path[MAX_ALGO_NAME_LEN * 4] = {0}; /*基准图文件路径*/    
    int base_image_exist = 1;
	char cur_proj_path[MAX_ALGO_NAME_LEN * 2] = {0};
	struct project_info proj_info = {0};
	struct switch_info swth_info = {0};
	IMG_PROC_ERROR_CODE_E lan_err = IMG_PROC_EC_SUCCESS;

	/*防止影响出错还原，错误码与目录不存在一样，皆为入参错误*/
	if ((NULL == project_name) || (NULL == passwd) || (NULL == src_proj_name))
	{
		LEAVE(-1, out);
	}
	
	if (FALSE == osal_is_dir_exist(DEVSLN_DIR)) /*目录不存在创建目录*/
	{
		LOGI("The save_path %s is not exist.Now creat path!!\n", DEVSLN_DIR);

		if ((ret = osal_create_dir(DEVSLN_DIR)) < 0)
		{
			LOGE("osal_create_dir %s error\n", DEVSLN_DIR);
		}
	}

	if (FALSE == osal_is_dir_exist(DEVSLN_BASE_IMG_DIR)) /*目录不存在创建目录*/
	{
		LOGI("The save_path %s is not exist.Now creat path!!\n", DEVSLN_BASE_IMG_DIR);

		if ((ret = osal_create_dir(DEVSLN_BASE_IMG_DIR)) < 0)
		{
			LOGE("osal_create_dir %s error\n", DEVSLN_BASE_IMG_DIR);
		}
	}

	if ((ret = vm_procedure_save_param(CREATE_PROCEDURE_ID)) < 0)
	{
		LOGE("vm_procedure_save_param error:%d\n", ret);
	}
	if ((ret = scfw_save_module_id_array_into_file(cur_project_name)) < 0)
	{
		LOGE("scfw_save_module_id_array_into_file %s error\n", cur_project_name);
	}

	if ((ret = scfw_save_module_connect_into_file(cur_project_name)) < 0)
	{
		LOGE("scfw_save_module_connect_into_file %s error\n", cur_project_name);
		LEAVE(-1, out);
	}

	if ((ret = scfw_save_module_sub_into_file(cur_project_name)) < 0)
	{
		LOGE("scfw_save_module_sub_into_file %s error\n", cur_project_name);
		LEAVE(-2, out);
	}

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 10);

	if (strncmp(cur_project_name, project_name, MAX_FNAME_LEN))
	{
		snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, project_name);
		snprintf(cur_proj_path, sizeof(cur_proj_path), "%s%s", PROJ_DIR, src_proj_name);

		if ((ret = osal_move(cur_proj_path, src_path)) < 0)
		{
			LOGE("osal move %s %s error\n", cur_proj_path, src_path);
			LEAVE(-3, out);
		}
	}
	else
	{
		snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, project_name);
	}

	update_cur_project_name(project_name);
	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 15);

	ret = scfw_get_project_switch_info_ex(src_proj_name, &swth_info);
    if (ret != 0)
    {
        LOGE("cant't get %s's switch info\n", project_name);
        LEAVE(-4, out);
    }        

	/*构造方案信息*/
	snprintf(proj_info.name, sizeof(proj_info.name), project_name);
	snprintf(proj_info.passwd, sizeof(proj_info.passwd), passwd);
	snprintf(proj_info.create_time, sizeof(proj_info.create_time), get_cur_rtc());    
    snprintf(proj_info.image_source, sizeof proj_info.image_source, "%s", cur_algo_image_source);
	proj_info.interval_ms = scfw_get_project_running_interval();

	lan_err = appApiGetIntParam(DEV_LANGUAGE, &language);
	if (IMG_PROC_EC_SUCCESS != lan_err)
	{
		LOGE("get language type error \n");
		LEAVE(-5, out);
	}

	snprintf(base_image_path,
        sizeof(base_image_path), 
        "%s/vtool/%d%s/%s", 
        src_path,
        PROJ_BASE_IMAGE_MODULE_ID, 
        BASE_IMAGE_ALGO_NAME, 
        BASE_IMAGE_FILE_NAME);
	if (!osal_is_file_exist(base_image_path))
	{
        base_image_exist = 0;
	}

	snprintf(file_path, sizeof(file_path), "%s%s.sln", DEVSLN_DIR, project_name);

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 20);

    {
        SLNAR_T *s = NULL;
        SLNAR_DEV_INFO_T devinfo = {0};
        SLNAR_SLN_BASE_INFO_T baseinfo = {0};
        SLNAR_SLN_SW_INFO_T swinfo = {0};
        int r = SLNAR_EC_OK;

        if ((s = slnar_write_new()) == NULL)
        {
            LOGE("slnar_write_new failed\n");
            LEAVE(-6, out);
        }

        devinfo.devtype = get_dev_type_from_env();
        devinfo.language = language;
        slnar_set_dev_info(s, &devinfo);
        scfw_convert_project_base_info(&proj_info ,&baseinfo);        
        slnar_set_sln_baseinfo(s, &baseinfo);
        scfw_convert_project_switch_info(&swth_info, &swinfo);
        slnar_set_sln_swinfo(s, &swinfo);
        slnar_set_runtime_path(s, src_path);
        slnar_set_archive_path(s, file_path);
        if (base_image_exist) {
            slnar_set_baseimage_path(s, base_image_path);
        }
        if ((r = slnar_write(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_write failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
            slnar_write_free(s);            
            LEAVE(-7, out);
        }
        slnar_write_free(s);
    }

	/* Need to check */
	if (osal_get_file_size(file_path) > MAX_UPDATE_SLN_LEN)
	{
		LOGE("solution size exceeds the upper limit\r\n");
		LEAVE(SC_EC_SLN_OVERSIZE_OLD, out);
	}

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 60);

	if (base_image_exist)
	{
		snprintf(file_path, sizeof(file_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, project_name);
        if ((ret = osal_copy(base_image_path, file_path)) < 0)
		{
			LOGE("osal copy %s %s error\n", base_image_path, file_path);
			LEAVE(-8, out);
		}
	}

	/* web set switch info ,set cur algo image source to json*/
	if (0 == strlen(cur_algo_image_source))
	{
		strncpy(cur_algo_image_source, DEFAULT_IMAGE_SOURCE, sizeof(cur_algo_image_source));
		cur_algo_image_source[sizeof(cur_algo_image_source) - 1] = '\0';
	}
	
	/* change json root */
	if ((ret = add_proj_to_proj_mng_json(&proj_info, &swth_info, cur_algo_image_source, SAVE_IMAGE_SOURCE)) < 0)
	{
		LOGE("[%s] add_proj_to_proj_mng_json:%d\n", __func__, ret);
		LEAVE(-17, out);
	}

    PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 80);
	
	/* 写入数据到文件中 */
	if ((ret = save_algo_project_mng_into_file(algo_proj_mng_root)) < 0)
	{
		LOGE("[%s] save_algo_project_mng_into_file:%d\n", __func__, ret);
		LEAVE(-18, out);
	}
	if ((ret = update_last_used_project_name(project_name)) < 0)
	{
		LOGE("update_last_used_project_name ret = %d\n", ret);
		LEAVE(-19, out);
	}	
	if (update_project_switch_mng() < 0)
	{
		LEAVE(-20, out);
	}

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 90);

out:
	if (error)
	{
		if (error == -3)
		{
			snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, src_proj_name);
			snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, project_name);
			
			if ((ret = osal_move(dst_path, src_path)) < 0)
			{
				LOGE("osal move %s %s error\n", dst_path, src_path);
				LEAVE(-3, out);
			}
		}

		/*原来的目录已经删除或者清空*/
		if (error < -4) /*其他的需要恢复目录名称要不一样*/
		{
			if (strncmp(cur_project_name, src_proj_name, MAX_FNAME_LEN)) /*名字不一样*/
			{
				/* 由A编辑后再保存为A，不删除JSON信息与方案文件，这样即使失败了方案A还是正常的方案*/
				if ((ret = delete_saved_project(project_name, passwd)) < 0)/*删除json信息与方案文件*/
				{
					LOGE("delete_saved_project fail:ret %d\r\n", ret);
				}

				LOGE("osal remv %s error\n", src_proj_name);
				snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, project_name); /*拷贝整个目录*/
				snprintf(cur_proj_path, sizeof(cur_proj_path), "%s%s -r", PROJ_DIR, src_proj_name);/*原来的目录*/

				if ((ret = osal_copy(dst_path, cur_proj_path)) < 0)
				{
					LOGE("osal copy %s %s error\n", cur_proj_path, dst_path);
				}

				/*删除拷贝的目录*/
				if ((ret = osal_remove_dir(dst_path)) < 0) //返过来
				{
					LOGE("osal copy %s %s error\n", cur_proj_path, dst_path);
				}

			}

			/*名字一样也一起更新掉*/
			update_cur_project_name(src_proj_name);
		}
	}
	else
	{
		update_pwd();
		update_procedure_name();
		PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 100);
	}
	return error;
}

int32_t scfw_save_project_to_path(const char *project_name, const char *passwd, const char *save_path)
{
	int32_t ret = 0;
	int32_t error = 0;
	int32_t language = 0;
	char file_path[MAX_ALGO_NAME_LEN * 2] = {0};/*方案文件路径 */
	char dst_path[MAX_ALGO_NAME_LEN * 2] = {0}; /*目标路径*/
	char src_path[MAX_ALGO_NAME_LEN * 2] = {0}; /*目标路径*/
	char base_image_path[MAX_ALGO_NAME_LEN * 4] = {0}; /*基准图文件路径*/
    int base_image_exist = 1;
	char cur_proj_path[MAX_ALGO_NAME_LEN * 2] = {0};
	char pre_project_name[MAX_ALGO_NAME_LEN * 2] = {0}; /*上一次运行的方案 用来防止保存失败*/
	struct project_info proj_info = {0};
	struct switch_info swth_info = {0};
	IMG_PROC_ERROR_CODE_E lan_err = IMG_PROC_EC_SUCCESS;

	/*防止影响出错还原，错误码与目录不存在一样，皆为入参错误*/
	if ((NULL == project_name) || (NULL == passwd) || (NULL == save_path))
	{
		LEAVE(-1, out);
	}

	if (FALSE == osal_is_dir_exist(save_path)) /*目录不存在创建目录*/
	{
		LOGI("The save_path %s is not exist.Now creat path!!\n", save_path);

		if ((ret = osal_create_dir(save_path)) < 0)
		{
			LOGE("osal_create_dir %s error\n", save_path);
		}
	}

	if (FALSE == osal_is_dir_exist(DEVSLN_BASE_IMG_DIR)) /*目录不存在创建目录*/
	{
		LOGI("The save_path %s is not exist.Now creat path!!\n", DEVSLN_BASE_IMG_DIR);

		if ((ret = osal_create_dir(DEVSLN_BASE_IMG_DIR)) < 0)
		{
			LOGE("osal_create_dir %s error\n", DEVSLN_BASE_IMG_DIR);
		}
	}

	if ((ret = vm_procedure_save_param(CREATE_PROCEDURE_ID)) < 0)
	{
		LOGE("vm_procedure_save_param error:%d\n", ret);
	}

	if ((ret = scfw_save_module_id_array_into_file(cur_project_name)) < 0)
	{
		LOGE("scfw_save_module_id_array_into_file %s error\n", cur_project_name);
	}
	
	if ((ret = scfw_save_module_connect_into_file(cur_project_name)) < 0)
	{
		LOGE("scfw_save_module_connect_into_file %s error\n", cur_project_name);
		LEAVE(-1, out);
	}

	if ((ret = scfw_save_module_sub_into_file(cur_project_name)) < 0)
	{
		LOGE("scfw_save_module_sub_into_file %s error\n", cur_project_name);
		LEAVE(-2, out);
	}

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 20);

	if (strncmp(cur_project_name, project_name, MAX_FNAME_LEN))
	{
		snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, project_name);
		snprintf(cur_proj_path, sizeof(cur_proj_path), "%s%s", PROJ_DIR, cur_project_name);

		if ((ret = osal_move(cur_proj_path, src_path)) < 0)
		{
			LOGE("osal move %s %s error\n", cur_proj_path, src_path);
			LEAVE(-3, out);
		}

		/*判断当前是否为default，是则创建default目录注：下发名字不准为default_project*/
		if (0 == strncmp(cur_project_name, DEFAULT_PROJ_NAME, MAX_ALGO_NAME_LEN))
		{
			if ((ret = osal_create_dir(DEFAULT_PROJ_DIR)) < 0)
			{
				LOGE("osal copy %s %s error\n", cur_proj_path, dst_path);
				LEAVE(-4, out);
			}
		}
		else
		{
			LOGE("osal_remove_dir %s error\n", cur_proj_path);
		}
	}
	else
	{
		snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, project_name);
	}

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 30);

	strncpy(pre_project_name, cur_project_name, MAX_ALGO_NAME_LEN);
	pre_project_name[MAX_ALGO_NAME_LEN - 1] = '\0';
	update_cur_project_name(project_name);

    ret = scfw_get_project_switch_info_ex(project_name, &swth_info);
    if (ret != 0)
    {
        LOGE("cant't get %s's switch info\n", project_name);
        LEAVE(-6, out);
    }        

    snprintf(proj_info.name, sizeof proj_info.name, "%s", project_name);
    snprintf(proj_info.passwd, sizeof proj_info.passwd, "%s", passwd);
    snprintf(proj_info.create_time, sizeof proj_info.create_time, "%s", get_cur_rtc());    
    snprintf(proj_info.image_source, sizeof proj_info.image_source, "%s", cur_algo_image_source);
    proj_info.interval_ms = scfw_get_project_running_interval();

    snprintf(base_image_path,
        sizeof(base_image_path), 
        "%s/vtool/%d%s/%s", 
        src_path, 
        PROJ_BASE_IMAGE_MODULE_ID, 
        BASE_IMAGE_ALGO_NAME, 
        BASE_IMAGE_FILE_NAME);
    if (!osal_is_file_exist(base_image_path))
    {
        base_image_exist = 0;
    }

    snprintf(file_path, sizeof(file_path), "%s%s.sln", save_path, project_name);
    
	lan_err = appApiGetIntParam(DEV_LANGUAGE, &language);
	if (IMG_PROC_EC_SUCCESS != lan_err)
	{
		LOGE("get language type error \n");
		LEAVE(-5, out);
	}

    PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 40);

    {
        SLNAR_T *s = NULL;
        SLNAR_DEV_INFO_T devinfo = {0};
        SLNAR_SLN_BASE_INFO_T baseinfo = {0};
        SLNAR_SLN_SW_INFO_T swinfo = {0};
        int r = SLNAR_EC_OK;

        if ((s = slnar_write_new()) == NULL)
        {
            LOGE("slnar_write_new failed\n");
            LEAVE(-6, out);
        }
        devinfo.devtype = get_dev_type_from_env();
        devinfo.language = language;
        slnar_set_dev_info(s, &devinfo);
        scfw_convert_project_base_info(&proj_info ,&baseinfo);        
        slnar_set_sln_baseinfo(s, &baseinfo);
        scfw_convert_project_switch_info(&swth_info, &swinfo);
        slnar_set_sln_swinfo(s, &swinfo);
        slnar_set_runtime_path(s, src_path);
        slnar_set_archive_path(s, file_path);
        if (base_image_exist) {
            slnar_set_baseimage_path(s, base_image_path);
        }
        if ((r = slnar_write(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_write failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
            slnar_write_free(s);            
            LEAVE(-7, out);
        }
        slnar_write_free(s);
    }

	/* Need to check */
	if (osal_get_file_size(file_path) > MAX_UPDATE_SLN_LEN)
	{
		LOGE("solution size exceeds the upper limit\r\n");
		LEAVE(SC_EC_SLN_OVERSIZE_OLD, out);
	}

    PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 70);

	/* 判断是否是默认路径,不是默认路径则为上传方案 */
	if (0 == strncmp(save_path, DEVSLN_DIR, MAX_ALGO_NAME_LEN))
	{
		/* web set switch info ,set cur algo image source to json*/
		if (0 == strlen(cur_algo_image_source))
		{
			strncpy(cur_algo_image_source, DEFAULT_IMAGE_SOURCE, sizeof(cur_algo_image_source));
			cur_algo_image_source[sizeof(cur_algo_image_source) - 1] = '\0';
		}

		/* change json root */
		if ((ret = add_proj_to_proj_mng_json(&proj_info, &swth_info, cur_algo_image_source, SAVE_IMAGE_SOURCE)) < 0)
		{
			LOGE("[%s] add_proj_9to_proj_mng_json:%d\n", __func__, ret);
			LEAVE(-8, out);
		}

		/* 写入数据到文件中 */
		if ((ret = save_algo_project_mng_into_file(algo_proj_mng_root)) < 0)
		{
			LOGE("[%s] save_algo_project_m10ng_into_file:%d\n", __func__, ret);
			LEAVE(-9, out);
		}

		if ((ret = update_last_used_project_name(project_name)) < 0)
		{
			LOGE("update_last_used_project_name ret = %d\n", ret);
			LEAVE(-10, out);
		}
		
		if (update_project_switch_mng() < 0)
		{
			LEAVE(-11, out);
		}
	}

	snprintf(file_path, sizeof(file_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, project_name);
    if (base_image_exist && (ret = osal_copy(base_image_path, file_path)) < 0)
	{
		LOGE("[%s] osal_copy failed, ret:%d, src:%s, dst:%s\n",
            __func__, ret, base_image_path, file_path);
		LEAVE(-12, out);
	}

	PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 90);

out:
	if (error)
	{
		/*方案保存逻辑是判断目前名字是否相同，相同则直接打包并且project下面一定要有default目录
		因此需要分类判断，对应.sln删除 没有也删一下，不会影响系统。最多报错
		ddr里面的方案此时存在以下几种（假设名字为123）
		1.之前的也是 123 当前运行的就是123 这种不用做操作
		2.之前是default 现在是123      //需要恢复目录此时需要保存到default 需要先删除再CP，目录存在进行cp会成原来目录的子目录
		3.之前是其他，现在是123 //可以直接CP
		*/
		/*名字不一样，而且原来的目录没有删掉不需要恢复防止成为子目录*/
		if ((error == -3) || (error == -4))
		{
			snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, project_name);//拷贝整个目录

			/*删除原来目录*/
			if ((ret = osal_remove_dir(dst_path)) < 0)
			{
				LOGE("osal osal_remove_dir %s %s error\n", __func__, dst_path);
			}
		}

		/*原来的目录已经删除或者清空*/
		if (error < -4) /*其他的需要恢复目录名称要不一样*/
		{

			if (strncmp(cur_project_name, pre_project_name, MAX_FNAME_LEN)) /*名字不一样*/
			{

				if ((ret = delete_saved_project(project_name, passwd)) < 0)/*删除json信息与方案文件*/
				{
					LOGE("delete_saved_project fail:ret %d\r\n", ret);
				}

				/*如果是原来是default，那么现在还保留着default目录需要先删除*/
				if (0 == strncmp(pre_project_name, DEFAULT_PROJ_NAME, MAX_FNAME_LEN))
				{
					snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, pre_project_name);

					if ((ret = osal_remove_dir(dst_path)) < 0)
					{
						LOGE("osal copy %s %s error\n", pre_project_name, dst_path);
					}
				}

				LOGE("osal remv %s error\n", pre_project_name);
				snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, project_name); /*拷贝整个目录*/
				snprintf(cur_proj_path, sizeof(cur_proj_path), "%s%s -r", PROJ_DIR, pre_project_name);/*原来的目录*/

				if ((ret = osal_copy(dst_path, cur_proj_path)) < 0)
				{
					LOGE("osal copy %s %s error\n", cur_proj_path, dst_path);
				}

				/*删除拷贝的目录*/
				if ((ret = osal_remove_dir(dst_path)) < 0) //返过来
				{
					LOGE("osal copy %s %s error\n", cur_proj_path, dst_path);
				}
			}

			/*名字一样也一起更新掉*/
			update_cur_project_name(pre_project_name);
		}
	}
	else
	{
		update_pwd();/*  update pwd*/
		update_procedure_name();/*  update pwd*/

		PUSH_RATE_PROJ(WS_CMD_SPRP, 0, 100);
	}

	return error;
}



/**
  * @brief  load a project to DDR
  * @param[in] proj_name   project name
  * @param[in] passwd      password
  * @param[in] type        0 : DI_LOAD_PROJ
  						   1 : USER_LOAD_PROJ
  						   2 : LAST_LOAD_PROJ
  						   3 ：BUTTON_LOAD_PROJ 
  * @return  0 on success; < 0 on failure
  * 注：调用加载之前必须保证之前方案被清空
  * 一般调用释放方案资源或者加载默认方案
  */
int32_t scfw_load_project(const char *proj_name, const char *passwd, uint32_t type,bool need_push_rate,bool need_push_fail)
{
	int32_t ret = 0;
	int32_t error = 0;
	char base_image_path[MAX_FILE_NAME_LEN * 2] = {0};
	char file_path[MAX_FILE_NAME_LEN * 2] = {0};
	char dst_path[MAX_FILE_NAME_LEN * 2] = {0};
	cJSON *proj_obj = NULL;
	cJSON *passwd_obj = NULL;
	cJSON *inter_obj = NULL;
	cJSON *img_src = NULL;
	struct proj_push_data proj_data = {0};
	IMG_PROC_ERROR_CODE_E err;
	int32_t language = 0;
	uint32_t time_out_cnt = 0;    

	if ((NULL == proj_name) || (NULL == passwd))
	{
		LEAVE(-1, out);
	}
	/* type=0 DI*/
	if (USER_LOAD_PROJ == type || BUTTON_LOAD_PROJ == type)
	{
		proj_data.cmd_data = WS_CMD_LPRP;
	}
	else
	{
		proj_data.cmd_data = WS_CMD_DCLP;
	}

	snprintf(cur_switch_name, sizeof(cur_switch_name), "%s", proj_name);
	
	if(need_push_rate)
	{
		proj_data.progress = 0;
		proj_data.status = 0;
		scfw_execute_project_cb(&proj_data);
	}

	if (scfw_is_need_recreate_procedure())
	{
		LEAVE(-1, out);
	}

	if(need_push_rate)
	{
		proj_data.progress = 20;
		proj_data.status = 0;
		scfw_execute_project_cb(&proj_data);
	}

	if (NULL == algo_proj_mng_root)
	{
		if (load_algo_project_mng_from_file(&algo_proj_mng_root) < 0)
		{
			LEAVE(-1, out);
		}
	}

	if ((proj_obj = get_project_obj(algo_proj_mng_root, proj_name)) == NULL)
	{
		LEAVE(-2, out);
	}

	if ((passwd_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)) == NULL)
	{
		LEAVE(-3, out);
	}

	if ((NULL != passwd_obj->valuestring)
			&& (0 != strncmp(passwd, passwd_obj->valuestring, MAX_FNAME_LEN)))
	{
		LOGE("passwd %s not correct !\n", passwd);
		LEAVE(-4, out);
	}

	if ((inter_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_RELOAD)) == NULL)
	{
		cJSON_AddItemToObject(proj_obj,
							  JSON_PROJECT_RELOAD,
							  cJSON_CreateNumber(true));
	}
	else
	{
		snprintf(file_path, sizeof(file_path), "%s%s", PROJ_DIR, proj_name);
		if (inter_obj->valueint && osal_is_dir_exist(file_path))
		{
			if ((ret = osal_remove_dir(file_path)) != 0)
			{
				LOGI("[%s]osal_remove_dir %s error:%d\n", __func__, file_path, ret);
			}
		}
		inter_obj->valueint = false;
		inter_obj->valuedouble = false;
	}

	snprintf(file_path, sizeof(file_path), "%s%s", PROJ_DIR, proj_name);

	/* 启动加载上一次执行的方案需删除之前的方案，因为升级时删除模块导致方案不完整 */
	if (LAST_LOAD_PROJ == type)
	{
		if ((ret = osal_remove_dir(file_path)) != 0)
		{
			LOGI("[%s]osal_remove_dir %s error:%d\n", __func__, file_path, ret);
		}
	}

	if (!osal_is_dir_exist(file_path))
	{
		if(need_push_rate)
		{
			proj_data.progress = 30;
			proj_data.status = 0;
			scfw_execute_project_cb(&proj_data);
		}

        err = appApiGetIntParam(DEV_LANGUAGE, &language);
        if (IMG_PROC_EC_SUCCESS != err)
        {
            LOGE("get language type error \n");
            LEAVE(-5, out);
        }

		snprintf(file_path, sizeof(file_path), "%s%s.sln", DEVSLN_DIR, proj_name);
        snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, proj_name);        
		snprintf(base_image_path, sizeof(base_image_path),  "%s%s.jpg", DEVSLN_BASE_IMG_DIR, proj_name);

        {
            SLNAR_T *s = NULL;            
            SLNAR_DEV_INFO_T devinfo = {0};
            int r = SLNAR_EC_OK;
    
            if ((s = slnar_read_new()) == NULL)
            {
                LOGE("slnar_read_new failed\n");
                LEAVE(-6, out);
            }
            slnar_set_archive_path(s, file_path);            
            slnar_set_runtime_path(s, dst_path);
            slnar_set_baseimage_path(s, base_image_path);
            if ((r = slnar_read_header(s)) != SLNAR_EC_OK)
            {
                LOGE("slnar_read_header failed, errno:%d, %s\n",
                    slnar_errno(s), slnar_errstr(s));
                slnar_read_free(s);            
                LEAVE(-7, out);
            }

            slnar_get_dev_info(s, &devinfo);
            if (devinfo.devtype != get_dev_type_from_env())
            {
                LOGE("devtype mismatch, devtype:%#x, sln-devtype:%#x\n",
                    get_dev_type_from_env(), devinfo.devtype);                
                slnar_read_free(s);            
                LEAVE(-8, out);
            }
            if (devinfo.language != language)
            {
                LOGE("language mismatch, language:%d, sln-language:%d\n",
                    language, devinfo.language);                
                slnar_read_free(s);            
                LEAVE(-9, out);
            }

            if ((r = slnar_read(s)) != SLNAR_EC_OK)
            {
                LOGE("slnar_write failed, errno:%d, %s\n",
                    slnar_errno(s), slnar_errstr(s));
                slnar_read_free(s);            
                LEAVE(-10, out);
            }
            slnar_read_free(s);
        }

		if(need_push_rate)
		{
			proj_data.progress = 60;
			proj_data.status = 0;
			scfw_execute_project_cb(&proj_data);
		}
	}

	update_cur_project_name(proj_name);

	if ((ret = update_last_used_project_name(proj_name)) < 0)
	{
		LOGE("update_last_used_project_name ret = %d\n", ret);
	}
	
	if ((ret = scfw_load_module_connect_from_file(proj_name)) < 0)
	{
		LOGE("scfw_load_module_connect_from_file %s %d\n", proj_name, ret);
		LEAVE(-19, out);
	}

	if ((ret = scfw_load_module_sub_from_file(proj_name)) < 0)
	{
		LOGE("scfw_load_module_sub_from_file %s \n", proj_name);
		LEAVE(-20, out);
	}

	if (is_comm_global_dev())
	{
		scfw_auto_sub_global_comm_algo();
	}

	if ((inter_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_INTER_MS)) == NULL)
	{
		LEAVE(-21, out);
	}
	else
	{
		scfw_reset_project_running_interval();
		scfw_set_project_running_interval(inter_obj->valueint);
	}

	if ((img_src = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_IMAGE_SOURCE)) == NULL)
	{
		LEAVE(-22, out);
	}
	else
	{
		strncpy(cur_algo_image_source, img_src->valuestring, MAX_IMAGE_SOURCE_LEN);
		cur_algo_image_source[sizeof(cur_algo_image_source) - 1] = '\0';
	}

	if(need_push_rate)
	{
		proj_data.progress = 80;
		proj_data.status = 0;
		scfw_execute_project_cb(&proj_data);
	}

	if ((ret = scfw_load_module_id_array_into_file(proj_name)) < 0)
	{
		LOGE("scfw_load_module_connect_from_file %s %d\n", proj_name, ret);
	}

	if ((ret = scfw_load_solution_finish_pre()) < 0)
	{
		LOGE("scfw_load_solution_finish_pre %d\n", ret);
	}

	if (ret = scfw_reset_module_rslt_info() != 0)
	{
		LOGE("scfw_reset_module_rslt_info %d\n", ret);
	}

	if ((ret = scfw_reset_project_count()) < 0)
	{
		LOGE("scfw_reset_project_count %d\n", ret);
	}

	if (LAST_LOAD_PROJ != type && BUTTON_LOAD_PROJ != type)/*通过按键操作加载的方案不需要开启连续运行*/
	{
		if ((ret = scfw_set_module_pub_mode(PROJ_PUB)) < 0)
		{
			LOGE("scfw_set_module_pub_mode\n");
			LEAVE(-23, out);
		}
		
		if ((ret = scfw_set_project_running_state(PROJ_RUN, 0, 0)) < 0)
		{
			LOGE("[%s] set_module_pub_mod manager error:%d\n", __func__, ret);
			LEAVE(-24, out);
		}
		
		while (!VM_M_IsProcedureRunning(CREATE_PROCEDURE_ID))
		{
			time_out_cnt++;
		
			if (time_out_cnt >= 100)
			{
				LEAVE(-25, out);
			}
		
			usleep(20 * 1000);
		}
	}

	comif_update_module_common_param();

out:
	if (0 == error && need_push_rate)
	{
		proj_data.progress = 100;
		proj_data.status = 0;
		scfw_execute_project_cb(&proj_data);
	}
	else
	{
		if (0 != error)
		{
			snprintf(file_path, sizeof(file_path), "%s%s", PROJ_DIR, proj_name);
			LOGI("osal remove file %s\n", file_path);
			if ((ret = osal_remove_dir(file_path)) != 0)
			{
				LOGE("[%s]osal_remove_dir %s error:%d\n", __func__, file_path, ret);
			}
		}
	
		do
		{
			SLNAR_BUFFER_T usrpriv = {0};
			SLNAR_T *s = NULL;
			uint32_t flag = SLNAR_MODIFIER_FLAG_SLN_UP_INFO;
			snprintf(file_path, sizeof(file_path), "%s%s.sln", DEVSLN_DIR, proj_name);

			if (NULL == (s = slnar_modify_new()))
			{
				LOGE("slnar_modify_new failed\n");
				break;
			}

			usrpriv.size = sizeof(usrpriv.size)+sizeof(error);
			usrpriv.buffer = (uint8_t *)PJ_MALLOC(usrpriv.size);
			if (NULL == usrpriv.buffer)
			{
				LOGE("Alloc memory failed\n");
				slnar_modify_free(s);
				break;
			}
			memcpy(usrpriv.buffer, &usrpriv.size, sizeof(usrpriv.size));
			memcpy(usrpriv.buffer, &error, sizeof(error));

			slnar_set_archive_path(s, file_path); 
			slnar_set_user_private(s, &usrpriv);
			slnar_modify_set_flags(s, flag);

			if (SLNAR_EC_OK != slnar_modify_header(s))
			{
                LOGE("slnar_modify_header failed, errno:%d, %s\n",
                    slnar_errno(s), slnar_errstr(s));
			}
	
			slnar_modify_free(s);
			PJ_FREE(usrpriv.buffer);
		} while(0);
		
		if(need_push_rate && need_push_fail)
		{
			proj_data.progress = 100;
			proj_data.status = -1;
			scfw_execute_project_cb(&proj_data);
		}
	}

	return error;
}

// according to the pr_flag , decide to push the progress rate.
int32_t scfw_load_default_project_with_PR(bool pr_flag)
{
	int32_t ret = IMVS_EC_OK;
	int32_t error = 0;
	int32_t module_id = 0;
	int32_t lock_state_http = -1;
	int32_t lock_state_ws = -1;
	int32_t lock_state_gvcp = -1;
	int32_t lock_state_gvmp = -1;
	struct module_name_info name_info = {0};
	char solution_dir[SOLUTION_DIR] = {0};
	char dir1[SOLUTION_DIR] = {0};
	char dir2[SOLUTION_DIR] = {0};
	char *current_solution_name = NULL;
	CREATE_PROCEDURE_PARAM procedure = {0};

	if ((lock_state_http = scfw_project_switch_trylock_proc(SWITCH_LOCK_HTTPD, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_HTTPD trylock error\n");
		LEAVE(-2, out);
	}

	if ((lock_state_ws = scfw_project_switch_trylock_proc(SWITCH_LOCK_WS, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_WS trylock error\n");
		LEAVE(-3, out);
	}
	
	if ((lock_state_gvmp = scfw_project_switch_trylock_proc(SWITCH_LOCK_GVMP, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_GVMP trylock error\n");
		LEAVE(-4, out);
	}
	
	if ((lock_state_gvcp = scfw_project_switch_trylock_proc(SWITCH_LOCK_GVCP, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_GVCP trylock error\n");
		LEAVE(-5, out);
	}
	
	if (true == pr_flag)
	{
		PUSH_RATE_PROJ(WS_CMD_CPRP, 0, 0);
	}

	if (0 == scfw_get_created_module_name_info(0, &name_info))
	{
		if ((ret = scfw_delete_module_tree(0)) < 0)
		{
			LOGE("free_algo_project error:%d\n", ret);
			LEAVE(ret, out);
		}
	}

	if (scfw_is_need_recreate_procedure())
	{
		LEAVE(-1, out);
	}

	if (true == pr_flag)
	{
		PUSH_RATE_PROJ(WS_CMD_CPRP, 0, 20);
	}


	current_solution_name = scfw_get_cur_project_name();

	if ((NULL != current_solution_name) && (0 != strlen(current_solution_name))
			&& ((0 != strncmp(DEFAULT_PROJ_NAME, current_solution_name, strlen(DEFAULT_PROJ_NAME)))
			|| (strlen(DEFAULT_PROJ_NAME) != strlen(current_solution_name))))
	{
		snprintf(solution_dir, sizeof(solution_dir), "%s%s", PROJ_DIR, current_solution_name);

		if ((ret = osal_remove_dir(solution_dir)) != 0)
		{
			LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
		}
	}

	if (true == pr_flag)
	{
		PUSH_RATE_PROJ(WS_CMD_CPRP, 0, 40);
	}

	if ((ret = osal_remove_dir(DEFAULT_PROJ_DIR)) != 0)
	{
		LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
	}

	if ((ret = osal_create_dir(DEFAULT_PROJ_DIR)) != 0)
	{
		LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
	}

	update_cur_project_name(DEFAULT_PROJ_NAME);
	
	if (update_last_used_project_name(DEFAULT_PROJ_NAME) < 0)
	{
		LOGW("[%s]update_last_used_project_name error!\n", __func__);
	}
	sprintf(dir1,"%s%s/vtool", PROJ_DIR, current_solution_name);
	sprintf(dir2,"%s%s/ctool", PROJ_DIR, current_solution_name);
	if ((ret = osal_create_dir(dir1)) != 0)
	{
		LOGW("osal_create_dir:%s default error:%d\n", dir1, ret);
	}
	if ((ret = osal_create_dir(dir2)) != 0)
	{
		LOGW("osal_create_dir:%s default error:%d\n", dir2, ret);
	}

	if (true == pr_flag)
	{
		PUSH_RATE_PROJ(WS_CMD_CPRP, 0, 60);
	}

#if 1
	scfw_reset_project_running_interval();
#else
	scfw_set_project_running_interval(0);
#endif

	//创建默认算子
	/*创建相机模块前先使fpga停止取图,防止fpga运行
	过程中设置相机参数导致fpga异常(如图像分割)*/
	if (DEFAULT_MODULE_IMAGE == (default_project_module_capability() & DEFAULT_MODULE_IMAGE))
	{
		if ((ret = scfw_create_module(IMAGE_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, IMAGE_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
	}

	if (DEFAULT_MODULE_BASE_IMAGE == (default_project_module_capability() & DEFAULT_MODULE_BASE_IMAGE))
	{
		if ((ret = scfw_create_module(BASEIMAGE_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, BASEIMAGE_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (DEFAULT_MODULE_FORMAT == (default_project_module_capability() & DEFAULT_MODULE_FORMAT))
	{
		if ((ret = scfw_create_module(FORMAT_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, FORMAT_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (DEFAULT_MODULE_LOGIC == (default_project_module_capability() & DEFAULT_MODULE_LOGIC))
	{
		if ((ret = scfw_create_module(LOGIC_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, LOGIC_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}
	if (DEFAULT_MODULE_SAVE_IMAGE == (default_project_module_capability() & DEFAULT_MODULE_SAVE_IMAGE))
	{
		if ((ret = scfw_create_module(SAVEIMG_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, LOGIC_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (DEFAULT_MODULE_IO == (default_project_module_capability() & DEFAULT_MODULE_IO))
	{
		if ((ret = scfw_create_module(IO_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, IO_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}
	
	if (is_comm_global_dev())
	{
		scfw_auto_sub_global_comm_algo();
	}
	
	if ((ret = scfw_load_solution_finish_pre()) < 0)
	{
		LOGE("scfw_load_solution_finish_pre %d\n", ret);
	}

	comif_update_module_common_param();

out:
	if (error != 0)
	{
		set_led_status(LED_TYPE_STS, LED_COLOR_RED);
	}

	if (true == pr_flag && (!error))
	{
		PUSH_RATE_PROJ(WS_CMD_CPRP, 0, 100);
	}
	if (!lock_state_http)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_HTTPD) < 0)
		{
			LOGE("SWITCH_LOCK_HTTPD proc\r\n");
		}
	}
	if (!lock_state_ws)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_WS) < 0)
		{
			LOGE("SWITCH_LOCK_WS proc\r\n");
		}
	}
	if (!lock_state_gvcp)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_GVCP) < 0)
		{
			LOGE("SWITCH_LOCK_GVCP proc\r\n");
		}
	}
	if (!lock_state_gvmp)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_GVMP) < 0)
		{
			LOGE("SWITCH_LOCK_GVMP proc\r\n");
		}
	}
	return ret;
}


int32_t scfw_load_default_project(void)
{
	int32_t ret = IMVS_EC_OK;
	int32_t error = 0;
	int32_t module_id = 0;
	struct module_name_info name_info = {0};
	char solution_dir[SOLUTION_DIR] = {0};
	char dir1[SOLUTION_DIR] = {0};
	char dir2[SOLUTION_DIR] = {0};
	char *current_solution_name = NULL;
	CREATE_PROCEDURE_PARAM procedure = {0};

    LOGI("DEFAULT-SLN LOADING...\n");
    
	if (0 == scfw_get_created_module_name_info(0, &name_info))
	{
		if ((ret = scfw_delete_module_tree(0)) < 0)
		{
			LOGE("free_algo_project error:%d\n", ret);
			LEAVE(ret, out);
		}
	}

	if (scfw_is_need_recreate_procedure())
	{
		LEAVE(-1, out);
	}

	current_solution_name = scfw_get_cur_project_name();

	if ((NULL != current_solution_name) && (0 != strlen(current_solution_name))
			&& (0 != strncmp(DEFAULT_PROJ_NAME, current_solution_name, strlen(DEFAULT_PROJ_NAME))))
	{
		snprintf(solution_dir, sizeof(solution_dir), "%s%s", PROJ_DIR, current_solution_name);

		if ((ret = osal_remove_dir(solution_dir)) != 0)
		{
			LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
		}
	}

	if (true == osal_is_dir_exist(DEFAULT_PROJ_DIR))
	{
		if ((ret = osal_remove_dir(DEFAULT_PROJ_DIR)) != 0)
		{
			LOGE("[%s]osal_create_dir default error:%d\n", __func__, ret);
		}
	}

	if ((ret = osal_create_dir(DEFAULT_PROJ_DIR)) != 0)
	{
		LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
	}

	update_cur_project_name(DEFAULT_PROJ_NAME);

	if (update_last_used_project_name(DEFAULT_PROJ_NAME) < 0)
	{
		LOGW("[%s]update_last_used_project_name error!\n", __func__);
	}
	sprintf(dir1,"%s%s/vtool", PROJ_DIR, current_solution_name);
	sprintf(dir2,"%s%s/ctool", PROJ_DIR, current_solution_name);
	
	if ((ret = osal_create_dir(dir1)) != 0)
	{
		LOGW("[%s]osal_create_dir %s error:%d\n", __func__, dir1, ret);
	}
	
	if ((ret = osal_create_dir(dir2)) != 0)
	{
		LOGW("[%s]osal_create_dir %s error:%d\n", __func__, dir2, ret);
	}
	
#if 1
	scfw_reset_project_running_interval();
#else
	scfw_set_project_running_interval(0);
#endif

	//创建默认算子
	/*创建相机模块前先使fpga停止取图,防止fpga运行
	过程中设置相机参数导致fpga异常(如图像分割)*/
	if (DEFAULT_MODULE_IMAGE == (default_project_module_capability() & DEFAULT_MODULE_IMAGE))
	{
		if ((ret = scfw_create_module(IMAGE_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, IMAGE_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
	}

	if (DEFAULT_MODULE_BASE_IMAGE == (default_project_module_capability() & DEFAULT_MODULE_BASE_IMAGE))
	{
		if ((ret = scfw_create_module(BASEIMAGE_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, BASEIMAGE_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (DEFAULT_MODULE_FORMAT == (default_project_module_capability() & DEFAULT_MODULE_FORMAT))
	{
		if ((ret = scfw_create_module(FORMAT_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, FORMAT_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (DEFAULT_MODULE_LOGIC == (default_project_module_capability() & DEFAULT_MODULE_LOGIC))
	{
		if ((ret = scfw_create_module(LOGIC_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, LOGIC_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (DEFAULT_MODULE_SAVE_IMAGE == (default_project_module_capability() & DEFAULT_MODULE_SAVE_IMAGE))
	{
		if ((ret = scfw_create_module(SAVEIMG_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, LOGIC_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (DEFAULT_MODULE_IO == (default_project_module_capability() & DEFAULT_MODULE_IO))
	{
		if ((ret = scfw_create_module(IO_ALGO_INTER_NAME, "", &module_id)) != 0)
		{
			LOGE("[%s]creat modue %s error:%d\n", __func__, IO_ALGO_INTER_NAME, ret);
			LEAVE(ret, out);
		}
		scfw_auto_sub(module_id);
	}

	if (is_comm_global_dev())
	{
		scfw_auto_sub_global_comm_algo();
	}

	if ((ret = scfw_load_solution_finish_pre()) < 0)
	{
		LOGE("scfw_load_solution_finish_pre %d\n", ret);
	}

	comif_update_module_common_param();

out:
	if (error != 0)
	{
		set_led_status(LED_TYPE_STS, LED_COLOR_RED);
	}

    LOGI("DEFAULT-SLN LOADED, ret, %d\n", ret);

exit:
	return ret;
}

static int32_t load_untitle_project(void)
{
	int32_t ret = IMVS_EC_OK;
	int32_t error = 0;
	struct module_name_info name_info = {0};
	char solution_dir[SOLUTION_DIR] = {0};
	char *current_solution_name = NULL;
	CREATE_PROCEDURE_PARAM procedure = {0};
	struct proj_push_data proj_data = {0};
	uint32_t module_id = 0;

	proj_data.cmd_data = WS_CMD_LPRP;

	proj_data.progress = 0;
	proj_data.status = 0;
	scfw_execute_project_cb(&proj_data);
	if (0 == scfw_get_created_module_name_info(PROJ_CAMERA_MODULE_ID, &name_info))
	{
		if ((ret = scfw_delete_module_tree(0)) < 0)
		{
			LOGE("free_algo_project error:%d\n", ret);
			LEAVE(ret, exit);
		}
	}

	if (scfw_is_need_recreate_procedure())
	{
		LEAVE(-1, exit);
	}

	current_solution_name = scfw_get_cur_project_name();

	if ((NULL != current_solution_name) && (0 != strlen(current_solution_name))
			&& (0 != strncmp(DEFAULT_PROJ_NAME, current_solution_name, strlen(DEFAULT_PROJ_NAME))))
	{
		snprintf(solution_dir, sizeof(solution_dir), "%s%s", PROJ_DIR, current_solution_name);

		if ((ret = osal_remove_dir(solution_dir)) != 0)
		{
			LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
		}
	}

	if ((ret = osal_remove_dir(DEFAULT_PROJ_DIR)) != 0)
	{
		LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
	}

	if ((ret = osal_create_dir(DEFAULT_PROJ_DIR)) != 0)
	{
		LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
	}
	
	update_cur_project_name(DEFAULT_PROJ_NAME);
	
	proj_data.progress = 20;
	proj_data.status = 0;
	scfw_execute_project_cb(&proj_data);
	
	if (update_last_used_project_name(DEFAULT_PROJ_NAME) < 0)
	{
		LOGW("[%s]update_last_used_project_name error!\n", __func__);
	}
	
	proj_data.progress = 60;
	proj_data.status = 0;
	scfw_execute_project_cb(&proj_data);
	scfw_reset_project_running_interval();

	/* creat 0 image algo */
	ret = scfw_get_created_module_name_info(PROJ_CAMERA_MODULE_ID, &name_info);
	if (ret < 0)
	{
		ret = fwif_create_module(PROJ_IMAGE_ALGO_NAME, "", &module_id);
	}
	
	proj_data.progress = 80;
	proj_data.status = 0;
	scfw_execute_project_cb(&proj_data);
exit:
	
	if (0 == error)
	{
		proj_data.progress = 100;
		proj_data.status = 0;
		scfw_execute_project_cb(&proj_data);
	}
	else
	{
		proj_data.progress = 100;
		proj_data.status = -1;
		scfw_execute_project_cb(&proj_data);
	}
	
	return ret;
}

/**
  * @brief  delete a project in NVM
  * @param[in] proj_name   project name
  * @return  0 on success; < 0 on failure
  *根据方案名删除json配置文件，NVM的方案文件，当前运行方案
  *禁止删除，调用需注意
  */
int32_t scfw_delete_project(const char *project_name)
{
	int ret = 0;
	int error = 0;
	char file_path[MAX_FILE_NAME_LEN * 2] = {0};
	cJSON *proj_obj = NULL;
	cJSON *tmp_obj = NULL;

	if ((NULL == project_name) || (strlen(project_name) >= MAX_FILE_NAME_LEN))
	{
		LEAVE(-1, out);
	}

	if (NULL == algo_proj_mng_root)
	{
		if (load_algo_project_mng_from_file(&algo_proj_mng_root) < 0)
		{
			LEAVE(-2, out);
		}
	}

	if ((proj_obj = get_project_obj(algo_proj_mng_root, project_name)) != NULL)
	{
		if ((tmp_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_NUM)) == NULL)
		{
			LEAVE(-3, out);
		}

		if (tmp_obj->valuedouble > 1)
		{
			if ((ret = delete_project_obj(algo_proj_mng_root, project_name)) < 0)
			{
				LEAVE(-4, out);
			}

			tmp_obj->valuedouble -= 1;
			tmp_obj->valueint -= 1;

			/* write the json str into file */
			if (save_algo_project_mng_into_file(algo_proj_mng_root) < 0)
			{
				LEAVE(-5, out);
			}

			if (update_project_switch_mng() < 0)
			{
				LEAVE(-6, out);
			}
		}
		else
		{
			if (osal_remove_file(PROJ_MNG_FNAME) < 0)
			{
				LEAVE(-7, out);
			}
			cJSON_Delete(algo_proj_mng_root);
			algo_proj_mng_root = NULL;
		}
	}

	/* delete project.sln file */
	memset(file_path, '\0', sizeof(file_path));
	snprintf(file_path, sizeof(file_path), "%s%s.sln", DEVSLN_DIR, project_name);

	if ((ret = osal_remove_dir(file_path)) < 0)
	{
		LOGE("osal_remove_dir %s error\n", file_path);
		LEAVE(-8, out);
	}
	
	snprintf(file_path, sizeof(file_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, project_name);

	if ((ret = osal_remove_dir(file_path)) < 0)
	{
		LOGE("osal_remove_dir %s error\n", file_path);
		LEAVE(-9, out);
	}

	snprintf(file_path, sizeof(file_path), "%s%s", PROJ_DIR, project_name);
	if (osal_is_dir_exist(file_path))
	{
		if ((ret = osal_remove_dir(file_path)) < 0)
		{
			LOGE("osal_remove_dir %s error\n", file_path);
			LEAVE(-9, out);
		}
	}

out:
	return error;
}
/**
  * @brief  delete ALL project in NVM
  * @return  0 on success; < 0 on failure
  * 清空目录所有配置，NVM所有方案文件保留当前文件
  */
int32_t scfw_delete_all_projects(void)
{
	if (osal_remove_dir(DEVSLN_DIR))
	{
		LOGE("free_algo_project();from mng.c hjj\n");
		return -1;
	}

	if (osal_create_dir(DEVSLN_DIR))
	{
		LOGE("free_algo_project();from mng.c hjj\n");
		return -1;
	}

	return 0;
}

/**
  * @brief  load the lastest used project
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_load_last_used_project(void)
{
	int ret = 0;
	int error = 0;
	uint32_t time_out_cnt = 0;
	int32_t lock_state_http = -1;
	int32_t lock_state_ws = -1;
	int32_t lock_state_gvcp = -1;
	int32_t lock_state_gvmp = -1;
	char filename[MAX_FNAME_LEN + 1] = {0};
	cJSON *obj = NULL;
	cJSON *proj_array = NULL;
	cJSON *proj_obj = NULL;
	cJSON *tmp_obj = NULL;	
	struct param_info_web value_info_buf = {0};
    char value[MAX_ALGO_PARAM_VALUE_LEN] = {0};
	char *end_char = NULL;
	int trigger_mode = 0;
    int startup_runmode = 0;
    LOGI("LAST-USED-SLN LOADING\n");

	if ((lock_state_http = scfw_project_switch_trylock_proc(SWITCH_LOCK_HTTPD, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_HTTPD trylock error\n");
		LEAVE(-2, out);
	}

	if ((lock_state_ws = scfw_project_switch_trylock_proc(SWITCH_LOCK_WS, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_WS trylock error\n");
		LEAVE(-3, out);
	}
	
	if ((lock_state_gvmp = scfw_project_switch_trylock_proc(SWITCH_LOCK_GVMP, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_GVMP trylock error\n");
		LEAVE(-4, out);
	}
	
	if ((lock_state_gvcp = scfw_project_switch_trylock_proc(SWITCH_LOCK_GVCP, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_GVCP trylock error\n");
		LEAVE(-5, out);
	}
	
	if ((ret = osal_create_dir(DEFAULT_PROJ_DIR)) != 0)
	{
		LOGW("[%s]osal_create_dir default error:%d\n", __func__, ret);
	}

	if (NULL == algo_proj_mng_root)
	{
		scfw_sync_project_mng_with_sln_files();
		if ((ret = load_algo_project_mng_from_file(&algo_proj_mng_root)) < 0)
		{
			LOGE("[%s] load algo proj mng root error:%d\n", __func__, ret);
			if ((ret = scfw_load_default_project()) < 0)
			{
				LOGE("[%s] load default proj error:%d\n", __func__, ret);
				LEAVE(-1, out);
			}
			LEAVE(0, out);
		}
	}
	else
	{
		/* algo project already exist,
		 * do not support reload temporarily.
		 */
		LOGE("[%s] algo project already exist!\n", __func__);
		LEAVE(-2, out);
	}

	if (((obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_LAST_USED_PROJ)) == NULL)
			|| (NULL == obj->valuestring))
	{
		LOGE("[%s] node \"last_used_project\" not found in algo_proj_mng root !\n", __func__);
		LEAVE(-3, out);
	}

	if (0 == strncmp(obj->valuestring, DEFAULT_PROJ_NAME, MAX_FNAME_LEN))
	{
		scfw_load_default_project();
		LEAVE(0, out);
	}

	if ((proj_obj = get_project_obj(algo_proj_mng_root, obj->valuestring)) == NULL)
	{
		if ((tmp_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_NUM)) != NULL
				&& tmp_obj->valueint > 0)
		{
			if ((proj_obj = get_project_index(algo_proj_mng_root, 0)) == NULL)
			{
				LEAVE(-4, out);
			}
			else
			{
				if (((obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME)) == NULL)
						|| (NULL == obj->valuestring))
				{
					LOGE("[%s] node \"new last_used_project\" not found in algo_proj_mng root !\n", __func__);
					LEAVE(-5, out);
				}
			}
		}
		else
		{
			LEAVE(-6, out);
		}
	}

	if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)) == NULL)
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-8, out);
	}

	if ((ret = scfw_load_project(obj->valuestring, tmp_obj->valuestring, LAST_LOAD_PROJ,true,true)) < 0)
	{
		LOGE("[%s] load algo %s proj error:%d\n", __func__, obj->valuestring, ret);
		LEAVE(-9, out);
	}
	
out:
    if (update_project_switch_mng() < 0)
	{
		LEAVE(-10, out);
	}

	if (is_comm_global_dev())
	{
		if ((ret = scfw_create_global_module()) < 0)
		{
			LOGE("create global module error:%d\n", ret);
		}
	}

	comif_update_module_common_param();

#ifdef R316
	/* 316平台默认方案不连续运行，因为会导致产线工具异常 */
	if (strncmp(cur_project_name, DEFAULT_PROJ_NAME, MAX_FNAME_LEN))
	{
		if ((ret = scfw_set_module_pub_mode(PROJ_PUB)) < 0)
		{
			LOGE("scfw_set_module_pub_mode\n");
		}
		
		if ((ret = scfw_set_project_running_state(PROJ_RUN, 0, 0)) < 0)
		{
			LOGE("[%s] set_module_pub_mod manager error:%d\n", __func__, ret);
		}
		
		while (!VM_M_IsProcedureRunning(CREATE_PROCEDURE_ID))
		{
			time_out_cnt++;
		
			if (time_out_cnt >= 100)
			{
				break;
			}
		
			usleep(20 * 1000);
		}
	}
#else
    if (ret = comif_get_device_commparam_info("StartUpRunMode", value, sizeof(value)))
    {
        LOGE("get StartUpRunMode fialed, error:%d\n", ret);
    }
    else
    {
        startup_runmode = *(int*)value;
    }
    
    switch(startup_runmode)
    {
        case PR_MODE_CONTINUOUS:
        {
            if ((ret = scfw_set_module_pub_mode(PROJ_PUB)) < 0)
        	{
        		LOGE("scfw_set_module_pub_mode\n");
        	}

        	if ((ret = scfw_set_project_running_state(PROJ_RUN, 0, 0)) < 0)
        	{
        		LOGE("[%s] set_module_pub_mod manager error:%d\n", __func__, ret);
        	}

        	while (!VM_M_IsProcedureRunning(CREATE_PROCEDURE_ID))
        	{
        		time_out_cnt++;

        		if (time_out_cnt >= 100)
        		{
        			break;
        		}

        		usleep(20 * 1000);
        	}
			
			if ((ret = scfw_get_module_param_data(0, TRIGGER_MODE_PARAM, &value_info_buf)) < 0)
			{
				LOGE("get trigger mode is error: 0x%x\n", ret);
			}
			
			trigger_mode = strtol(value_info_buf.value, &end_char, 0);
			if (end_char && ('\0' != *end_char))
			{
				LOGE("get trigger mode is error\n");
			}
			if (trigger_mode)
			{
				set_trigger_status(TRIGGER_STATUS_READY);
			}
			else
			{
				set_trigger_status(TRIGGER_STATUS_BUSY);
			}
			set_run_status(SCHEME_RUN_RUN);
        }
        break;
        case PR_MODE_RUNONCE:
        {
        	if (ret = scfw_run_once())
        	{
        		LOGE("run once failed %d\r\n", ret);
        	}
        }
        break;
        case PR_MODE_STOP:
            LOGI("stop run\n");
        break;
        default:
        break;          
    }
	
#endif

	if (!lock_state_http)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_HTTPD) < 0)
		{
			LOGE("SWITCH_LOCK_HTTPD proc\r\n");
		}
	}
	if (!lock_state_ws)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_WS) < 0)
		{
			LOGE("SWITCH_LOCK_WS proc\r\n");
		}
	}
	if (!lock_state_gvcp)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_GVCP) < 0)
		{
			LOGE("SWITCH_LOCK_GVCP proc\r\n");
		}
	}
	if (!lock_state_gvmp)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_GVMP) < 0)
		{
			LOGE("SWITCH_LOCK_GVMP proc\r\n");
		}
	}
	proj_status = PR_STATUS_READY;
    LOGE("LAST-USED-SLN LOADED, error: %d\n", error);

	return error;
}

/**
  * @brief  upload project file buf from client
  * @param[in] proj_buf   project file buffer
  * @param[in] len        size of project file
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_upload_project(const char *proj_file_path)
{
	int error =  0;
	int ret = 0;
	uint32_t crc = 0;
	int32_t language = 0;
	IMG_PROC_ERROR_CODE_E language_err;
	struct project_info proj_info;				/**< 方案信息 */
	struct switch_info swth_info;				/**< 切换信息 */    
	char file_path[MAX_FILE_NAME_LEN * 4] = {0};
    SLNAR_T *s = NULL;
    SLNAR_DEV_INFO_T devinfo = {0};
    SLNAR_SLN_BASE_INFO_T baseinfo = {0};
    SLNAR_SLN_SW_INFO_T swinfo = {0};
    int tmp = MAX_SCHEME_NUM;
    char tmp_name[MAX_SOLUTION_NAME] = {0};
    if (proj_file_path == NULL)
    {
		LOGE("proj_file_path is null\n");
		LEAVE(-1, out);
    }

	if (FALSE == osal_is_dir_exist(DEVSLN_DIR)) /*目录不存在创建目录*/
	{
		LOGW("The save_path %s is not exist.Now creat path!!\n", DEVSLN_DIR);

		if ((ret = osal_create_dir(DEVSLN_DIR)) < 0)
		{
			LOGE("osal_create_dir %s error\n", DEVSLN_DIR);
		}
	}

	if (FALSE == osal_is_dir_exist(DEVSLN_BASE_IMG_DIR)) /*目录不存在创建目录*/
	{
		LOGW("The save_path %s is not exist.Now creat path!!\n", DEVSLN_BASE_IMG_DIR);

		if ((ret = osal_create_dir(DEVSLN_BASE_IMG_DIR)) < 0)
		{
			LOGE("osal_create_dir %s error\n", DEVSLN_BASE_IMG_DIR);
		}
	}

	PUSH_RATE_PROJ(WS_CMD_UPRP, 0, 30);
    
	language_err = appApiGetIntParam(DEV_LANGUAGE, &language);
	if (IMG_PROC_EC_SUCCESS != language_err)
	{
		LOGE("get language type error \n");
		LEAVE(-2, out);
	}

    {
        int r = SLNAR_EC_OK;     

        if ((s = slnar_read_new()) == NULL)
        {
            LOGE("slnar_read_new failed\n");
            LEAVE(-3, out);
        }
        slnar_set_archive_path(s, proj_file_path);
		slnar_set_header_hook_fn(s, scfw_slnar_header_hook_save_baseimage);
		            
        if ((r = slnar_read_header(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_read_header failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
            slnar_read_free(s);
			s = NULL;            
            LEAVE(-4, out);
        }
        slnar_get_dev_info(s, &devinfo);
        slnar_get_sln_baseinfo(s, &baseinfo);   
        slnar_get_sln_swinfo(s, &swinfo);
    }

    if (devinfo.devtype != get_dev_type_from_env())
    {
        LOGE("devtype mismatch, devtype:%#x, sln-devtype:%#x\n",
            get_dev_type_from_env(), devinfo.devtype);                
        LEAVE(-5, out);
    }

    if (devinfo.language != language)
    {
        LOGE("language mismatch, language:%d, sln-language:%d\n",
            language, devinfo.language);                
        LEAVE(-6, out);
    }

     if((ret = comif_check_sln_param_with_operate(SLN_OPERATE_UPLOAD, &tmp, tmp_name, baseinfo.name)) < 0)
     {
        LOGE("sln operate check failed, ret = %d\r\n", ret);
        LEAVE(ret, out);
     } 

	if (0 == strcmp(baseinfo.name, scfw_get_cur_project_name()))
	{
		LOGE("confilct_with_current_solution\n");        
        LEAVE(SC_EC_SLN_NAME_CONFILCT, out);
    }

    if (scfw_check_is_solution_file_exist(baseinfo.name))
    {
		LOGE("solution_exist\n");        
        LEAVE(SC_EC_SLN_EXIST, out);
    }

	PUSH_RATE_PROJ(WS_CMD_UPRP, 0, 50);

    slnar_read_free(s);
    s = NULL;

	snprintf(file_path, sizeof(file_path), "%s%s.sln", DEVSLN_DIR, baseinfo.name);

	if ((ret = osal_move(proj_file_path, file_path)) < 0)
	{
		LOGE("osal_move, ret:%d, src:%s, dst:%s \n", ret, proj_file_path, file_path);
		LEAVE(-10, out);
	}

    scfw_convert_project_base_info_r(&baseinfo, &proj_info);
    scfw_convert_project_switch_info_r(&swinfo, &swth_info);

	if (add_proj_to_proj_mng_json(&proj_info, &swth_info, DEFAULT_IMAGE_SOURCE, SAVE_IMAGE_SOURCE) < 0)
	{
		LEAVE(-11, out);
	}

	PUSH_RATE_PROJ(WS_CMD_UPRP, 0, 80);

	/* write the json str into file */
	if (save_algo_project_mng_into_file(algo_proj_mng_root) < 0)
	{
		LEAVE(-12, out);
	}

	if (update_project_switch_mng() < 0)
	{
		LEAVE(-13, out);
	}

	PUSH_RATE_PROJ(WS_CMD_UPRP, 0, 90);
out:
    if (s != NULL)
        slnar_read_free(s);

	if (0 == error)
	{
		PUSH_RATE_PROJ(WS_CMD_UPRP, 0, 100);
	}

	return error;
}

/**
  * @brief  upload the display data of according module from client.
  * @param[in] module_id  module id
  * @param[in] buf        display data buffer
  * @param[in] len        size of display data
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_upload_disp_data(uint32_t module_id, char *buf, uint32_t buf_size)
{

	int error = 0;

	if (NULL == buf)
	{
		return -1;
	}

	if ((error = VM_M_SetModuleDisplayFileData(module_id, buf, buf_size)) != 0)
	{
		LOGE("VM_M_GetModuleDisplayFileData error error = %d\n", error);
	}

	return error;
}

/**
  * @brief  dnload the display data of according module to client.
  * @param[in]  module_id  module id
  * @param[in]  buf        display data buffer
  * @param[out] len        size of display data
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_dnload_disp_data(uint32_t module_id, char *buf, uint32_t buf_size, uint32_t *out_len)
{
	int error = 0;

	if ((NULL == buf) || (NULL == out_len))
	{
		return -1;
	}

	if ((error = VM_M_GetModuleDisplayFileData(module_id, buf, buf_size, out_len)) != 0)
	{
		LOGE("VM_M_GetModuleDisplayFileData error error = %d\n", error);
	}

	return error;
}

/**
  * @brief  modify current projetc name.
  * @param[in] old_proj_name  old project name
  * @param[in] new_proj_name  new project name
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_modify_project_name(const char *old_proj_name, const char *new_proj_name)
{
	int32_t error = 0;
	int32_t ret = 0;
	cJSON *proj_obj = NULL;
	cJSON *tmp_obj = NULL;
	int8_t file_path[MAX_FNAME_LEN * 2] = {0};
	struct project_info sln_info = {0};

	if (NULL == old_proj_name || NULL == new_proj_name)
	{
		LEAVE(-1, out);
	}

	/* 修改的名称和原方案名一致时直接返回*/
	if (0 == strncmp(old_proj_name, new_proj_name, MAX_FNAME_LEN))
	{
		LEAVE(0, out);
	}

	if (strlen(new_proj_name) > MAX_FNAME_LEN)
	{
		LEAVE(-2, out);
	}

	if (NULL != (proj_obj = get_project_obj(algo_proj_mng_root, new_proj_name)))
	{
		LEAVE(-3, out);
	}

	if (NULL == (proj_obj = get_project_obj(algo_proj_mng_root, old_proj_name)))
	{
		LEAVE(-4, out);
	}

	strncpy(sln_info.name, new_proj_name, sizeof(sln_info.name));
	sln_info.name[sizeof(sln_info.name) - 1] = '\0';

	if (NULL == ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)))
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-5, out);
	}

	strncpy(sln_info.passwd, tmp_obj->valuestring, sizeof(sln_info.passwd));
	sln_info.passwd[sizeof(sln_info.passwd) - 1] = '\0';

	if (NULL == ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CRAT_TIME)))
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-6, out);
	}

	strncpy(sln_info.create_time, tmp_obj->valuestring, sizeof(sln_info.create_time));
	sln_info.create_time[sizeof(sln_info.create_time) - 1] = '\0';

	if (NULL == ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_INTER_MS))))
	{
		LEAVE(-7, out);
	}

	sln_info.interval_ms = tmp_obj->valueint;

	if (modify_emmc_sln_file_info(old_proj_name, &sln_info, NULL))
	{
		LEAVE(-8, out);
	}

out:

	return error;
}

/**
  * @brief  update the switch information of given project
  * @param[in] proj_name  project name
  * @param[in] swt_info   the buffer addr of project switch parameter information
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_set_project_switch_info(const char *proj_name, struct switch_info *swt_info)
{
	int32_t error = 0;
	int32_t ret = 0;
	cJSON *proj_obj = NULL;
	cJSON *tmp_obj = NULL;
	struct project_info sln_info = {0};

	if ((NULL == proj_name) || (NULL == swt_info))
	{
		LEAVE(-1, out);
	}

	/* modify .sln flie */
	if (modify_emmc_sln_switch_info(proj_name, swt_info) < 0)
	{
		LEAVE(-1, out);
	}

	if (NULL == (proj_obj = get_project_obj(algo_proj_mng_root, proj_name)))
	{
		LEAVE(-1, out);
	}

	strncpy(sln_info.name, proj_name, sizeof(sln_info.name));
	sln_info.name[sizeof(sln_info.name) - 1] = '\0';

	if (NULL == ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)))
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-2, out);
	}

	strncpy(sln_info.passwd, tmp_obj->valuestring, sizeof(sln_info.passwd));
	sln_info.passwd[sizeof(sln_info.passwd) - 1] = '\0';

	if (NULL == ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CRAT_TIME)))
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-3, out);
	}

	strncpy(sln_info.create_time, tmp_obj->valuestring, sizeof(sln_info.create_time));
	sln_info.create_time[sizeof(sln_info.create_time) - 1] = '\0';

	if (NULL == ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_INTER_MS))))
	{
		LEAVE(-4, out);
	}

	sln_info.interval_ms = tmp_obj->valueint;

	if (NULL == ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_IO_INPUT))))
	{
		LEAVE(-5, out);
	}

	swt_info->digital_io.io_input = tmp_obj->valueint;

	/* web set switch info ,set cur algo image source to json*/
	if (0 == strlen(cur_algo_image_source))
	{
		strncpy(cur_algo_image_source, DEFAULT_IMAGE_SOURCE, sizeof(cur_algo_image_source));
		cur_algo_image_source[sizeof(cur_algo_image_source) - 1] = '\0';
	}
	
	/* change project json */
	if ((ret = add_proj_to_proj_mng_json(&sln_info, swt_info, cur_algo_image_source, UNSAVE_IMAGE_SOURCE)) < 0)
	{
		LOGE("[%s] add_proj_to_proj_mng_json:%d\n", __func__, ret);
		LEAVE(-6, out);
	}

	/* write the json str into file */
	if ((ret = save_algo_project_mng_into_file(algo_proj_mng_root)) < 0)
	{
		LOGE("[%s] save_algo_project_mng_into_file:%d\n", __func__, ret);
		LEAVE(-7, out);
	}

	/* update switch mng */
	if (update_project_switch_mng() < 0)
	{
		LEAVE(-8, out);
	}

out:
	return (error);
}

/**
  * @brief  get the switch information of given project
  * @param[in]  proj_name  project name
  * @param[out] swt_info   the buffer addr of project switch parameter information
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_get_project_switch_info(const char *proj_name, struct switch_info *swt_info)
{
	int32_t i = 0;

	if (NULL == proj_name || NULL == swt_info)
	{
		return -1;
	}

	for (i = 0; i < proj_switch_mng.project_num; i++)
	{
		if (0 == strncmp(proj_switch_mng.project_name[i], proj_name, MAX_FNAME_LEN))
		{
			break;
		}
	}

	if (proj_switch_mng.project_num == i)
	{
		return -2;
	}

	memcpy(swt_info, &proj_switch_mng.swth_info[i], sizeof(struct switch_info));
	return 0;
}

int32_t scfw_get_project_switch_info_ex(const char *proj_name, struct switch_info *swt_info)
{        
	int32_t trig = 0;
	char trig_param[FW_PARAM_VALUE_LEN] = {0};    
	int sel = 0;    
	IMG_PROC_ERROR_CODE_E line_err = IMG_PROC_EC_SUCCESS;    
	int32_t line_mode = 0;
    int ret;

	if (NULL == proj_name || NULL == swt_info)
		return -1;

	ret = scfw_get_project_switch_info(proj_name, swt_info);
	if ((ret < 0) || (NULL == algo_proj_mng_root))
	{
		snprintf(swt_info->communication.cm_str, MAX_SWITCH_STR_LEN, "switch");
		snprintf(swt_info->communication.cm_ret, MAX_SWITCH_STR_LEN, "ok");
		snprintf(swt_info->communication.cm_fail, MAX_SWITCH_STR_LEN, "fail");
		swt_info->digital_io.active = DI_HIGH;
		swt_info->digital_io.time = 1000;
		swt_info->communication.cm_enable = FALSE;
		swt_info->digital_io.di_enable = FALSE;
		swt_info->digital_io.camera_trigger_source = FALSE;
		swt_info->digital_io.camera_trigger_mode = FALSE;
		strncpy(swt_info->communication.trigger_cm_str, "", MAX_TRIGGER_STR_LEN);
		strncpy(swt_info->digital_io.line_cfg, "********", get_cfg_line_num());
		swt_info->digital_io.line_cfg[MAX_DI_LINE_NUM-1] = '\0';

		/* 保存切换源信息 */
		ret = vm_get_module_param(0, "TriggerSource", trig_param);
		if (0 != ret)
		{
			LOGE("vm get camera trigger mode is is %s\n", trig_param);
			return -2;
		}

		trig = atoi(trig_param);
		if (trig < 0)
		{
			LOGE("camera trigger mode is %d %s\n", trig, trig_param);
			return -3;
		}
		swt_info->digital_io.source = !trig;
	}

	ret = vm_get_module_param(0, "TriggerSource", trig_param);
	trig = atoi(trig_param);
	if (trig < 0)
	{
		LOGE("camera trigger mode is %d %s\n", trig, trig_param);
        return -4;
	}
	swt_info->digital_io.camera_trigger_source = trig;

	ret = vm_get_module_param(0, "TriggerMode", trig_param);
	trig = atoi(trig_param);
	if (trig < 0)
	{
		LOGE("camera trigger mode is %d %s\n", trig, trig_param);
        return -5;
	}

	swt_info->digital_io.camera_trigger_mode = trig;

	sel = LINE_ALL;
	line_err = appApiGet2DimParam(LINE_MODE, &sel, &line_mode);
	if (IMG_PROC_EC_SUCCESS != line_err)
	{
		LOGE("get line output type error\n");
        return -6;
	}
	
	LOGI("line_mode = %d\n", line_mode);

	swt_info->digital_io.io_input = line_mode;
	ret = vm_get_module_param(0, "CommunicationString", trig_param);
	if (ret < 0)
	{
		LOGE("camera trigger mode is %d %s\n", trig, trig_param);
        return -7;
	}

	strncpy(swt_info->communication.trigger_cm_str,trig_param,MAX_TRIGGER_STR_LEN);

    return 0;
}

static void scfw_convert_project_switch_info(const struct switch_info *from, SLNAR_SLN_SW_INFO_T *to)
{
    {
        to->comm.enable = from->communication.cm_enable;
        snprintf(to->comm.failstr, sizeof to->comm.failstr,
            "%s", from->communication.cm_fail);        
        snprintf(to->comm.retstr, sizeof to->comm.retstr,
            "%s", from->communication.cm_ret);        
        snprintf(to->comm.switchstr, sizeof to->comm.switchstr,
            "%s", from->communication.cm_str);
		snprintf(to->comm.triggercommstr, sizeof to->comm.triggercommstr,
            "%s", from->communication.trigger_cm_str);
		snprintf(to->comm.switchsuffixstr, sizeof to->comm.switchsuffixstr,
            "%s", from->communication.cm_suffix_str);
    }
	{
        to->di.enable = from->digital_io.di_enable;
        to->di.active = from->digital_io.active;
        to->di.input = from->digital_io.io_input;
        snprintf(to->di.lines, sizeof to->di.lines,
            "%s", from->digital_io.line_cfg);
        to->di.source = from->digital_io.source;
        to->di.time = from->digital_io.time;
        to->di.trigmode = from->digital_io.camera_trigger_mode;
        to->di.trigsrc = from->digital_io.camera_trigger_source;        
	}

    to->mode = from->mode;
}

static void scfw_convert_project_switch_info_r(const SLNAR_SLN_SW_INFO_T *from, struct switch_info *to)
{
    {
        to->communication.cm_enable = from->comm.enable;
        snprintf(to->communication.cm_fail, sizeof to->communication.cm_fail,
            "%s", from->comm.failstr);        
        snprintf(to->communication.cm_ret, sizeof to->communication.cm_ret,
            "%s", from->comm.retstr);        
        snprintf(to->communication.cm_str, sizeof to->communication.cm_str,
            "%s", from->comm.switchstr);
        snprintf(to->communication.trigger_cm_str, sizeof to->communication.trigger_cm_str,
            "%s", from->comm.triggercommstr);
        snprintf(to->communication.cm_suffix_str, sizeof to->communication.cm_suffix_str,
            "%s", from->comm.switchsuffixstr);
    }
	{
        to->digital_io.di_enable = from->di.enable;
        to->digital_io.active = from->di.active;
        to->digital_io.io_input = from->di.input;
        snprintf(to->digital_io.line_cfg, sizeof to->digital_io.line_cfg,
            "%s", from->di.lines);
        to->digital_io.source = from->di.source;
        to->digital_io.time = from->di.time;
        to->digital_io.camera_trigger_mode = from->di.trigmode;
        to->digital_io.camera_trigger_source = from->di.trigsrc;
	}

    to->mode = from->mode;
}

static void scfw_convert_project_base_info(const struct project_info *from, SLNAR_SLN_BASE_INFO_T *to)
{
    snprintf(to->name, sizeof to->name, "%s", from->name);
    snprintf(to->passwd, sizeof to->passwd, "%s", from->passwd);
    snprintf(to->ctime, sizeof to->ctime, "%s", from->create_time);    
    to->interval = from->interval_ms;
}

static void scfw_convert_project_base_info_r(const SLNAR_SLN_BASE_INFO_T *from, struct project_info *to)
{
    snprintf(to->name, sizeof to->name, "%s", from->name);
    snprintf(to->passwd, sizeof to->passwd, "%s", from->passwd);
    snprintf(to->create_time, sizeof to->create_time, "%s", from->ctime);    
    snprintf(to->image_source, sizeof to->image_source, "%s", DEFAULT_IMAGE_SOURCE);
    to->interval_ms = from->interval;
}


/**
  * @brief  get total project num in NVM.
  * @param[out] proj_num   project num buffer addr
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_get_project_num(uint32_t *proj_num)
{
	int error = 0;
	int ret = 0;
	cJSON *ele_obj = NULL;

	if (NULL == proj_num)
	{
		LEAVE(-1, out);
	}

	if (NULL == algo_proj_mng_root)
	{
		if (load_algo_project_mng_from_file(&algo_proj_mng_root) < 0)
		{
			LEAVE(-2, out);
		}
	}

	if ((ele_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_NUM)) == NULL)
	{
		LEAVE(-3, out);
	}

	*proj_num = ele_obj->valueint;
	ret = update_project_switch_mng();

	if (ret < 0)
	{
		LOGE("update project switch man error\n");
	}

out:

	if (error < 0)
	{
		if (proj_num)
		{
			*proj_num = 0;
		}
	}

	return error;
}

bool scfw_check_is_solution_file_exist(const char *name)
{
	uint32_t sln_num = 0;
    struct project_info proj_info;

    if (name == NULL)
	{
		return true;
	}

    if (!osal_is_file_exist(PROJ_MNG_FNAME))
	{
		return false;
	}

	if (scfw_get_project_num(&sln_num) < 0)
	{
		return true;
	}

	for (int i = 0; i < sln_num; i++)
	{
		if (scfw_get_project_info(i, &proj_info) < 0)
		{
			return true;
		}
		if (0 == strcmp(proj_info.name, name))
		{
			return true;
		}
	}

	return false;
}

/**
  * @brief  get the project information of given project index.
  * @param[in]  proj_idx    project index in project management json file
  * @param[out] proj_info   project information buffer addr
  * @return  0 on success; < 0 on failure
  */
int32_t scfw_get_project_info(int proj_idx, struct project_info *proj_info)
{
	int error = 0;
	cJSON *num_obj = NULL;
	cJSON *list_obj = NULL;
	cJSON *proj_obj = NULL;
	cJSON *tmp_obj = NULL;

	if (NULL == proj_info)
	{
		LEAVE(-1, out);
	}

	if (NULL == algo_proj_mng_root)
	{
		if (load_algo_project_mng_from_file(&algo_proj_mng_root) < 0)
		{
			LEAVE(-2, out);
		}
	}

	if ((num_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_NUM)) == NULL)
	{
		LEAVE(-3, out);
	}

	if ((list_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_LIST)) == NULL)
	{
		LEAVE(-4, out);
	}

	if (proj_idx >= num_obj->valueint)
	{
		LEAVE(-5, out);
	}

	if ((proj_obj = cJSON_GetArrayItem(list_obj, proj_idx)) == NULL)
	{
		LEAVE(-6, out);
	}

	if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME)) == NULL)
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-7, out);
	}

	strncpy(proj_info->name, tmp_obj->valuestring, sizeof(proj_info->name));
	proj_info->name[sizeof(proj_info->name) - 1] = '\0';

	if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)) == NULL)
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-8, out);
	}

	strncpy(proj_info->passwd, tmp_obj->valuestring, sizeof(proj_info->passwd));
	proj_info->passwd[sizeof(proj_info->passwd) - 1] = '\0';

	if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CRAT_TIME)) == NULL)
			|| (NULL == tmp_obj->valuestring))
	{
		LEAVE(-9, out);
	}

	strncpy(proj_info->create_time, tmp_obj->valuestring, sizeof(proj_info->create_time));
	proj_info->create_time[sizeof(proj_info->create_time) - 1] = '\0';

	if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_INTER_MS)) == NULL)
	{
		LEAVE(-10, out);
	}

	proj_info->interval_ms = tmp_obj->valueint;

out:
	return error;
}

/**
 * @brief      修改emmc 中方案的切换信息
 * @param[in]  project_name 被修改内容的方案名
 * @param[in]  swth_info 替换的切换信息(可以为NULL)
 * @return       0-执行成功，负数-执行失败
 */
int32_t modify_emmc_sln_switch_info(const char *proj_name, const struct switch_info *swth_info)
{
	/*
	 修改方案名逻辑:
	 1. 先把旧的方案信息全部读出
	 2. 更新头里面的方案头信息的切换信息
	 3. 重新生成crc
	 4. 写入方案
	*/
	int32_t error = 0;
	int32_t fsize = 0;
	IMG_PROC_ERROR_CODE_E err;
	int32_t language = 0;
	char file_path[MAX_FNAME_LEN * 2] = {0};
    SLNAR_DEV_INFO_T devinfo = {0};
    SLNAR_SLN_SW_INFO_T swinfo = {0};
	
	if ((NULL == proj_name) || (NULL == swth_info))
	{
		LEAVE(-1, out);
	}

	/* 检查方案是否存在osal */
	snprintf(file_path, sizeof(file_path), "%s%s.sln", DEVSLN_DIR, proj_name);

	if (!osal_is_file_exist(file_path))
	{
		LOGE("%s is not exist\n", file_path);
		LEAVE(-2, out);
	}

    {
        SLNAR_T *s = NULL;
        int r = SLNAR_EC_OK; 

        if ((s = slnar_read_new()) == NULL)
        {
            LOGE("slnar_read_new failed\n");
            LEAVE(-3, out);
        }
        slnar_set_archive_path(s, file_path);              
        if ((r = slnar_read_header(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_read_header failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
            slnar_read_free(s);            
            LEAVE(-4, out);
        }

        slnar_get_dev_info(s, &devinfo);
        slnar_read_free(s);
    }

    if (devinfo.devtype != get_dev_type_from_env())
    {
        LOGE("devtype mismatch, devtype:%#x, sln-devtype:%#x\n",
            get_dev_type_from_env(), devinfo.devtype);                
        LEAVE(-5, out);
    }

	err = appApiGetIntParam(DEV_LANGUAGE, &language);
	if (IMG_PROC_EC_SUCCESS != err)
	{
		LOGE("get language type error \n");
		LEAVE(-6, out);
	}

	if (devinfo.language != language)
	{
        LOGE("language mismatch, language:%d, sln-language:%d\n",
            language, devinfo.language);                
		LEAVE(-7, out);
	}

    scfw_convert_project_switch_info(swth_info, &swinfo);

    {
        SLNAR_T *s = NULL;
        int r = SLNAR_EC_OK;

        if ((s = slnar_modify_new()) == NULL)
        {
            LOGE("slnar_modify_new failed\n");
            LEAVE(-8, out);
        }
        slnar_set_archive_path(s, file_path);        
        slnar_modify_set_flags(s, SLNAR_MODIFIER_FLAG_SLN_SW_INFO);
        slnar_set_sln_swinfo(s, &swinfo);
        if ((r = slnar_modify_header(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_modify_header failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
            slnar_modify_free(s);            
            LEAVE(-9, out);
        }

        slnar_modify_free(s);
    }

out:
	
	return error;
}

/**
 * @brief      修改emmc 中方案文件信息
 * @param[in]  project_name 被修改内容的方案名
 * @param[in]  proj_info 替换的方案信息
 * @param[in]  swth_info 替换的切换信息(可以为NULL)
 * @return       0-执行成功，负数-执行失败
 */
int32_t modify_emmc_sln_file_info(const char *proj_name, const struct project_info *proj_info,
								  const struct switch_info *swth_info)
{
	/*
	修改方案名逻辑:
	1. 先把旧的方案文件读出
	2. 修改旧的方案切换信息与方案信息
	3. 判断名称是否相同
	4. 把方案内容写入新的方案文件
	*/
	int32_t error = 0;
	int32_t ret = 0;
	char src_path[MAX_FNAME_LEN * 2] = {0};
	char dst_path[MAX_FNAME_LEN * 2] = {0};
	char need_delete = 0;    
    SLNAR_SLN_BASE_INFO_T baseinfo = {0};
    SLNAR_SLN_SW_INFO_T swinfo = {0};
	int base_image_exist = -1;

	PUSH_RATE_PROJ(WS_CMD_EDRP, PUSH_OK, 20);

	if ((NULL == proj_name) || (NULL == proj_info))
	{
		LEAVE(-1, out);
	}

	LOGI("[%s] oldname:%s newname:%s\n", __func__, proj_name, proj_info->name);

	/*方案名一致时不用更新方案管理信息与压缩文件目录信息，不一致更新信息重新创建一个方案文件*/
	if (0 == strncmp(proj_name, proj_info->name, MAX_FNAME_LEN))
	{
		/*说明传输数据有误，名称与传入切换信息一致，认为不需要修改*/
		LEAVE(0, out);
	}

	/*检查方案是否存在osal */
	snprintf(src_path, sizeof(src_path), "%s%s.sln", DEVSLN_DIR, proj_name);

	if (!osal_is_file_exist(src_path))
	{
		LOGE("%s is not exist\n", src_path);
		LEAVE(-2, out);
	}

	snprintf(dst_path, sizeof(dst_path), "%s%s.sln", DEVSLN_DIR, proj_info->name);

    if (osal_copy(src_path, dst_path) < 0)
    {
		LOGE("osal_copy failed, src:%s, dst:%s\n", src_path, dst_path);
		LEAVE(-3, out);
    }

	PUSH_RATE_PROJ(WS_CMD_EDRP, PUSH_OK, 30);
     
    scfw_convert_project_base_info(proj_info, &baseinfo); 
    if (swth_info != NULL) {        
        scfw_convert_project_switch_info(swth_info, &swinfo);
    }

	{
        SLNAR_T *s = NULL;
        uint32_t flags = SLNAR_MODIFIER_FLAG_SLN_BASE_INFO;
        int r = SLNAR_EC_OK;

        if ((s = slnar_modify_new()) == NULL)
        {
            LOGE("slnar_modify_new failed\n");
            LEAVE(-7, out);
        }
        slnar_set_archive_path(s, dst_path);
        if (swth_info != NULL)
            flags |= SLNAR_MODIFIER_FLAG_SLN_SW_INFO;
        slnar_modify_set_flags(s, flags);
        slnar_set_sln_baseinfo(s, &baseinfo);
		slnar_set_header_hook_fn(s, scfw_slnar_header_hook);
        if (swth_info != NULL)
            slnar_set_sln_swinfo(s, &swinfo);
        if ((r = slnar_modify_header(s)) != SLNAR_EC_OK)
        {
            LOGE("slnar_modify_header failed, errno:%d, %s\n",
                slnar_errno(s), slnar_errstr(s));
            slnar_modify_free(s);            
            LEAVE(-8, out);
        }

        slnar_modify_free(s);
    }

	PUSH_RATE_PROJ(WS_CMD_EDRP, PUSH_OK, 70);

	/*如果是当前运行的方案*/
	if (0 == strncmp(proj_name, scfw_get_cur_project_name(), MAX_FNAME_LEN))
	{
		snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, proj_name);
		snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, proj_info->name);

		if ((ret = osal_copy(src_path, dst_path)) < 0)
		{
			LOGE("osal_remove_file %s to %s error\n", src_path, dst_path);
			LEAVE(-9, out);
		}
		
		snprintf(src_path, sizeof(src_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, proj_name);
		snprintf(dst_path, sizeof(dst_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, proj_info->name);

		if (!osal_is_file_exist(src_path))
		{
			base_image_exist = 0;
		}
		if (base_image_exist && (ret = osal_copy(src_path, dst_path)) < 0)
		{
			LOGE("osal_remove_file %s to %s error\n", src_path, dst_path);
			LEAVE(-10, out);
		}
	}

	PUSH_RATE_PROJ(WS_CMD_EDRP, PUSH_OK, 80);

	update_cur_project_name(proj_info->name);

	update_pwd();
	update_procedure_name();

	if (modify_algo_project_info(proj_name, proj_info, swth_info))
	{
		LEAVE(-11, out);
	}

	if (update_project_switch_mng() < 0)
	{
		LEAVE(-12, out);
	}

	if ((ret = update_last_used_project_name(proj_info->name)) < 0)
	{
		LOGE("update_last_used_project_name ret = %d\n", ret);
	}
	PUSH_RATE_PROJ(WS_CMD_EDRP, PUSH_OK, 90);

	need_delete = 1;
out:	
	/*成功! 删除原来的方案文件*/
	if (need_delete)
	{
		snprintf(src_path, sizeof(src_path), "%s%s.sln", DEVSLN_DIR, proj_name);	
		if ((ret = osal_remove_file(src_path)) < 0)
		{
			LOGW("osal_remove_file fail:%s error:%d\n", src_path, ret);
		}

		snprintf(src_path, sizeof(src_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, proj_name);
		if ((ret = osal_remove_file(src_path)) < 0)
		{
			LOGW("osal_remove_file fail:%s error:%d\n", src_path, ret);
		}

		snprintf(src_path, sizeof(src_path), "%s%s", PROJ_DIR, proj_name);
		if ((ret = osal_remove_dir(src_path)) < 0)
		{
			LOGW("osal_remove_dir fail:%s error:%d\n", src_path, ret);
		}
	}

	/*现场恢复逻辑 暴力恢复 将所有可能生成的文件删除一遍，最后对移动当前文件目录坐修改*/
	if (error < 0)
	{
		snprintf(dst_path, sizeof(dst_path), "%s%s.sln", DEVSLN_DIR, proj_info->name);
		if ((ret = osal_remove_file(dst_path)) < 0)
		{
			LOGW("osal_remove_file fail:%s error:%d\n", dst_path, ret);
		}

		snprintf(dst_path, sizeof(dst_path), "%s%s.jpg", DEVSLN_BASE_IMG_DIR, proj_info->name);
		if ((ret = osal_remove_file(dst_path)) < 0)
		{
			LOGW("osal_remove_file fail:%s error:%d\n", dst_path, ret);
		}

		snprintf(dst_path, sizeof(dst_path), "%s%s", PROJ_DIR, proj_info->name);
		if ((ret = osal_remove_dir(dst_path)) < 0)
		{
			LOGW("osal_remove_dir fail:%s error:%d\n", dst_path, ret);
		}
	}

	if (0 == error)
	{
		PUSH_RATE_PROJ(WS_CMD_EDRP, PUSH_OK, 100);
	}
	/* 这里如果error不为0，则不推送失败的进度(status为-1)，否则外面还有逻辑要处理的话
	就会导致设置了进度失败，但是错误码还没有及时设置(还是0)，导致客户端显示未知错误。*/
	return error;
}
/**
 * @brief      修改emmc 中切换管理json文件信息
 * @param[in]  project_name 被修改内容的方案名
 * @param[in]  proj_info 替换的方案信息
 * @param[in]  swth_info 替换的切换信息(可以为NULL)
 * @return       0-执行成功，负数-执行失败
 */
int modify_algo_project_info(const char *proj_name,  const struct project_info *proj_info,
							 const struct switch_info *swth_info)
{
	int ret = 0;
	int error = 0;
	cJSON *proj_obj = NULL;
	cJSON *proj_array = NULL;
	cJSON *tmp_obj = NULL;
	int interval = 0;

	if ((NULL == proj_name) || (NULL == proj_info))
	{
		LEAVE(-1, out);
	}

	/* load json root */
	if (NULL == algo_proj_mng_root)
	{
		ret = load_algo_project_mng_from_file(&algo_proj_mng_root);

		if (ret < 0)
		{
			LEAVE(-2, out);
		}
	}

	/* replace the valuestring of node "passwd" if chaneged */
	if (((proj_obj = cJSON_GetObjectItem(algo_proj_mng_root, JSON_PROJECT_LAST_USED_PROJ)) == NULL)
			|| (NULL == proj_obj->valuestring))
	{
		LEAVE(-3, out);
	}

	if (0 == strcmp(proj_obj->valuestring, proj_name))
	{
		cJSON_ReplaceItemInObject(algo_proj_mng_root, JSON_PROJECT_LAST_USED_PROJ, cJSON_CreateString((char *)proj_info->name));
	}

	/* is the project already exist ? */
	if ((proj_obj = get_project_obj(algo_proj_mng_root, proj_name)) == NULL)
	{
		LEAVE(-4, out);
	}
	else
	{
		/* replace the valuestring of node "passwd" if chaneged */
		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_NAME)) == NULL)
				|| (NULL == tmp_obj->valuestring))
		{
			LEAVE(-5, out);
		}

		cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_NAME, cJSON_CreateString((char *)proj_info->name));

		/* replace the valuestring of node "passwd" if chaneged */
		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_PASSWD)) == NULL)
				|| (NULL == tmp_obj->valuestring))
		{
			LEAVE(-6, out);
		}

		cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_PASSWD, cJSON_CreateString((char *)proj_info->passwd));

		/* replace the valuestring of node "create_time" if chaneged */
		if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CRAT_TIME)) == NULL)
				|| (NULL == tmp_obj->valuestring))
		{
			LEAVE(-7, out);
		}

		cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_CRAT_TIME, cJSON_CreateString((char *)proj_info->create_time));

		/* replace the valuestring of node "interval_ms" if chaneged */
		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_INTER_MS)) == NULL)
		{
			LEAVE(-8, out);
		}

		tmp_obj->valuedouble = interval;
		tmp_obj->valueint = interval;

		if (NULL != swth_info)
		{
			/* replace the valuestring of node "switch_str" if chaneged */
			if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_STR)) == NULL)
					|| (NULL == tmp_obj->valuestring))
			{
				LEAVE(-9, out);
			}

			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_SWITCH_STR,
									  cJSON_CreateString((char *)swth_info->communication.cm_str));

			/* replace the valuestring of node "switch_ret" if chaneged */
			if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_RET)) == NULL)
					|| (NULL == tmp_obj->valuestring))
			{
				LEAVE(-10, out);
			}

			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_SWITCH_RET,
									  cJSON_CreateString((char *)swth_info->communication.cm_ret));

			/* replace the valuestring of node "switch_fail" if chaneged */
			if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_FAIL)) == NULL)
					|| (NULL == tmp_obj->valuestring))
			{
				LEAVE(-11, out);
			}

			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_SWITCH_FAIL,
									  cJSON_CreateString((char *)swth_info->communication.cm_fail));

			/* replace the valuestring of node "switch_source" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_SOURCE)) == NULL)
			{
				LEAVE(-12, out);
			}

			tmp_obj->valuedouble = swth_info->digital_io.source;
			tmp_obj->valueint = swth_info->digital_io.source;

			/* replace the valuestring of node "switch_active" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_SWITCH_ACTIVE)) == NULL)
			{
				LEAVE(-13, out);
			}

			tmp_obj->valuedouble = swth_info->digital_io.active;
			tmp_obj->valueint = swth_info->digital_io.active;

			/* replace the valuestring of node "false_time" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_FALSE_TIME)) == NULL)
			{
				LEAVE(-14, out);
			}

			tmp_obj->valuedouble = swth_info->digital_io.time;
			tmp_obj->valueint = swth_info->digital_io.time;

			/* replace the valuestring of node "io_input" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_IO_INPUT)) == NULL)
			{
				LEAVE(-15, out);
			}

			tmp_obj->valuedouble = swth_info->digital_io.io_input;
			tmp_obj->valueint = swth_info->digital_io.io_input;

			/* replace the valuestring of node "cm_enable" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CM_ENABLE)) == NULL)
			{
				LEAVE(-16, out);
			}

			tmp_obj->valuedouble = swth_info->communication.cm_enable;
			tmp_obj->valueint = swth_info->communication.cm_enable;

			/* replace the valuestring of node "di_enable" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_DI_ENABLE)) == NULL)
			{
				LEAVE(-17, out);
			}

			tmp_obj->valuedouble = swth_info->digital_io.di_enable;
			tmp_obj->valueint = swth_info->digital_io.di_enable;

			/* replace the valuestring of node "di_enable" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CARM_TRI_SOURCE)) == NULL)
			{
				LEAVE(-18, out);
			}

			tmp_obj->valuedouble = swth_info->digital_io.camera_trigger_source;
			tmp_obj->valueint = swth_info->digital_io.camera_trigger_source;

			/* replace the valuestring of node "di_enable" if chaneged */
			if ((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_CARM_TRI_MODE)) == NULL)
			{
				LEAVE(-19, out);
			}

			tmp_obj->valuedouble = swth_info->digital_io.camera_trigger_mode;
			tmp_obj->valueint = swth_info->digital_io.camera_trigger_mode;

			/* replace the valuestring of node "line_cfg" if chaneged */
			if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_DI_LINE_CFG)) == NULL)
					|| (NULL == tmp_obj->valuestring))
			{
				LEAVE(-20, out);
			}

			cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_DI_LINE_CFG,
									  cJSON_CreateString((char *)swth_info->digital_io.line_cfg));


			/* replace the valuestring of node "camera_trigger_comm" if chaneged */
			if (((tmp_obj = cJSON_GetObjectItem(proj_obj, JSON_PROJECT_TRI_COMM)) == NULL)
					|| (NULL == tmp_obj->valuestring))
			{
				/*老版本升级上来没有这个字段，要主动加上*/
				cJSON_AddItemToObject(proj_obj,
					JSON_PROJECT_TRI_COMM,
					cJSON_CreateString(swth_info->communication.trigger_cm_str));	
			}
			else
			{
				cJSON_ReplaceItemInObject(proj_obj, JSON_PROJECT_TRI_COMM,
									  cJSON_CreateString((char *)swth_info->communication.trigger_cm_str));
			}


		}

		/* write the json str into file */
		if (save_algo_project_mng_into_file(algo_proj_mng_root) < 0)
		{
			LEAVE(-20, out);
		}
	}

out:
	return (error);
}
/**
 * @brief   switch project
 * @param[in]  project_name
 * @param[in]  restore_type 1:出错不还原  0:出错还原
 * @param[in]  load_type    1:user        0:DI/communication
 * @return       0-执行成功，负数-执行失败
 */
int32_t scfw_switch_project(const char *proj_name, uint32_t restore_type, uint32_t load_type)
{
	int32_t ret = 0;
	int32_t error = 0;
	int32_t project_num = 0;
	int32_t i = 0;
	int32_t lock_state_http = -1;
	int32_t lock_state_ws = -1;
	int32_t lock_state_gvcp = -1;
	int32_t lock_state_gvmp = -1;
	char tmp_project_name[MAX_FNAME_LEN + 1] = {0};
	struct module_name_info module_info;
	struct project_info proj_info = {0};

	// 切换方案的时候先停止emmc监测的事件推送
	emmc_stop_run_all_mod();

	if (NULL == proj_name)
	{
		LEAVE(-1, out);
	}
	
	if ((lock_state_http = scfw_project_switch_trylock_proc(SWITCH_LOCK_HTTPD, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_HTTPD trylock error\n");
		LEAVE(-2, out);
	}

	if ((lock_state_ws = scfw_project_switch_trylock_proc(SWITCH_LOCK_WS, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_WS trylock error\n");
		LEAVE(-3, out);
	}
	
	if ((lock_state_gvmp = scfw_project_switch_trylock_proc(SWITCH_LOCK_GVMP, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_GVMP trylock error\n");
		LEAVE(-4, out);
	}
	
	if ((lock_state_gvcp = scfw_project_switch_trylock_proc(SWITCH_LOCK_GVCP, MAX_LOCK_WAIT_TIME)) < 0)
	{
		LOGE("SWITCH_LOCK_GVCP trylock error\n");
		LEAVE(-5, out);
	}

	ret = scfw_get_project_num(&project_num);

	if (ret < 0)
	{
		LOGE("get_algo_project_num ret = %d\r\n", ret);
		LEAVE(-7, out);
	}

	for (i = 0; i < project_num; i++)
	{
		ret = scfw_get_project_info(i, &proj_info);
		if (ret < 0)
		{
			LOGE("get_algo_project_info ret = %d\r\n", ret);
			continue;
		}

		if (0 == strcmp(proj_name, proj_info.name)) /*方案列表中有该方案时加载该方案*/
		{
            /* 先记录切换之前的方案，目的是在切换新方案失败时候可以恢复*/
            snprintf(tmp_project_name, sizeof(tmp_project_name), cur_project_name);

			set_led_status(LED_TYPE_OK_NG, LED_COLOR_YELLOW);

			if (scfw_wait_until_module_stop() < 0)
			{
				LOGE("wait_until_algo_stop timeout\r\n");
				LEAVE(-6, out);
			}

			if (scfw_wait_testmode_stop(MAX_LOCK_WAIT_TIME) < 0)
			{
				LOGE("scfw_wait_testmode_stop timeout\r\n");
				LEAVE(-6, out);
			}
	
			scfw_free_cur_project(0);
			/* 加载目标方案*/
		    /* 这里load失败后，不设置状态为-1，不然这里就设置了，错误码设置的太晚
				客户端来拿的时候就拿不到错误码了
			*/
			ret = scfw_load_project(proj_name, "123456", load_type,true,false);
			
			if (restore_type)
			{
				LEAVE(ret, out);
			}

			if (0 == ret)
			{
				//设置参数到io模块来输出切换方案成功的信号
				int32_t id = -1;
				int32_t nErrorCode = 0;
				if((id = fwif_get_mod_id_of_dup_name("iomodule")) != -1)
				{
					nErrorCode = scfw_set_module_param_value(id, "SwitchPrjStatus", "1");
					if (nErrorCode < 0)
					{
						LOGW("scfw_set_module_param_value failed, ret %d\r\n", ret);
					}
				}

				#ifdef SC2000E
				if(0 != strcmp(tmp_project_name, proj_name))
				{
					char file_path[MAX_FILE_NAME_LEN * 2] = {0};
					snprintf(file_path, sizeof(file_path), "%s%s", PROJ_DIR, tmp_project_name);
					LOGI("osal_remove_dir %s \n", file_path); 
					if ((ret = osal_remove_dir(file_path)) != 0)
					{
						LOGE("osal_remove_dir %s error:%d\n", file_path, ret);
					}
				}
                #endif
			}

			if (0 != ret)
			{
				error = SC_EC_SLN_RESTORE; /*用于返回方案切换失败*/
				LOGE("switch project %s failed ret %d, restore load tmp project %s\r\n", proj_name, ret, tmp_project_name);
				/* 切换方案失败，恢复原来的方案*/
				scfw_delete_module_tree(0);

				if (0 == strcmp(tmp_project_name, DEFAULT_PROJ_NAME))
				{
					/* 原来方案是无名方案*/
					ret = scfw_load_default_project();
					if (0 != ret)
					{
						LOGE("load default project failed ret %d\r\n", ret);
						LEAVE(-8, out);
					}
				}
				else
				{
					/* 原来方案非无名方案*/
					/* 切换失败后的恢复逻辑不推送进度，否则会出现进度回退的情况，不太友好*/
					ret = scfw_load_project(tmp_project_name, "123456", USER_LOAD_PROJ,false,true); //加载失败启动一个无名方案
					if (0 != ret)
					{
						/* 恢复原来的方案失败，加载无名方案*/
						LOGE("scfw_load_project %s failed ret %d start load algo_project\r\n", tmp_project_name, ret);
						scfw_delete_module_tree(0);
						ret = scfw_load_default_project();
						if (0 != ret)
						{
							LOGE("scfw load default project failed ret %d\r\n", ret);
							LEAVE(-9, out);
						}
					}
				}
			}

			break;
		}
	}

	if (project_num == i)
	{
		LOGE("not found the %s in solution list\r\n", proj_name);
		LEAVE(-10, out);
	}
	
out:
	if (!lock_state_http)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_HTTPD) < 0)
		{
			LOGE("SWITCH_LOCK_HTTPD proc\r\n");
		}
	}
	if (!lock_state_ws)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_WS) < 0)
		{
			LOGE("SWITCH_LOCK_WS proc\r\n");
		}
	}
	if (!lock_state_gvcp)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_GVCP) < 0)
		{
			LOGE("SWITCH_LOCK_GVCP proc\r\n");
		}
	}
	if (!lock_state_gvmp)
	{
		if (scfw_project_switch_unlock_proc(SWITCH_LOCK_GVMP) < 0)
		{
			LOGE("SWITCH_LOCK_GVMP proc\r\n");
		}
	}
	if (error != 0)
	{
		set_led_status(LED_TYPE_STS, LED_COLOR_RED);
		// 仅仅在外部触发方案切换的时候推送进度和错误状态。
		// WS_CMD_LPRP方式切换的时候不进行推送（调用此函数的上层做了推送），否则会因为错误状态推送过于提前错误码设置导致客户端拿到不正确的错误码。
		if (load_type != USER_LOAD_PROJ && load_type != BUTTON_LOAD_PROJ)
		{
		    struct proj_push_data proj_data = {0};
		    proj_data.cmd_data = WS_CMD_DCLP;
		    proj_data.progress = 100;
		    proj_data.status = -1;
		    scfw_execute_project_cb(&proj_data);			
		}
	}
	else
	{
		set_led_status(LED_TYPE_STS, LED_COLOR_GREEN);
	}
	set_led_status(LED_TYPE_OK_NG, LED_COLOR_OFF);

	emmc_start_run_all_mod();
	return error;
}

/**
 * @brief    get image source
 * @param[in]  image_source 
 * @return     0:ok  other:error
 */
 
int32_t scfw_get_image_source(char *image_source)
{
	int error = 0;
	int ret = 0;
	cJSON *proj_obj = NULL;
	cJSON *img_src_obj = NULL;

	if (NULL == image_source)
	{
		LOGE("error image source is NULL\n");
		LEAVE(-1, out);
	}
	
	if (0 == strlen(cur_algo_image_source))
	{
		strncpy(cur_algo_image_source, DEFAULT_IMAGE_SOURCE, sizeof(cur_algo_image_source));
		cur_algo_image_source[sizeof(cur_algo_image_source) - 1] = '\0';
		strncpy(image_source, cur_algo_image_source, sizeof(cur_algo_image_source));
	}
	else
	{
		strncpy(image_source, cur_algo_image_source, sizeof(cur_algo_image_source));
		image_source[sizeof(cur_algo_image_source) - 1] = '\0';
	}

	LOGD("cur_algo_image_source is %s image_source is %s\n", cur_algo_image_source, image_source);
		
out:
	
	return error;
}
/**
 * @brief    set image source
 * @param[in]  image_source 
 * @return     0:ok  other:error
 */
int32_t scfw_set_image_source(char *image_source)
{
	int32_t error = 0;
	int32_t ret = 0;
	cJSON *proj_obj = NULL;
	cJSON *tmp_obj = NULL;
	cJSON *img_src_obj = NULL;
	struct project_info sln_info = {0};

	if (NULL == image_source)
	{
		LOGE("error image source is NULL\n");
		LEAVE(-1, out);
	}
	
	/* default proj name update cur_algo_image_source*/
	strncpy(cur_algo_image_source, image_source, sizeof(cur_algo_image_source));
	cur_algo_image_source[sizeof(cur_algo_image_source) - 1] = '\0';
	
out:
	return error;
}


/**
 * @brief      focus prepare of load solution finsih
 * @param[in]      
 * @return     success: 0; fail:other
 */
 static int32_t scfw_solution_finish_focus_pre(void)
{
	char *end_char = NULL;
	int32_t error = 0;
	int32_t val_int = 0;
	int32_t num = 0;
	int32_t count = 0;
	int32_t target_pos = 0;
	int32_t current_pos = 0;
	struct param_info_web value_info_buf = {0};
	struct param_info_web value_info_buf1 = {0};
	
	/* 目标焦距位置 */
	if ((error = scfw_get_module_param_data(0, "FocusPositive", &value_info_buf)) < 0)
	{
		LOGE("get image param FocusPositive is error: 0x%x\n", error);
		LEAVE(-1, out);
	}
	
	val_int = strtol(value_info_buf.value, &end_char, 0);
	if (end_char && ('\0' != *end_char))
	{
		LOGE("get image param FocusPositive is error\n");
		LEAVE(-2, out);
	}
	target_pos = val_int;

	while(1)
	{
		/* 焦距调整到目标位置 */
		if (num >= TIME_OUT_S_3)
		{
			/* 超过5S，直接退出 */
			LOGE("timeou( %d > %d)\r\n", num, TIME_OUT_S_3);
			break;
		}

		/* 当前焦距位置 */
		if ((error = scfw_update_module_single_param(0, "FocusPosition", false)) < 0)
		{
			LOGE("update image param FocusPosition is error: 0x%x\n", error);
			LEAVE(-5, out);
		}
		if ((error = scfw_get_module_param_data(0, "FocusPosition", &value_info_buf)) < 0)
		{
			LOGE("get image param FocusPosition is error: 0x%x\n", error);
			LEAVE(-6, out);
		}
		val_int = strtol(value_info_buf.value, &end_char, 0);
		if (end_char && ('\0' != *end_char))
		{
			LOGE("get image param FocusPosition is error\n");
			LEAVE(-7, out);
		}
		current_pos = val_int;
		
		LOGI(">>>> (%d ms)target_pos: %d, current_pos:%d\n", num * 20, target_pos, current_pos);
		
		if (target_pos == current_pos)
		{
			/* 等待调到目标位置 */
			num = 0;
			break;
		}
		
		usleep(20000);
		num++;
	}
	
out:
	return error;
}


/**
 * @brief      prepare of load solution finsih
 * @param[in]      
 * @return     success: 0; fail:other
 */
int32_t scfw_load_solution_finish_pre(void)
{
	int32_t error = 0;

	/* 等待调焦电机加载到目标位置 */
	if ((error = scfw_solution_finish_focus_pre()) < 0)
	{
		LOGE("get image param FocusPosition is error\n");
		LEAVE(-4, out);
	}

out:
	return error;	
}

/**
 * @brief      set project param
 * @param[in]  proj_name project name
 * @param[in]  param param name
 * @param[in]  value_buf value buf
 * @param[in]  buf_size value buf size
 * @return     success: 0; fail:other
 */
int32_t scfw_set_project_param(const char *proj_name, const char *param, char *value_buf, uint32_t buf_size)
{
	int error = 0;
	int ret = 0;
	cJSON *proj_obj = NULL;
	cJSON *tmp_obj = NULL;

	if (NULL == proj_name || NULL == param || NULL == value_buf)
	{
		LEAVE(-1, out);
	}

	if (NULL == algo_proj_mng_root)
	{
		if ((ret = load_algo_project_mng_from_file(&algo_proj_mng_root)) < 0)
		{
			LOGE("ERROR load algo project mang from file error ret = %d\n", ret);
		}

		if (NULL == algo_proj_mng_root)
		{
			if ((algo_proj_mng_root = cJSON_CreateObject()) == NULL)
			{
				LEAVE(-2, out);
			}

			cJSON_AddItemToObject(algo_proj_mng_root,
								  JSON_PROJECT_LAST_USED_PROJ,
								  cJSON_CreateString(DEFAULT_PROJ_NAME));
			cJSON_AddItemToObject(algo_proj_mng_root,
								  JSON_PROJECT_NUM,
								  cJSON_CreateNumber(0));

			cJSON_AddItemToObject(algo_proj_mng_root,
								  JSON_PROJECT_LIST,
								  cJSON_CreateArray());
		}
	}
	
	/* is the project already exist ? */
	if ((proj_obj = get_project_obj(algo_proj_mng_root, proj_name)) == NULL)
	{
		LEAVE(-3, out);
	}
	else
	{
		if ((tmp_obj = cJSON_GetObjectItem(proj_obj, param)) == NULL)
		{
			LEAVE(-4, out);
		}

		/* replace the value if chaneged */
		switch ((tmp_obj->type) & 0xFF)
		{
			case cJSON_NULL:
			case cJSON_Array:
			case cJSON_Object:
			case cJSON_Raw:
			case cJSON_False:
			case cJSON_True:
				LEAVE(-5, out);
			case cJSON_Number:
				tmp_obj->valuedouble = atoi(value_buf);
				tmp_obj->valueint = atoi(value_buf);
				break;
			case cJSON_String:
				cJSON_ReplaceItemInObject(proj_obj, param, cJSON_CreateString(value_buf));
				break;
			default:
				LEAVE(-6, out);
		}
	}

	/* 写入数据到文件中 */
	if ((ret = save_algo_project_mng_into_file(algo_proj_mng_root)) < 0)
	{
		LOGE("[%s] save_algo_project_mng_into_file:%d\n", __func__, ret);
		LEAVE(-7, out);
	}

out:
	if (error < 0)
	{
		LOGE("[%s_%d] error:%d!\r\n", __func__, __LINE__, error);
		return -1;
	}
	return (error);
}


void scfw_set_last_used_project_default(void)
{
	int32_t ret = 0;
	if ((ret = update_last_used_project_name(DEFAULT_PROJ_NAME)) < 0)
	{
		LOGE("update_last_used_project_name ret = %d\n", ret);
	}
}

static void *project_init_thread_init(void *args)
{
	scfw_load_last_used_project();

 	osal_cond_release_all(proj_cond, scfw_proj_is_ready, NULL);
	return NULL;
}

int project_init(void)
{
	int ret = thread_spawn_ex(NULL, 1, SCHED_POLICY_OTHER, SCHED_PRI_NA, 1024 * 1024, project_init_thread_init, NULL);
	if (ret < 0)
	{
		LOGE("httpd_thread_init thread creation failed!\r\n");
		return -1;
	}

	return 0;
}
