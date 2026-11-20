// Holds one scan (several turns) and draws it, centred, in a square on the screen
#pragma once
#include <cyd_display.h>

class ScanView {
 public:
  static constexpr int MAX_POINTS = 4000;

  void clear(const String &name);
  void nextTurn() { turns++; }
  void addPoint(float deg, float mm);
  int count() const { return pointCount; }
  // Draw the scan in a size x size square at the top, with notes underneath
  void draw(cyd::Display &lcd, int size) const;

 private:
  struct Point {
    int16_t x, y;  // mm from the lidar, 0° up, clockwise
    uint8_t turn;
  };
  Point points[MAX_POINTS];
  int pointCount = 0, turns = 0;
  String name;
};
