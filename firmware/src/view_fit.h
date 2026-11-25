// How a scan is fitted into the square view, zoomed and moved. Plain C++, so it can be checked on
// the computer. Scan positions are mm from the lidar with y up, sketch coordinates (sketch.h) metres
// with y down, as SexyTopo's cross section view.
#pragma once
#include "sketch.h"

class ViewFit {
 public:
  // Zoom steps, each x2, from x1/4 to x8, x1 being the fit
  static constexpr int MIN_STEP = -2, MAX_STEP = 3;

  // What is fitted, in mm
  struct Box {
    float minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool empty = true;
    void add(float x, float y);
    void add(const Sketch &sketch);  // its lines, areas, text and symbols
  };

  // Fit the box into a size x size view, margin a fraction of the view each side, at x1 centred
  void fit(int size, const Box &box, float margin);
  bool fitted() const { return size > 0; }
  // x2 or half about the view centre, false if already at the end
  bool zoomIn();
  bool zoomOut();
  int zoomStep() const { return step; }
  // Move what is shown with the pen, by pixels, the box keeping at least a quarter of the view
  void moveBy(float dx, float dy);

  float screenX(float mm) const { return size / 2.0f + (mm - midX) * scale; }
  float screenY(float mm) const { return size / 2.0f - (mm - midY) * scale; }
  float mmX(float px) const { return midX + (px - size / 2.0f) / scale; }
  float mmY(float py) const { return midY - (py - size / 2.0f) / scale; }
  Coord toSketch(int px, int py) const { return {mmX(px) / 1000, -mmY(py) / 1000}; }
  void toScreen(Coord c, int &px, int &py) const;
  float metresPerPixel() const { return 1 / (scale * 1000); }
  float visibleSpan() const { return size / scale; }  // mm across the view

 private:
  void setScale();
  void keepInView();
  int size = 0, step = 0;
  Box box;
  float fitScale = 1;
  float scale = 1, midX = 0, midY = 0;  // px per mm, and the mm at the view centre
};
