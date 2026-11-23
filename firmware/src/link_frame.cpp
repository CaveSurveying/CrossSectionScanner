#include "link_frame.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace link_frame {
namespace {
const char START[] = "XS1 ";
constexpr size_t START_LENGTH = 4;
constexpr int NESTING = 32;  // a drawing point sits 6 deep in a section, ArduinoJson allows 10 by default

// Header "XS1 <length> <crc>", length 1 to 9 digits and crc 8 lower-case hex digits
bool parseHeader(const std::string &header, size_t &length, uint32_t &crc) {
  if (header.compare(0, START_LENGTH, START) != 0) return false;
  size_t i = START_LENGTH, digits = 0;
  length = 0;
  for (; i < header.size() && header[i] >= '0' && header[i] <= '9'; i++, digits++) length = length * 10 + (header[i] - '0');
  if (digits < 1 || digits > 9 || i >= header.size() || header[i] != ' ') return false;
  i++;
  if (header.size() - i != 8) return false;
  crc = 0;
  for (; i < header.size(); i++) {
    const char c = header[i];
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else return false;
    crc = crc << 4 | v;
  }
  return true;
}
}  // namespace

uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = crc >> 1 ^ (0xEDB88320 & (0 - (crc & 1)));
  }
  return ~crc;
}

std::string frame(const std::string &json) {
  char header[MAX_HEADER + 1];
  snprintf(header, sizeof header, "XS1 %u %08x\n", (unsigned)json.size(),
           (unsigned)crc32((const uint8_t *)json.data(), json.size()));
  return header + json;
}

std::vector<Result> Reader::feed(const uint8_t *data, size_t length) {
  buffer.append((const char *)data, length);
  std::vector<Result> found;
  while (true) {
    const size_t end = buffer.find('\n');
    if (end == std::string::npos) {
      if (buffer.size() > MAX_HEADER) {
        found.emplace_back();
        found.back().dropped = "bad_header";
        skip();
        continue;
      }
      return found;
    }
    size_t bodyLength;
    uint32_t crc;
    if (end > MAX_HEADER || !parseHeader(buffer.substr(0, end), bodyLength, crc) || bodyLength > MAX_JSON) {
      found.emplace_back();
      found.back().dropped = "bad_header";
      skip();
      continue;
    }
    const size_t start = end + 1;
    if (buffer.size() < start + bodyLength) return found;
    const std::string body = buffer.substr(start, bodyLength);
    buffer.erase(0, start + bodyLength);
    found.emplace_back();
    Result &result = found.back();
    if (crc32((const uint8_t *)body.data(), body.size()) != crc) {
      result.dropped = "bad_checksum";
      continue;
    }
    if (deserializeJson(result.message, body, DeserializationOption::NestingLimit(NESTING)) || !result.message.is<JsonObject>() ||
        !result.message["type"].is<const char *>()) {
      result.message.clear();
      result.dropped = "unknown_format";
    }
  }
}

// Drop bytes up to the next frame start after the first byte, or all but a possible part XS1 at the end
void Reader::skip() {
  const size_t next = buffer.find(START, 1);
  if (next != std::string::npos) {
    buffer.erase(0, next);
    return;
  }
  for (size_t keep = START_LENGTH - 1; keep > 0; keep--) {
    if (buffer.size() >= keep && buffer.compare(buffer.size() - keep, keep, START, keep) == 0) {
      buffer.erase(0, buffer.size() - keep);
      return;
    }
  }
  buffer.clear();
}
}  // namespace link_frame
