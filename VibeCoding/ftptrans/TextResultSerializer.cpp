#include "TextResultSerializer.h"

#include <sstream>

std::string TextResultSerializer::serialize(const TextSnapshot &snapshot,
                                            TextFileFormat format,
                                            bool include_timestamp) {
  if (format == TEXT_FILE_FORMAT_CSV) {
    return serializeCsv(snapshot, include_timestamp);
  }
  if (format == TEXT_FILE_FORMAT_JSON) {
    return serializeJson(snapshot, include_timestamp);
  }
  return serializeTxt(snapshot, include_timestamp);
}

std::string TextResultSerializer::extension(TextFileFormat format) {
  if (format == TEXT_FILE_FORMAT_CSV) {
    return ".csv";
  }
  if (format == TEXT_FILE_FORMAT_JSON) {
    return ".json";
  }
  return ".txt";
}

std::string TextResultSerializer::serializeTxt(const TextSnapshot &snapshot,
                                               bool include_timestamp) {
  std::ostringstream oss;
  for (size_t idx = 0; idx < snapshot.records.size(); ++idx) {
    const TextRecord &record = snapshot.records[idx];
    if (include_timestamp) {
      oss << record.timestamp_text << " ";
    }
    oss << record.raw_text << "\r\n";
  }
  return oss.str();
}

std::string TextResultSerializer::serializeCsv(const TextSnapshot &snapshot,
                                               bool include_timestamp) {
  std::ostringstream oss;
  bool need_header = include_timestamp || snapshot.max_item_count > 0;
  if (need_header) {
    bool first = true;
    if (include_timestamp) {
      oss << "timestamp";
      first = false;
    }
    for (size_t idx = 0; idx < snapshot.max_item_count; ++idx) {
      if (!first) {
        oss << ",";
      }
      oss << "col" << (idx + 1);
      first = false;
    }
    oss << "\r\n";
  }

  for (size_t row = 0; row < snapshot.records.size(); ++row) {
    const TextRecord &record = snapshot.records[row];
    bool first = true;
    if (include_timestamp) {
      oss << escapeCsv(record.timestamp_text);
      first = false;
    }
    for (size_t col = 0; col < snapshot.max_item_count; ++col) {
      if (!first) {
        oss << ",";
      }
      if (col < record.items.size()) {
        oss << escapeCsv(record.items[col]);
      }
      first = false;
    }
    oss << "\r\n";
  }
  return oss.str();
}

std::string TextResultSerializer::serializeJson(const TextSnapshot &snapshot,
                                                bool include_timestamp) {
  std::ostringstream oss;
  oss << "[";
  for (size_t idx = 0; idx < snapshot.records.size(); ++idx) {
    const TextRecord &record = snapshot.records[idx];
    if (idx > 0) {
      oss << ",";
    }
    oss << "{";
    bool first_field = true;
    if (include_timestamp) {
      oss << "\"timestamp\":\"" << escapeJson(record.timestamp_text) << "\"";
      first_field = false;
    }
    for (size_t item_idx = 0; item_idx < record.items.size(); ++item_idx) {
      if (!first_field) {
        oss << ",";
      }
      oss << "\"item" << (item_idx + 1) << "\":\""
          << escapeJson(record.items[item_idx]) << "\"";
      first_field = false;
    }
    oss << "}";
  }
  oss << "]";
  return oss.str();
}

std::string TextResultSerializer::escapeCsv(const std::string &value) {
  bool need_quotes = false;
  std::string escaped;
  for (size_t idx = 0; idx < value.size(); ++idx) {
    const char ch = value[idx];
    if (ch == '"' || ch == ',' || ch == '\r' || ch == '\n') {
      need_quotes = true;
    }
    if (ch == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(ch);
  }

  if (!need_quotes) {
    return escaped;
  }

  return std::string("\"") + escaped + "\"";
}

std::string TextResultSerializer::escapeJson(const std::string &value) {
  std::string escaped;
  for (size_t idx = 0; idx < value.size(); ++idx) {
    const char ch = value[idx];
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}
