#include "../TextResultCache.h"
#include "../TextUploadController.h"
#include "../TextResultSerializer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FAIL] " << message << std::endl;
    std::exit(1);
  }
}

TextRecord MakeRecord(int64_t timestamp_sec,
                      const std::string &timestamp_text,
                      const std::string &raw_text) {
  TextRecord record;
  record.timestamp_sec = timestamp_sec;
  record.timestamp_text = timestamp_text;
  record.raw_text = raw_text;
  return record;
}

void TestCountRetentionAndFieldExpansion() {
  TextResultCache cache(3);
  cache.setPolicy(TEXT_RETENTION_BY_COUNT);
  cache.setRetentionCount(2);

  cache.addRecord(MakeRecord(1, "2026_04_24_10_00_01", "a;b"));
  cache.addRecord(MakeRecord(2, "2026_04_24_10_00_02", "x;y;z"));
  cache.addRecord(MakeRecord(3, "2026_04_24_10_00_03", "last"));

  const TextSnapshot snapshot = cache.snapshot();
  Expect(snapshot.records.size() == 2, "count retention should keep last two records");
  Expect(snapshot.max_item_count == 3, "snapshot should track max expanded field count");
  Expect(snapshot.records[0].items.size() == 3, "first retained record should keep three split items");
  Expect(snapshot.records[1].items.size() == 1, "second retained record should keep one split item");
}

void TestTimeRangeRetention() {
  TextResultCache cache(10);
  cache.setPolicy(TEXT_RETENTION_BY_TIME_RANGE);
  cache.setRetentionTimeRangeSec(5);

  cache.addRecord(MakeRecord(10, "2026_04_24_10_00_10", "old"));
  cache.addRecord(MakeRecord(15, "2026_04_24_10_00_15", "keep1"));
  cache.addRecord(MakeRecord(20, "2026_04_24_10_00_20", "keep2"));

  const TextSnapshot snapshot = cache.snapshot();
  Expect(snapshot.records.size() == 2, "time range retention should drop expired records");
  Expect(snapshot.records[0].raw_text == "keep1", "time range should keep newer record");
  Expect(snapshot.records[1].raw_text == "keep2", "time range should keep latest record");
}

void TestCsvSerializationWithTimestamp() {
  TextResultCache cache(10);
  cache.setPolicy(TEXT_RETENTION_BY_COUNT);
  cache.setRetentionCount(10);
  cache.addRecord(MakeRecord(1, "2026_04_24_10_00_01", "alpha;beta"));
  cache.addRecord(MakeRecord(2, "2026_04_24_10_00_02", "gamma"));

  const std::string csv = TextResultSerializer::serialize(cache.snapshot(), TEXT_FILE_FORMAT_CSV, true);
  const std::string expected =
      "timestamp,col1,col2\r\n"
      "2026_04_24_10_00_01,alpha,beta\r\n"
      "2026_04_24_10_00_02,gamma,\r\n";
  Expect(csv == expected, "csv output should include header, timestamp column and empty trailing fields");
}

void TestJsonSerializationWithoutTimestamp() {
  TextResultCache cache(10);
  cache.setPolicy(TEXT_RETENTION_BY_COUNT);
  cache.setRetentionCount(10);
  cache.addRecord(MakeRecord(1, "2026_04_24_10_00_01", "hello;world"));

  const std::string json = TextResultSerializer::serialize(cache.snapshot(), TEXT_FILE_FORMAT_JSON, false);
  const std::string expected = "[{\"item1\":\"hello\",\"item2\":\"world\"}]";
  Expect(json == expected, "json output should be an array of objects without timestamp when disabled");
}

void TestTxtSerializationWithTimestamp() {
  TextResultCache cache(10);
  cache.setPolicy(TEXT_RETENTION_BY_COUNT);
  cache.setRetentionCount(10);
  cache.addRecord(MakeRecord(1, "2026_04_24_10_00_01", "plain;text"));

  const std::string txt = TextResultSerializer::serialize(cache.snapshot(), TEXT_FILE_FORMAT_TXT, true);
  const std::string expected = "2026_04_24_10_00_01 plain;text\r\n";
  Expect(txt == expected, "txt output should prefix each raw record with timestamp");
}

void TestRefreshStepAndFinalFlush() {
  TextUploadController controller;
  controller.setRefreshStep(3);

  Expect(!controller.onRecordAccepted(), "first record should not trigger upload");
  Expect(!controller.onRecordAccepted(), "second record should not trigger upload");
  Expect(controller.onRecordAccepted(), "third record should trigger upload");
  Expect(!controller.onStopRequested(), "stop should not trigger when no pending records");

  Expect(!controller.onRecordAccepted(), "counter should restart after upload");
  Expect(controller.onStopRequested(), "stop should trigger final upload for pending records");
  Expect(!controller.onStopRequested(), "second stop without new records should not trigger upload");
}

}  // namespace

int main() {
  TestCountRetentionAndFieldExpansion();
  TestTimeRangeRetention();
  TestCsvSerializationWithTimestamp();
  TestJsonSerializationWithoutTimestamp();
  TestTxtSerializationWithTimestamp();
  TestRefreshStepAndFinalFlush();
  std::cout << "[PASS] ftptrans text transfer logic tests" << std::endl;
  return 0;
}
