#pragma once

#include <stdbool.h>
#include "./utils/utils_time.h"

#define DS_TYPE_COUNTER 0
#define DS_TYPE_GAUGE 1
#define DS_TYPE_DERIVE 2
#define DS_TYPE_ABSOLUTE 3
#define DS_TYPE_UNDEFINED 4

#define DS_TYPE_TO_STRING(t)                                                   \
  (t == DS_TYPE_COUNTER)                                                       \
      ? "counter"                                                              \
      : (t == DS_TYPE_GAUGE)                                                   \
            ? "gauge"                                                          \
            : (t == DS_TYPE_DERIVE)                                            \
                  ? "derive"                                                   \
                  : (t == DS_TYPE_ABSOLUTE) ? "absolute" : "unknown"

#ifndef LOG_ERR
#define LOG_ERR 6
#endif
#ifndef LOG_WARNING
#define LOG_WARNING 5
#endif
#ifndef LOG_INFO
#define LOG_INFO 4
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG 3
#endif

#define NOTIF_MAX_MSG_LEN 256

#define NOTIF_FAILURE 1
#define NOTIF_WARNING 2
#define NOTIF_OKAY 4

#ifndef DATA_MAX_NAME_LEN
#define DATA_MAX_NAME_LEN 128
#endif


/*
 * Public data types
 */
struct identifier_s
{
	char *plugin;
	char *plugin_instance;
	char *type;
	char *type_instance;
};
typedef struct identifier_s identifier_t;

typedef unsigned long long counter_t;
typedef double gauge_t;
typedef int64_t derive_t;
typedef uint64_t absolute_t;

union value_u {
	counter_t counter;
	gauge_t gauge;
	derive_t derive;
	absolute_t absolute;
};
typedef union value_u value_t;

struct value_list_s
{
	value_t *values;
	size_t values_len;
	cdtime_t time;
	cdtime_t interval;
	char plugin[DATA_MAX_NAME_LEN];
	char plugin_instance[DATA_MAX_NAME_LEN];
	char type[DATA_MAX_NAME_LEN];
	char type_instance[DATA_MAX_NAME_LEN];
};
typedef struct value_list_s value_list_t;

#define VALUE_LIST_INIT                                                        \
	{ .values = NULL }

struct MetricDataPoint
{
	const char* type_instance_name;
	gauge_t value;
};

struct data_source_s
{
	char name[DATA_MAX_NAME_LEN];
	int type;
	double min;
	double max;
};
typedef struct data_source_s data_source_t;

struct data_set_s
{
	char type[DATA_MAX_NAME_LEN];
	size_t ds_num;
	data_source_t *ds;
};
typedef struct data_set_s data_set_t;

enum notification_meta_type_e
{
	NM_TYPE_STRING,
	NM_TYPE_SIGNED_INT,
	NM_TYPE_UNSIGNED_INT,
	NM_TYPE_DOUBLE,
	NM_TYPE_BOOLEAN
};

typedef struct notification_meta_s
{
	char name[DATA_MAX_NAME_LEN];
	enum notification_meta_type_e type;
	union
	{
		const char *nm_string;
		int64_t nm_signed_int;
		uint64_t nm_unsigned_int;
		double nm_double;
		bool nm_boolean;
	} nm_value;
	struct notification_meta_s *next;
} notification_meta_t;

typedef struct notification_s
{
	int severity;
	cdtime_t time;
	char message[NOTIF_MAX_MSG_LEN];
	char plugin[DATA_MAX_NAME_LEN];
	char plugin_instance[DATA_MAX_NAME_LEN];
	char type[DATA_MAX_NAME_LEN];
	char type_instance[DATA_MAX_NAME_LEN];
	notification_meta_t *meta;
} notification_t;

struct user_data_s
{
	void *data;
	void (*free_func)(void *);
};
typedef struct user_data_s user_data_t;

enum cache_event_type_e { CE_VALUE_NEW, CE_VALUE_UPDATE, CE_VALUE_EXPIRED };

typedef struct cache_event_s
{
	enum cache_event_type_e type;
	const value_list_t *value_list;
	const char *value_list_name;
	int ret;
} cache_event_t;

struct plugin_ctx_s
{
	char *name;
	cdtime_t interval;
	cdtime_t flush_interval;
	cdtime_t flush_timeout;
};
typedef struct plugin_ctx_s plugin_ctx_t;

#define plugin_log(s, ...)                                                     \
  do {                                                                         \
    printf("[severity %i] ", s);                                               \
    printf(__VA_ARGS__);                                                       \
    printf("\n");                                                              \
  } while (0)


#define ERROR(...) plugin_log(LOG_ERR, __VA_ARGS__)
#define WARNING(...) plugin_log(LOG_WARNING, __VA_ARGS__)
#define INFO(...) plugin_log(LOG_INFO, __VA_ARGS__)
#if COLLECT_DEBUG
#define DEBUG(...) plugin_log(LOG_DEBUG, __VA_ARGS__)
#else              /* COLLECT_DEBUG */
#define DEBUG(...) /* noop */
#endif             /* ! COLLECT_DEBUG */

