#include <mntent.h>
#include <paths.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "utils.h"
#include "mount.h"

#define _GNU_SOURCE

#define P_ERROR(format, ...) fprintf(stderr, "ERROR: " format "\n", ##__VA_ARGS__)
#define P_DEBUG(format, ...) fprintf(stderr, "DEBUG: " format "\n", ##__VA_ARGS__)
//#define P_DEBUG(...)

#if defined(__linux__)
#define COLLECTD_MNTTAB "/proc/mounts"
#else
#define COLLECTD_MNTTAB "/etc/mnttab"
#endif

/* *** *** *** ********************************************* *** *** *** */
/* *** *** *** *** *** ***   private functions   *** *** *** *** *** *** */
/* *** *** *** ********************************************* *** *** *** */

/* stolen from quota-3.13 (quota-tools) */

#define PROC_PARTITIONS "/proc/partitions"
#define DEVLABELDIR "/dev"
#define UUID 1
#define VOL 2

static struct uuidCache_s {
  struct uuidCache_s *next;
  char uuid[16];
  char *label;
  char *device;
} *uuidCache = NULL;

#define EXT2_SUPER_MAGIC 0xEF53
struct ext2_super_block {
  unsigned char s_dummy1[56];
  unsigned char s_magic[2];
  unsigned char s_dummy2[46];
  unsigned char s_uuid[16];
  char s_volume_name[16];
};
#define ext2magic(s) ((unsigned int)s.s_magic[0] + (((unsigned int)s.s_magic[1]) << 8))

#define REISER_SUPER_MAGIC "ReIsEr2Fs"
struct reiserfs_super_block {
  unsigned char s_dummy1[52];
  unsigned char s_magic[10];
  unsigned char s_dummy2[22];
  unsigned char s_uuid[16];
  char s_volume_name[16];
};

static int get_label_uuid(const char *device, char **label, char *uuid) {
  int fd, rv = 1;
  size_t namesize;
  struct ext2_super_block e2sb;
  struct reiserfs_super_block reisersb;

  fd = open(device, O_RDONLY);
  if (fd == -1) {
    return rv;
  }

  if (lseek(fd, 1024, SEEK_SET) == 1024 &&
      read(fd, (char *)&e2sb, sizeof(e2sb)) == sizeof(e2sb) &&
      ext2magic(e2sb) == EXT2_SUPER_MAGIC) {
    memcpy(uuid, e2sb.s_uuid, sizeof(e2sb.s_uuid));
    namesize = sizeof(e2sb.s_volume_name);
    *label = smalloc(namesize + 1);
    sstrncpy(*label, e2sb.s_volume_name, namesize);
    rv = 0;
  } else if (lseek(fd, 65536, SEEK_SET) == 65536 &&
             read(fd, (char *)&reisersb, sizeof(reisersb)) == sizeof(reisersb) &&
             !strncmp((char *)&reisersb.s_magic, REISER_SUPER_MAGIC, 9)) {
    memcpy(uuid, reisersb.s_uuid, sizeof(reisersb.s_uuid));
    namesize = sizeof(reisersb.s_volume_name);
    *label = smalloc(namesize + 1);
    sstrncpy(*label, reisersb.s_volume_name, namesize);
    rv = 0;
  }
  close(fd);
  return rv;
}

static void uuidcache_addentry(char *device, char *label, char *uuid) {
  struct uuidCache_s *last;

  if (!uuidCache) {
    last = uuidCache = smalloc(sizeof(*uuidCache));
  } else {
    for (last = uuidCache; last->next; last = last->next)
      ;
    last->next = smalloc(sizeof(*uuidCache));
    last = last->next;
  }
  last->next = NULL;
  last->device = device;
  last->label = label;
  memcpy(last->uuid, uuid, sizeof(last->uuid));
}

