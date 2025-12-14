#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "oplog_list.h"
#include "osal_file.h"
#include "osal_mutex.h"
#include "thread/ThreadApi.h"
#include "simple_fifo.h"

#define LOG_NDEBUG				(0)
#define LOG_TAG					"OPLOG"
#include "log/log.h"

#define DB_OPLOG_DEFAULT_IP			"0.0.0.0"

#define DB_OPLOG_LIST_NAME			"/mnt/data/db/oplog_list_db"
#define DB_OPLOG_LIST_TABLE_NAME	"oplog_list"
#define MAX_DB_ADD_MTX_TIMEOUT		(5000)

//数据库的操作句柄
static DATABASE *sqlite_db = NULL;
static void *mutex = NULL;

#define DB_LOG_FIFO_NUM				(300)						/* 操作日志fifo节点个数 */
#define DB_BATCH_NUM				(30)						/* 操作日志批处理个数 */
#define DB_WAIT_NUM					(150)						/* 操作写入最大等待个数，计算最大等待时长 */
static struct simple_fifo db_log_queue;							/* 操作日志推送fifo */
static DB_OPLOG_INFO db_log_fifo_array[DB_LOG_FIFO_NUM];		/* 操作日志fifo节点池 */

static OSAL_MUTEX g_oplog_operator_ip_mtx = OSAL_MUTEX_INITIALIZER;
static char g_oplog_operator_ip[DB_IP_LEN] = DB_OPLOG_DEFAULT_IP;

static void *oplog_patched_thread(void *args);

static void oplog_get_operator_ip(char *ip_buf, size_t ip_buf_len)
{
	if ((NULL == ip_buf) || (0 == ip_buf_len))
	{
		return;
	}

	pthread_mutex_lock(&g_oplog_operator_ip_mtx);
	snprintf(ip_buf, ip_buf_len, "%s", g_oplog_operator_ip[0] ? g_oplog_operator_ip : DB_OPLOG_DEFAULT_IP);
	pthread_mutex_unlock(&g_oplog_operator_ip_mtx);
}

static void oplog_fill_ip_if_empty(DB_OPLOG_INFO *oplog)
{
	if (NULL == oplog)
	{
		return;
	}

	oplog->ip[DB_IP_LEN - 1] = '\0';
	if (oplog->ip[0] != '\0')
	{
		return;
	}

	oplog_get_operator_ip(oplog->ip, sizeof(oplog->ip));
	oplog->ip[DB_IP_LEN - 1] = '\0';
}

int db_oplog_list_set_operator_ip(const char *ip)
{
	const char *ip_to_set = (ip && ip[0]) ? ip : DB_OPLOG_DEFAULT_IP;

	pthread_mutex_lock(&g_oplog_operator_ip_mtx);
	snprintf(g_oplog_operator_ip, sizeof(g_oplog_operator_ip), "%s", ip_to_set);
	g_oplog_operator_ip[DB_IP_LEN - 1] = '\0';
	pthread_mutex_unlock(&g_oplog_operator_ip_mtx);

	return 0;
}

static int oplog_list_table_has_column(DATABASE *db, const char *table, const char *column)
{
	QUERY_TABLE pragma_query = {0};
	char sql[256] = {0};
	int rc = 0;
	int rec_count = 0;
	int found = 0;

	if ((NULL == db) || (NULL == table) || (NULL == column))
	{
		return 0;
	}

	snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
	rc = db_query_open_v2(db, sql, &pragma_query);
	if (rc != SQLITE_OK)
	{
		return 0;
	}

	rec_count = db_query_rec_count_v2(&pragma_query);
	for (int i = 0; i < rec_count; i++)
	{
		char *name = db_query_fields_v2(&pragma_query, "name", i);
		if ((NULL != name) && (0 == strcmp(name, column)))
		{
			found = 1;
			break;
		}
	}

	db_query_close_v2(&pragma_query);
	return found;
}

