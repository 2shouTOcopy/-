# FTP Text Transfer Design

## Summary
- Keep the existing image FTP path unchanged for `jpg/bmp`.
- Rebuild text transfer as a separate path based on cache window, serializer, and overwrite upload.
- Reuse the existing FTP worker thread for text delete/upload so all FTP control operations stay serialized.

## Text Data Flow
1. `ftptrans.cpp` reads `SINGLE_ftp_sub_txt_info`.
2. `FtpClientManager::enqueueTextData()` stamps local time and pushes a `TextRecord` into `TextResultCache`.
3. `TextUploadController` counts newly accepted records and decides whether the refresh step is reached.
4. When refresh is required, the current cache snapshot is serialized by `TextResultSerializer`.
5. The FTP worker thread overwrites the target file under `RootDirectory`.

## Formats
- `TXT`: one line per record; when `TextTimestampEnable=1`, prefix each line with `timestamp + space`.
- `CSV`: one file with header row; columns are `timestamp,col1..colN` when timestamp is enabled, otherwise `col1..colN`.
- `JSON`: one file with a top-level array; each record is an object with `timestamp` and `item1..itemN` as applicable.

## Cache Rules
- Records are split by `;` into fields.
- `TextRetentionPolicy=按最近N条`: keep the latest `TextRetentionCount` records.
- `TextRetentionPolicy=按最近时间范围`: keep records within `TextRetentionTimeRangeSec` from the newest record timestamp.
- Internal hard limit is fixed at `10000` records; overflow is evicted FIFO.

## Lifecycle Rules
- On startup with text transfer enabled: clear cache, clear pending upload state, delete the current target file.
- On text format switch: clear cache, clear pending upload state, delete the old target file, and wait for the next upload trigger.
- On `ALGO_PLAY_STOP`: if there are unsent increments after the last step upload, force one final overwrite upload.

## Compatibility
- Existing image-side `TimeStampEnable` keeps its old meaning for picture file names.
- Existing image-side `TransportType`, `FileNameStrategy`, and `CustomFileName` remain unchanged.
- New text parameters are isolated from image naming and picture-format behavior.
