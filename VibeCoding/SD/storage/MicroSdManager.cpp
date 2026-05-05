#include "MicroSdManager.h"

#include "FileOps.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace storage {
namespace {

MicroSdManager g_micro_sd_manager;

void copyString(char* dst, size_t dst_len, const char* src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (src != NULL) {
        std::strncpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
    }
}

} // namespace

MicroSdManager::MicroSdManager()
{
    std::memset(&m_info, 0, sizeof(m_info));
    m_info.state = SD_STATE_NOT_INSERTED;
    copyString(m_info.device_node, sizeof(m_info.device_node), STORAGE_MICROSD_DEVICE);
    copyString(m_info.mount_root, sizeof(m_info.mount_root), STORAGE_MICROSD_ROOT);
    copyString(m_info.fs_type, sizeof(m_info.fs_type), "");
}

MicroSdManager* MicroSdManager::getInstance()
{
    return &g_micro_sd_manager;
}

StorageErrorCode MicroSdManager::init()
{
    StorageErrorCode ec = refreshState();
    return ec == STORAGE_E_SD_NOT_INSERTED ? STORAGE_OK : ec;
}

StorageErrorCode MicroSdManager::refreshState()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!isDevicePresentLocked()) {
        if (m_info.state != SD_STATE_NOT_INSERTED && m_info.state != SD_STATE_EJECTED) {
            bumpGenerationLocked();
        }
        m_info.state = SD_STATE_NOT_INSERTED;
        m_info.inserted = 0;
        m_info.mounted = 0;
        m_info.writable = 0;
        m_info.available_size = 0;
        m_info.total_size = 0;
        return STORAGE_E_SD_NOT_INSERTED;
    }

    m_info.inserted = 1;
    m_info.state = SD_STATE_MOUNTING;

    StorageErrorCode ec = checkFat32Locked();
    if (ec != STORAGE_OK) {
        m_info.state = SD_STATE_UNSUPPORTED_FORMAT;
        bumpGenerationLocked();
        return ec;
    }

    ec = checkClusterSizeLocked();
    if (ec != STORAGE_OK) {
        m_info.state = SD_STATE_UNSUPPORTED_FORMAT;
        bumpGenerationLocked();
        return ec;
    }

    if (!isMountedLocked()) {
        ec = mountLocked();
        if (ec != STORAGE_OK) {
            m_info.state = SD_STATE_MOUNT_FAILED;
            bumpGenerationLocked();
            return ec;
        }
    }

    m_info.mounted = 1;
    updateCapacityLocked();
    m_info.writable = checkWritableLocked() ? 1 : 0;
    if (!m_info.writable) {
        m_info.state = SD_STATE_ONLINE_READONLY;
    } else if (m_info.available_size == 0) {
        m_info.state = SD_STATE_SPACE_LOW;
    } else {
        m_info.state = SD_STATE_ONLINE;
    }
    bumpGenerationLocked();
    return STORAGE_OK;
}

StorageErrorCode MicroSdManager::onCardInserted()
{
    return refreshState();
}

StorageErrorCode MicroSdManager::onCardRemoved(bool safe_ejected)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bumpGenerationLocked();
    m_info.inserted = 0;
    m_info.mounted = 0;
    m_info.writable = 0;
    m_info.available_size = 0;
    m_info.total_size = 0;
    m_info.state = safe_ejected ? SD_STATE_EJECTED : SD_STATE_ABNORMAL_REMOVED;
    return safe_ejected ? STORAGE_OK : STORAGE_E_SD_REMOVED;
}

StorageErrorCode MicroSdManager::safeEject()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_info.inserted && !isDevicePresentLocked()) {
        return STORAGE_E_SD_NOT_INSERTED;
    }

    m_info.state = SD_STATE_EJECTING;
    bumpGenerationLocked();
    FileOps::syncPath(m_info.mount_root);

    StorageErrorCode ec = unmountLocked();
    if (ec != STORAGE_OK) {
        m_info.state = SD_STATE_MOUNT_FAILED;
        return ec;
    }

    m_info.mounted = 0;
    m_info.writable = 0;
    m_info.state = SD_STATE_EJECTED;
    bumpGenerationLocked();
    return STORAGE_OK;
}

