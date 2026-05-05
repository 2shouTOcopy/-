#include "WriteGuard.h"

namespace storage {

WriteGuard::WriteGuard(MicroSdManager* micro_sd_manager)
    : m_micro_sd_manager(micro_sd_manager),
      m_next_token_id(1)
{
}

StorageErrorCode WriteGuard::beginWrite(const StorageResolvedPath& resolved,
                                        StorageWriteToken* token)
{
    if (token == NULL || m_micro_sd_manager == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    if (resolved.media == STORAGE_MEDIA_MICROSD) {
        StorageErrorCode ec = m_micro_sd_manager->checkWritable(resolved.required_size);
        if (ec != STORAGE_OK) {
            return ec;
        }
        if (m_micro_sd_manager->getGeneration() != resolved.generation) {
            return STORAGE_E_SD_REMOVED;
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const uint32_t token_id = m_next_token_id++;
    if (m_next_token_id == 0) {
        m_next_token_id = 1;
    }

    TokenState state = {};
    state.media = resolved.media;
    state.generation = resolved.generation;
    state.required_size = resolved.required_size;
    state.cancelled = false;
    m_tokens[token_id] = state;

    token->id = token_id;
    token->media = resolved.media;
    token->generation = resolved.generation;
    token->required_size = resolved.required_size;
    token->active = 1;
    return STORAGE_OK;
}

StorageErrorCode WriteGuard::commitWrite(const StorageWriteToken& token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::map<uint32_t, TokenState>::iterator it = m_tokens.find(token.id);
    if (it == m_tokens.end() || token.active == 0) {
        return STORAGE_E_INVALID_PARAM;
    }

    const TokenState state = it->second;
    m_tokens.erase(it);
    if (state.cancelled) {
        return STORAGE_E_WRITE_CANCELLED;
    }
    if (state.media == STORAGE_MEDIA_MICROSD &&
        m_micro_sd_manager->getGeneration() != state.generation) {
        return STORAGE_E_SD_REMOVED;
    }
    return STORAGE_OK;
}

StorageErrorCode WriteGuard::abortWrite(const StorageWriteToken& token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::map<uint32_t, TokenState>::iterator it = m_tokens.find(token.id);
    if (it == m_tokens.end()) {
        return STORAGE_E_INVALID_PARAM;
    }
    m_tokens.erase(it);
    return STORAGE_OK;
}

void WriteGuard::cancelMediaWrites(StorageMedia media)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (std::map<uint32_t, TokenState>::iterator it = m_tokens.begin();
         it != m_tokens.end(); ++it) {
        if (it->second.media == media) {
            it->second.cancelled = true;
        }
    }
}

} // namespace storage
