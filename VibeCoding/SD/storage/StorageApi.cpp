#include "StorageApi.h"

#include "MicroSdManager.h"
#include "FileOps.h"
#include "StorageRouter.h"
#include "WriteGuard.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {

storage::StorageRouter g_router(storage::MicroSdManager::getInstance());
storage::WriteGuard g_write_guard(storage::MicroSdManager::getInstance());

int writeTextFileReplace(const char* path, const char* data, size_t size)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd < 0) {
        return STORAGE_E_WRITE_FAILED;
    }

    const char* cursor = data;
    size_t left = size;
    while (left > 0) {
        ssize_t written = write(fd, cursor, left);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return STORAGE_E_WRITE_FAILED;
        }
        if (written == 0) {
            close(fd);
            return STORAGE_E_WRITE_FAILED;
        }
        cursor += written;
        left -= static_cast<size_t>(written);
    }

    if (fsync(fd) != 0) {
        close(fd);
        return STORAGE_E_WRITE_FAILED;
    }
    close(fd);
    return STORAGE_OK;
}

} // namespace

int storage_init(void)
{
    return storage::MicroSdManager::getInstance()->init();
}

int storage_refresh_micro_sd(void)
{
    return storage::MicroSdManager::getInstance()->refreshState();
}

int storage_get_micro_sd_info(MicroSdInfo* info)
{
    if (info == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }
    *info = storage::MicroSdManager::getInstance()->getInfo();
    return STORAGE_OK;
}

int storage_save_micro_sd_info_to_file(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return STORAGE_E_INVALID_PARAM;
    }

    MicroSdInfo info = storage::MicroSdManager::getInstance()->getInfo();
    char json[1024] = {};
    const int written = std::snprintf(
        json,
        sizeof(json),
        "{\n"
        "  \"State\":\"%s\",\n"
        "  \"DeviceNode\":\"%s\",\n"
        "  \"MountRoot\":\"%s\",\n"
        "  \"FsType\":\"%s\",\n"
        "  \"AvailableSize\":%llu,\n"
        "  \"TotalSize\":%llu,\n"
        "  \"Generation\":%u,\n"
        "  \"Inserted\":%s,\n"
        "  \"Mounted\":%s,\n"
        "  \"Writable\":%s\n"
        "}\n",
        storage_sd_state_to_string(info.state),
        info.device_node,
        info.mount_root,
        info.fs_type,
        static_cast<unsigned long long>(info.available_size),
        static_cast<unsigned long long>(info.total_size),
        info.generation,
        info.inserted ? "true" : "false",
        info.mounted ? "true" : "false",
        info.writable ? "true" : "false");
    if (written < 0 || static_cast<size_t>(written) >= sizeof(json)) {
        return STORAGE_E_WRITE_FAILED;
    }

    return writeTextFileReplace(path, json, static_cast<size_t>(written));
}

int storage_safe_eject_micro_sd(void)
{
    g_write_guard.cancelMediaWrites(STORAGE_MEDIA_MICROSD);
    return storage::MicroSdManager::getInstance()->safeEject();
}

int storage_format_micro_sd(void)
{
    return storage::MicroSdManager::getInstance()->formatMicroSd();
}

int storage_resolve_write_path(const StorageWriteRequest* request, StorageResolvedPath* out)
{
    if (request == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }
    return g_router.resolveWritePath(*request, out);
}

int storage_begin_write(const StorageResolvedPath* resolved, StorageWriteToken* token)
{
    if (resolved == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }
    return g_write_guard.beginWrite(*resolved, token);
}

int storage_commit_write(const StorageWriteToken* token)
{
    if (token == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }
    return g_write_guard.commitWrite(*token);
}

int storage_abort_write(const StorageWriteToken* token)
{
    if (token == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }
    return g_write_guard.abortWrite(*token);
}

void storage_cancel_media_writes(StorageMedia media)
{
    g_write_guard.cancelMediaWrites(media);
}
