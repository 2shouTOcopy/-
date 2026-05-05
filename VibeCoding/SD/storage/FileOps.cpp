#include "FileOps.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/mount.h>
#else
#include <sys/statfs.h>
#endif

namespace storage {
namespace {

std::string parentDir(const std::string& path)
{
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return pos == 0 ? "/" : "";
    }
    return path.substr(0, pos);
}

StorageErrorCode writeAll(int fd, const void* data, size_t size)
{
    const char* cursor = static_cast<const char*>(data);
    size_t left = size;
    while (left > 0) {
        ssize_t written = write(fd, cursor, left);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return STORAGE_E_WRITE_FAILED;
        }
        if (written == 0) {
            return STORAGE_E_WRITE_FAILED;
        }
        cursor += written;
        left -= static_cast<size_t>(written);
    }
    return STORAGE_OK;
}

} // namespace

bool FileOps::pathExists(const std::string& path)
{
    return access(path.c_str(), F_OK) == 0;
}

bool FileOps::isDirectory(const std::string& path)
{
    struct stat st = {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

StorageErrorCode FileOps::mkdirs(const std::string& path)
{
    if (path.empty()) {
        return STORAGE_E_INVALID_PARAM;
    }

    std::string current;
    size_t start = 0;
    if (path[0] == '/') {
        current = "/";
        start = 1;
    }

    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const std::string part = path.substr(start, slash - start);
        if (!part.empty()) {
            if (!current.empty() && current[current.size() - 1] != '/') {
                current += "/";
            }
            current += part;
            if (mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) {
                return STORAGE_E_WRITE_FAILED;
            }
            if (!isDirectory(current)) {
                return STORAGE_E_INVALID_PATH;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return STORAGE_OK;
}

StorageErrorCode FileOps::statFs(const std::string& root, uint64_t* available_size,
                                 uint64_t* total_size)
{
    if (root.empty() || available_size == NULL || total_size == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    struct statfs st = {};
    if (statfs(root.c_str(), &st) != 0) {
        return STORAGE_E_NOT_FOUND;
    }

    *available_size = static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_bsize);
    *total_size = static_cast<uint64_t>(st.f_blocks) * static_cast<uint64_t>(st.f_bsize);
    return STORAGE_OK;
}

StorageErrorCode FileOps::syncPath(const std::string& path)
{
    if (path.empty()) {
        return STORAGE_E_INVALID_PARAM;
    }

    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        sync();
        return STORAGE_OK;
    }

    const int ret = fsync(fd);
    close(fd);
    return ret == 0 ? STORAGE_OK : STORAGE_E_WRITE_FAILED;
}

StorageErrorCode FileOps::removePath(const std::string& path)
{
    if (path.empty()) {
        return STORAGE_E_INVALID_PARAM;
    }
    return remove(path.c_str()) == 0 ? STORAGE_OK : STORAGE_E_WRITE_FAILED;
}

StorageErrorCode FileOps::writeFileAtomic(const std::string& path, const void* data,
                                          size_t size, int overwrite)
{
    (void)overwrite;

    if (path.empty() || (data == NULL && size != 0)) {
        return STORAGE_E_INVALID_PARAM;
    }
    if (pathExists(path)) {
        return STORAGE_E_ALREADY_EXISTS;
    }

    const std::string dir = parentDir(path);
    StorageErrorCode ec = mkdirs(dir);
    if (ec != STORAGE_OK) {
        return ec;
    }

    char tmp_path[STORAGE_MAX_PATH_LEN + 64] = {};
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld.%u", path.c_str(),
             static_cast<long>(getpid()), static_cast<unsigned>(rand()));

    int fd = open(tmp_path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd < 0) {
        return STORAGE_E_WRITE_FAILED;
    }

    ec = writeAll(fd, data, size);
    if (ec == STORAGE_OK && fsync(fd) != 0) {
        ec = STORAGE_E_WRITE_FAILED;
    }
    close(fd);

    if (ec != STORAGE_OK) {
        unlink(tmp_path);
        return ec;
    }

    if (rename(tmp_path, path.c_str()) != 0) {
        unlink(tmp_path);
        return STORAGE_E_WRITE_FAILED;
    }
    return STORAGE_OK;
}

} // namespace storage
