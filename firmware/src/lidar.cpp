#include "lidar.h"

namespace {
constexpr long BAUD = 460800;
constexpr uint8_t CMD_STOP = 0x25, CMD_SCAN = 0x20, CMD_INFO = 0x50;
constexpr uint8_t SCAN_DESCRIPTOR[7] = {0xA5, 0x5A, 0x05, 0x00, 0x00, 0x40, 0x81};
constexpr int SETTLE_TURNS = 3;  // skipped while the motor gets up to speed, as on the computer
HardwareSerial &port = Serial2;
}  // namespace

void Lidar::begin() {
  port.setRxBufferSize(4096);
  port.begin(BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
}

void Lidar::command(uint8_t cmd) {
  const uint8_t bytes[2] = {0xA5, cmd};
  port.write(bytes, 2);
}

bool Lidar::readExactly(uint8_t *out, int n, unsigned long timeoutMs) {
  const unsigned long start = millis();
  int got = 0;
  while (got < n) {
    if (port.available()) out[got++] = port.read();
    else if (millis() - start > timeoutMs) return false;
    else delay(1);
  }
  return true;
}

void Lidar::drain() {
  delay(20);
  while (port.available()) port.read();
}

void Lidar::stop() {
  command(CMD_STOP);
  drain();
}

bool Lidar::readInfo(Info &info) {
  stop();
  command(CMD_INFO);
  uint8_t reply[27];
  if (!readExactly(reply, sizeof reply, 300) || reply[0] != 0xA5 || reply[1] != 0x5A) return false;
  const uint8_t *d = reply + 7;
  info.model = d[0];
  info.firmwareMinor = d[1];
  info.firmwareMajor = d[2];
  info.hardware = d[3];
  memcpy(info.serial, d + 4, 16);
  return true;
}

bool Lidar::capture(int wanted) {
  wanted = constrain(wanted, 1, MAX_TURNS);
  turns = 0;
  stop();
  command(CMD_SCAN);
  uint8_t descriptor[7];
  if (!readExactly(descriptor, 7, 1000) || memcmp(descriptor, SCAN_DESCRIPTOR, 7) != 0) {
    stop();
    return false;
  }

  // Slide a 5 byte window along the stream, taking a node whenever its check bits are right
  uint8_t node[5];
  int have = 0, seen = 0, count = 0;
  bool keeping = false;
  const unsigned long deadline = millis() + 5000 + wanted * 150;
  while (millis() < deadline) {
    if (!port.available()) {
      delay(1);
      continue;
    }
    node[have++] = port.read();
    if (have < 5) continue;
    const bool start = node[0] & 1;
    const bool valid = start != ((node[0] >> 1) & 1) && (node[1] & 1);
    if (!valid) {
      memmove(node, node + 1, 4);  // out of step, slip one byte
      have = 4;
      continue;
    }
    have = 0;
    if (start) {
      if (keeping) {
        starts[++turns] = count;
        if (turns == wanted) {
          stop();
          return true;
        }
      } else if (++seen > SETTLE_TURNS) {
        keeping = true;
        starts[0] = count;
      }
    }
    if (keeping && count < MAX_POINTS) {
      buffer[count++] = {(uint16_t)((node[1] >> 1) | (node[2] << 7)), (uint16_t)(node[3] | (node[4] << 8)),
                         (uint8_t)(node[0] >> 2)};
    }
  }
  stop();
  return false;
}
