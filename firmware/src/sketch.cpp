#include "sketch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
using Path = Sketch::Path;
using Info = LineTypeInfo;

// LineType.java, its constructor values and stamps (Stamp.ticked, hachured and toothed)
const Info TYPES[Info::COUNT] = {
    {"SKETCH", "Sketch", false, false, false, 1.0f, 0, 0, Info::NONE, 0, 0, 0, 0},
    {"WALL", "Wall", true, false, false, 1.5f, 0, 0, Info::NONE, 0, 0, 0, 0},
    {"FLOOR_STEP", "Floor step", false, false, false, 1.0f, 0, 0, Info::TICKED, 1.2f, 1.2f, -0.35f, 0},
    {"PIT", "Pit", false, true, false, 1.0f, 0, 0, Info::TICKED, 1.2f, 1.2f, -0.6f, 0},
    {"CHIMNEY", "Chimney", false, true, false, 1.0f, 0.8f, 0.4f, Info::TICKED, 1.2f, 0.8f, 0.5f, 0},
    {"CEILING_STEP", "Ceiling step", false, false, false, 1.0f, 0.8f, 0.4f, Info::TICKED, 1.2f, 0.8f, 0.3f, 0},
    {"OVERHANG", "Overhang", false, false, false, 1.0f, 0, 0, Info::TOOTHED, 1.2f, 1.2f, -0.9f, 0},
    {"SLOPE", "Slope", false, false, false, 1.0f, 0, 0, Info::HACHURED, 1.0f, 1.0f, 1.2f, 0.6f},
    {"ROCK_BORDER", "Rock border", false, false, true, 0.75f, 0, 0, Info::NONE, 0, 0, 0, 0},
    {"ROCK_EDGE", "Rock edge", false, false, false, 0.5f, 0, 0, Info::NONE, 0, 0, 0, 0},
    {"PRESUMED_WALL", "Presumed wall", true, false, false, 1.0f, 0.8f, 0.4f, Info::NONE, 0, 0, 0, 0},
    {"UNDERLYING_WALL", "Underlying wall", true, false, false, 1.5f, 0.4f, 0.4f, Info::NONE, 0, 0, 0, 0},
};

const Info &info(const Path &path) { return Info::of(path.type); }

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

// Shape.getMaxDimension
float maxDimension(const Path &path) {
  float minX = path.start().x, maxX = minX, minY = path.start().y, maxY = minY;
  for (const Coord &p : path.points) {
    minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
  }
  return std::max(maxX - minX, maxY - minY);
}

// Space2DUtils.simplificationEpsilon, from the size of the line
float simplificationEpsilon(const Path &path) { return std::max(maxDimension(path) / 500, 0.001f); }

void simplify(Path &path) {
  std::vector<Coord> out{path.start()};
  douglasPeucker(path.points, 0, path.points.size() - 1, simplificationEpsilon(path), out);
  path.points = out;
}

// PathDetail.reachesBeyond, whether the line ever gets distance or more from its start
bool reachesBeyond(const Path &path, float d) {
  return std::any_of(path.points.begin(), path.points.end(), [&](const Coord &p) { return distance(path.start(), p) >= d; });
}

const Coord &endNearest(const Path &path, const Coord &point) {
  return distance(point, path.start()) < distance(point, path.end()) ? path.start() : path.end();
}

// PathDetail.getEndToJoinAfter and getEndToJoinBefore. A line whose direction matters joins head
// to tail, so a line joins on after its end and before its start. Otherwise the nearest end.
const Coord &endToJoinAfter(const Path &path, const Coord &point) {
  return info(path).isOriented() ? path.end() : endNearest(path, point);
}
const Coord &endToJoinBefore(const Path &path, const Coord &point) {
  return info(path).isOriented() ? path.start() : endNearest(path, point);
}

// PathDetail.canJoin, lines of the same type and colour unless either is a loop
bool canJoin(const Path &a, const Path &b) {
  return a.type == b.type && a.colour == b.colour && !a.isClosed() && !b.isClosed();
}