static int oplog_list_ensure_ip_column(DATABASE *db)
{
	char str[256] = {0};
	int rc = 0;

	RETURN_IFNULL(db);

	if (oplog_list_table_has_column(db, DB_OPLOG_LIST_TABLE_NAME, "ip"))
	{
		return SQLITE_OK;
	}

	snprintf(str, sizeof(str),
	    "ALTER TABLE %s ADD COLUMN ip char(%d) DEFAULT '%s';",
	    DB_OPLOG_LIST_TABLE_NAME, DB_IP_LEN, DB_OPLOG_DEFAULT_IP);

	rc = db_execsql(db, str);
	if (rc != SQLITE_OK)
	{
		LOGE("add ip column failed rc:%d err:%d\n", rc, DB_ERRCODE(db));
		return DB_RET_FAIL;
	}

	return SQLITE_OK;
}

static int oplog_list_get_index(int *Index, int *CoverageIndex)
{
	QUERY_TABLE index_query = {0};
	char str[1024] = {0};
	char *get_date = NULL;
	DATABASE *db = GET_DB_HANDLE();
	int rc= 0;

	RETURN_IFNULL(Index);
	RETURN_IFNULL(CoverageIndex);
	RETURN_IFNULL(db);
	snprintf(STR_PTR(str), STR_SPACE(str), "SELECT * FROM %s WHERE id = %d", \
						DB_OPLOG_LIST_TABLE_NAME, DB_OPLOG_MAX + 1);

	rc = db_query_open_v2(db, str, &index_query);
	if (rc != SQLITE_OK)
	{
		return DB_RET_FAIL;
	}
	
	get_date = db_query_fields_v2(&index_query, "ts", 0);
	if(NULL != get_date)
	{
		*Index = strtoul(get_date, NULL, 10);
	}
	else
	{
		db_query_close_v2(&index_query);
		return -3;
	}

	get_date = db_query_fields_v2(&index_query, "type", 0);
	if(NULL != get_date)
	{
		*CoverageIndex = strtoul(get_date, NULL, 10);
	}
	else
	{
		db_query_close_v2(&index_query);
		return -4;
	}

	db_query_close_v2(&index_query);
	
	return rc;

	
}

int db_oplog_list_init(void)
{
	int ret = 0;
	char str[1024] = {0};
	char *pstr = str;

#ifndef R315
	pthread_t thread_db_oplog;
#endif

	if (NULL != sqlite_db)
	{
		return SQLITE_OK;
	}

	if (NULL == mutex)
	{
		if (0 != osal_mutex_create(&mutex))
		{
			return -1;
		}
	}

	sqlite_db = db_open(DB_OPLOG_LIST_NAME);
	if (!sqlite_db)
	{
		return -1;
	}

	/* version table */
	snprintf(str, sizeof(str), "%s", C_VERSION);
	pstr = str;
	if (creat_table(sqlite_db, &pstr, 1))
	{
		ret = -2;
		goto exit_close;
	}

	/* oplog table */
	snprintf(str, sizeof(str),
	    "CREATE TABLE IF NOT EXISTS %s ( "
	    "id bigint unsigned primary key, "
	    "type integer, "
	    "ts integer, "
	    "ip char(%d), "
	    "info char(%d));",
	    DB_OPLOG_LIST_TABLE_NAME, DB_IP_LEN, DB_OPLOG_LIST_LEN);
	pstr = str;
	if (creat_table(sqlite_db, &pstr, 1))
	{
		ret = -3;
		goto exit_close;
	}

	SET_DB_HANDLE(sqlite_db);

	ret = oplog_list_ensure_ip_column(sqlite_db);
	if (ret != SQLITE_OK)
	{
		ret = -4;
		goto exit_close;
	}

	snprintf(str, sizeof(str),
	    "INSERT OR IGNORE INTO %s (id, type, ts, ip, info) "
	    "VALUES (%d, %d, %llu, '%s', '%s');",
	    DB_OPLOG_LIST_TABLE_NAME,
	    DB_OPLOG_MAX + 1, 0, 0ULL,
	    DB_OPLOG_DEFAULT_IP, "Database maintenance fields");

	ret = db_execsql(sqlite_db, str);
	if (ret != SQLITE_OK)
	{
		ret = -5;
		goto exit_close;
	}

#ifndef R315
	sfifo_init(&db_log_queue, sizeof(DB_OPLOG_INFO), DB_LOG_FIFO_NUM, (void*)(&db_log_fifo_array[0]));
	ret = thread_spawn_ex(&thread_db_oplog, 1, SCHED_POLICY_OTHER, SCHED_PRI_NA, 1024 * 1024, oplog_patched_thread, NULL);
	if (ret < 0)
	{
		ret = -6;
		goto exit_close;
	}
#endif

	return SQLITE_OK;

exit_close:
	db_close(&sqlite_db);
	return ret;

}

