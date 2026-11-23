// Frames of the link with SexyTopo (docs/LINK.md), as tools/xscan_link.py, the reference both ends
// follow. Plain C++ so it can be checked on the computer (tests/firmware_link).
#pragma once
#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace link_frame {
constexpr size_t MAX_JSON = 262144;
constexpr size_t MAX_HEADER = 32;

uint32_t crc32(const uint8_t *data, size_t length);
// The bytes of one frame for a message's JSON
std::string frame(const std::string &json);

// What the bytes completed, a message or a frame dropped with its reason
struct Result {
  const char *dropped = nullptr;  // bad_header, bad_checksum or unknown_format, null for a message
  JsonDocument message;
};

// Joins received bytes into messages
class Reader {
 public:
  std::vector<Result> feed(const uint8_t *data, size_t length);
  // Bytes held for a frame not yet complete, so a caller can drop them after a timeout
  size_t waiting() const { return buffer.size(); }
  void clear() { buffer.clear(); }

 private:
  void skip();
  std::string buffer;
};
}  // namespace link_frame
