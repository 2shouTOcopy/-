# FTP Text Transfer Implementation Plan

## Scope
- Add text-specific parameters for format, timestamp, file name, cache retention, and refresh step.
- Introduce text cache/serializer/controller helpers inside `ftptrans`.
- Integrate final flush on stop and startup/format-switch reset behavior.
- Update UI/PM/XML metadata and keep docs in sync.

## Code Changes
- `TextResultCache.*`
  - Store ordered `TextRecord` items.
  - Apply count/time-range trimming and hard-limit trimming.
- `TextResultSerializer.*`
  - Serialize cache snapshots to TXT/CSV/JSON.
  - Generate file extensions from text format.
- `TextUploadController.*`
  - Count accepted records.
  - Trigger overwrite upload on refresh step.
  - Trigger final upload on stop when pending records exist.
- `FtpClientManager.*`
  - Replace legacy one-minute text buffer with text cache state.
  - Queue pending remote deletes and latest pending overwrite snapshot.
  - Execute delete/upload in the FTP worker thread.
- `ftptrans.cpp`
  - Map new text params to the manager.
  - Stop requiring an active FTP connection before caching text.
  - Trigger final text refresh on `ALGO_PLAY_STOP`.

## Parameters
- `TextFileFormat`
- `TextTimestampEnable`
- `TextFileName`
- `TextRetentionPolicy`
- `TextRetentionCount`
- `TextRetentionTimeRangeSec`
- `TextRefreshStep`

## Notes
- Text file uploads always use the current cache snapshot and overwrite the same remote file.
- Text target file name is `TextFileName + extension`, defaulting to `result.txt`.
- The text file stays in `RootDirectory`; image directory strategy is not reused for text.