static int oplog_list_del_all(void)
{
	char str[1024] = {0};
	DATABASE *db = GET_DB_HANDLE();
	int rc = 0;

	RETURN_IFNULL(db);

	snprintf(STR_PTR(str), STR_SPACE(str), "DELETE FROM %s WHERE id != %d;", 
		DB_OPLOG_LIST_TABLE_NAME, DB_OPLOG_MAX + 1);

	rc = db_execsql(db, str);
	if (rc != SQLITE_OK)
	{
		LOGE("del error: %d\n", rc);
	}

	return 0;
}

int db_oplog_list_add(DB_OPLOG_INFO *oplog)
{
#ifndef R315
	DB_OPLOG_INFO oplog_tmp = {0};

	RETURN_IFNULL(oplog);

	oplog_tmp = *oplog;
	oplog_tmp.info[DB_OPLOG_LIST_LEN - 1] = '\0';
	oplog_fill_ip_if_empty(&oplog_tmp);

	return sfifo_in(&db_log_queue, &oplog_tmp);
#else
    return 0;
#endif
}

int db_oplog_list_get_num(DB_OPLOG_GET_FILTER *filter, int *num, DB_OPLOG_GET_HANDLE *hndl)
{
	char str[1024] = {0};
	DATABASE *db = GET_DB_HANDLE();
	int rc = 0;

	RETURN_IFNULL(hndl);
	RETURN_IFNULL(num);
	RETURN_IFNULL(db);
	if (filter)
	{
		switch(filter->col)
		{
			case E_DB_OPLOG_TS:
				snprintf(STR_PTR(str), STR_SPACE(str),
                    "SELECT * FROM %s WHERE ts >= %llu AND ts <= %llu AND id != %d order by ts desc, type asc;",
                    DB_OPLOG_LIST_TABLE_NAME, 
                    filter->ts_range[0], filter->ts_range[1],
                    DB_OPLOG_MAX + 1);
				break;
			case E_DB_OPLOG_TYPE:
				snprintf(STR_PTR(str), STR_SPACE(str),
                    "SELECT * FROM %s WHERE type = %d AND id != %d order by ts desc, type asc;",
                    DB_OPLOG_LIST_TABLE_NAME, 
                    filter->type, 
                    DB_OPLOG_MAX + 1);
				break;
			case E_DB_OPLOG_TYPE_AND_TS:
				snprintf(STR_PTR(str), STR_SPACE(str),
                    "SELECT * FROM %s WHERE ts >= %llu AND ts <= %llu AND type = %d AND id != %d order by ts desc, type asc;",
                    DB_OPLOG_LIST_TABLE_NAME, 
                    filter->ts_range[0], filter->ts_range[1], filter->type,
                    DB_OPLOG_MAX + 1);
				break;
			default:
				return -1;
		}

	}
	else
	{
		snprintf(STR_PTR(str), STR_SPACE(str),
            "SELECT * FROM %s WHERE id != %d order by ts desc, type asc;",
            DB_OPLOG_LIST_TABLE_NAME, 
            DB_OPLOG_MAX + 1);
	}
	rc = db_query_open_v2 (db, str, &(hndl->db_date_query));
	if (rc != SQLITE_OK)
	{
		return DB_RET_FAIL;
	}

	*num = db_query_rec_count_v2(&(hndl->db_date_query));
	hndl->need_free_after_used = 1;
	
	return 0;

}

