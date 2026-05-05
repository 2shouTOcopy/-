#include "project_storage_adapter.h"

#include "MicroSdManager.h"
#include "StorageRouter.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

storage::StorageRouter makeRouter()
{
    return storage::StorageRouter(storage::MicroSdManager::getInstance());
}

bool isMediaValid(StorageMedia media)
{
    return media == STORAGE_MEDIA_EMMC || media == STORAGE_MEDIA_MICROSD;
}

StorageErrorCode sdStateToAccessError(MicroSdState state)
{
    switch (state) {
    case SD_STATE_ONLINE:
    case SD_STATE_ONLINE_READONLY:
    case SD_STATE_SPACE_LOW:
        return STORAGE_OK;
    case SD_STATE_NOT_INSERTED:
    case SD_STATE_EJECTED:
        return STORAGE_E_SD_NOT_INSERTED;
    case SD_STATE_UNSUPPORTED_FORMAT:
        return STORAGE_E_SD_UNSUPPORTED_FORMAT;
    case SD_STATE_ABNORMAL_REMOVED:
        return STORAGE_E_SD_REMOVED;
    case SD_STATE_MOUNT_FAILED:
    case SD_STATE_MOUNTING:
    case SD_STATE_EJECTING:
    case SD_STATE_FORMATTING:
    default:
        return STORAGE_E_SD_MOUNT_FAILED;
    }
}

StorageErrorCode copyOutput(const std::string& path, char* buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return STORAGE_E_INVALID_PARAM;
    }
    if (path.size() >= len) {
        buf[0] = '\0';
        return STORAGE_E_INVALID_PATH;
    }
    std::snprintf(buf, len, "%s", path.c_str());
    return STORAGE_OK;
}

StorageErrorCode makeProjectFileName(const char* project_name, const char* suffix,
                                     char* buf, size_t len)
{
    if (project_name == NULL || suffix == NULL || buf == NULL || len == 0) {
        return STORAGE_E_INVALID_PARAM;
    }
    const int written = std::snprintf(buf, len, "%s%s", project_name, suffix);
    if (written < 0 || static_cast<size_t>(written) >= len) {
        buf[0] = '\0';
        return STORAGE_E_INVALID_PATH;
    }

    storage::StorageRouter router = makeRouter();
    if (!router.isSafeRelativePath(buf)) {
        buf[0] = '\0';
        return STORAGE_E_INVALID_PATH;
    }
    return STORAGE_OK;
}

StorageErrorCode getProjectPath(StorageMedia media, StorageBusinessType type,
                                const char* relative_path, char* buf, size_t len)
{
    if (!isMediaValid(media)) {
        return STORAGE_E_INVALID_PARAM;
    }

    storage::StorageRouter router = makeRouter();
    if (relative_path != NULL && !router.isSafeRelativePath(relative_path)) {
        return STORAGE_E_INVALID_PATH;
    }

    const std::string root = router.getBusinessRoot(media, type);
    std::string path = root;
    if (relative_path != NULL && relative_path[0] != '\0') {
        path += "/";
        path += relative_path;
    }
    return copyOutput(path, buf, len);
}

StorageErrorCode copyWithTrailingSlash(const char* path, char* buf, size_t len)
{
    if (path == NULL || buf == NULL || len == 0) {
        return STORAGE_E_INVALID_PARAM;
    }

    const size_t path_len = std::strlen(path);
    const bool has_trailing_slash = path_len > 0 && path[path_len - 1] == '/';
    const size_t required_len = path_len + (has_trailing_slash ? 0 : 1);
    if (required_len >= len) {
        buf[0] = '\0';
        return STORAGE_E_INVALID_PATH;
    }

    if (has_trailing_slash) {
        std::snprintf(buf, len, "%s", path);
    } else {
        std::snprintf(buf, len, "%s/", path);
    }
    return STORAGE_OK;
}

bool pathEqualsIgnoringOneTrailingSlash(const char* lhs, const char* rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }

    size_t lhs_len = std::strlen(lhs);
    size_t rhs_len = std::strlen(rhs);
    if (lhs_len > 1 && lhs[lhs_len - 1] == '/') {
        --lhs_len;
    }
    if (rhs_len > 1 && rhs[rhs_len - 1] == '/') {
        --rhs_len;
    }

    return lhs_len == rhs_len && std::strncmp(lhs, rhs, lhs_len) == 0;
}

