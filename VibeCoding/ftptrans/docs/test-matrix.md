# FTP Text Transfer Test Matrix

## Logic Tests
- Count retention keeps only the latest `N` records.
- Time-range retention drops records outside the configured window.
- Mixed field counts expand CSV headers to the snapshot maximum width.
- TXT serialization prefixes timestamps only when enabled.
- CSV serialization outputs header and empty trailing columns for short rows.
- JSON serialization outputs an array of per-record objects.
- Refresh step triggers upload exactly on the `M`th accepted record.
- Stop event triggers one final upload only when pending increments exist.

## Focused Build Checks
- `FtpClientManager.cpp` compiles with module include flags under `-std=gnu++17`.
- `FtpLogManager.cpp` compiles with module include flags under `-std=gnu++17`.
- `ftptrans.cpp` compiles with module include flags when local warning-only issues from upstream headers are suppressed.

## Manual Integration Cases
- `TXT/CSV/JSON` each generate `result.<ext>` under FTP root.
- Changing text format deletes the old target file and delays new file creation until the next refresh threshold.
- `TextTimestampEnable=0/1` changes TXT/CSV/JSON payload layout correctly.
- `TextRetentionPolicy` switches between count-based and time-window-based cache trimming.
- FTP reconnect after disconnect still uploads the latest pending snapshot.
- `ALGO_PLAY_STOP` flushes the last partial step window.

## Commands
```bash
g++ -std=c++11 -Isource/algos/modules/ftptrans \
  source/algos/modules/ftptrans/TextResultCache.cpp \
  source/algos/modules/ftptrans/TextResultSerializer.cpp \
  source/algos/modules/ftptrans/TextUploadController.cpp \
  source/algos/modules/ftptrans/test/test_text_transfer_logic.cpp \
  -o /tmp/ftptrans_text_logic_test && /tmp/ftptrans_text_logic_test
```
