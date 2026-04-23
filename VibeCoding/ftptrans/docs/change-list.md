# FTP Text Transfer Change List

## New Source Files
- `TextResultTypes.h`
- `TextResultCache.h`
- `TextResultCache.cpp`
- `TextResultSerializer.h`
- `TextResultSerializer.cpp`
- `TextUploadController.h`
- `TextUploadController.cpp`
- `test/test_text_transfer_logic.cpp`

## Updated Source Files
- `FtpClientManager.h`
- `FtpClientManager.cpp`
- `ftptrans.cpp`
- `FtpClientMonitor.h`

## Updated Metadata
- `source/algos/so/ftptrans/json/pm_conf.json`
- `source/algos/so/ftptrans/json/ui_conf.json`
- `source/algos/so/ftptrans/json/ftptrans.xml`

## New Parameters
- `TextFileFormat`
- `TextTimestampEnable`
- `TextFileName`
- `TextRetentionPolicy`
- `TextRetentionCount`
- `TextRetentionTimeRangeSec`
- `TextRefreshStep`

## Risks
- Full module `make` in the current local shell depends on explicit platform/toolchain flags; without them, unrelated legacy warnings/macros break the build before link.
- Text files are uploaded at FTP root by design; if downstream expects image-style subdirectories for text, that would need a separate requirement.
