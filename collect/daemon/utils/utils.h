#ifndef UTILS_H
#define UTILS_H

#include "../ModuleDef.h"

#ifdef __cplusplus
  extern "C" {
#endif

#define STATIC_ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))

#define sfree(ptr)                                                             \
	do {																		 \
	  free(ptr);																 \
	  (ptr) = NULL; 															 \
	} while (0)

#define IS_TRUE(s)                                                             \
  ((strcasecmp("true", (s)) == 0) || (strcasecmp("yes", (s)) == 0) ||          \
   (strcasecmp("on", (s)) == 0))
#define IS_FALSE(s)                                                            \
  ((strcasecmp("false", (s)) == 0) || (strcasecmp("no", (s)) == 0) ||          \
   (strcasecmp("off", (s)) == 0))

struct rate_to_value_state_s
{
	value_t last_value;
	cdtime_t last_time;
	gauge_t residual;
};
typedef struct rate_to_value_state_s rate_to_value_state_t;

struct value_to_rate_state_s
{
	value_t last_value;
	cdtime_t last_time;
};

typedef struct value_to_rate_state_s value_to_rate_state_t;

int format_name(char *ret, int ret_len,
                const char *plugin, const char *plugin_instance,
                const char *type, const char *type_instance);
#define FORMAT_VL(ret, ret_len, vl)                                            \
  format_name(ret, ret_len, (vl)->plugin, (vl)->plugin_instance,   \
              (vl)->type, (vl)->type_instance)
int format_values(char *ret, size_t ret_len, const data_set_t *ds,
                  const value_list_t *vl, bool store_rates);

void *smalloc(size_t size);

char *sstrdup(const char *s);

size_t sstrnlen(const char *s, size_t n);

char *sstrndup(const char *s, size_t n);

char *sstrncpy(char *dest, const char *src, size_t n);

int escape_string(char *buffer, size_t buffer_size);

int check_create_dir(const char *file_orig);

int get_pid_by_name(const char *task_name, int *pid, int *pid_array_len);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */

