#ifndef UTILS_TIME_H
#define UTILS_TIME_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/time.h>
#include <time.h>

/*
 * "cdtime_t" is a 64bit unsigned integer. The time is stored at a 2^-30 second
 * resolution, i.e. the most significant 34 bit are used to store the time in
 * seconds, the least significant bits store the sub-second part in something
 * very close to nanoseconds.
 */
typedef uint64_t cdtime_t;

/* Conversion macros */
#define TIME_T_TO_CDTIME_T(t) (((cdtime_t)(t)) << 30)
#define MS_TO_CDTIME_T(ms) ((((cdtime_t)(ms)) / 1000) << 30) | \
                          (((((cdtime_t)(ms)) % 1000) << 30) + 500) / 1000
#define US_TO_CDTIME_T(us) ((((cdtime_t)(us)) / 1000000) << 30) | \
                          (((((cdtime_t)(us)) % 1000000) << 30) + 500000) / 1000000
#define NS_TO_CDTIME_T(ns) ((((cdtime_t)(ns)) / 1000000000) << 30) | \
                          (((((cdtime_t)(ns)) % 1000000000) << 30) + 500000000) / 1000000000

#define CDTIME_T_TO_TIME_T(t) ((time_t)(((t) + (1 << 29)) >> 30))
#define CDTIME_T_TO_MS(t) ((((t) >> 30) * 1000) + \
                          ((((t)&0x3fffffff) * 1000 + (1 << 29)) >> 30))
#define CDTIME_T_TO_US(t) ((((t) >> 30) * 1000000) + \
                          ((((t)&0x3fffffff) * 1000000 + (1 << 29)) >> 30))
#define CDTIME_T_TO_NS(t) ((((t) >> 30) * 1000000000) + \
                          ((((t)&0x3fffffff) * 1000000000 + (1 << 29)) >> 30))

#define CDTIME_T_TO_DOUBLE(t) ((double)(t) / 1073741824.0)
#define DOUBLE_TO_CDTIME_T(d) ((cdtime_t)((d)*1073741824.0))

/* Timeval/timespec conversions */
#define CDTIME_T_TO_TIMEVAL(t) (struct timeval){ \
    .tv_sec = (time_t)((t) >> 30), \
    .tv_usec = (suseconds_t)((((t)&0x3fffffff) * 1000000 + (1 << 29)) >> 30) \
}
#define TIMEVAL_TO_CDTIME_T(tv) US_TO_CDTIME_T(1000000 * (tv)->tv_sec + (tv)->tv_usec)

#define CDTIME_T_TO_TIMESPEC(t) (struct timespec){ \
    .tv_sec = (time_t)((t) >> 30), \
    .tv_nsec = (long)((((t)&0x3fffffff) * 1000000000 + (1 << 29)) >> 30) \
}
#define TIMESPEC_TO_CDTIME_T(ts) NS_TO_CDTIME_T(1000000000ULL * (ts)->tv_sec + (ts)->tv_nsec)

/* Function prototypes */
cdtime_t cdtime(void);

#define RFC3339_SIZE 26     /* 2006-01-02T15:04:05+00:00 */
#define RFC3339NANO_SIZE 36 /* 2006-01-02T15:04:05.999999999+00:00 */

int rfc3339(char *buffer, size_t buffer_size, cdtime_t t);
int rfc3339nano(char *buffer, size_t buffer_size, cdtime_t t);
int rfc3339_local(char *buffer, size_t buffer_size, cdtime_t t);
int rfc3339nano_local(char *buffer, size_t buffer_size, cdtime_t t);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_TIME_H */