// LineOrienter.orientToInterior, with the station (0, 0) as the only leg. Each segment votes,
// weighted by its length, for the side the station is on, and the line is turned round if needed
// to have it on the side Therion expects the passage
void orientToInterior(Path &path) {
  if (path.points.size() < 2) return;
  float weightedSideSum = 0;
  for (size_t i = 0; i + 1 < path.points.size(); i++) {
    const Coord &from = path.points[i], &to = path.points[i + 1];
    const float midX = (from.x + to.x) / 2, midY = (from.y + to.y) / 2;
    const float cross = (to.x - from.x) * (0 - midY) - (to.y - from.y) * (0 - midX);
    weightedSideSum += (cross > 0 ? 1 : cross < 0 ? -1 : 0) * distance(from, to);
  }
  if (weightedSideSum < 0) std::reverse(path.points.begin(), path.points.end());
}

// PolygonUtils.signedArea, positive for a loop clockwise on the screen
double signedArea(const std::vector<Coord> &polygon) {
  double twice = 0;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    twice += (double)polygon[j].x * polygon[i].y - (double)polygon[i].x * polygon[j].y;
  return twice / 2;
}

// LoopOrienter.WallPosition, the point on a wall nearest a point, and how far along the wall
struct WallPosition {
  Coord point;
  int segment = -1;
  float position = 0, distance = 0;  // position is the segment plus the fraction along it
};

WallPosition nearestOnWall(const std::vector<Coord> &wall, const Coord &target) {
  WallPosition best;
  for (size_t i = 0; i + 1 < wall.size(); i++) {
    const Coord &a = wall[i], &b = wall[i + 1];
    const float dx = b.x - a.x, dy = b.y - a.y, lengthSquared = dx * dx + dy * dy;
    float fraction = lengthSquared == 0 ? 0 : ((target.x - a.x) * dx + (target.y - a.y) * dy) / lengthSquared;
    fraction = std::max(0.0f, std::min(1.0f, fraction));
    const Coord point{a.x + fraction * dx, a.y + fraction * dy};
    const float d = distance(point, target);
    if (best.segment < 0 || d < best.distance) best.point = point, best.segment = i, best.position = i + fraction, best.distance = d;
  }
  return best;
}

// LoopOrienter.findLoop, the closed outline the line makes by itself or with the stretch of wall
// between its ends, false if it makes none
bool findLoop(const std::vector<Coord> &points, const std::vector<Path> &lines, float closeDistance,
              std::vector<Coord> &loop) {
  if (distance(points.front(), points.back()) <= closeDistance) {
    loop = points;
    return true;
  }
  for (const Path &line : lines) {
    if (!info(line).isWall) continue;
    const std::vector<Coord> &wall = line.points;
    const WallPosition fromEnd = nearestOnWall(wall, points.back()), fromStart = nearestOnWall(wall, points.front());
    if (fromEnd.segment < 0 || fromStart.segment < 0 || fromEnd.distance > closeDistance ||
        fromStart.distance > closeDistance)
      continue;
    // carry on from the line's end along the wall back to its start
    loop = points;
    loop.push_back(fromEnd.point);
    if (fromEnd.position < fromStart.position)
      for (int i = fromEnd.segment + 1; i <= fromStart.segment; i++) loop.push_back(wall[i]);
    else
      for (int i = fromEnd.segment; i > fromStart.segment; i--) loop.push_back(wall[i]);
    loop.push_back(fromStart.point);
    return true;
  }
  return false;
}

// LoopOrienter.orientInsideOfLoop, a line drawn round something turned to have it on the given side.
// Left as drawn if it encloses nothing
void orientInsideOfLoop(Path &path, bool insideOnRight, const std::vector<Path> &lines, float closeDistance) {
  std::vector<Coord> loop;
  if (path.points.size() < 3 || !findLoop(path.points, lines, closeDistance, loop)) return;
  const double area = signedArea(loop);
  if (area != 0 && (area > 0) != insideOnRight) std::reverse(path.points.begin(), path.points.end());
}

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

const LineTypeInfo &LineTypeInfo::of(LineType type) { return TYPES[(int)type]; }

LineType LineTypeInfo::fromName(const char *name) {
  for (int i = 0; name && i < COUNT; i++)
    if (!strcmp(name, TYPES[i].name)) return (LineType)i;
  return LineType::SKETCH;
}

bool LineTypeInfo::isSemantic() const { return this != &TYPES[(int)LineType::SKETCH]; }

