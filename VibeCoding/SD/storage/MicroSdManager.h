/** @file
  * @brief microSD state and platform operation manager.
  */

#ifndef STORAGE_MICRO_SD_MANAGER_H_
#define STORAGE_MICRO_SD_MANAGER_H_

#include "StorageCommon.h"

#include <mutex>
#include <string>

namespace storage {

class MicroSdManager {
public:
    MicroSdManager();

    static MicroSdManager* getInstance();

    StorageErrorCode init();
    StorageErrorCode refreshState();
    StorageErrorCode onCardInserted();
    StorageErrorCode onCardRemoved(bool safe_ejected);
    StorageErrorCode safeEject();
    StorageErrorCode formatMicroSd();

    MicroSdInfo getInfo() const;
    MicroSdState getState() const;
    uint32_t getGeneration() const;
    bool isMicroSdOnline() const;
    bool isMicroSdWritable() const;
    StorageErrorCode checkWritable(uint64_t required_size) const;

    void setPlatformConfig(const char* device_node, const char* mount_root);
    void setStateForTest(MicroSdState state, uint64_t available_size, uint64_t total_size,
                         bool writable, uint32_t generation);

private:
    bool isDevicePresentLocked() const;
    bool isMountedLocked() const;
    bool checkWritableLocked() const;
    StorageErrorCode mountLocked();
    StorageErrorCode unmountLocked();
    StorageErrorCode updateCapacityLocked();
    StorageErrorCode checkFat32Locked() const;
    StorageErrorCode checkClusterSizeLocked() const;
    int runCommandLocked(const std::string& command) const;
    std::string readCommandFirstLineLocked(const std::string& command) const;
    void bumpGenerationLocked();

    mutable std::mutex m_mutex;
    MicroSdInfo m_info;
};

} // namespace storage

#endif /* STORAGE_MICRO_SD_MANAGER_H_ */
