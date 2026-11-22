#include "line_style.h"

#include <cmath>

namespace line_style {
namespace {
constexpr float STEP_PX = 1;  // how finely the line is walked for its pattern

class Pen {
 public:
  Pen(lgfx::LGFXBase &lcd, bool wide, uint16_t colour) : lcd(lcd), wide(wide), colour(colour) {}

  void line(Point a, Point b) {
    lcd.drawLine(lround(a.x), lround(a.y), lround(b.x), lround(b.y), colour);
    if (!wide) return;
    // a second line beside the first, a pixel across the way it runs
    const bool flat = std::fabs(b.x - a.x) > std::fabs(b.y - a.y);
    lcd.drawLine(lround(a.x) + !flat, lround(a.y) + flat, lround(b.x) + !flat, lround(b.y) + flat, colour);
  }
  void tick(Point at, Point right, float length) {
    lcd.drawLine(lround(at.x), lround(at.y), lround(at.x + right.x * length), lround(at.y + right.y * length), colour);
  }
  void tooth(Point a, Point b, Point apex) {
    lcd.fillTriangle(lround(a.x), lround(a.y), lround(b.x), lround(b.y), lround(apex.x), lround(apex.y), colour);
  }

 private:
  lgfx::LGFXBase &lcd;
  bool wide;
  uint16_t colour;
};
}  // namespace

void draw(lgfx::LGFXBase &lcd, const std::vector<Point> &points, LineType type, uint16_t colour) {
  const LineTypeInfo &info = LineTypeInfo::of(type);
  // thinner than a plain line can't be drawn, so it is greyed
  Pen pen(lcd, info.widthFactor >= 1.5f, info.widthFactor < 0.75f ? (uint16_t)TFT_DARKGREY : colour);
  if (points.size() == 1 || (points.size() == 2 && points[0].x == points[1].x && points[0].y == points[1].y)) {
    lcd.drawPixel(lround(points[0].x), lround(points[0].y), colour);
    return;
  }
  const bool stamped = info.stamp != LineTypeInfo::NONE;
  if (!stamped && info.dashOn == 0) {
    for (size_t i = 1; i < points.size(); i++) pen.line(points[i - 1], points[i]);
    return;
  }

  // LineType.Stamp, a bar every advance with its ticks or tooth, or a dash pattern
  const float period = (stamped ? info.advance : info.dashOn + info.dashOff) * ORNAMENT_PX;
  const float bar = (stamped ? info.barLength : info.dashOn) * ORNAMENT_PX;
  const int ticks = info.stamp == LineTypeInfo::HACHURED ? 2 : info.stamp == LineTypeInfo::TICKED ? 1 : 0;
  const float tickLengths[] = {info.tick * ORNAMENT_PX, info.shortTick * ORNAMENT_PX};
  float along = 0;  // distance along the line so far
  Point last = points[0], toothStart = points[0];
  for (size_t i = 1; i < points.size(); i++) {
    const Point &a = points[i - 1], &b = points[i];
    const float length = std::hypot(b.x - a.x, b.y - a.y);
    if (length == 0) continue;
    const Point dir{(b.x - a.x) / length, (b.y - a.y) / length}, right{-dir.y, dir.x};  // y is down
    for (float t = STEP_PX;; t += STEP_PX) {
      t = std::min(t, length);
      const Point p{a.x + dir.x * t, a.y + dir.y * t};
      const float step = std::hypot(p.x - last.x, p.y - last.y);
      const float from = std::fmod(along, period), to = from + step;  // phase within the pattern
      along += step;
      if (from < bar) pen.line(last, p);
      for (int k = 0; k < ticks; k++) {
        const float at = bar * (k + 0.5f) / ticks;
        if (from < at && to >= at) pen.tick(p, right, tickLengths[k]);
      }
      if (info.stamp == LineTypeInfo::TOOTHED && to >= period) {  // the teeth abut, one per advance
        const Point mid{(toothStart.x + p.x) / 2, (toothStart.y + p.y) / 2};
        pen.tooth(toothStart, p, {mid.x + right.x * tickLengths[0], mid.y + right.y * tickLengths[0]});
        toothStart = p;
      }
      last = p;
      if (t >= length) break;
    }
  }
}
}  // namespace line_style
