// RPLIDAR C1 on a board UART, standard scan mode (same protocol as tools/scan_once.py)
#pragma once
#include <Arduino.h>

class Lidar {
 public:
  // Lidar TX goes to the board's RX pin, see docs/WIRING.md
#ifdef BOARD_E32R24P
  static constexpr int PIN_RX = 19, PIN_TX = 27;  // SPI connector pins 2 and 4, SD slot left empty
#else
  static constexpr int PIN_RX = 27, PIN_TX = 22;  // old CYD, wiring not yet confirmed
#endif
  static constexpr int MAX_TURNS = 5, MAX_POINTS = 3000;

  struct Info {
    uint8_t model, firmwareMajor, firmwareMinor, hardware;
    uint8_t serial[16];
  };
  struct Point {
    uint16_t angleQ6;     // degrees x 64
    uint16_t distanceQ2;  // mm x 4, 0 when nothing returned
    uint8_t quality;
    float angleDeg() const { return angleQ6 / 64.0f; }
    float distanceMm() const { return distanceQ2 / 4.0f; }
  };

  void begin();
  // Ask the lidar for its details, false if it does not answer
  bool readInfo(Info &info);
  // Start the motor, skip the first turns while it settles, keep the next `turns` turns, stop the motor
  bool capture(int turns);
  void stop();

  int turnCount() const { return turns; }
  // Points of turn t are points()[turnStart(t)] up to points()[turnStart(t + 1)]
  int turnStart(int t) const { return starts[t]; }
  const Point *points() const { return buffer; }

 private:
  void command(uint8_t cmd);
  bool readExactly(uint8_t *out, int n, unsigned long timeoutMs);
  void drain();

  Point buffer[MAX_POINTS];
  int starts[MAX_TURNS + 1] = {};
  int turns = 0;
};
