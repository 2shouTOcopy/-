#include "StorageCommon.h"

const char *storage_error_to_string(StorageErrorCode err)
{
    switch (err) {
    case STORAGE_OK:
        return "OK";
    case STORAGE_E_INVALID_PARAM:
        return "Invalid parameter";
    case STORAGE_E_INVALID_PATH:
        return "Invalid path";
    case STORAGE_E_SD_NOT_INSERTED:
        return "SD card not inserted";
    case STORAGE_E_SD_UNSUPPORTED_FORMAT:
        return "Unsupported SD card format";
    case STORAGE_E_SD_MOUNT_FAILED:
        return "SD card mount failed";
    case STORAGE_E_SD_READONLY:
        return "SD card is readonly";
    case STORAGE_E_NO_SPACE:
        return "No space left";
    case STORAGE_E_SD_REMOVED:
        return "SD card removed";
    case STORAGE_E_WRITE_FAILED:
        return "Write failed";
    case STORAGE_E_FORMAT_FAILED:
        return "SD card format failed";
    case STORAGE_E_WRITE_CANCELLED:
        return "Write cancelled";
    case STORAGE_E_NOT_FOUND:
        return "Not found";
    case STORAGE_E_ALREADY_EXISTS:
        return "Target already exists";
    default:
        return "Unknown storage error";
    }
}

const char *storage_media_to_string(StorageMedia media)
{
    switch (media) {
    case STORAGE_MEDIA_EMMC:
        return "eMMC";
    case STORAGE_MEDIA_MICROSD:
        return "microSD";
    case STORAGE_MEDIA_AUTO:
        return "AUTO";
    default:
        return "Unknown";
    }
}

const char *storage_sd_state_to_string(MicroSdState state)
{
    switch (state) {
    case SD_STATE_NOT_INSERTED:
        return "NotInserted";
    case SD_STATE_MOUNTING:
        return "Mounting";
    case SD_STATE_ONLINE:
        return "Online";
    case SD_STATE_ONLINE_READONLY:
        return "OnlineReadOnly";
    case SD_STATE_UNSUPPORTED_FORMAT:
        return "UnsupportedFormat";
    case SD_STATE_SPACE_LOW:
        return "SpaceLow";
    case SD_STATE_EJECTING:
        return "Ejecting";
    case SD_STATE_EJECTED:
        return "Ejected";
    case SD_STATE_ABNORMAL_REMOVED:
        return "AbnormalRemoved";
    case SD_STATE_MOUNT_FAILED:
        return "MountFailed";
    case SD_STATE_FORMATTING:
        return "Formatting";
    default:
        return "Unknown";
    }
}
