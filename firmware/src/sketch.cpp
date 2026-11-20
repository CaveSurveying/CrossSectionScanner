#include "sketch.h"

#include <algorithm>
#include <cmath>

namespace {
using Path = Sketch::Path;

float distance(const Coord &a, const Coord &b) { return hypotf(a.x - b.x, a.y - b.y); }

// Space2DUtils.getDistanceFromLine, distance from a point to a line segment
float distanceFromLine(const Coord &p, const Coord &a, const Coord &b) {
  const float c = b.x - a.x, d = b.y - a.y;
  const float lenSq = c * c + d * d;
  const float param = lenSq != 0 ? ((p.x - a.x) * c + (p.y - a.y) * d) / lenSq : -1;
  Coord nearest = param < 0 ? a : param > 1 ? b : Coord{a.x + param * c, a.y + param * d};
  return distance(p, nearest);
}

// Space2DUtils.douglasPeukerIteration, appending the simplified points after path[first]
void douglasPeucker(const std::vector<Coord> &path, int first, int last, float epsilon, std::vector<Coord> &out) {
  int indexMax = first;
  float distMax = 0;
  for (int i = first + 1; i <= last; i++) {
    const float dist = distanceFromLine(path[i], path[first], path[last]);
    if (dist > distMax) distMax = dist, indexMax = i;
  }
  if (distMax > epsilon) {
    douglasPeucker(path, first, indexMax, epsilon, out);
    douglasPeucker(path, indexMax, last, epsilon, out);
  } else {
    out.push_back(path[last]);
  }
}

// Space2DUtils.simplificationEpsilon, from the size of the line
float simplificationEpsilon(const Path &path) {
  float minX = path.start().x, maxX = minX, minY = path.start().y, maxY = minY;
  for (const Coord &p : path.points) {
    minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
  }
  return std::max(std::max(maxX - minX, maxY - minY) / 500, 0.001f);
}

void simplify(Path &path) {
  std::vector<Coord> out{path.start()};
  douglasPeucker(path.points, 0, path.points.size() - 1, simplificationEpsilon(path), out);
  path.points = out;
}

// PathDetail.reachesBeyond, whether the line ever gets distance or more from its start
bool reachesBeyond(const Path &path, float d) {
  return std::any_of(path.points.begin(), path.points.end(), [&](const Coord &p) { return distance(path.start(), p) >= d; });
}

// PathDetail.getEndToJoinAfter and getEndToJoinBefore, which for a line with no direction are
// both the end nearest the point
const Coord &endToJoin(const Path &path, const Coord &point) {
  return distance(point, path.start()) < distance(point, path.end()) ? path.start() : path.end();
}

// PathDetail.canJoin, plain lines can always join unless either is a loop
bool canJoin(const Path &a, const Path &b) { return !a.isClosed() && !b.isClosed(); }

// PathDetail.snapStartTo and snapEndTo, dropping the points drawn within delta of the join first
void snapStartTo(Path &path, Coord join, float delta) {
  while (path.points.size() > 1 && distance(join, path.start()) < delta) path.points.erase(path.points.begin());
  path.points.insert(path.points.begin(), join);
}

void snapEndTo(Path &path, Coord join, float delta) {
  while (path.points.size() > 1 && distance(join, path.end()) < delta) path.points.pop_back();
  path.points.push_back(join);
}

// PathDetail.pointsRunningInto and pointsRunningOutOf, the line's points ordered to finish (or
// start) at the given end, false if it isn't one of its ends
bool pointsRunning(const Path &path, const Coord &end, bool into, std::vector<Coord> &out) {
  out = path.points;
  if ((into ? path.end() : path.start()) == end) return true;
  if ((into ? path.start() : path.end()) == end) {
    std::reverse(out.begin(), out.end());
    return true;
  }
  return false;
}
}  // namespace

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
  if (point == current.end()) return;  // pen held still
  current.points.push_back(point);
}

