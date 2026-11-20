#include "sketch.h"

int Sketch::pointCount() const {
  int n = current.points.size();
  for (const Path &p : finished) n += p.points.size();
  return n;
}

void Sketch::startPath(Coord start) {
  current.points.clear();
  current.points.push_back(start);
  active = true;
}

void Sketch::lineTo(Coord point) {
  if (!active || pointCount() >= MAX_POINTS) return;
  const Coord &last = current.points.back();
  if (point.x == last.x && point.y == last.y) return;  // pen held still
  current.points.push_back(point);
}

void Sketch::finishPath() {
  if (!active) return;
  active = false;
  finished.push_back(current);
  history.push_back({});
  current.points.clear();
}

bool Sketch::undo() {
  if (history.empty()) return false;
  Step &step = history.back();
  if (step.cleared.empty()) finished.pop_back();
  else finished = step.cleared;
  history.pop_back();
  return true;
}

void Sketch::clear() {
  if (finished.empty()) return;
  history.push_back({finished});
  finished.clear();
}

void Sketch::forget() {
  finished.clear();
  history.clear();
  current.points.clear();
  active = false;
}
