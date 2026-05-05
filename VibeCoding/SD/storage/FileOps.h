/** @file
  * @brief File-system helpers used by storage routing.
  */

#ifndef STORAGE_FILE_OPS_H_
#define STORAGE_FILE_OPS_H_

#include "StorageCommon.h"

#include <string>

namespace storage {

class FileOps {
public:
    static bool pathExists(const std::string& path);
    static bool isDirectory(const std::string& path);
    static StorageErrorCode mkdirs(const std::string& path);
    static StorageErrorCode statFs(const std::string& root, uint64_t* available_size,
                                   uint64_t* total_size);
    static StorageErrorCode syncPath(const std::string& path);
    static StorageErrorCode removePath(const std::string& path);
    static StorageErrorCode writeFileAtomic(const std::string& path, const void* data,
                                            size_t size, int overwrite);
};

} // namespace storage

#endif /* STORAGE_FILE_OPS_H_ */