StorageErrorCode MicroSdManager::formatMicroSd()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!isDevicePresentLocked()) {
            m_info.state = SD_STATE_NOT_INSERTED;
            return STORAGE_E_SD_NOT_INSERTED;
        }

        m_info.state = SD_STATE_FORMATTING;
        bumpGenerationLocked();
        if (isMountedLocked()) {
            unmountLocked();
        }

        std::ostringstream cmd;
        cmd << "mkfs.vfat -F 32 -s 64 " << m_info.device_node;
        if (runCommandLocked(cmd.str()) != 0) {
            m_info.state = SD_STATE_UNSUPPORTED_FORMAT;
            return STORAGE_E_FORMAT_FAILED;
        }
    }

    return refreshState();
}

MicroSdInfo MicroSdManager::getInfo() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_info;
}

MicroSdState MicroSdManager::getState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_info.state;
}

uint32_t MicroSdManager::getGeneration() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_info.generation;
}

bool MicroSdManager::isMicroSdOnline() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_info.state == SD_STATE_ONLINE || m_info.state == SD_STATE_SPACE_LOW ||
           m_info.state == SD_STATE_ONLINE_READONLY;
}

bool MicroSdManager::isMicroSdWritable() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_info.state == SD_STATE_ONLINE && m_info.writable != 0;
}

StorageErrorCode MicroSdManager::checkWritable(uint64_t required_size) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    switch (m_info.state) {
    case SD_STATE_NOT_INSERTED:
    case SD_STATE_EJECTED:
        return STORAGE_E_SD_NOT_INSERTED;
    case SD_STATE_UNSUPPORTED_FORMAT:
        return STORAGE_E_SD_UNSUPPORTED_FORMAT;
    case SD_STATE_MOUNT_FAILED:
        return STORAGE_E_SD_MOUNT_FAILED;
    case SD_STATE_ABNORMAL_REMOVED:
        return STORAGE_E_SD_REMOVED;
    case SD_STATE_ONLINE_READONLY:
        return STORAGE_E_SD_READONLY;
    case SD_STATE_SPACE_LOW:
        return STORAGE_E_NO_SPACE;
    case SD_STATE_ONLINE:
        if (!m_info.writable) {
            return STORAGE_E_SD_READONLY;
        }
        if (m_info.available_size < required_size) {
            return STORAGE_E_NO_SPACE;
        }
        return STORAGE_OK;
    default:
        return STORAGE_E_SD_MOUNT_FAILED;
    }
}

void MicroSdManager::setPlatformConfig(const char* device_node, const char* mount_root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    copyString(m_info.device_node, sizeof(m_info.device_node),
               device_node != NULL ? device_node : STORAGE_MICROSD_DEVICE);
    copyString(m_info.mount_root, sizeof(m_info.mount_root),
               mount_root != NULL ? mount_root : STORAGE_MICROSD_ROOT);
    bumpGenerationLocked();
}

void MicroSdManager::setStateForTest(MicroSdState state, uint64_t available_size,
                                     uint64_t total_size, bool writable, uint32_t generation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_info.state = state;
    m_info.available_size = available_size;
    m_info.total_size = total_size;
    m_info.writable = writable ? 1 : 0;
    m_info.inserted = (state != SD_STATE_NOT_INSERTED && state != SD_STATE_EJECTED) ? 1 : 0;
    m_info.mounted = (state == SD_STATE_ONLINE || state == SD_STATE_ONLINE_READONLY ||
                      state == SD_STATE_SPACE_LOW) ? 1 : 0;
    m_info.generation = generation;
}

bool MicroSdManager::isDevicePresentLocked() const
{
    return access(m_info.device_node, F_OK) == 0;
}

bool MicroSdManager::isMountedLocked() const
{
    std::ifstream mounts("/proc/mounts");
    std::string device;
    std::string mount_point;
    std::string fs_type;
    while (mounts >> device >> mount_point >> fs_type) {
        if (device == m_info.device_node || mount_point == m_info.mount_root) {
            return mount_point == m_info.mount_root;
        }
    }
    return false;
}

