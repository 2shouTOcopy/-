/** @file
  * @brief Business storage path routing.
  */

#ifndef STORAGE_ROUTER_H_
#define STORAGE_ROUTER_H_

#include "MicroSdManager.h"
#include "StorageCommon.h"

#include <string>
#include <vector>

namespace storage {

class StorageRouter {
public:
    explicit StorageRouter(MicroSdManager* micro_sd_manager);

    StorageErrorCode resolveWritePath(const StorageWriteRequest& request,
                                      StorageResolvedPath* out) const;
    StorageErrorCode resolveReadRoots(StorageBusinessType type,
                                      std::vector<StorageResolvedPath>* roots) const;
    std::string getBusinessRoot(StorageMedia media, StorageBusinessType type) const;
    bool isSafeRelativePath(const char* relative_path) const;

private:
    StorageErrorCode resolveWriteTarget(const StorageWriteRequest& request,
                                        StorageMedia* media) const;
    StorageErrorCode fillResolved(StorageMedia media, StorageBusinessType type,
                                  const char* relative_path, uint64_t required_size,
                                  StorageResolvedPath* out) const;
    const char* businessRelativeRoot(StorageBusinessType type) const;

    MicroSdManager* m_micro_sd_manager;
};

} // namespace storage

#endif /* STORAGE_ROUTER_H_ */
