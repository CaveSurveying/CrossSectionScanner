// Holds one scan (several turns) and draws it, centred, in a square on the screen
#pragma once
#include <cyd_display.h>

class ScanView {
 public:
  static constexpr int MAX_POINTS = 4000;
  static constexpr int NOTES_H = 22;  // two lines of notes under the view

  void clear(const String &name);
  // One line of scan text (see scan_store.h), T and P lines are used, others ignored
  void addLine(const String &line);
  int count() const { return pointCount; }
  // Draw the scan in a size x size square at the top, with notes underneath
  void draw(cyd::Display &lcd, int size) const;

 private:
  void addPoint(float deg, float mm);
  struct Point {
    int16_t x, y;  // mm from the lidar as seen on the screen (from the lidar bottom), 0° up
    uint8_t turn;
  };
  Point points[MAX_POINTS];
  int pointCount = 0, turns = 0;
  String name;
};
