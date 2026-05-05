/** @file
  * @brief Common definitions for business storage routing.
  */

#ifndef STORAGE_COMMON_H_
#define STORAGE_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STORAGE_EMMC_ROOT
#define STORAGE_EMMC_ROOT "/mnt/data"
#endif

#ifndef STORAGE_MICROSD_DEVICE
#define STORAGE_MICROSD_DEVICE "/dev/mmcblk1p10"
#endif

#ifndef STORAGE_MICROSD_ROOT
#define STORAGE_MICROSD_ROOT "/mnt/sdcard"
#endif

#define STORAGE_MAX_PATH_LEN 512
#define STORAGE_MAX_REL_PATH_LEN 256
#define STORAGE_MAX_FS_TYPE_LEN 32

typedef enum {
    STORAGE_OK = 0,
    STORAGE_E_INVALID_PARAM = -1,
    STORAGE_E_INVALID_PATH = -2,
    STORAGE_E_SD_NOT_INSERTED = -10,
    STORAGE_E_SD_UNSUPPORTED_FORMAT = -11,
    STORAGE_E_SD_MOUNT_FAILED = -12,
    STORAGE_E_SD_READONLY = -13,
    STORAGE_E_NO_SPACE = -14,
    STORAGE_E_SD_REMOVED = -15,
    STORAGE_E_WRITE_FAILED = -16,
    STORAGE_E_FORMAT_FAILED = -17,
    STORAGE_E_WRITE_CANCELLED = -18,
    STORAGE_E_NOT_FOUND = -19,
    STORAGE_E_ALREADY_EXISTS = -20,
} StorageErrorCode;

typedef enum {
    STORAGE_MEDIA_EMMC = 0,
    STORAGE_MEDIA_MICROSD = 1,
    STORAGE_MEDIA_AUTO = 2
} StorageMedia;

typedef enum {
    STORAGE_BIZ_PROJECT_PACKAGE = 0,
    STORAGE_BIZ_PROJECT_WORKDIR,
    STORAGE_BIZ_SAVE_IMAGE,
    STORAGE_BIZ_TEST_IMAGE,
    STORAGE_BIZ_TRAIN_IMAGE,
    STORAGE_BIZ_PROJECT_INDEX,
    STORAGE_BIZ_IMAGE_INDEX,
    STORAGE_BIZ_TEST_IMAGE_INDEX
} StorageBusinessType;

typedef enum {
    SD_STATE_NOT_INSERTED = 0,
    SD_STATE_MOUNTING,
    SD_STATE_ONLINE,
    SD_STATE_ONLINE_READONLY,
    SD_STATE_UNSUPPORTED_FORMAT,
    SD_STATE_SPACE_LOW,
    SD_STATE_EJECTING,
    SD_STATE_EJECTED,
    SD_STATE_ABNORMAL_REMOVED,
    SD_STATE_MOUNT_FAILED,
    SD_STATE_FORMATTING
} MicroSdState;

typedef struct {
    StorageMedia media;
    StorageBusinessType type;
    char relative_path[STORAGE_MAX_REL_PATH_LEN];
    uint64_t required_size;
    int overwrite;
} StorageWriteRequest;

typedef struct {
    StorageMedia media;
    StorageBusinessType type;
    MicroSdState sd_state;
    char abs_path[STORAGE_MAX_PATH_LEN];
    uint64_t available_size;
    uint64_t total_size;
    uint64_t required_size;
    uint32_t generation;
} StorageResolvedPath;

typedef struct {
    MicroSdState state;
    char device_node[STORAGE_MAX_PATH_LEN];
    char mount_root[STORAGE_MAX_PATH_LEN];
    char fs_type[STORAGE_MAX_FS_TYPE_LEN];
    uint64_t available_size;
    uint64_t total_size;
    uint32_t generation;
    int inserted;
    int mounted;
    int writable;
} MicroSdInfo;

typedef struct {
    uint32_t id;
    StorageMedia media;
    uint32_t generation;
    uint64_t required_size;
    int active;
} StorageWriteToken;

const char *storage_error_to_string(StorageErrorCode err);
const char *storage_media_to_string(StorageMedia media);
const char *storage_sd_state_to_string(MicroSdState state);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_COMMON_H_ */