int db_oplog_list_get_value(DB_OPLOG_GET_HANDLE *hndl, int idx, DB_OPLOG_INFO *oplog)
{
	char *get_date = NULL;

	RETURN_IFNULL(hndl);
	RETURN_IFNULL(oplog);

	if (hndl->need_free_after_used == 0)
	{
		return -1;
	}

	get_date = db_query_fields_v2(&(hndl->db_date_query), "type", idx);
	if(NULL != get_date)
	{
		oplog->type = strtol(get_date, NULL, 10);
	}
	else
	{
		return -2;
	}

	get_date = db_query_fields_v2(&(hndl->db_date_query), "ts", idx);
	if(NULL != get_date)
	{
		oplog->ts = strtoull(get_date, NULL, 10);
	}
	else
	{
		return -3;
	}

	get_date = db_query_fields_v2(&(hndl->db_date_query), "ip", idx);
	if(NULL != get_date)
	{
		snprintf(oplog->ip, DB_IP_LEN, "%s", get_date);
	}
	else
	{
		snprintf(oplog->ip, DB_IP_LEN, "%s", DB_OPLOG_DEFAULT_IP);
	}

	get_date = db_query_fields_v2(&(hndl->db_date_query), "info", idx);
	if(NULL != get_date)
	{
		snprintf(oplog->info, DB_OPLOG_LIST_LEN, "%s", get_date);
	}
	else
	{
		return -5;
	}

	get_date = db_query_fields_v2(&(hndl->db_date_query), "id", idx);
	if(NULL != get_date)
	{
		oplog->id = strtol(get_date, NULL, 10);
	}
	else
	{
		return -6;
	}

	return 0;

}

int db_oplog_list_get_free(DB_OPLOG_GET_HANDLE *hndl)
{
	RETURN_IFNULL(hndl);
	
	if (hndl->need_free_after_used == 1)
	{
		db_query_close_v2 (&(hndl->db_date_query)); 
		hndl->need_free_after_used = 0;
	}

	return 0;
}

int db_oplog_list_del_all(void)
{
	return oplog_list_del_all();
}

static int oplog_list_batch_add(DB_OPLOG_INFO *oplog , int nCurrentIndex, int num)
{
	char str[1024] = {0};
	DATABASE *db = GET_DB_HANDLE();
	int rc = 0;

	RETURN_IFNULL(oplog);
	RETURN_IFNULL(db);

	/*BEGIN TRANSACTION;
	INSERT INTO table_name (column1, column2, column3) VALUES (value1, value2, value3);
	INSERT INTO table_name (column1, column2, column3) VALUES (value4, value5, value6);
	INSERT INTO table_name (column1, column2, column3) VALUES (value7, value8, value9);
	COMMIT;*/

	rc = db_execsql(db, "BEGIN TRANSACTION;");
	if (rc != SQLITE_OK)
	{
		return DB_RET_FAIL;
	}

	for (int i = 0;i < num;i++)
	{
		char *ip_esc = db_single_quote_2x(oplog->ip);
		char *info_esc = db_single_quote_2x(oplog->info);
		snprintf(str, sizeof(str),
		    "INSERT INTO %s (id, type, ts, ip, info) VALUES (%d, %d, %llu, '%s', '%s');",
		    DB_OPLOG_LIST_TABLE_NAME,
		    nCurrentIndex, oplog->type, oplog->ts, ip_esc ? ip_esc : oplog->ip, info_esc ? info_esc : oplog->info);
		free(ip_esc);
		free(info_esc);
		rc = db_execsql(db, str);
		if (rc != SQLITE_OK)
		{
			db_execsql(db, "ROLLBACK;");
			if (SQLITE_CONSTRAINT == DB_ERRCODE(db))
			{
				return SQLITE_CONSTRAINT;
			}
			return DB_RET_FAIL;
		}
		nCurrentIndex++;
		oplog++;
	}

	rc = db_execsql(db, "COMMIT;");
	if (rc != SQLITE_OK)
	{
		db_execsql(db, "ROLLBACK;");
		if (SQLITE_CONSTRAINT == DB_ERRCODE(db))
		{
			return SQLITE_CONSTRAINT;
		}
		return DB_RET_FAIL;
	}
	else
	{
		return DB_RET_OK;
	}
}

