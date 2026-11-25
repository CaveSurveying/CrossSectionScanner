// Holds one scan (several turns) and draws it, centred, in a square on the screen
#pragma once
#include <cyd_display.h>

#include "sketch.h"
#include "view_fit.h"

class ScanView {
 public:
  static constexpr int MAX_POINTS = 4000;
  static constexpr int NOTES_H = 22;  // two lines of notes under the view

  void clear(const String &name);
  // One line of scan text (see scan_store.h), T and P lines are used, others ignored
  void addLine(const String &line);
  int count() const { return pointCount; }
  // Fit the scan and the drawing into a size x size view at x1, kept until the next fit
  void fit(int size, const Sketch &sketch);
  // Draw the scan in the view at the top, with notes underneath, fitting the scan alone if not fitted
  void draw(cyd::Display &lcd, int size);
  // Between screen pixels and sketch coordinates (sketch.h)
  Coord toSketch(int px, int py) const { return view.toSketch(px, py); }
  void toScreen(Coord c, int &px, int &py) const { view.toScreen(c, px, py); }
  float metresPerPixel() const { return view.metresPerPixel(); }
  bool inView(int px, int py) const { return px >= 0 && px < size && py >= 0 && py < size; }
  // Zoom and move, kept until the next fit, taking effect on the next draw
  bool zoomIn() { return view.zoomIn(); }
  bool zoomOut() { return view.zoomOut(); }
  int zoomStep() const { return view.zoomStep(); }
  void moveBy(float dx, float dy) { view.moveBy(dx, dy); }

 private:
  void addPoint(float deg, float mm);
  struct Point {
    int16_t x, y;  // mm from the lidar as seen on the screen (from the lidar bottom), 0° up
    uint8_t turn;
  };
  Point points[MAX_POINTS];
  int pointCount = 0, turns = 0;
  String name;
  int size = 0;  // view size in px
  ViewFit view;
  bool needsFit = true;  // a new scan, not yet fitted
};
