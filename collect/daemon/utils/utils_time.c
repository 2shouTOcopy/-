#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "utils_time.h"

cdtime_t cdtime(void)
{
	struct timespec ts = {0, 0};

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
	{
		return 0;
	}

	return TIMESPEC_TO_CDTIME_T(&ts);
}

/* 内部辅助函数 */
static int get_utc_time(cdtime_t t, struct tm *t_tm, long *nsec)
{
  struct timespec t_spec = CDTIME_T_TO_TIMESPEC(t);
  
  if (gmtime_r(&t_spec.tv_sec, t_tm) == NULL) {
    return errno;
  }
  
  *nsec = t_spec.tv_nsec;
  return 0;
}

static int get_local_time(cdtime_t t, struct tm *t_tm, long *nsec)
{
  struct timespec t_spec = CDTIME_T_TO_TIMESPEC(t);
  
  if (localtime_r(&t_spec.tv_sec, t_tm) == NULL) {
    return errno;
  }
  
  *nsec = t_spec.tv_nsec;
  return 0;
}

static int format_zone(char *buffer, size_t buffer_size, const struct tm *tm)
{
  if (buffer_size < 7) return EINVAL;
  
  char tmp[6];
  if (strftime(tmp, sizeof(tmp), "%z", tm) == 0) {
    return ENOMEM;
  }
  
  snprintf(buffer, buffer_size, "%.3s:%.2s", tmp, tmp+3);
  return 0;
}

static int format_rfc3339(char *buffer, size_t buffer_size, 
                         const struct tm *t_tm, long nsec, 
                         bool print_nano, const char *zone)
{
  size_t len = strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", t_tm);
  if (len == 0) return ENOMEM;
  
  char *pos = buffer + len;
  size_t remaining = buffer_size - len;
  
  if (print_nano) {
    len = snprintf(pos, remaining, ".%09ld", nsec);
    if (len >= remaining) return ENOMEM;
    pos += len;
    remaining -= len;
  }
  
  strncpy(pos, zone, remaining);
  return 0;
}

/* 公共接口 */
int rfc3339(char *buffer, size_t buffer_size, cdtime_t t)
{
  if (buffer_size < RFC3339_SIZE) return ENOMEM;
  
  struct tm t_tm;
  long nsec;
  if (get_utc_time(t, &t_tm, &nsec) != 0) {
    return errno;
  }
  
  return format_rfc3339(buffer, buffer_size, &t_tm, nsec, false, "Z");
}

int rfc3339nano(char *buffer, size_t buffer_size, cdtime_t t)
{
  if (buffer_size < RFC3339NANO_SIZE) return ENOMEM;
  
  struct tm t_tm;
  long nsec;
  if (get_utc_time(t, &t_tm, &nsec) != 0) {
    return errno;
  }
  
  return format_rfc3339(buffer, buffer_size, &t_tm, nsec, true, "Z");
}

int rfc3339_local(char *buffer, size_t buffer_size, cdtime_t t)
{
  if (buffer_size < RFC3339_SIZE) return ENOMEM;
  
  struct tm t_tm;
  long nsec;
  char zone[7];
  
  if (get_local_time(t, &t_tm, &nsec) != 0) {
    return errno;
  }
  if (format_zone(zone, sizeof(zone), &t_tm) != 0) {
    return errno;
  }
  
  return format_rfc3339(buffer, buffer_size, &t_tm, nsec, false, zone);
}

int rfc3339nano_local(char *buffer, size_t buffer_size, cdtime_t t)
{
  if (buffer_size < RFC3339NANO_SIZE) return ENOMEM;
  
  struct tm t_tm;
  long nsec;
  char zone[7];
  
  if (get_local_time(t, &t_tm, &nsec) != 0) {
    return errno;
  }
  if (format_zone(zone, sizeof(zone), &t_tm) != 0) {
    return errno;
  }
  
  return format_rfc3339(buffer, buffer_size, &t_tm, nsec, true, zone);
}

