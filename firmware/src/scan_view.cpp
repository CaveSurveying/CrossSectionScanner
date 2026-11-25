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
  needsFit = true;
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

// The scan's own extent (not the lidar) with the drawing, keeping the scale equal both ways
void ScanView::fit(int viewSize, const Sketch &sketch) {
  ViewFit::Box box;
  for (int i = 0; i < pointCount; i++) box.add(points[i].x, points[i].y);
  box.add(sketch);
  view.fit(viewSize, box, MARGIN);
  needsFit = false;
}

void ScanView::draw(cyd::Display &lcd, int viewSize) {
  size = viewSize;
  lcd.fillRect(0, 0, size, size + NOTES_H, TFT_BLACK);

  if (needsFit) fit(size, Sketch());
  auto sx = [&](float x) { return (int)lroundf(view.screenX(x)); };
  auto sy = [&](float y) { return (int)lroundf(view.screenY(y)); };

  // Grid through the lidar, 1 m, 0.5 m for small scans, 0.25 m zoomed in close, 2 m zoomed well out
  const float shown = view.visibleSpan() * (1 - 2 * MARGIN);  // as the fit's span at x1
  const int grid = view.zoomStep() > 0 && shown < 1500 ? 250 : shown < 3000 ? 500 : shown < 12000 ? 1000 : 2000;
  lcd.setClipRect(0, 0, size, size);  // zoomed or moved, much of the scan is outside the view
  for (int g = floorf(view.mmX(0) / grid) * grid; g <= view.mmX(size); g += grid)
    lcd.drawFastVLine(sx(g), 0, size, g == 0 ? AXIS_COLOUR : GRID_COLOUR);
  for (int g = floorf(view.mmY(size) / grid) * grid; g <= view.mmY(0); g += grid)
    lcd.drawFastHLine(0, sy(g), size, g == 0 ? AXIS_COLOUR : GRID_COLOUR);

  for (int i = 0; i < pointCount; i++) lcd.fillRect(sx(points[i].x) - 1, sy(points[i].y) - 1, 2, 2, turnColour(points[i].turn));

  // Lidar and its 0° direction
  const int lx = sx(0), ly = sy(0);
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
  lcd.printf("%d points, grid %g m", pointCount, grid / 1000.0);
}
