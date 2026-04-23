#ifndef TEXT_RESULT_SERIALIZER_H
#define TEXT_RESULT_SERIALIZER_H

#include "TextResultTypes.h"

#include <string>

class TextResultSerializer {
 public:
  static std::string serialize(const TextSnapshot &snapshot,
                               TextFileFormat format,
                               bool include_timestamp);
  static std::string extension(TextFileFormat format);

 private:
  static std::string serializeTxt(const TextSnapshot &snapshot,
                                  bool include_timestamp);
  static std::string serializeCsv(const TextSnapshot &snapshot,
                                  bool include_timestamp);
  static std::string serializeJson(const TextSnapshot &snapshot,
                                   bool include_timestamp);
  static std::string escapeCsv(const std::string &value);
  static std::string escapeJson(const std::string &value);
};

#endif
