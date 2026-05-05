/** @file
  * @brief C API wrapper for storage module.
  */

#ifndef STORAGE_API_H_
#define STORAGE_API_H_

#include "StorageCommon.h"

#ifdef __cplusplus
extern "C" {
#endif

int storage_init(void);
int storage_refresh_micro_sd(void);
int storage_get_micro_sd_info(MicroSdInfo* info);
int storage_save_micro_sd_info_to_file(const char* path);
int storage_safe_eject_micro_sd(void);
int storage_format_micro_sd(void);
int storage_resolve_write_path(const StorageWriteRequest* request, StorageResolvedPath* out);
int storage_begin_write(const StorageResolvedPath* resolved, StorageWriteToken* token);
int storage_commit_write(const StorageWriteToken* token);
int storage_abort_write(const StorageWriteToken* token);
void storage_cancel_media_writes(StorageMedia media);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_API_H_ */