bool hasProjectName(const StorageProjectRecord* records, size_t count, const char* name)
{
    if (records == NULL || name == NULL) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (std::strncmp(records[i].name, name, STORAGE_PROJECT_NAME_LEN) == 0) {
            return true;
        }
    }
    return false;
}

StorageErrorCode appendUniqueRecord(const StorageProjectRecord* src, StorageProjectRecord* out,
                                    size_t max_count, size_t* out_count)
{
    if (src == NULL || out == NULL || out_count == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }
    if (src->name[0] == '\0') {
        return STORAGE_OK;
    }
    if (hasProjectName(out, *out_count, src->name)) {
        return STORAGE_OK;
    }
    if (*out_count >= max_count) {
        return STORAGE_E_NO_SPACE;
    }
    out[*out_count] = *src;
    ++(*out_count);
    return STORAGE_OK;
}

} // namespace

int storage_project_get_package_path(StorageMedia media, const char *project_name,
                                     char *buf, size_t len)
{
    char file_name[STORAGE_MAX_REL_PATH_LEN] = {};
    StorageErrorCode ec = makeProjectFileName(project_name, ".sln", file_name, sizeof(file_name));
    if (ec != STORAGE_OK) {
        return ec;
    }
    return getProjectPath(media, STORAGE_BIZ_PROJECT_PACKAGE, file_name, buf, len);
}

int storage_project_get_package_root_dir_path(StorageMedia media, char *buf, size_t len)
{
    char path[STORAGE_MAX_PATH_LEN] = {};
    StorageErrorCode ec = getProjectPath(media, STORAGE_BIZ_PROJECT_PACKAGE, NULL,
                                         path, sizeof(path));
    if (ec != STORAGE_OK) {
        return ec;
    }
    return copyWithTrailingSlash(path, buf, len);
}

int storage_project_is_emmc_package_root_path(const char *path)
{
    char root_path[STORAGE_MAX_PATH_LEN] = {};
    if (storage_project_get_package_root_dir_path(STORAGE_MEDIA_EMMC,
                                                  root_path, sizeof(root_path)) != STORAGE_OK) {
        return 0;
    }
    return pathEqualsIgnoringOneTrailingSlash(path, root_path) ? 1 : 0;
}

int storage_project_get_external_save_file_path(const char *save_dir,
                                                const char *project_name,
                                                char *buf, size_t len)
{
    if (save_dir == NULL || save_dir[0] == '\0' || buf == NULL || len == 0) {
        return STORAGE_E_INVALID_PARAM;
    }

    char file_name[STORAGE_MAX_REL_PATH_LEN] = {};
    StorageErrorCode ec = makeProjectFileName(project_name, ".sln", file_name, sizeof(file_name));
    if (ec != STORAGE_OK) {
        buf[0] = '\0';
        return ec;
    }

    const size_t save_dir_len = std::strlen(save_dir);
    const bool has_trailing_slash = save_dir[save_dir_len - 1] == '/';
    const size_t required_len = save_dir_len + (has_trailing_slash ? 0 : 1) +
        std::strlen(file_name);
    if (required_len >= len) {
        buf[0] = '\0';
        return STORAGE_E_INVALID_PATH;
    }

    std::snprintf(buf, len, "%s%s%s", save_dir, has_trailing_slash ? "" : "/", file_name);
    return STORAGE_OK;
}

int storage_project_get_workdir_root_dir_path(StorageMedia media, char *buf, size_t len)
{
    char path[STORAGE_MAX_PATH_LEN] = {};
    StorageErrorCode ec = getProjectPath(media, STORAGE_BIZ_PROJECT_WORKDIR, NULL,
                                         path, sizeof(path));
    if (ec != STORAGE_OK) {
        return ec;
    }
    return copyWithTrailingSlash(path, buf, len);
}

