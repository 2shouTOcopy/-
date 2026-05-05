/** @file
  * @brief Write token guard for removable storage.
  */

#ifndef STORAGE_WRITE_GUARD_H_
#define STORAGE_WRITE_GUARD_H_

#include "MicroSdManager.h"
#include "StorageCommon.h"

#include <map>
#include <mutex>

namespace storage {

class WriteGuard {
public:
    explicit WriteGuard(MicroSdManager* micro_sd_manager);

    StorageErrorCode beginWrite(const StorageResolvedPath& resolved, StorageWriteToken* token);
    StorageErrorCode commitWrite(const StorageWriteToken& token);
    StorageErrorCode abortWrite(const StorageWriteToken& token);
    void cancelMediaWrites(StorageMedia media);

private:
    struct TokenState {
        StorageMedia media;
        uint32_t generation;
        uint64_t required_size;
        bool cancelled;
    };

    MicroSdManager* m_micro_sd_manager;
    std::mutex m_mutex;
    uint32_t m_next_token_id;
    std::map<uint32_t, TokenState> m_tokens;
};

} // namespace storage

#endif /* STORAGE_WRITE_GUARD_H_ */
