// Lines drawn round a scan, following SexyTopo's sketch model, upstream main 26c93a7a
// (model/sketch/Sketch.java, PathDetail.java and control/util/Space2DUtils.java). Coordinates are
// metres from the scanner in SexyTopo's cross section view, x to the right and y down.
// Only plain sketch lines so far, which have no direction and no inside.
#pragma once
#include <vector>

struct Coord {
  float x, y;
  bool operator==(const Coord &o) const { return x == o.x && y == o.y; }
};

class Sketch {
 public:
  static constexpr int MAX_POINTS = 6000;  // all lines together, to keep within the board's memory

  struct Path {
    std::vector<Coord> points;
    const Coord &start() const { return points.front(); }
    const Coord &end() const { return points.back(); }
    bool isClosed() const { return points.size() > 2 && start() == end(); }
  };

  void startPath(Coord start);
  void lineTo(Coord point);
  // Finish the line as SexyTopo does, snapping its ends to line ends within snapDistance (metres),
  // then simplifying it. A tap leaves a dot.
  void finishPath(float snapDistance);
  // Undo the last step (a line, a join or a clear), false if there is nothing to undo
  bool undo();
  void clear();
  void forget();  // drop everything, history too, for a different scan

  const std::vector<Path> &paths() const { return finished; }
  const Path *activePath() const { return active ? &current : nullptr; }

 private:
  // One undoable step. The lines it removed, with where they were, and how many it added at the end
  struct Step {
    std::vector<std::pair<int, Path>> removed;
    int added = 0;
  };
  int pointCount() const;
  void snapActivePath(float delta, int &startJoin, int &endJoin);
  int findLineToSnapTo(const Coord &point, float delta, const Coord *excluded) const;
  void apply(Step step, const Path &added);

  std::vector<Path> finished;
  std::vector<Step> history;
  Path current;
  bool active = false;
};
