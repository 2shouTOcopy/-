#include "StorageRouter.h"

#include "FileOps.h"

#include <cstring>

namespace storage {
namespace {

bool isSlash(char c)
{
    return c == '/';
}

std::string joinPath(const std::string& left, const std::string& right)
{
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    if (left[left.size() - 1] == '/') {
        return left + right;
    }
    return left + "/" + right;
}

void copyPath(char* dst, size_t dst_len, const std::string& src)
{
    if (dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    std::strncpy(dst, src.c_str(), dst_len - 1);
    dst[dst_len - 1] = '\0';
}

} // namespace

StorageRouter::StorageRouter(MicroSdManager* micro_sd_manager)
    : m_micro_sd_manager(micro_sd_manager)
{
}

StorageErrorCode StorageRouter::resolveWritePath(const StorageWriteRequest& request,
                                                 StorageResolvedPath* out) const
{
    if (out == NULL || m_micro_sd_manager == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }
    if (!isSafeRelativePath(request.relative_path)) {
        return STORAGE_E_INVALID_PATH;
    }

    StorageMedia media = STORAGE_MEDIA_EMMC;
    StorageErrorCode ec = resolveWriteTarget(request, &media);
    if (ec != STORAGE_OK) {
        return ec;
    }
    return fillResolved(media, request.type, request.relative_path, request.required_size, out);
}

StorageErrorCode StorageRouter::resolveReadRoots(StorageBusinessType type,
                                                 std::vector<StorageResolvedPath>* roots) const
{
    if (roots == NULL || m_micro_sd_manager == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    roots->clear();
    StorageResolvedPath emmc = {};
    StorageErrorCode ec = fillResolved(STORAGE_MEDIA_EMMC, type, "", 0, &emmc);
    if (ec != STORAGE_OK) {
        return ec;
    }
    roots->push_back(emmc);

    if (m_micro_sd_manager->isMicroSdOnline()) {
        StorageResolvedPath sd = {};
        ec = fillResolved(STORAGE_MEDIA_MICROSD, type, "", 0, &sd);
        if (ec == STORAGE_OK) {
            roots->push_back(sd);
        }
    }
    return STORAGE_OK;
}

std::string StorageRouter::getBusinessRoot(StorageMedia media, StorageBusinessType type) const
{
    const char* media_root = STORAGE_EMMC_ROOT;
    if (media == STORAGE_MEDIA_MICROSD && m_micro_sd_manager != NULL) {
        MicroSdInfo info = m_micro_sd_manager->getInfo();
        media_root = info.mount_root;
    }
    return joinPath(media_root, businessRelativeRoot(type));
}

bool StorageRouter::isSafeRelativePath(const char* relative_path) const
{
    if (relative_path == NULL) {
        return false;
    }

    const std::string path(relative_path);
    if (path.empty()) {
        return true;
    }
    if (isSlash(path[0])) {
        return false;
    }

    size_t part_start = 0;
    while (part_start <= path.size()) {
        const size_t slash = path.find('/', part_start);
        const std::string part = path.substr(part_start, slash - part_start);
        if (part.empty()) {
            return false;
        }
        if (part == "." || part == "..") {
            return false;
        }
        for (size_t i = 0; i < part.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(part[i]);
            if (ch < 32 || part[i] == '\\') {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        part_start = slash + 1;
    }
    return true;
}

StorageErrorCode StorageRouter::resolveWriteTarget(const StorageWriteRequest& request,
                                                   StorageMedia* media) const
{
    if (media == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    if (request.media == STORAGE_MEDIA_EMMC) {
        *media = STORAGE_MEDIA_EMMC;
        return STORAGE_OK;
    }
    if (request.media == STORAGE_MEDIA_MICROSD) {
        StorageErrorCode ec = m_micro_sd_manager->checkWritable(request.required_size);
        if (ec != STORAGE_OK) {
            return ec;
        }
        *media = STORAGE_MEDIA_MICROSD;
        return STORAGE_OK;
    }
    if (request.media == STORAGE_MEDIA_AUTO) {
        StorageErrorCode ec = m_micro_sd_manager->checkWritable(request.required_size);
        if (ec != STORAGE_OK) {
            return ec;
        }
        *media = STORAGE_MEDIA_MICROSD;
        return STORAGE_OK;
    }
    return STORAGE_E_INVALID_PARAM;
}

StorageErrorCode StorageRouter::fillResolved(StorageMedia media, StorageBusinessType type,
                                             const char* relative_path, uint64_t required_size,
                                             StorageResolvedPath* out) const
{
    if (out == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    const std::string root = getBusinessRoot(media, type);
    const std::string rel = relative_path != NULL ? relative_path : "";
    const std::string full_path = rel.empty() ? root : joinPath(root, rel);
    if (full_path.size() >= STORAGE_MAX_PATH_LEN) {
        return STORAGE_E_INVALID_PATH;
    }

    std::memset(out, 0, sizeof(*out));
    out->media = media;
    out->type = type;
    out->required_size = required_size;
    copyPath(out->abs_path, sizeof(out->abs_path), full_path);

    if (media == STORAGE_MEDIA_MICROSD) {
        MicroSdInfo info = m_micro_sd_manager->getInfo();
        out->sd_state = info.state;
        out->available_size = info.available_size;
        out->total_size = info.total_size;
        out->generation = info.generation;
    } else {
        out->sd_state = SD_STATE_NOT_INSERTED;
        uint64_t available = 0;
        uint64_t total = 0;
        if (FileOps::statFs(STORAGE_EMMC_ROOT, &available, &total) == STORAGE_OK) {
            out->available_size = available;
            out->total_size = total;
        }
    }
    return STORAGE_OK;
}

const char* StorageRouter::businessRelativeRoot(StorageBusinessType type) const
{
    switch (type) {
    case STORAGE_BIZ_PROJECT_PACKAGE:
        return "project";
    case STORAGE_BIZ_PROJECT_WORKDIR:
        return "project_dir";
    case STORAGE_BIZ_SAVE_IMAGE:
        return "save_img";
    case STORAGE_BIZ_TEST_IMAGE:
    case STORAGE_BIZ_TRAIN_IMAGE:
        return "test_img";
    case STORAGE_BIZ_PROJECT_INDEX:
        return "project";
    case STORAGE_BIZ_IMAGE_INDEX:
        return "db/img_list_db";
    case STORAGE_BIZ_TEST_IMAGE_INDEX:
        return "db/test_img_list_db";
    default:
        return "";
    }
}

} // namespace storage
