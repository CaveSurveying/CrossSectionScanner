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

void ScanView::addLine(const String &line) {
  float deg, mm;
  if (line.startsWith("T")) turns++;
  else if (line.startsWith("P ") && sscanf(line.c_str() + 2, "%f %f", &deg, &mm) == 2 && mm > 0) addPoint(deg, mm);
}

void ScanView::addPoint(float deg, float mm) {
  if (pointCount >= MAX_POINTS || turns == 0) return;
  const float a = radians(deg);
  // Angles run clockwise seen from the lidar top, so seen from below x is mirrored
  points[pointCount++] = {(int16_t)(-mm * sinf(a)), (int16_t)(mm * cosf(a)), (uint8_t)(turns - 1)};
}

void ScanView::draw(cyd::Display &lcd, int viewSize) {
  size = viewSize;
  lcd.fillRect(0, 0, size, size + NOTES_H, TFT_BLACK);

  // Fit the scan's own extent (not the lidar) into the view, keeping the scale equal both ways
  int minX = 0, maxX = 0, minY = 0, maxY = 0;
  for (int i = 0; i < pointCount; i++) {
    const Point &p = points[i];
    if (i == 0) minX = maxX = p.x, minY = maxY = p.y;
    minX = min(minX, (int)p.x), maxX = max(maxX, (int)p.x);
    minY = min(minY, (int)p.y), maxY = max(maxY, (int)p.y);
  }
  const int span = max(max(maxX - minX, maxY - minY), 200);
  scale = size * (1 - 2 * MARGIN) / span;
  midX = (minX + maxX) / 2.0, midY = (minY + maxY) / 2.0;
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
  lcd.setCursor(4, size + 3);
  lcd.print(name);
  lcd.setCursor(4, size + 13);
  if (turns > 1) lcd.printf("%d turns, ", turns);
  lcd.printf("%d points, grid %.1f m", pointCount, grid / 1000.0);
}

// Scan points are mm with y up, sketch coordinates metres with y down
Coord ScanView::toSketch(int px, int py) const {
  return {(midX + (px - size / 2) / scale) / 1000, -(midY - (py - size / 2) / scale) / 1000};
}

void ScanView::toScreen(Coord c, int &px, int &py) const {
  px = lroundf(size / 2 + (c.x * 1000 - midX) * scale);
  py = lroundf(size / 2 - (-c.y * 1000 - midY) * scale);
}
