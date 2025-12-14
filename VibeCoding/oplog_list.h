/** @file    
  * @note    Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
  * @brief   
  *
  * @author  tanpeng7
  * @date    2019/11/30
  *
  * @version
  *  date        |version |author              |mseeage
  *  :----       |:----   |:----               |:------
  *  2019/11/30  |V1.0.0  |tanpeng7            |new
  * @warning 
  */
  
#ifndef _OPLOG_LIST_H
#define _OPLOG_LIST_H

#include <stdint.h>
#include <dbutil.h>

#define DB_OPLOG_MAX		(100000)
#define DB_OPLOG_LIST_LEN	(256)
#define DB_IP_LEN			(32)

typedef enum 
{
	E_DB_OPLOG_TS,
	E_DB_OPLOG_TYPE,
	E_DB_OPLOG_TYPE_AND_TS,
} E_DB_OPLOG_COL;

typedef enum
{
	OPLOG_INFO = 0,
	OPLOG_WARNING = 1,
	OPLOG_ERROR = 2,
} OPLOG_TYPE;


typedef struct
{
	int id;
	OPLOG_TYPE type;
	unsigned long long ts;
	char ip[DB_IP_LEN];
	char info[DB_OPLOG_LIST_LEN];
} DB_OPLOG_INFO;

typedef struct
{
	E_DB_OPLOG_COL col;
	unsigned long long ts_range[2];
	OPLOG_TYPE type;
} DB_OPLOG_GET_FILTER;

typedef struct
{
	QUERY_TABLE db_date_query;
	int need_free_after_used;
} DB_OPLOG_GET_HANDLE;


#ifdef __cplusplus
extern "C"{
#endif

/**
 * @brief   init oplog_list database
 * @return  0 on success; < 0 on failure
 */
int db_oplog_list_init(void);

/**
 * @brief      set current operator ip (for audit log)
 * @param[in]  ip: operator ip string, NULL/"" will reset to default ("0.0.0.0")
 * @return     0 on success; < 0 on failure
 */
int db_oplog_list_set_operator_ip(const char *ip);

/**
 * @brief      add a record
 * @param[in]  oplog 
 * @return     0 on success; < 0 on failure
 */
int db_oplog_list_add(DB_OPLOG_INFO *oplog);

/**
 * @brief      get record(s)'s number, ***user must call db_oplog_list_get_free() after get all details.***
 * @param[in]  filter: the filter, if filter = NULL, means there is no filter
 * @param[out] num:    the number of record(s)
 * @param[out] hndl:   handler used to get details
 * @return     0 on success; < 0 on failure
 */
int db_oplog_list_get_num(DB_OPLOG_GET_FILTER *filter, int *num, DB_OPLOG_GET_HANDLE *hndl);

/**
 * @brief      get record(s)'s details, ***user must call db_oplog_list_get_free() after get all details.***
 * @param[in]  hndl: handler used to get details
 * @param[in]  idx:  idxth record
 * @param[out] oplog:  record details
 * @return     0 on success; < 0 on failure
 */
int db_oplog_list_get_value(DB_OPLOG_GET_HANDLE *hndl, int idx, DB_OPLOG_INFO *oplog);

/**
 * @brief      free after get all details.
 * @param[in]  hndl: handler used to get details
 * @return     0 on success; < 0 on failure
 */
int db_oplog_list_get_free(DB_OPLOG_GET_HANDLE *hndl);

/**
 * @brief      delete all record
 * @return     0 on success; < 0 on failure
 */
int db_oplog_list_del_all(void);

#ifdef __cplusplus
}
#endif

#endif
