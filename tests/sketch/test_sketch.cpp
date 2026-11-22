// Checks firmware/src/sketch.cpp on the computer, against how SexyTopo finishes lines.
//   g++ -std=c++11 -I../../firmware/src test_sketch.cpp ../../firmware/src/sketch.cpp -o /tmp/test_sketch && /tmp/test_sketch
#include <cstdio>
#include <cstdlib>
#include <vector>

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

// A stroke through the corners, of the given type
void strokeThrough(Sketch &s, std::vector<Coord> corners, LineType type, float snap = 0.05, float minimum = 0.1) {
  s.startPath(corners[0], type);
  for (size_t c = 1; c < corners.size(); c++)
    for (int i = 1; i <= 20; i++) {
      const Coord &a = corners[c - 1], &b = corners[c];
      s.lineTo({a.x + (b.x - a.x) * i / 20, a.y + (b.y - a.y) * i / 20});
    }
  s.finishPath(snap, minimum);
}

bool runs(const Sketch::Path &p, Coord from, Coord to) { return p.start() == from && p.end() == to; }

// Whether the line goes straight from a to b somewhere along it
bool goes(const Sketch::Path &p, Coord a, Coord b) {
  for (size_t i = 0; i + 1 < p.points.size(); i++)
    if (p.points[i] == a && p.points[i + 1] == b) return true;
  return false;
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
  // Line types
  {
    Sketch a, b;
    strokeThrough(a, {{-1, -1}, {1, -1}}, LineType::WALL);
    strokeThrough(b, {{1, -1}, {-1, -1}}, LineType::WALL);
    check(runs(a.paths()[0], {-1, -1}, {1, -1}) && runs(b.paths()[0], {-1, -1}, {1, -1}),
          "a wall above the station runs the same way whichever way it was drawn");
    strokeThrough(b, {{1, 1}, {-1, 1}}, LineType::WALL);
    check(runs(b.paths()[1], {1, 1}, {-1, 1}), "and a wall below it the other way, the passage on the same side");
  }
  {
    Sketch s;
    strokeThrough(s, {{-1, -1}, {0, -1}}, LineType::WALL);
    strokeThrough(s, {{0.01, -1.01}, {1, -1}}, LineType::SKETCH);
    check(s.paths().size() == 2, "a sketch line doesn't join a wall");
    strokeThrough(s, {{1, -1.2}, {0.01, -1.01}}, LineType::WALL);
    check(s.paths().size() == 2 && runs(s.paths()[1], {-1, -1}, {1, -1.2}),
          "a wall drawn backwards is turned round, then joins head to tail");
  }
  {
    Sketch s;
    strokeThrough(s, {{0, 0}, {0, 0.05}}, LineType::PIT);
    s.startPath({0.5, 0.5}, LineType::WALL);
    s.finishPath(0.05, 0.1);
    check(s.paths().empty() && !s.changed(), "a typed line smaller than the minimum, or a tap, is dropped");
  }
  {
    Sketch a, b;
    const std::vector<Coord> square{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.02, 0.02}};
    strokeThrough(a, square, LineType::ROCK_BORDER);
    strokeThrough(b, {square.rbegin(), square.rend()}, LineType::ROCK_BORDER);
    const Sketch::Path &p = a.paths()[0], &q = b.paths()[0];
    check(p.isClosed() && q.isClosed(), "a rock border whose ends nearly meet is closed");
    check(goes(p, {1, 0}, {1, 1}) && goes(q, {1, 0}, {1, 1}),
          "and runs clockwise on the screen (rock on its right) whichever way it was drawn");
  }
  {
    Sketch s;
    strokeThrough(s, {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.03, 0.03}}, LineType::PIT);
    const Sketch::Path &p = s.paths()[0];
    check(goes(p, {1, 1}, {1, 0}), "a pit drawn round its hole runs anticlockwise, ticks (on its left) inside");
  }
  {
    Sketch s;
    strokeThrough(s, {{-2, 1}, {2, 1}}, LineType::WALL);
    strokeThrough(s, {{-0.5, 1.02}, {-0.5, 0.5}, {0.5, 0.5}, {0.5, 1.02}}, LineType::PIT);
    const Sketch::Path &p = s.paths()[1];
    check(runs(p, {0.5, 1.02}, {-0.5, 1.02}), "a pit edge closed by a wall is turned to have its ticks inside");
  }
  {
    Sketch s;
    strokeThrough(s, {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.03, 0.03}}, LineType::FLOOR_STEP);
    check(goes(s.paths()[0], {1, 0}, {1, 1}), "a floor step stays as drawn");
    check(LineTypeInfo::fromName("CEILING_STEP") == LineType::CEILING_STEP &&
              LineTypeInfo::fromName("LAVA") == LineType::SKETCH && !LineTypeInfo::of(LineType::SKETCH).isSemantic() &&
              LineTypeInfo::of(LineType::SLOPE).isSemantic(),
          "line types by name");
  }
  std::printf("%d failed\n", failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
