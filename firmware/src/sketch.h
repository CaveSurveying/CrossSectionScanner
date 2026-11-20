// Lines drawn round a scan, following SexyTopo's sketch model (model/sketch/Sketch.java and
// PathDetail.java, upstream main 26c93a7a). Coordinates are metres from the scanner in SexyTopo's
// cross section view, x to the right and y down.
#pragma once
#include <vector>

struct Coord {
  float x, y;
};

class Sketch {
 public:
  static constexpr int MAX_POINTS = 6000;  // all lines together, to keep within the board's memory

  struct Path {
    std::vector<Coord> points;
  };

  void startPath(Coord start);
  void lineTo(Coord point);
  void finishPath();
  // Undo the last finished line or clear, false if there is nothing to undo
  bool undo();
  void clear();
  void forget();  // drop everything, history too, for a different scan

  const std::vector<Path> &paths() const { return finished; }
  const Path *activePath() const { return active ? &current : nullptr; }

 private:
  struct Step {
    std::vector<Path> cleared;  // empty for a line added, the lines removed for a clear
  };
  int pointCount() const;
  std::vector<Path> finished;
  std::vector<Step> history;
  Path current;
  bool active = false;
};