bool MicroSdManager::checkWritableLocked() const
{
    if (!FileOps::isDirectory(m_info.mount_root)) {
        return false;
    }
    return access(m_info.mount_root, W_OK) == 0;
}

StorageErrorCode MicroSdManager::mountLocked()
{
    StorageErrorCode ec = FileOps::mkdirs(m_info.mount_root);
    if (ec != STORAGE_OK) {
        return ec;
    }

    std::ostringstream cmd;
    cmd << "mount -t vfat " << m_info.device_node << " " << m_info.mount_root;
    return runCommandLocked(cmd.str()) == 0 ? STORAGE_OK : STORAGE_E_SD_MOUNT_FAILED;
}

StorageErrorCode MicroSdManager::unmountLocked()
{
    if (!isMountedLocked()) {
        return STORAGE_OK;
    }

    std::ostringstream cmd;
    cmd << "umount " << m_info.mount_root;
    return runCommandLocked(cmd.str()) == 0 ? STORAGE_OK : STORAGE_E_SD_MOUNT_FAILED;
}

StorageErrorCode MicroSdManager::updateCapacityLocked()
{
    uint64_t available = 0;
    uint64_t total = 0;
    StorageErrorCode ec = FileOps::statFs(m_info.mount_root, &available, &total);
    if (ec != STORAGE_OK) {
        m_info.available_size = 0;
        m_info.total_size = 0;
        return ec;
    }
    m_info.available_size = available;
    m_info.total_size = total;
    return STORAGE_OK;
}

StorageErrorCode MicroSdManager::checkFat32Locked() const
{
    std::ostringstream cmd;
    cmd << "blkid -o value -s TYPE " << m_info.device_node << " 2>/dev/null";
    const std::string fs_type = readCommandFirstLineLocked(cmd.str());
    if (fs_type.empty()) {
        return STORAGE_OK;
    }
    if (fs_type == "vfat" || fs_type == "fat32" || fs_type == "FAT32") {
        return STORAGE_OK;
    }
    return STORAGE_E_SD_UNSUPPORTED_FORMAT;
}

StorageErrorCode MicroSdManager::checkClusterSizeLocked() const
{
    std::ostringstream cmd;
    cmd << "fsck.vfat -vn " << m_info.device_node << " 2>/dev/null";
    FILE* fp = popen(cmd.str().c_str(), "r");
    if (fp == NULL) {
        return STORAGE_OK;
    }

    char line[256] = {};
    bool saw_cluster_line = false;
    bool cluster_ok = false;
    while (fgets(line, sizeof(line), fp) != NULL) {
        std::string text(line);
        if (text.find("bytes per cluster") != std::string::npos) {
            saw_cluster_line = true;
            if (text.find("32768") != std::string::npos) {
                cluster_ok = true;
            }
            break;
        }
    }
    pclose(fp);

    if (!saw_cluster_line) {
        return STORAGE_OK;
    }
    return cluster_ok ? STORAGE_OK : STORAGE_E_SD_UNSUPPORTED_FORMAT;
}

int MicroSdManager::runCommandLocked(const std::string& command) const
{
    if (command.empty()) {
        return -1;
    }
    return system(command.c_str());
}

std::string MicroSdManager::readCommandFirstLineLocked(const std::string& command) const
{
    FILE* fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        return "";
    }

    char line[128] = {};
    std::string result;
    if (fgets(line, sizeof(line), fp) != NULL) {
        result = line;
        while (!result.empty() && (result[result.size() - 1] == '\n' ||
                                   result[result.size() - 1] == '\r' ||
                                   result[result.size() - 1] == ' ' ||
                                   result[result.size() - 1] == '\t')) {
            result.erase(result.size() - 1);
        }
    }
    pclose(fp);
    return result;
}

void MicroSdManager::bumpGenerationLocked()
{
    ++m_info.generation;
    if (m_info.generation == 0) {
        m_info.generation = 1;
    }
}

} // namespace storage