static void uuidcache_init(void) {
  char line[100];
  char *s;
  int ma, mi, sz;
  static char ptname[100];
  FILE *procpt;
  char uuid[16], *label = NULL;
  char device[110];
  int handleOnFirst;

  if (uuidCache) {
    return;
  }

  procpt = fopen(PROC_PARTITIONS, "r");
  if (procpt == NULL) {
    return;
  }

  for (int firstPass = 1; firstPass >= 0; firstPass--) {
    fseek(procpt, 0, SEEK_SET);
    while (fgets(line, sizeof(line), procpt)) {
      if (sscanf(line, " %d %d %d %[^\n ]", &ma, &mi, &sz, ptname) != 4) {
        continue;
      }

      if (sz == 1) {
        continue;
      }

      handleOnFirst = !strncmp(ptname, "md", 2);
      if (firstPass != handleOnFirst) {
        continue;
      }

      for (s = ptname; *s; s++)
        ;

      if (isdigit((int)s[-1])) {
        snprintf(device, sizeof(device), "%s/%s", DEVLABELDIR, ptname);
        if (!get_label_uuid(device, &label, uuid)) {
          uuidcache_addentry(sstrdup(device), label, uuid);
        }
      }
    }
  }
  fclose(procpt);
}

static unsigned char fromhex(char c) {
  if (isdigit((int)c)) {
    return c - '0';
  } else if (islower((int)c)) {
    return c - 'a' + 10;
  } else {
    return c - 'A' + 10;
  }
}

static char *get_spec_by_x(int n, const char *t) {
  struct uuidCache_s *uc;

  uuidcache_init();
  uc = uuidCache;

  while (uc) {
    switch (n) {
    case UUID:
      if (!memcmp(t, uc->uuid, sizeof(uc->uuid))) {
        return sstrdup(uc->device);
      }
      break;
    case VOL:
      if (!strcmp(t, uc->label)) {
        return sstrdup(uc->device);
      }
      break;
    }
    uc = uc->next;
  }
  return NULL;
}

static char *get_spec_by_uuid(const char *s) {
  char uuid[16];

  if (strlen(s) != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' ||
      s[23] != '-') {
    goto bad_uuid;
  }

  for (int i = 0; i < 16; i++) {
    if (*s == '-') {
      s++;
    }
    if (!isxdigit((int)s[0]) || !isxdigit((int)s[1])) {
      goto bad_uuid;
    }
    uuid[i] = ((fromhex(s[0]) << 4) | fromhex(s[1]));
    s += 2;
  }
  return get_spec_by_x(UUID, uuid);

bad_uuid:
  P_DEBUG("utils_mount: Found an invalid UUID: %s", s);
  return NULL;
}

static char *get_spec_by_volume_label(const char *s) {
  return get_spec_by_x(VOL, s);
}

static char *get_device_name(const char *optstr) {
  char *rc;

  if (optstr == NULL) {
    return NULL;
  } else if (strncmp(optstr, "UUID=", 5) == 0) {
    P_DEBUG("utils_mount: TODO: check UUID= code!");
    rc = get_spec_by_uuid(optstr + 5);
  } else if (strncmp(optstr, "LABEL=", 6) == 0) {
    P_DEBUG("utils_mount: TODO: check LABEL= code!");
    rc = get_spec_by_volume_label(optstr + 6);
  } else {
    rc = sstrdup(optstr);
  }

  if (!rc) {
    P_DEBUG("utils_mount: Error checking device name: optstr = %s", optstr);
  }
  return rc;
}

