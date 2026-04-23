#include "TextResultCache.h"

TextResultCache::TextResultCache(size_t hard_limit)
    : m_hard_limit(hard_limit),
      m_retention_count(hard_limit),
      m_retention_time_range_sec(0),
      m_policy(TEXT_RETENTION_BY_COUNT) {}

void TextResultCache::setPolicy(TextRetentionPolicy policy) { m_policy = policy; }

void TextResultCache::setRetentionCount(size_t retention_count) {
  m_retention_count = retention_count;
  trimByPolicy();
  trimByHardLimit();
}

void TextResultCache::setRetentionTimeRangeSec(int64_t retention_time_range_sec) {
  m_retention_time_range_sec = retention_time_range_sec;
  trimByPolicy();
  trimByHardLimit();
}

void TextResultCache::clear() { m_records.clear(); }

void TextResultCache::addRecord(const TextRecord &record) {
  TextRecord normalized = record;
  normalized.items = splitItems(normalized.raw_text);
  m_records.push_back(normalized);
  trimByPolicy();
  trimByHardLimit();
}

TextSnapshot TextResultCache::snapshot() const {
  TextSnapshot snapshot;
  for (std::deque<TextRecord>::const_iterator it = m_records.begin();
       it != m_records.end(); ++it) {
    snapshot.records.push_back(*it);
    if (it->items.size() > snapshot.max_item_count) {
      snapshot.max_item_count = it->items.size();
    }
  }
  return snapshot;
}

bool TextResultCache::empty() const { return m_records.empty(); }

std::vector<std::string> TextResultCache::splitItems(const std::string &raw_text) {
  std::vector<std::string> items;
  std::string current;

  for (size_t idx = 0; idx < raw_text.size(); ++idx) {
    if (raw_text[idx] == ';') {
      items.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(raw_text[idx]);
  }

  items.push_back(current);
  return items;
}

void TextResultCache::trimByPolicy() {
  if (m_policy == TEXT_RETENTION_BY_COUNT) {
    if (m_retention_count == 0) {
      m_records.clear();
      return;
    }
    while (m_records.size() > m_retention_count) {
      m_records.pop_front();
    }
    return;
  }

  if (m_policy == TEXT_RETENTION_BY_TIME_RANGE) {
    if (m_retention_time_range_sec <= 0) {
      return;
    }
    if (m_records.empty()) {
      return;
    }

    const int64_t latest_timestamp = m_records.back().timestamp_sec;
    const int64_t cutoff = latest_timestamp - m_retention_time_range_sec;
    while (!m_records.empty() && m_records.front().timestamp_sec < cutoff) {
      m_records.pop_front();
    }
  }
}

void TextResultCache::trimByHardLimit() {
  if (m_hard_limit == 0) {
    m_records.clear();
    return;
  }

  while (m_records.size() > m_hard_limit) {
    m_records.pop_front();
  }
}
