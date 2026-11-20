// Checks firmware/src/sketch.cpp on the computer, against how SexyTopo finishes lines.
//   g++ -std=c++11 -I../../firmware/src test_sketch.cpp ../../firmware/src/sketch.cpp -o /tmp/test_sketch && /tmp/test_sketch
#include <cstdio>
#include <cstdlib>

#include "sketch.h"

namespace {
int failures = 0;

void check(bool ok, const char *what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

// A straight stroke from a to b with many points, as the pen gives
void stroke(Sketch &s, Coord a, Coord b, float snap = 0.05, int steps = 50) {
  s.startPath(a);
  for (int i = 1; i <= steps; i++) s.lineTo({a.x + (b.x - a.x) * i / steps, a.y + (b.y - a.y) * i / steps});
  s.finishPath(snap);
}
}  // namespace

int main() {
  {
    Sketch s;
    stroke(s, {0, 0}, {1, 0});
    check(s.paths().size() == 1 && s.paths()[0].points.size() == 2, "straight stroke simplifies to its two ends");
  }
  {
    Sketch s;
    s.startPath({0.5, 0.5});
    s.finishPath(0.05);
    const Sketch::Path &p = s.paths()[0];
    check(p.points.size() == 2 && p.start() == p.end(), "a tap leaves a dot of two equal points");
  }
  {
    Sketch s;
    stroke(s, {0, 0}, {1, 0});
    stroke(s, {1.02, 0.01}, {1, 1});
    const Sketch::Path &p = s.paths()[0];
    check(s.paths().size() == 1, "a line starting near another's end joins it into one line");
    check(p.start() == Coord{0, 0} && p.points[1] == Coord{1, 0} && p.end() == Coord{1, 1}, "joined line runs on through the join");
    check(s.undo() && s.paths().size() == 1 && s.paths()[0].end() == Coord{1, 0}, "undo of a join gives back the line it joined");
  }
  {
    Sketch s;
    stroke(s, {0, 0}, {1, 0});
    stroke(s, {1, 1}, {1.02, 0.01});
    check(s.paths().size() == 1 && s.paths()[0].start() == Coord{1, 1} && s.paths()[0].end() == Coord{0, 0},
          "a line ending near another's end joins it, the other read backwards");
  }
  {
    Sketch s;
    s.startPath({0, 0});
    for (int i = 1; i <= 100; i++) {
      const float t = i / 100.0;
      const Coord square[] = {{t * 4, 0}, {1, (t - 0.25f) * 4}, {1 - (t - 0.5f) * 4, 1}, {0, 1 - (t - 0.75f) * 4}};
      s.lineTo(square[t <= 0.25 ? 0 : t <= 0.5 ? 1 : t <= 0.75 ? 2 : 3]);
    }
    s.lineTo({0.01, 0.02});
    s.finishPath(0.05);
    const Sketch::Path &p = s.paths()[0];
    check(p.isClosed() && p.points.size() == 5, "a line coming back near its start closes into a square loop");
    stroke(s, {0.01, 0.01}, {-1, -1});
    check(s.paths().size() == 2, "nothing joins a loop");
  }
  {
    Sketch s;
    stroke(s, {0, 0}, {1, 0});
    stroke(s, {0, 1}, {1, 1});
    stroke(s, {1.01, 0.01}, {1.01, 0.99});
    check(s.paths().size() == 1 && s.paths()[0].points.size() == 6, "a line between two ends joins all three into one");
    s.undo();
    check(s.paths().size() == 2, "undo gives back both lines");
  }
  {
    Sketch s;
    stroke(s, {0, 0}, {1, 0});
    stroke(s, {0, 1}, {1, 1});
    s.clear();
    check(s.paths().empty() && s.undo() && s.paths().size() == 2, "undo of clear brings the lines back");
    check(s.undo() && s.undo() && !s.undo() && s.paths().empty(), "undo runs back to nothing");
  }
  {
    Sketch s;
    stroke(s, {0, 0}, {1, 0});
    stroke(s, {1.2, 0}, {2, 0});
    check(s.paths().size() == 2, "ends further apart than the snap distance stay apart");
  }
  {
    Sketch s;
    stroke(s, {0, 0}, {1, 0});
    s.forgetHistory();  // as saved, then the drawing screen opened again
    stroke(s, {1.01, 0}, {1, 1});
    stroke(s, {0, 1}, {-1, 1});
    s.clear();
    stroke(s, {5, 5}, {6, 6});
    s.revert();
    check(s.paths().size() == 1 && s.paths()[0].points.size() == 2 && s.paths()[0].end() == Coord{1, 0} && !s.changed(),
          "revert (Cancel) puts the lines back as they were when opened");
  }
  std::printf("%d failed\n", failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