// Sketch.findLineToSnapTo, the finished line with a joinable end nearest the point within delta
int Sketch::findLineToSnapTo(const Coord &point, float delta, const Coord *excluded) const {
  int best = -1;
  float bestDistance = delta;
  for (int i = 0; i < (int)finished.size(); i++) {
    const Path &line = finished[i];
    if (!canJoin(current, line)) continue;
    const Coord &end = endToJoin(line, point);
    if (excluded && end == *excluded) continue;
    const float d = distance(point, end);
    if (d < bestDistance) best = i, bestDistance = d;
  }
  return best;
}

// Sketch.snapActivePath. A line coming back near its own start closes into a loop, otherwise
// each end snaps to the nearest joinable end of another line
void Sketch::snapActivePath(float delta, int &startJoin, int &endJoin) {
  startJoin = endJoin = -1;
  if (current.points.size() < 2 || current.isClosed()) return;
  const Coord start = current.start(), end = current.end();

  const float distanceToStart = distance(end, start);
  int endTarget = findLineToSnapTo(end, delta, nullptr);
  if (reachesBeyond(current, delta) && distanceToStart < delta &&
      (endTarget < 0 || distanceToStart < distance(end, endToJoin(finished[endTarget], end)))) {
    snapEndTo(current, start, delta);
    return;
  }

  const int startTarget = findLineToSnapTo(start, delta, nullptr);
  if (startTarget >= 0) {
    snapStartTo(current, endToJoin(finished[startTarget], start), delta);
    startJoin = startTarget;
  }

  // the end mustn't snap to the very point the start just did
  endTarget = findLineToSnapTo(end, delta, &current.start());
  if (endTarget >= 0) {
    snapEndTo(current, endToJoin(finished[endTarget], end), delta);
    endJoin = endTarget;
  }
}

// Sketch.finishPath with PathDetail.joinedWith. Joined lines are replaced by the one line they make
void Sketch::finishPath(float snapDistance) {
  if (!active) return;
  active = false;
  const bool isDot = current.points.size() == 1;
  if (isDot) current.points.push_back(current.start());  // as SexyTopo, a tap is a line of two equal points

  int startJoin = -1, endJoin = -1;
  if (!isDot) snapActivePath(snapDistance, startJoin, endJoin);
  simplify(current);

  Step step;
  Path joined{current.points};
  std::vector<Coord> leadingIn, leadingOut;
  const bool joinedBefore = startJoin >= 0 && pointsRunning(finished[startJoin], current.start(), true, leadingIn);
  if (joinedBefore) {
    leadingIn.insert(leadingIn.end(), current.points.begin() + 1, current.points.end());
    joined.points = leadingIn;
    step.removed.push_back({startJoin, finished[startJoin]});
  }
  // joined at both ends to the same line, the start has already closed it into a loop
  const bool closedBefore = joinedBefore && endJoin == startJoin;
  if (endJoin >= 0 && !closedBefore && pointsRunning(finished[endJoin], current.end(), false, leadingOut)) {
    joined.points.insert(joined.points.end(), leadingOut.begin() + 1, leadingOut.end());
    step.removed.push_back({endJoin, finished[endJoin]});
  }
  current.points.clear();
  step.added = 1;
  apply(step, joined);
}

void Sketch::apply(Step step, const Path &added) {
  std::sort(step.removed.begin(), step.removed.end(), [](const std::pair<int, Path> &a, const std::pair<int, Path> &b) { return a.first < b.first; });
  for (auto it = step.removed.rbegin(); it != step.removed.rend(); ++it) finished.erase(finished.begin() + it->first);
  if (step.added) finished.push_back(added);
  history.push_back(step);
}

bool Sketch::undo() {
  if (history.empty()) return false;
  const Step &step = history.back();
  finished.resize(finished.size() - step.added);
  for (const std::pair<int, Path> &r : step.removed) finished.insert(finished.begin() + r.first, r.second);  // in order of position
  history.pop_back();
  return true;
}

void Sketch::clear() {
  if (finished.empty()) return;
  Step step;
  for (int i = 0; i < (int)finished.size(); i++) step.removed.push_back({i, finished[i]});
  apply(step, {});
}

void Sketch::forget() {
  finished.clear();
  history.clear();
  current.points.clear();
  active = false;
}