static int oplog_list_batch_update(DB_OPLOG_INFO *oplogs, int index, int num)
{
	RETURN_IFNULL(oplogs);

	int rt = 0;
	int count = 0;
	char str[1024] = {0};
	DATABASE *db = GET_DB_HANDLE();
	DB_OPLOG_GET_HANDLE hndl = {0};

	RETURN_IFNULL(db);
	
	snprintf(str, sizeof(str), "SELECT * FROM %s WHERE id == %d ;", DB_OPLOG_LIST_TABLE_NAME, index);
	rt = db_query_open_v2(db, str, &(hndl.db_date_query));
	if (rt != SQLITE_OK)
	{
		return DB_RET_FAIL;
	}
	count = db_query_rec_count_v2(&(hndl.db_date_query));
	hndl.need_free_after_used = 1;
	db_oplog_list_get_free(&hndl);

	if (count != 1)
	{
		return -6;
	}

	/*
	BEGIN TRANSACTION;
	UPDATE table_name
	SET column1 = value1, column2 = value2, ...
	WHERE condition1;

	UPDATE table_name
	SET column1 = value1, column2 = value2, ...
	WHERE condition2;

	COMMIT;
	*/
	
	rt = db_execsql(db, "BEGIN TRANSACTION;");
	if (rt != SQLITE_OK)
	{
		if (SQLITE_CONSTRAINT == DB_ERRCODE(db))
		{
			return SQLITE_CONSTRAINT;
		}
		return DB_RET_FAIL;
	}

	for (int i = 0;i < num;i++)
	{
		char *ip_esc = db_single_quote_2x(oplogs->ip);
		char *info_esc = db_single_quote_2x(oplogs->info);
		snprintf(str, sizeof(str),
		    "UPDATE %s SET type = %d, ts = %llu, ip = '%s', info = '%s' WHERE id = %d;",
		    DB_OPLOG_LIST_TABLE_NAME,
		    oplogs->type, oplogs->ts, ip_esc ? ip_esc : oplogs->ip, info_esc ? info_esc : oplogs->info, index);
		free(ip_esc);
		free(info_esc);
		rt = db_execsql(db, str);
		if (rt != SQLITE_OK)
		{
			db_execsql(db, "ROLLBACK;");
			return DB_RET_FAIL;
		}

		index++;
		oplogs++;
	}

	rt = db_execsql(db, "COMMIT;");
	return rt;
}