int Sketch::pointCount() const {
  int n = current.points.size();
  for (const Path &p : finished) n += p.points.size();
  return n;
}

void Sketch::startPath(Coord start, LineType type, const std::string &colour) {
  current.points.clear();
  current.points.push_back(start);
  current.type = type;
  current.colour = colour;
  active = true;
}

void Sketch::lineTo(Coord point) {
  if (!active || pointCount() >= MAX_POINTS) return;
  if (point == current.end()) return;  // pen held still
  current.points.push_back(point);
}

// Sketch.findLineToSnapTo, the finished line with a joinable end nearest the point within delta.
// atStart says which end of the active line is joining, its start joins on after another line
int Sketch::findLineToSnapTo(const Coord &point, float delta, bool atStart, const Coord *excluded) const {
  int best = -1;
  float bestDistance = delta;
  for (int i = 0; i < (int)finished.size(); i++) {
    const Path &line = finished[i];
    if (!canJoin(current, line)) continue;
    const Coord &end = atStart ? endToJoinAfter(line, point) : endToJoinBefore(line, point);
    if (excluded && end == *excluded) continue;
    const float d = distance(point, end);
    if (d < bestDistance) best = i, bestDistance = d;
  }
  return best;
}

// Sketch.snapActivePath. A line coming back near its own start closes into a loop, otherwise
// each end snaps to the nearest joinable end of another line of the same type
void Sketch::snapActivePath(float delta, int &startJoin, int &endJoin) {
  startJoin = endJoin = -1;
  if (current.points.size() < 2 || current.isClosed()) return;
  const Coord start = current.start(), end = current.end();

  const float distanceToStart = distance(end, start);
  int endTarget = findLineToSnapTo(end, delta, false, nullptr);
  if (reachesBeyond(current, delta) && distanceToStart < delta &&
      (endTarget < 0 || distanceToStart < distance(end, endToJoinBefore(finished[endTarget], end)))) {
    snapEndTo(current, start, delta);
    return;
  }

  const int startTarget = findLineToSnapTo(start, delta, true, nullptr);
  if (startTarget >= 0) {
    snapStartTo(current, endToJoinAfter(finished[startTarget], start), delta);
    startJoin = startTarget;
  }

  // the end mustn't snap to the very point the start just did
  endTarget = findLineToSnapTo(end, delta, false, &current.start());
  if (endTarget >= 0) {
    snapEndTo(current, endToJoinBefore(finished[endTarget], end), delta);
    endJoin = endTarget;
  }
}

// SketchView.orientActivePathIfRequired, with Auto-Orientation on
void Sketch::orientActivePath(float closeDistance) {
  const Info &type = info(current);
  if (type.isWall) orientToInterior(current);
  else if (type.isOrientedByLoop()) orientInsideOfLoop(current, type.hasLoopInsideOnRight(), finished, closeDistance);
}

// SketchView.handleLine on lifting the pen, then Sketch.finishPath with PathDetail.joinedWith.
// Joined lines are replaced by the one line they make
void Sketch::finishPath(float snapDistance, float minimumSize) {
  if (!active) return;
  active = false;
  const bool isDot = current.points.size() == 1;
  if (isDot) current.points.push_back(current.start());  // as SexyTopo, a tap is a line of two equal points
  // SketchView.closeActivePathIfOutline, an outline whose ends nearly meet is joined up
  if (info(current).isOutline && current.points.size() >= 3 && !(current.start() == current.end()) &&
      distance(current.start(), current.end()) <= snapDistance)
    current.points.push_back(current.start());
  orientActivePath(snapDistance);

  // snapped once the line's direction is settled, as that decides which ends of other lines it can join
  int startJoin = -1, endJoin = -1;
  if (!isDot) snapActivePath(snapDistance, startJoin, endJoin);
  // a typed line that small is a mistake, it would mean nothing in an export
  if (maxDimension(current) < (info(current).isSemantic() ? minimumSize : 0)) {
    current.points.clear();
    return;
  }
  simplify(current);

  Step step;
  Path joined = current;
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

void Sketch::setPaths(const std::vector<Path> &paths) {
  forget();
  finished = paths;
}

void Sketch::forget() {
  finished.clear();
  kept = Extras();
  history.clear();
  current.points.clear();
  active = false;
}
