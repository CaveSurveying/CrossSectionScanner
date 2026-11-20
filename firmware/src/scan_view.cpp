#include "scan_view.h"

namespace {
constexpr float MARGIN = 0.05;  // of the view, each side
constexpr uint16_t GRID_COLOUR = 0x2104, AXIS_COLOUR = 0x4208;
// Same order as the matplotlib default so screen and computer plots match
constexpr uint32_t TURN_COLOURS[] = {0x1f77b4, 0xff7f0e, 0x2ca02c, 0xd62728, 0x9467bd,
                                     0x8c564b, 0xe377c2, 0x7f7f7f, 0xbcbd22, 0x17becf};

uint16_t turnColour(int turn) {
  const uint32_t c = TURN_COLOURS[turn % 10];
  return lgfx::color565(c >> 16, c >> 8 & 0xff, c & 0xff);
}
}  // namespace

void ScanView::clear(const String &newName) {
  name = newName;
  pointCount = turns = 0;
}

void ScanView::addPoint(float deg, float mm) {
  if (pointCount >= MAX_POINTS || turns == 0) return;
  const float a = radians(deg);
  points[pointCount++] = {(int16_t)(mm * sinf(a)), (int16_t)(mm * cosf(a)), (uint8_t)(turns - 1)};
}

void ScanView::draw(cyd::Display &lcd, int size) const {
  lcd.fillRect(0, 0, size, size + 66, TFT_BLACK);

  // Fit the scan's own extent (not the lidar) into the view, keeping the scale equal both ways
  int minX = 0, maxX = 0, minY = 0, maxY = 0;
  for (int i = 0; i < pointCount; i++) {
    const Point &p = points[i];
    if (i == 0) minX = maxX = p.x, minY = maxY = p.y;
    minX = min(minX, (int)p.x), maxX = max(maxX, (int)p.x);
    minY = min(minY, (int)p.y), maxY = max(maxY, (int)p.y);
  }
  const int span = max(max(maxX - minX, maxY - minY), 200);
  const float scale = size * (1 - 2 * MARGIN) / span;  // px per mm
  const float midX = (minX + maxX) / 2.0, midY = (minY + maxY) / 2.0;
  auto sx = [&](float x) { return (int)(size / 2 + (x - midX) * scale); };
  auto sy = [&](float y) { return (int)(size / 2 - (y - midY) * scale); };

  // Grid through the lidar, 1 m or 0.5 m for small scans
  const int grid = span < 3000 ? 500 : 1000;
  const float half = size / 2 / scale;  // mm from the view centre to its edge
  for (int g = floorf((midX - half) / grid) * grid; g <= midX + half; g += grid)
    lcd.drawFastVLine(sx(g), 0, size, g == 0 ? AXIS_COLOUR : GRID_COLOUR);
  for (int g = floorf((midY - half) / grid) * grid; g <= midY + half; g += grid)
    lcd.drawFastHLine(0, sy(g), size, g == 0 ? AXIS_COLOUR : GRID_COLOUR);

  for (int i = 0; i < pointCount; i++) lcd.fillRect(sx(points[i].x) - 1, sy(points[i].y) - 1, 2, 2, turnColour(points[i].turn));

  // Lidar and its 0° direction
  const int lx = sx(0), ly = sy(0);
  lcd.setClipRect(0, 0, size, size);
  lcd.drawLine(lx - 5, ly, lx + 5, ly, TFT_RED);
  lcd.drawLine(lx, ly - 5, lx, ly + 5, TFT_RED);
  lcd.fillTriangle(lx, ly - 18, lx - 4, ly - 10, lx + 4, ly - 10, TFT_RED);
  lcd.clearClipRect();
  lcd.drawRect(0, 0, size, size, AXIS_COLOUR);

  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setCursor(4, size + 6);
  lcd.print(name);
  lcd.setCursor(4, size + 20);
  lcd.printf("%d turns, %d points", turns, pointCount);
  lcd.setCursor(4, size + 34);
  lcd.printf("grid %.1f m, red arrow = 0 deg", grid / 1000.0);
  for (int t = 0; t < turns && t < 10; t++) lcd.fillRect(4 + t * 22, size + 50, 18, 10, turnColour(t));
}