static cu_mount_t *cu_mount_getmntent(void) {
  FILE *fp;
  struct mntent *me;

  cu_mount_t *first = NULL;
  cu_mount_t *last = NULL;
  cu_mount_t *new = NULL;

  P_DEBUG("utils_mount: (void); COLLECTD_MNTTAB = %s", COLLECTD_MNTTAB);

  if ((fp = setmntent(COLLECTD_MNTTAB, "r")) == NULL) {
    P_ERROR("setmntent (%s): %s", COLLECTD_MNTTAB, strerror(errno));
    return NULL;
  }

  while ((me = getmntent(fp)) != NULL) {
    if ((new = calloc(1, sizeof(*new))) == NULL)
      break;

    new->dir = sstrdup(me->mnt_dir);
    new->spec_device = sstrdup(me->mnt_fsname);
    new->type = sstrdup(me->mnt_type);
    new->options = sstrdup(me->mnt_opts);
    new->device = get_device_name(new->options);
    new->next = NULL;

    P_DEBUG("utils_mount: new = {dir = %s, spec_device = %s, type = %s, options "
          "= %s, device = %s}",
          new->dir, new->spec_device, new->type, new->options, new->device);

    if (first == NULL) {
      first = new;
      last = new;
    } else {
      last->next = new;
      last = new;
    }
  }

  endmntent(fp);

  P_DEBUG("utils_mount: return 0x%p", (void *)first);

  return first;
}

/* *** *** *** ******************************************** *** *** *** */
/* *** *** *** *** *** ***   public functions   *** *** *** *** *** *** */
/* *** *** *** ******************************************** *** *** *** */

cu_mount_t *cu_mount_getlist(cu_mount_t **list) {
  cu_mount_t *new;
  cu_mount_t *first = NULL;
  cu_mount_t *last = NULL;

  if (list == NULL)
    return NULL;

  if (*list != NULL) {
    first = *list;
    last = first;
    while (last->next != NULL)
      last = last->next;
  }

  new = cu_mount_getmntent();

  if (first != NULL) {
    last->next = new;
  } else {
    first = new;
    last = new;
    *list = first;
  }

  while ((last != NULL) && (last->next != NULL))
    last = last->next;

  return last;
}

void cu_mount_freelist(cu_mount_t *list) {
  cu_mount_t *next;

  for (cu_mount_t *this = list; this != NULL; this = next) {
    next = this->next;

    sfree(this->dir);
    sfree(this->spec_device);
    sfree(this->device);
    sfree(this->type);
    sfree(this->options);
    sfree(this);
  }
}

char *cu_mount_checkoption(char *line, const char *keyword, int full) {
  char *line2, *l2, *p1, *p2;
  size_t l;

  if (line == NULL || keyword == NULL) {
    return NULL;
  }

  if (full != 0) {
    full = 1;
  }

  line2 = sstrdup(line);
  l2 = line2;
  while (*l2 != '\0') {
    if (*l2 == ',') {
      *l2 = '\0';
    }
    l2++;
  }

  l = strlen(keyword);
  p1 = line - 1;
  p2 = strchr(line, ',');
  do {
    if (strncmp(line2 + (p1 - line) + 1, keyword, l + full) == 0) {
      free(line2);
      return p1 + 1;
    }
    p1 = p2;
    if (p1 != NULL) {
      p2 = strchr(p1 + 1, ',');
    }
  } while (p1 != NULL);

  free(line2);
  return NULL;
}

char *cu_mount_getoptionvalue(char *line, const char *keyword) {
  char *r;

  r = cu_mount_checkoption(line, keyword, 0);
  if (r != NULL) {
    char *p;
    r += strlen(keyword);
    p = strchr(r, ',');
    if (p == NULL) {
      return sstrdup(r);
    } else {
      char *m;
      if ((p - r) == 1) {
        return NULL;
      }
      m = smalloc(p - r + 1);
      sstrncpy(m, r, p - r + 1);
      return m;
    }
  }
  return r;
}

int cu_mount_type(const char *type) {
  if (strcmp(type, "ext3") == 0)
    return CUMT_EXT3;
  if (strcmp(type, "ext2") == 0)
    return CUMT_EXT2;
  if (strcmp(type, "ufs") == 0)
    return CUMT_UFS;
  if (strcmp(type, "vxfs") == 0)
    return CUMT_VXFS;
  if (strcmp(type, "zfs") == 0)
    return CUMT_ZFS;
  return CUMT_UNKNOWN;
}

