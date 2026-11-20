// Holds one scan (several turns) and draws it, centred, in a square on the screen
#pragma once
#include <cyd_display.h>

#include "sketch.h"

class ScanView {
 public:
  static constexpr int MAX_POINTS = 4000;
  static constexpr int NOTES_H = 22;  // two lines of notes under the view

  void clear(const String &name);
  // One line of scan text (see scan_store.h), T and P lines are used, others ignored
  void addLine(const String &line);
  int count() const { return pointCount; }
  // Draw the scan in a size x size square at the top, with notes underneath, and keep that fit
  void draw(cyd::Display &lcd, int size);
  // Between screen pixels and sketch coordinates (sketch.h), using the fit of the last draw
  Coord toSketch(int px, int py) const;
  void toScreen(Coord c, int &px, int &py) const;
  float metresPerPixel() const { return 1 / (scale * 1000); }
  bool inView(int px, int py) const { return px >= 0 && px < size && py >= 0 && py < size; }

 private:
  void addPoint(float deg, float mm);
  struct Point {
    int16_t x, y;  // mm from the lidar as seen on the screen (from the lidar bottom), 0° up
    uint8_t turn;
  };
  Point points[MAX_POINTS];
  int pointCount = 0, turns = 0;
  String name;
  int size = 0;                        // view size in px
  float scale = 1, midX = 0, midY = 0;  // px per mm, and the scan point (mm) at the view centre
};