static int db_oplog_list_batch_add(DB_OPLOG_INFO *oplog, int num)
{
	char str[1024] = {0};
	DATABASE *db = GET_DB_HANDLE();
	int rc = 0;
	int nCurrentIndex = 0, nCurrentNum1 = 0, nCurrentNum2 = 0;
	int nCoverageIndex = 0, nCoverageNum1 = 0, nCoverageNum2 = 0;

	RETURN_IFNULL(oplog);
	RETURN_IFNULL(db);
	RETURN_IFNULL(mutex);

	if (oplog->type < OPLOG_INFO || oplog->type > OPLOG_ERROR)
	{
		return -1;
	}

	rc = osal_mutex_timed_lock(mutex, MAX_DB_ADD_MTX_TIMEOUT);
	if (rc != 0)
	{
		return -2;
	}

	do
	{
		rc = oplog_list_get_index(&nCurrentIndex, &nCoverageIndex);
		if (rc != SQLITE_OK)
		{
			break;	
		}

		if (nCurrentIndex >= DB_OPLOG_MAX)
		{
			/* 循环覆盖写入时计算末尾写入的个数和开头写入的个数 */
			nCoverageNum1 = (DB_OPLOG_MAX - nCoverageIndex >= num) ? num : (DB_OPLOG_MAX- nCoverageIndex);
			nCoverageNum2 = (DB_OPLOG_MAX - nCoverageIndex >= num) ? 0 : (num - (DB_OPLOG_MAX - nCoverageIndex));
			
			if (nCoverageNum1 > 0)
			{
				nCoverageIndex++;
				rc = oplog_list_batch_update(oplog, nCoverageIndex, nCoverageNum1);
				if (rc != SQLITE_OK)
				{
					if (rc == -6)
					{
						rc = oplog_list_batch_add(oplog, nCoverageIndex, nCoverageNum1);
					}

					if (SQLITE_CONSTRAINT == rc)
					{
						LOGE("cover oplog idx :%d num %d err, need update header index\n", nCoverageIndex, nCoverageNum1);
					}
					else if (rc != SQLITE_OK)
					{
						break;
					}
				}
				nCoverageIndex += (nCoverageNum1 - 1);
			}

			if (nCoverageNum2 > 0)
			{
				nCoverageIndex = 1;
				rc = oplog_list_batch_update(&oplog[nCoverageNum1], nCoverageIndex, nCoverageNum2);
				if (rc != SQLITE_OK)
				{
					if (rc == -6)
					{
						rc = oplog_list_batch_add(&oplog[nCoverageNum1], nCoverageIndex, nCoverageNum2);
					}

					if (SQLITE_CONSTRAINT == rc)
					{
						LOGE("cover oplog idx :%d num %d err, need update header index\n", nCoverageIndex, nCoverageNum2);
					}
					else if (rc != SQLITE_OK)
					{
						break;
					}
				}

				nCoverageIndex += (nCoverageNum2 - 1);
			}			

			/* 更新覆盖索引 */
			snprintf(STR_PTR(str), STR_SPACE(str), "UPDATE %s SET type = %d WHERE id = %d;", \
									DB_OPLOG_LIST_TABLE_NAME, \
									nCoverageIndex, DB_OPLOG_MAX + 1);
			rc = db_execsql(db, str);
			if (rc != SQLITE_OK)
			{
				break;
			}
		}
		else
		{
			/* 插入写入时计算末尾写入的个数和开头覆盖写入的个数 */
			nCurrentNum1 = (DB_OPLOG_MAX - nCurrentIndex >= num) ? num : (DB_OPLOG_MAX - nCurrentIndex);
			nCurrentNum2 = (DB_OPLOG_MAX - nCurrentIndex >= num) ? 0 : (num - (DB_OPLOG_MAX - nCurrentIndex));	

			if (nCurrentNum1 > 0)
			{
				nCurrentIndex++;
				rc = oplog_list_batch_add(oplog, nCurrentIndex, nCurrentNum1);
				if (SQLITE_CONSTRAINT == rc)
				{
					LOGE("add oplog idx :%d num %d err, need update header index\n", nCurrentIndex, nCurrentNum1);
				}
				else if (rc != SQLITE_OK)
				{
					break;
				}
				nCurrentIndex += (nCurrentNum1 - 1);
			}
		
			if (nCurrentNum2 > 0)
			{
				nCoverageIndex = 1;
				rc = oplog_list_batch_update(&oplog[nCurrentNum1], nCoverageIndex, nCurrentNum2);
				if (rc != SQLITE_OK)
				{
					if (rc == -6)
					{
						rc = oplog_list_batch_add(&oplog[nCurrentNum1], nCoverageIndex, nCurrentNum2);
					}

					if (SQLITE_CONSTRAINT == rc)
					{
						LOGE("cover oplog idx :%d num %d err, need update header index\n", nCoverageIndex, nCurrentNum2);
					}
					else if (rc != SQLITE_OK)
					{
						break;
					}
				}

				nCoverageIndex += (nCurrentNum2 - 1);

				/* 更新覆盖索引 */
				snprintf(STR_PTR(str), STR_SPACE(str), "UPDATE %s SET type = %d WHERE id = %d;", \
									DB_OPLOG_LIST_TABLE_NAME, \
									nCoverageIndex, DB_OPLOG_MAX + 1);
				rc = db_execsql(db, str);
				if (rc != SQLITE_OK)
				{
					break;
				}
				
			}

			/* 更新插入索引 */
			snprintf(STR_PTR(str), STR_SPACE(str), "UPDATE %s SET ts = %llu WHERE id = %d;", \
									DB_OPLOG_LIST_TABLE_NAME, \
									(uint64_t)nCurrentIndex, DB_OPLOG_MAX + 1);
			rc = db_execsql(db, str);
			if (rc != SQLITE_OK)
			{
				break;
			}
		}
	}while(0);

	osal_mutex_unlock(mutex);

	return rc;
}

static void *oplog_patched_thread(void *args)
{
	int ret = 0;
	int db_num = 0;
	int wait_cnt = 0;
	DB_OPLOG_INFO db_log[DB_BATCH_NUM] = {0};
	
	thread_set_name("oplog_patched_thread");

	while(1)
	{
		if (!sfifo_out(&db_log_queue, &db_log[db_num]))
		{
			db_num++;
		}

		if (wait_cnt > DB_WAIT_NUM || db_num >= DB_BATCH_NUM)
		{
			if (db_num > 0)
			{
				ret = db_oplog_list_batch_add(db_log, db_num);
				if (ret < 0)
				{
					LOGE("db_oplog_list_batch_add failed ret:%d\r\n", ret);
				}
			}
			
			db_num = 0;
			wait_cnt = 0;
		}
		
		usleep(5000);
		wait_cnt++;
	}	
}
