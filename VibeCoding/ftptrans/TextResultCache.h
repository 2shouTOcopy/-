#ifndef TEXT_RESULT_CACHE_H
#define TEXT_RESULT_CACHE_H

#include "TextResultTypes.h"

#include <deque>

class TextResultCache {
 public:
  explicit TextResultCache(size_t hard_limit = 10000);

  void setPolicy(TextRetentionPolicy policy);
  void setRetentionCount(size_t retention_count);
  void setRetentionTimeRangeSec(int64_t retention_time_range_sec);
  void clear();
  void addRecord(const TextRecord &record);
  TextSnapshot snapshot() const;
  bool empty() const;

 private:
  static std::vector<std::string> splitItems(const std::string &raw_text);
  void trimByPolicy();
  void trimByHardLimit();

  std::deque<TextRecord> m_records;
  size_t m_hard_limit;
  size_t m_retention_count;
  int64_t m_retention_time_range_sec;
  TextRetentionPolicy m_policy;
};

#endif
