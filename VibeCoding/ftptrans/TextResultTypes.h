#ifndef TEXT_RESULT_TYPES_H
#define TEXT_RESULT_TYPES_H

#include <stdint.h>

#include <string>
#include <vector>

enum TextFileFormat {
  TEXT_FILE_FORMAT_TXT = 0,
  TEXT_FILE_FORMAT_CSV = 1,
  TEXT_FILE_FORMAT_JSON = 2
};

enum TextRetentionPolicy {
  TEXT_RETENTION_BY_COUNT = 0,
  TEXT_RETENTION_BY_TIME_RANGE = 1
};

struct TextRecord {
  int64_t timestamp_sec;
  std::string timestamp_text;
  std::string raw_text;
  std::vector<std::string> items;

  TextRecord() : timestamp_sec(0) {}
};

struct TextSnapshot {
  std::vector<TextRecord> records;
  size_t max_item_count;

  TextSnapshot() : max_item_count(0) {}
};

#endif
