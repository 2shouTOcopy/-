/** @file
  * @brief C adapter for project storage paths.
  */

#ifndef PROJECT_STORAGE_ADAPTER_H_
#define PROJECT_STORAGE_ADAPTER_H_

#include "StorageCommon.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_PROJECT_NAME_LEN 128

typedef enum {
    STORAGE_PROJECT_INDEX_VALID = 0,
    STORAGE_PROJECT_INDEX_INVALID = 1,
    STORAGE_PROJECT_CONFLICT_HIDDEN = 2
} StorageProjectIndexStatus;

typedef struct {
    StorageMedia media;
    StorageProjectIndexStatus index_status;
    char name[STORAGE_PROJECT_NAME_LEN];
    char package_path[STORAGE_MAX_PATH_LEN];
    char workdir_path[STORAGE_MAX_PATH_LEN];
    char base_image_path[STORAGE_MAX_PATH_LEN];
} StorageProjectRecord;

typedef struct {
    StorageMedia media;
    uint32_t generation;
    char package_path[STORAGE_MAX_PATH_LEN];
    char workdir_path[STORAGE_MAX_PATH_LEN];
    char base_image_path[STORAGE_MAX_PATH_LEN];
    char index_path[STORAGE_MAX_PATH_LEN];
} StorageProjectSavePaths;

int storage_project_get_package_path(StorageMedia media, const char *project_name,
                                     char *buf, size_t len);
int storage_project_get_package_root_dir_path(StorageMedia media, char *buf, size_t len);
int storage_project_is_emmc_package_root_path(const char *path);
int storage_project_get_external_save_file_path(const char *save_dir,
                                                const char *project_name,
                                                char *buf, size_t len);
int storage_project_get_workdir_root_dir_path(StorageMedia media, char *buf, size_t len);
int storage_project_get_workdir_path(StorageMedia media, const char *project_name,
                                     char *buf, size_t len);
int storage_project_get_workdir_dir_path(StorageMedia media, const char *project_name,
                                         char *buf, size_t len);
int storage_project_get_workdir_child_path(StorageMedia media, const char *project_name,
                                           const char *child_relative_path,
                                           char *buf, size_t len);
int storage_project_get_base_image_path(StorageMedia media, const char *project_name,
                                        char *buf, size_t len);
int storage_project_get_index_path(StorageMedia media, char *buf, size_t len);
int storage_project_resolve_package_write(StorageMedia requested_media, const char *project_name,
                                          uint64_t required_size, StorageResolvedPath *out);
int storage_project_resolve_save_paths(StorageMedia requested_media, const char *project_name,
                                       uint64_t required_size,
                                       StorageProjectSavePaths *out_paths);
int storage_project_resolve_access_paths(StorageMedia requested_media, const char *project_name,
                                         StorageProjectSavePaths *out_paths);
int storage_project_resolve_rename_paths(StorageMedia requested_media,
                                         const char *old_project_name,
                                         const char *new_project_name,
                                         uint64_t required_size,
                                         StorageProjectSavePaths *old_paths,
                                         StorageProjectSavePaths *new_paths);
int storage_project_resolve_copy_paths(StorageMedia requested_media,
                                       const char *src_project_name,
                                       const char *dst_project_name,
                                       uint64_t required_size,
                                       StorageProjectSavePaths *src_paths,
                                       StorageProjectSavePaths *dst_paths);
int storage_project_fill_record(StorageMedia media, const char *project_name,
                                StorageProjectRecord *out_record);
int storage_project_merge_records(const StorageProjectRecord *emmc_records, size_t emmc_count,
                                  const StorageProjectRecord *sd_records, size_t sd_count,
                                  StorageProjectRecord *out_records, size_t max_count,
                                  size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* PROJECT_STORAGE_ADAPTER_H_ */
