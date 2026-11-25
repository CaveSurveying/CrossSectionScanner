#include "view_fit.h"

#include <algorithm>
#include <cmath>

void ViewFit::Box::add(float x, float y) {
  if (empty) {
    minX = maxX = x, minY = maxY = y;
    empty = false;
    return;
  }
  minX = std::min(minX, x), maxX = std::max(maxX, x);
  minY = std::min(minY, y), maxY = std::max(maxY, y);
}

void ViewFit::Box::add(const Sketch &sketch) {
  auto addCoord = [&](Coord c) { add(c.x * 1000, -c.y * 1000); };
  for (const Sketch::Path &path : sketch.paths())
    for (const Coord &c : path.points) addCoord(c);
  for (const Sketch::Extras::Outline &outline : sketch.extras().outlines)
    for (const Coord &c : outline.points) addCoord(c);
  for (const Sketch::Extras::Mark &mark : sketch.extras().marks) addCoord(mark.at);
}

void ViewFit::fit(int viewSize, const Box &fitBox, float margin) {
  size = viewSize;
  box = fitBox;
  if (box.empty) box.add(0, 0);
  const float span = std::fmax(std::fmax(box.maxX - box.minX, box.maxY - box.minY), 200);
  fitScale = size * (1 - 2 * margin) / span;
  step = 0;
  midX = (box.minX + box.maxX) / 2, midY = (box.minY + box.maxY) / 2;
  setScale();
}

bool ViewFit::zoomIn() {
  if (step >= MAX_STEP) return false;
  step++;
  setScale();
  keepInView();
  return true;
}

bool ViewFit::zoomOut() {
  if (step <= MIN_STEP) return false;
  step--;
  setScale();
  return true;
}

void ViewFit::moveBy(float dx, float dy) {
  midX -= dx / scale, midY += dy / scale;
  keepInView();
}

void ViewFit::setScale() { scale = fitScale * std::ldexp(1.0f, step); }

// The view centre goes at most a quarter of the view beyond the box
void ViewFit::keepInView() {
  const float quarter = visibleSpan() / 4;
  midX = std::fmin(std::fmax(midX, box.minX - quarter), box.maxX + quarter);
  midY = std::fmin(std::fmax(midY, box.minY - quarter), box.maxY + quarter);
}

void ViewFit::toScreen(Coord c, int &px, int &py) const {
  px = lroundf(screenX(c.x * 1000));
  py = lroundf(screenY(-c.y * 1000));
}
