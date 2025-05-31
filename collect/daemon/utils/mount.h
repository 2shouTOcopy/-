#ifndef UTILS_MOUNT_H
#define UTILS_MOUNT_H 1

#include <stdio.h>
#include <paths.h>
#include <mntent.h>
#include <sys/statfs.h>
#include <sys/vfs.h>

/* Collectd Utils Mount Type */
#define CUMT_UNKNOWN (0)
#define CUMT_EXT2 (1)
#define CUMT_EXT3 (2)
#define CUMT_XFS (3)
#define CUMT_UFS (4)
#define CUMT_VXFS (5)
#define CUMT_ZFS (6)

#ifdef __cplusplus
  extern "C" {
#endif

typedef struct _cu_mount_t cu_mount_t;
struct _cu_mount_t {
  char *dir;         /* "/sys" or "/" */
  char *spec_device; /* "LABEL=/" or "none" or "proc" or "/dev/hda1" */
  char *device;      /* "none" or "proc" or "/dev/hda1" */
  char *type;        /* "sysfs" or "ext3" */
  char *options;     /* "rw,noatime,commit=600,quota,grpquota" */
  cu_mount_t *next;
};

cu_mount_t *cu_mount_getlist(cu_mount_t **list);

void cu_mount_freelist(cu_mount_t *list);

char *cu_mount_checkoption(char *line, const char *keyword, int full);

char *cu_mount_getoptionvalue(char *line, const char *keyword);

int cu_mount_type(const char *type);

#ifdef __cplusplus
}
#endif

#endif /* !UTILS_MOUNT_H */