int storage_project_get_workdir_path(StorageMedia media, const char *project_name,
                                     char *buf, size_t len)
{
    if (project_name == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    storage::StorageRouter router = makeRouter();
    if (!router.isSafeRelativePath(project_name)) {
        return STORAGE_E_INVALID_PATH;
    }
    return getProjectPath(media, STORAGE_BIZ_PROJECT_WORKDIR, project_name, buf, len);
}

int storage_project_get_workdir_dir_path(StorageMedia media, const char *project_name,
                                         char *buf, size_t len)
{
    char path[STORAGE_MAX_PATH_LEN] = {};
    StorageErrorCode ec = static_cast<StorageErrorCode>(
        storage_project_get_workdir_path(media, project_name, path, sizeof(path)));
    if (ec != STORAGE_OK) {
        return ec;
    }

    return copyWithTrailingSlash(path, buf, len);
}

int storage_project_get_workdir_child_path(StorageMedia media, const char *project_name,
                                           const char *child_relative_path,
                                           char *buf, size_t len)
{
    if (child_relative_path == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    storage::StorageRouter router = makeRouter();
    if (!router.isSafeRelativePath(child_relative_path)) {
        if (buf != NULL && len > 0) {
            buf[0] = '\0';
        }
        return STORAGE_E_INVALID_PATH;
    }

    char workdir[STORAGE_MAX_PATH_LEN] = {};
    StorageErrorCode ec = static_cast<StorageErrorCode>(
        storage_project_get_workdir_path(media, project_name, workdir, sizeof(workdir)));
    if (ec != STORAGE_OK) {
        return ec;
    }

    const size_t required_len = std::strlen(workdir) + 1 + std::strlen(child_relative_path);
    if (buf == NULL || len == 0) {
        return STORAGE_E_INVALID_PARAM;
    }
    if (required_len >= len) {
        buf[0] = '\0';
        return STORAGE_E_INVALID_PATH;
    }

    std::snprintf(buf, len, "%s/%s", workdir, child_relative_path);
    return STORAGE_OK;
}

int storage_project_get_base_image_path(StorageMedia media, const char *project_name,
                                        char *buf, size_t len)
{
    char file_name[STORAGE_MAX_REL_PATH_LEN] = {};
    StorageErrorCode ec = makeProjectFileName(project_name, ".jpg", file_name, sizeof(file_name));
    if (ec != STORAGE_OK) {
        return ec;
    }

    char rel_path[STORAGE_MAX_REL_PATH_LEN] = {};
    const int written = std::snprintf(rel_path, sizeof(rel_path), "base_image/%s", file_name);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(rel_path)) {
        return STORAGE_E_INVALID_PATH;
    }
    return getProjectPath(media, STORAGE_BIZ_PROJECT_PACKAGE, rel_path, buf, len);
}

int storage_project_get_index_path(StorageMedia media, char *buf, size_t len)
{
    return getProjectPath(media, STORAGE_BIZ_PROJECT_INDEX, "project_mng.json", buf, len);
}

int storage_project_resolve_package_write(StorageMedia requested_media, const char *project_name,
                                          uint64_t required_size, StorageResolvedPath *out)
{
    if (out == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    char file_name[STORAGE_MAX_REL_PATH_LEN] = {};
    StorageErrorCode ec = makeProjectFileName(project_name, ".sln", file_name, sizeof(file_name));
    if (ec != STORAGE_OK) {
        return ec;
    }

    StorageWriteRequest req = {};
    req.media = requested_media;
    req.type = STORAGE_BIZ_PROJECT_PACKAGE;
    req.required_size = required_size;
    req.overwrite = 0;
    std::snprintf(req.relative_path, sizeof(req.relative_path), "%s", file_name);

    storage::StorageRouter router = makeRouter();
    return router.resolveWritePath(req, out);
}

int storage_project_resolve_save_paths(StorageMedia requested_media, const char *project_name,
                                       uint64_t required_size,
                                       StorageProjectSavePaths *out_paths)
{
    if (out_paths == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    std::memset(out_paths, 0, sizeof(*out_paths));

    StorageResolvedPath package = {};
    StorageErrorCode ec = static_cast<StorageErrorCode>(
        storage_project_resolve_package_write(requested_media, project_name,
                                              required_size, &package));
    if (ec != STORAGE_OK) {
        return ec;
    }

    out_paths->media = package.media;
    out_paths->generation = package.generation;
    std::snprintf(out_paths->package_path, sizeof(out_paths->package_path),
                  "%s", package.abs_path);

    ec = static_cast<StorageErrorCode>(
        storage_project_get_workdir_path(package.media, project_name,
                                         out_paths->workdir_path,
                                         sizeof(out_paths->workdir_path)));
    if (ec != STORAGE_OK) {
        std::memset(out_paths, 0, sizeof(*out_paths));
        return ec;
    }

    ec = static_cast<StorageErrorCode>(
        storage_project_get_base_image_path(package.media, project_name,
                                            out_paths->base_image_path,
                                            sizeof(out_paths->base_image_path)));
    if (ec != STORAGE_OK) {
        std::memset(out_paths, 0, sizeof(*out_paths));
        return ec;
    }

    ec = static_cast<StorageErrorCode>(
        storage_project_get_index_path(package.media, out_paths->index_path,
                                       sizeof(out_paths->index_path)));
    if (ec != STORAGE_OK) {
        std::memset(out_paths, 0, sizeof(*out_paths));
        return ec;
    }

    return STORAGE_OK;
}

int storage_project_resolve_access_paths(StorageMedia requested_media, const char *project_name,
                                         StorageProjectSavePaths *out_paths)
{
    if (out_paths == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    std::memset(out_paths, 0, sizeof(*out_paths));

    StorageMedia media = requested_media;
    if (requested_media == STORAGE_MEDIA_AUTO) {
        media = STORAGE_MEDIA_EMMC;
    }
    if (!isMediaValid(media)) {
        return STORAGE_E_INVALID_PARAM;
    }

    if (media == STORAGE_MEDIA_MICROSD) {
        MicroSdInfo info = storage::MicroSdManager::getInstance()->getInfo();
        StorageErrorCode ec = sdStateToAccessError(info.state);
        if (ec != STORAGE_OK) {
            return ec;
        }
        out_paths->generation = info.generation;
    }

    out_paths->media = media;

    StorageErrorCode ec = static_cast<StorageErrorCode>(
        storage_project_get_package_path(media, project_name,
                                         out_paths->package_path,
                                         sizeof(out_paths->package_path)));
    if (ec != STORAGE_OK) {
        std::memset(out_paths, 0, sizeof(*out_paths));
        return ec;
    }

    ec = static_cast<StorageErrorCode>(
        storage_project_get_workdir_path(media, project_name,
                                         out_paths->workdir_path,
                                         sizeof(out_paths->workdir_path)));
    if (ec != STORAGE_OK) {
        std::memset(out_paths, 0, sizeof(*out_paths));
        return ec;
    }

    ec = static_cast<StorageErrorCode>(
        storage_project_get_base_image_path(media, project_name,
                                            out_paths->base_image_path,
                                            sizeof(out_paths->base_image_path)));
    if (ec != STORAGE_OK) {
        std::memset(out_paths, 0, sizeof(*out_paths));
        return ec;
    }

    ec = static_cast<StorageErrorCode>(
        storage_project_get_index_path(media, out_paths->index_path,
                                       sizeof(out_paths->index_path)));
    if (ec != STORAGE_OK) {
        std::memset(out_paths, 0, sizeof(*out_paths));
        return ec;
    }

    return STORAGE_OK;
}

int storage_project_resolve_rename_paths(StorageMedia requested_media,
                                         const char *old_project_name,
                                         const char *new_project_name,
                                         uint64_t required_size,
                                         StorageProjectSavePaths *old_paths,
                                         StorageProjectSavePaths *new_paths)
{
    if (old_paths == NULL || new_paths == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    std::memset(old_paths, 0, sizeof(*old_paths));
    std::memset(new_paths, 0, sizeof(*new_paths));

    StorageMedia media = requested_media;
    if (requested_media == STORAGE_MEDIA_AUTO) {
        media = STORAGE_MEDIA_EMMC;
    }
    if (!isMediaValid(media)) {
        return STORAGE_E_INVALID_PARAM;
    }

    StorageErrorCode ec = static_cast<StorageErrorCode>(
        storage_project_resolve_access_paths(media, old_project_name, old_paths));
    if (ec != STORAGE_OK) {
        std::memset(old_paths, 0, sizeof(*old_paths));
        std::memset(new_paths, 0, sizeof(*new_paths));
        return ec;
    }

    if (media == STORAGE_MEDIA_MICROSD) {
        ec = storage::MicroSdManager::getInstance()->checkWritable(required_size);
        if (ec != STORAGE_OK) {
            std::memset(old_paths, 0, sizeof(*old_paths));
            return ec;
        }
    }

    ec = static_cast<StorageErrorCode>(
        storage_project_resolve_access_paths(media, new_project_name, new_paths));
    if (ec != STORAGE_OK) {
        std::memset(old_paths, 0, sizeof(*old_paths));
        std::memset(new_paths, 0, sizeof(*new_paths));
        return ec;
    }

    return STORAGE_OK;
}

int storage_project_resolve_copy_paths(StorageMedia requested_media,
                                       const char *src_project_name,
                                       const char *dst_project_name,
                                       uint64_t required_size,
                                       StorageProjectSavePaths *src_paths,
                                       StorageProjectSavePaths *dst_paths)
{
    return storage_project_resolve_rename_paths(requested_media, src_project_name,
                                                dst_project_name, required_size,
                                                src_paths, dst_paths);
}

int storage_project_fill_record(StorageMedia media, const char *project_name,
                                StorageProjectRecord *out_record)
{
    if (project_name == NULL || out_record == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    StorageProjectRecord record = {};
    record.media = media;
    record.index_status = STORAGE_PROJECT_INDEX_VALID;

    storage::StorageRouter router = makeRouter();
    if (!router.isSafeRelativePath(project_name)) {
        return STORAGE_E_INVALID_PATH;
    }

    if (std::snprintf(record.name, sizeof(record.name), "%s", project_name) >=
        static_cast<int>(sizeof(record.name))) {
        return STORAGE_E_INVALID_PATH;
    }

    StorageErrorCode ec = static_cast<StorageErrorCode>(
        storage_project_get_package_path(media, project_name,
                                         record.package_path, sizeof(record.package_path)));
    if (ec != STORAGE_OK) {
        return ec;
    }
    ec = static_cast<StorageErrorCode>(
        storage_project_get_workdir_path(media, project_name,
                                         record.workdir_path, sizeof(record.workdir_path)));
    if (ec != STORAGE_OK) {
        return ec;
    }
    ec = static_cast<StorageErrorCode>(
        storage_project_get_base_image_path(media, project_name,
                                            record.base_image_path,
                                            sizeof(record.base_image_path)));
    if (ec != STORAGE_OK) {
        return ec;
    }

    *out_record = record;
    return STORAGE_OK;
}

int storage_project_merge_records(const StorageProjectRecord *emmc_records, size_t emmc_count,
                                  const StorageProjectRecord *sd_records, size_t sd_count,
                                  StorageProjectRecord *out_records, size_t max_count,
                                  size_t *out_count)
{
    if ((emmc_count > 0 && emmc_records == NULL) ||
        (sd_count > 0 && sd_records == NULL) ||
        out_records == NULL || out_count == NULL) {
        return STORAGE_E_INVALID_PARAM;
    }

    *out_count = 0;
    for (size_t i = 0; i < max_count; ++i) {
        std::memset(&out_records[i], 0, sizeof(out_records[i]));
    }

    for (size_t i = 0; i < emmc_count; ++i) {
        StorageErrorCode ec = appendUniqueRecord(&emmc_records[i], out_records,
                                                 max_count, out_count);
        if (ec != STORAGE_OK) {
            return ec;
        }
    }

    for (size_t i = 0; i < sd_count; ++i) {
        StorageErrorCode ec = appendUniqueRecord(&sd_records[i], out_records,
                                                 max_count, out_count);
        if (ec != STORAGE_OK) {
            return ec;
        }
    }
    return STORAGE_OK;
}
