// CrossSectionScanner, stand-alone unit: SCAN takes a lidar scan, shows it and saves it in flash.
// ‹ and › step through saved scans, Stn gives the scan shown its station from SexyTopo's list (with
// vertical or horizontal and facing on or back, then Accept), so it is sent to SexyTopo over
// Bluetooth (link.h),
// and Draw opens the drawing screen for the scan shown, where Type picks the colour and kind of line
// (as SexyTopo's), Save keeps the drawing and Cancel puts it back as it was. Drawings edited in
// SexyTopo come back and are shown in their colours, with its text, symbols and areas, though only
// lines can be edited here.
// A computer on the USB port can watch, ask for scans and fetch saved ones for debugging
// (tools/board_link.py). Touch calibration runs if the screen is held at power-on.
//
// Board to computer
//   READY           after boot and in reply to ?
//   LIDAR <model> <firmware> <hardware> <serial>   lidar details, in reply to ?
//   LIDAR none      no lidar answers
//   SCANDATA start  a scan follows in the saved text format (scan_store.h), each new scan and in reply to GET
//   SCANDATA end
//   LIDAR failed    a scan did not complete
//   SAVED <name> <overwritten>, or SAVE failed <name>
//   FILE ... and LIST end ...   in reply to LIST, see scan_store.h
//   GET failed <name>, DELETED <name>, DELETE failed <name>
//   SKETCHDATA start  a drawing follows (sketch_file.h), each saved drawing and in reply to GETSKETCH
//   SKETCHDATA end
//   SKETCH SAVED <name> <revision> <overwritten>, or SKETCH failed <name>
//   BUSY drawing    SCAN or DELETE refused while the drawing screen is open
//   TOUCH x y rx ry each new touch, screen and raw (for checking calibration)
//   CAL ...         new touch calibration, or why it failed
// Computer to board
//   ?               reply READY and the lidar details
//   SCAN            scan as if SCAN were pressed
//   LIST            list the saved scans
//   GET <name>      send a saved scan
//   GETSKETCH <name>  send a saved scan's drawing
//   DELETE <name>   delete a saved scan
//   LINK            the link state, stations and each scan's station and status, as JSON
// and LINK ... lines from the Bluetooth link with SexyTopo, for debugging
#include <Arduino.h>
#include <algorithm>
#include <cyd_display.h>

#include "lidar.h"
#include "link.h"
#include "colours.h"
#include "line_style.h"
#include "scan_store.h"
#include "scan_view.h"
#include "sketch.h"
#include "sketch_file.h"

constexpr long BAUD = 460800;
constexpr int VIEW = 240;  // square scan view at the top of the portrait screen
constexpr int MSG_Y = VIEW + ScanView::NOTES_H;
constexpr int BTN_Y = 272, BTN_H = 42;
// Scan screen buttons
constexpr int PREV_X = 4, NEXT_X = 116, STEP_W = 44;
constexpr int SCAN_X = 52, SCAN_W = 60;
constexpr int STN_X = 164, STN_W = 32;
constexpr int DRAW_X = 200, DRAW_W = 36;
// Station picker over the view, two columns, stations newest first in the first rows
constexpr int STN_COLS = 2, STN_ROWS = 4, STN_CELL_W = VIEW / STN_COLS, STN_CELL_H = VIEW / (STN_ROWS + 2);
// Station picker buttons in place of the scan screen's
constexpr int PICK_CANCEL_X = 4, ACCEPT_X = 124, PICK_BTN_W = 112;
// Drawing screen buttons
constexpr int UNDO_X = 2, CLEAR_X = 45, TYPE_X = 88, CANCEL_X = 157, SAVE_X = 200;
constexpr int EDIT_W = 40, TYPE_W = 66;
// Line type picker over the view, a row of brush colours then the types in two columns
constexpr int SWATCH_H = 32, SWATCH_W = VIEW / colours::BRUSH_COUNT;
constexpr int PICK_W = VIEW / 2, PICK_H = (VIEW - SWATCH_H) / ((LineTypeInfo::COUNT + 1) / 2);
constexpr unsigned long LIDAR_CHECK_MS = 2000;  // how often to look for a lidar while none is found
constexpr int SCAN_TURNS = 1;
constexpr int SNAP_PX = 10;  // SexyTopo's SNAP_TO_LINE_SENSITIVITY_DP, about 1 px per dp on this screen
constexpr int MIN_LINE_PX = 10;  // SexyTopo's MINIMUM_DETAIL_SIZE_DP, smaller typed lines are dropped

cyd::Display lcd;
ScanView scanView;
ScanStore store;
Sketch sketch;
Lidar lidar;
Lidar::Info lidarInfo;
Link sexytopo;  // the Bluetooth link with SexyTopo
bool lidarFound = false;
bool storeOk = false;
bool scanning = false;
bool drawing = false;  // drawing screen open
bool picking = false;  // line type picker open on the drawing screen
LineType lineType = LineType::SKETCH;  // for the next line drawn
int brush = 0;                          // colours::BRUSHES, for the next line drawn
int shown = -1;        // saved scan on the screen, -1 for none
int sketchRevision = 0;  // of the drawing shown, 0 if it has never been saved
bool reloadShown = false;  // SexyTopo has replaced the drawing shown
bool choosingStation = false;  // station picker open on the scan screen
int stationPage = 0;           // of the station picker, 0 for the newest
bool pickHorizontal = false, pickBack = false;  // for the station picked

uint16_t brushColour() { return colours::of(colours::BRUSHES[brush].name); }
String pickedStation;  // to be given on Accept, empty for no station

bool inside(int x, int y, int bx, int bw) { return x >= bx && x < bx + bw && y >= BTN_Y && y < BTN_Y + BTN_H; }

// Large text if it fits the button, small if not, on two lines if it still doesn't fit
void drawButton(int x, int w, bool active, uint16_t colour, const char *text) {
  lcd.fillRoundRect(x, BTN_Y, w, BTN_H, 8, active ? colour : TFT_DARKGREY);
  lcd.setTextSize(2);
  if (lcd.textWidth(text) > w - 6) lcd.setTextSize(1);
  const char *space = strchr(text, ' ');
  if (lcd.textWidth(text) <= w - 6 || !space) {
    lcd.drawString(text, x + w / 2, BTN_Y + BTN_H / 2);
    return;
  }
  lcd.drawString(String(text).substring(0, space - text), x + w / 2, BTN_Y + BTN_H / 2 - 5);
  lcd.drawString(space + 1, x + w / 2, BTN_Y + BTN_H / 2 + 5);
}

void drawButtons() {
  lcd.fillRect(0, BTN_Y, VIEW, BTN_H, TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  if (drawing) {  // only Type while picking
    drawButton(UNDO_X, EDIT_W, !picking && sketch.changed(), TFT_NAVY, "Undo");
    drawButton(CLEAR_X, EDIT_W, !picking && !sketch.paths().empty(), TFT_MAROON, "Clear");
    drawButton(TYPE_X, TYPE_W, true, picking ? TFT_ORANGE : TFT_PURPLE, LineTypeInfo::of(lineType).label);
    lcd.fillRect(TYPE_X + 10, BTN_Y + BTN_H - 7, TYPE_W - 20, 3, brushColour());  // the colour in use
    drawButton(CANCEL_X, EDIT_W, !picking, TFT_NAVY, "Cancel");
    drawButton(SAVE_X, EDIT_W, !picking, TFT_DARKGREEN, "Save");
  } else if (choosingStation) {
    drawButton(PICK_CANCEL_X, PICK_BTN_W, true, TFT_NAVY, "Cancel");
    drawButton(ACCEPT_X, PICK_BTN_W, true, TFT_DARKGREEN, "Accept");
  } else {
    drawButton(PREV_X, STEP_W, shown > 0, TFT_NAVY, "<");
    drawButton(SCAN_X, SCAN_W, lidarFound && !scanning, TFT_DARKGREEN,
               scanning ? "Scanning" : lidarFound ? "SCAN" : "No lidar");
    drawButton(NEXT_X, STEP_W, shown < store.count() - 1, TFT_NAVY, ">");
    drawButton(STN_X, STN_W, shown >= 0, TFT_PURPLE, "Stn");
    drawButton(DRAW_X, DRAW_W, scanView.count() > 0, TFT_PURPLE, "Draw");
  }
  lcd.setTextDatum(top_left);
}

void showMessage(const String &text) {
  lcd.fillRect(0, MSG_Y, VIEW, BTN_Y - MSG_Y, TFT_BLACK);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(1);
  lcd.setCursor(4, MSG_Y + 1);
  lcd.print(text);
}

// The shown scan's station and how far it has got, and whether SexyTopo is connected
void showLinkState() {
  if (shown < 0) return;
  showMessage(sexytopo.describe(store.name(shown)) +
              (sexytopo.ready() ? ", SexyTopo linked" : sexytopo.connected() ? ", SexyTopo connecting" : ""));
}

void showLidarState() {
  if (!storeOk) showMessage("Flash not working, scans not saved");
  else showMessage(String(lidarFound ? "Lidar found" : "No lidar found") + ", flash " + store.percentFull() + "% full");
}

void drawPath(const Sketch::Path &path) {
  std::vector<line_style::Point> points;
  for (const Coord &c : path.points) {
    int x, y;
    scanView.toScreen(c, x, y);
    points.push_back({(float)x, (float)y});
  }
  line_style::draw(lcd, points, path.type, colours::of(path.colour));
}

// Text as SexyTopo draws it (PointRenderer.drawTextDetail), size the font size in pixels, from the
// bottom left of its first line, a line further down for each new line
void drawText(const std::string &text, int x, int y, float size) {
  constexpr float FREE_SANS_PX = 17.6f;  // FreeSans9pt7b's font size, 9 pt at 141 dpi
  constexpr float SMALLEST = 12;         // below this the board's own 8 px font, as smaller can't be read
  if (size < SMALLEST) {
    lcd.setFont(&fonts::Font0);
    lcd.setTextSize(1);
  } else {
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextSize(size / FREE_SANS_PX);
  }
  lcd.setTextDatum(baseline_left);
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    lcd.drawString(text.substr(start, end - start).c_str(), x, y);
    y += lcd.fontHeight();
    start = end + 1;
  }
  lcd.setFont(&fonts::Font0);
  lcd.setTextSize(1);
  lcd.setTextDatum(top_left);
}

// What SexyTopo drew that can't be edited here, in its colours. Areas as outlines, text as text and
// symbols as a ring their size with a short name.
void drawExtras() {
  const Sketch::Extras &extras = sketch.extras();
  const float pixelsPerMetre = 1 / scanView.metresPerPixel();
  for (const Sketch::Extras::Outline &outline : extras.outlines)
    for (size_t i = 0; i < outline.points.size(); i++) {
      int x0, y0, x1, y1;
      scanView.toScreen(outline.points[i], x0, y0);
      scanView.toScreen(outline.points[(i + 1) % outline.points.size()], x1, y1);
      lcd.drawLine(x0, y0, x1, y1, colours::of(outline.colour));
    }
  for (const Sketch::Extras::Mark &mark : extras.marks) {
    int x, y;
    scanView.toScreen(mark.at, x, y);
    const uint16_t colour = colours::of(mark.colour);
    lcd.setTextColor(colour);
    if (!mark.symbol) {
      drawText(mark.text, x, y, mark.size * pixelsPerMetre);
      continue;
    }
    const int radius = std::max(3, (int)lroundf(mark.size * pixelsPerMetre / 2));
    lcd.drawCircle(x, y, radius, colour);
    lcd.setTextDatum(middle_left);
    lcd.drawString(mark.text.c_str(), x + radius + 2, y);
    lcd.setTextDatum(top_left);
  }
}

// Scan with its sketch over it, in the view
void drawView() {
  scanView.draw(lcd, VIEW);
  lcd.setClipRect(1, 1, VIEW - 2, VIEW - 2);
  drawExtras();
  for (const Sketch::Path &p : sketch.paths()) drawPath(p);
  lcd.clearClipRect();
}

void showSaved(int i) {
  if (!store.load(i, scanView)) return;
  shown = i;
  sketch.forget();
  sketchRevision = 0;
  store.loadSketch(store.name(i), sketch, sketchRevision);
  drawView();
  drawButtons();
  showLinkState();
}

// The brush colours along the top, then the line types each with a sample in the colour in use,
// those in use highlighted
void drawPicker() {
  lcd.fillRect(0, 0, VIEW, VIEW, TFT_BLACK);
  for (int i = 0; i < colours::BRUSH_COUNT; i++) {
    const int x = i * SWATCH_W;
    lcd.fillRoundRect(x + 3, 3, SWATCH_W - 6, SWATCH_H - 6, 4, colours::of(colours::BRUSHES[i].name));
    if (i == brush) lcd.drawRoundRect(x + 1, 1, SWATCH_W - 2, SWATCH_H - 2, 5, TFT_WHITE);
  }
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE);
  for (int i = 0; i < LineTypeInfo::COUNT; i++) {
    const int x = i % 2 * PICK_W, y = SWATCH_H + i / 2 * PICK_H;
    if (i == (int)lineType) lcd.fillRoundRect(x + 2, y + 2, PICK_W - 4, PICK_H - 4, 6, TFT_NAVY);
    else lcd.drawRoundRect(x + 2, y + 2, PICK_W - 4, PICK_H - 4, 6, TFT_DARKGREY);
    lcd.drawString(LineTypeInfo::of((LineType)i).label, x + 8, y + 5);
    const float lineY = y + PICK_H * 0.65f;
    line_style::draw(lcd, {{x + 12.0f, lineY}, {x + PICK_W - 12.0f, lineY}}, (LineType)i, brushColour());
  }
}

String lineDescription() {
  return String(LineTypeInfo::of(lineType).label) + " lines, " + colours::BRUSHES[brush].label;
}

void showPicker(bool open) {
  picking = open;
  if (open) {
    drawPicker();
    showMessage("Pick the colour and type of line");
  } else {
    drawView();
    showMessage(lineDescription());
  }
  drawButtons();
}

// A colour leaves the picker open for the type, a type closes it
void pressPicker(int x, int y) {
  if (y < SWATCH_H) {
    brush = std::min(x / SWATCH_W, colours::BRUSH_COUNT - 1);
    drawPicker();
    showMessage(lineDescription());
    drawButtons();
    return;
  }
  const int i = (y - SWATCH_H) / PICK_H * 2 + x / PICK_W;
  if (i < LineTypeInfo::COUNT) lineType = (LineType)i;
  showPicker(false);
}

// The shown scan's station, empty if none
String currentStation() {
  const ScanLink *l = shown >= 0 ? sexytopo.model().find(store.name(shown).c_str()) : nullptr;
  return l ? l->station.c_str() : "";
}

// Stations newest first, a page at a time, then the orientation and facing toggles, Older and No station.
// The one picked is filled, others with a scan are outlined in orange.
void drawStationPicker() {
  const std::vector<std::string> &stations = sexytopo.model().stations();
  lcd.fillRect(0, 0, VIEW, VIEW, TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.setTextSize(2);
  for (int i = 0; i < STN_COLS * STN_ROWS; i++) {
    const int n = stations.size() - 1 - (stationPage * STN_COLS * STN_ROWS + i);
    if (n < 0) break;
    const int x = i % STN_COLS * STN_CELL_W, y = i / STN_COLS * STN_CELL_H;
    const String name = stations[n].c_str();
    const bool taken = !sexytopo.model().scanAt(stations[n]).empty();
    if (name == pickedStation) lcd.fillRoundRect(x + 2, y + 2, STN_CELL_W - 4, STN_CELL_H - 4, 6, TFT_NAVY);
    else lcd.drawRoundRect(x + 2, y + 2, STN_CELL_W - 4, STN_CELL_H - 4, 6, taken ? TFT_ORANGE : TFT_DARKGREY);
    lcd.drawString(name, x + STN_CELL_W / 2, y + STN_CELL_H / 2);
  }
  if (stations.empty()) {
    lcd.setTextSize(1);
    lcd.drawString("No stations yet,", VIEW / 2, STN_CELL_H);
    lcd.drawString("connect SexyTopo", VIEW / 2, STN_CELL_H + 12);
  }
  lcd.setTextSize(1);
  const char *labels[] = {pickHorizontal ? "Horizontal" : "Vertical", pickBack ? "Facing back" : "Facing on",
                          "Older", "No station"};
  for (int i = 0; i < 4; i++) {
    const int x = i % 2 * STN_CELL_W, y = (STN_ROWS + i / 2) * STN_CELL_H;
    const bool noneUsed = i == 3 && pickedStation.isEmpty();
    lcd.fillRoundRect(x + 2, y + 2, STN_CELL_W - 4, STN_CELL_H - 4, 6,
                      i < 2 ? TFT_PURPLE : noneUsed ? TFT_NAVY : TFT_DARKGREY);
    lcd.drawString(labels[i], x + STN_CELL_W / 2, y + STN_CELL_H / 2);
  }
  lcd.setTextDatum(top_left);
}

void showStationPicker(bool open) {
  choosingStation = open;
  pickedStation = currentStation();
  if (open) {
    const ScanLink *l = sexytopo.model().find(store.name(shown).c_str());
    pickHorizontal = l && l->horizontal;
    pickBack = l && l->back;
    stationPage = 0;
    drawStationPicker();
    showMessage("Pick the scan's station, then Accept");
  } else {
    drawView();
    showLinkState();
  }
  drawButtons();
}

void pressStationPicker(int x, int y) {
  const int col = x / STN_CELL_W, row = y / STN_CELL_H;
  const String scan = store.name(shown);
  if (row == STN_ROWS) {  // toggles
    if (col == 0) pickHorizontal = !pickHorizontal;
    else pickBack = !pickBack;
    drawStationPicker();
    return;
  }
  if (row == STN_ROWS + 1) {
    if (col == 0) {  // older, back to the newest after the last page
      const int pages = (sexytopo.model().stations().size() + STN_COLS * STN_ROWS - 1) / (STN_COLS * STN_ROWS);
      stationPage = pages ? (stationPage + 1) % pages : 0;
      drawStationPicker();
    } else {
      pickedStation = "";
      drawStationPicker();
      showMessage("No station, Accept takes it off");
    }
    return;
  }
  const std::vector<std::string> &stations = sexytopo.model().stations();
  const int n = stations.size() - 1 - (stationPage * STN_COLS * STN_ROWS + row * STN_COLS + col);
  if (n < 0) return;
  pickedStation = stations[n].c_str();
  drawStationPicker();
  const String other = sexytopo.model().scanAt(stations[n]).c_str();
  if (other.length() && other != scan) showMessage("Stn " + pickedStation + " has " + other + ", Accept replaces it");
  else showMessage("Stn " + pickedStation + ", then Accept");
}

// Gives the shown scan the station picked, if anything has changed
void acceptStation() {
  const String scan = store.name(shown);
  const ScanLink *l = sexytopo.model().find(scan.c_str());
  const bool same = pickedStation == currentStation() &&
                    (pickedStation.isEmpty() || (l && l->horizontal == pickHorizontal && l->back == pickBack));
  if (same) {
    showStationPicker(false);
    return;
  }
  if (pickedStation.isEmpty()) {
    sexytopo.clearStation(scan);
    showStationPicker(false);
    return;
  }
  String displaced;
  sexytopo.accept(scan, pickedStation, pickHorizontal, pickBack, displaced);
  showStationPicker(false);
  if (displaced.length()) showMessage(sexytopo.describe(scan) + ", " + displaced + " lost it");
}

void openDrawing() {
  drawing = true;
  drawView();
  showMessage("Draw round the scan with the stylus");
  drawButtons();
}

void sendSketch(const String &name) {
  Serial.println("SKETCHDATA start");
  store.sendSketch(name, Serial);
  Serial.println("SKETCHDATA end");
}

// Back to the scan screen with the drawing as it was when opened
void cancelDrawing() {
  const bool changed = sketch.changed();
  sketch.revert();
  drawing = false;
  drawView();
  drawButtons();
  if (changed) showMessage("Drawing not changed");
  else showLidarState();
  if (shown >= 0) sexytopo.drawingClosed(store.name(shown), false);
}

// Save the drawing if it changed, undo goes back only to here
void saveDrawing() {
  drawing = false;
  drawView();
  drawButtons();
  if (!sketch.changed()) {
    showLidarState();
    if (shown >= 0) sexytopo.drawingClosed(store.name(shown), false);
    return;
  }
  sketch.forgetHistory();
  if (shown < 0) {
    showMessage("Drawing not saved, its scan was not saved");
    return;
  }
  const String &name = store.name(shown);
  const int revision = LinkModel::nextRevision(sketchRevision);
  const int overwritten = storeOk ? store.saveSketch(name, sketch, revision) : -1;
  if (overwritten < 0) {
    Serial.printf("SKETCH failed %s\n", name.c_str());
    showMessage("Drawing not saved, flash error");
    sexytopo.drawingClosed(name, false);
    return;
  }
  sketchRevision = revision;
  Serial.printf("SKETCH SAVED %s %d %d\n", name.c_str(), sketchRevision, overwritten);
  sendSketch(name);
  sexytopo.scansChanged();
  sexytopo.drawingSaved(name);
  sexytopo.drawingClosed(name, true);
  if (overwritten) {
    showSaved(shown - overwritten);  // the older scans before it were overwritten
    showMessage(String("Drawing saved, flash full so ") + overwritten + " oldest overwritten");
  } else {
    showMessage(String("Drawing saved, flash ") + store.percentFull() + "% full");
  }
}

// Look for the lidar, true if that changed whether it is found
bool checkLidar() {
  const bool was = lidarFound;
  lidarFound = lidar.readInfo(lidarInfo);
  return lidarFound != was;
}

void sendLidarInfo() { Serial.println(lidarFound ? Lidar::infoLine(lidarInfo) : String("LIDAR none")); }

void sendScanText(const String &text) {
  Serial.println("SCANDATA start");
  Serial.print(text);
  Serial.println("SCANDATA end");
}

// Show and save a scan just taken
void keepScan() {
  String name;
  const String text = store.record(lidar, lidarInfo, name);
  const int overwritten = storeOk ? store.save(name, text) : -1;
  sendScanText(text);
  if (overwritten >= 0) {
    Serial.printf("SAVED %s %d\n", name.c_str(), overwritten);
    sexytopo.scansChanged();
    showSaved(store.count() - 1);
    if (overwritten) showMessage(String("Saved, flash full so ") + overwritten + " oldest overwritten");
    else showMessage(String("Saved, flash ") + store.percentFull() + "% full");
    return;
  }
  Serial.printf("SAVE failed %s\n", name.c_str());
  scanView.clear(name + " not saved");
  int from = 0;
  for (int to; (to = text.indexOf('\n', from)) >= 0; from = to + 1) scanView.addLine(text.substring(from, to));
  shown = -1;
  sketch.forget();
  drawView();
  showMessage("Not saved, flash error");
}

void startScan() {
  if (!lidarFound || scanning) {
    sendLidarInfo();
    return;
  }
  scanning = true;
  drawButtons();
  if (lidar.capture(SCAN_TURNS)) {
    keepScan();
  } else {
    Serial.println("LIDAR failed");
    checkLidar();  // it may have been unplugged
    showMessage(lidarFound ? "Lidar scan failed" : "Lidar scan failed, no lidar found");
  }
  scanning = false;
  drawButtons();
}

void handleLine(const String &line) {
  if ((drawing || choosingStation) && (line == "SCAN" || line.startsWith("DELETE "))) {
    Serial.println("BUSY drawing");
  } else if (line == "?") {
    Serial.println("READY");
    sendLidarInfo();
  } else if (line == "SCAN") {
    startScan();
  } else if (line == "LINK") {
    sexytopo.print(Serial);
  } else if (line == "LIST") {
    store.list(Serial);
  } else if (line.startsWith("GET ")) {
    const String name = line.substring(4);
    Serial.println("SCANDATA start");
    const bool sent = store.send(name, Serial);
    Serial.println("SCANDATA end");
    if (!sent) Serial.printf("GET failed %s\n", name.c_str());
  } else if (line.startsWith("GETSKETCH ")) {
    sendSketch(line.substring(10));
  } else if (line.startsWith("DELETE ")) {
    const String name = line.substring(7);
    const bool deleted = store.remove(name);
    Serial.printf(deleted ? "DELETED %s\n" : "DELETE failed %s\n", name.c_str());
    if (!deleted) return;
    sexytopo.scansChanged();
    if (store.count()) {
      showSaved(constrain(shown, 0, store.count() - 1));
    } else {
      shown = -1;
      drawButtons();
    }
  }
}

void readSerial() {
  static String line;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c != '\n') {
      line += c;
      continue;
    }
    line.trim();
    handleLine(line);
    line = "";
  }
}

void pressScanScreen(int x, int y) {
  lcd.fillCircle(x, y, 3, TFT_YELLOW);  // shows where the touch landed
  if (inside(x, y, SCAN_X, SCAN_W) && lidarFound) startScan();
  if (inside(x, y, PREV_X, STEP_W) && shown > 0) showSaved(shown - 1);
  if (inside(x, y, NEXT_X, STEP_W) && shown < store.count() - 1) showSaved(shown + 1);
  if (inside(x, y, STN_X, STN_W) && shown >= 0) showStationPicker(true);
  if (inside(x, y, DRAW_X, DRAW_W) && scanView.count() > 0) openDrawing();
}

void pressDrawingButtons(int x, int y) {
  if (inside(x, y, TYPE_X, TYPE_W)) {
    showPicker(!picking);
    return;
  }
  if (picking) return;
  if (inside(x, y, UNDO_X, EDIT_W) && sketch.undo()) {  // nothing before the drawing was opened
    drawView();
    drawButtons();
  }
  if (inside(x, y, CLEAR_X, EDIT_W) && !sketch.paths().empty()) {
    sketch.clear();
    drawView();
    drawButtons();
  }
  if (inside(x, y, CANCEL_X, EDIT_W)) cancelDrawing();
  if (inside(x, y, SAVE_X, EDIT_W)) saveDrawing();
}

// Pen on the drawing screen, a line follows the pen while it stays in the view
void penOnDrawing(bool touched, int x, int y) {
  static int lastX, lastY;
  const bool onView = touched && scanView.inView(x, y);
  if (sketch.activePath() && !onView) {
    sketch.finishPath(SNAP_PX * scanView.metresPerPixel(), MIN_LINE_PX * scanView.metresPerPixel());
    drawView();  // the line as finished, snapped and simplified
    drawButtons();  // Clear may now be possible
  } else if (onView && !sketch.activePath()) {
    sketch.startPath(scanView.toSketch(x, y), lineType, colours::BRUSHES[brush].name);
    lcd.drawPixel(x, y, brushColour());
  } else if (onView && (x != lastX || y != lastY)) {
    sketch.lineTo(scanView.toSketch(x, y));
    lcd.drawLine(lastX, lastY, x, y, brushColour());
  }
  lastX = x, lastY = y;
}

void readTouch() {
  static bool wasTouched = false;
  int x, y, rx, ry;
  const bool touched = lcd.touchedAt(x, y, &rx, &ry);
  const bool pressed = touched && !wasTouched;
  if (pressed) Serial.printf("TOUCH %d %d %d %d\n", x, y, rx, ry);
  if (drawing && picking) {
    if (pressed && scanView.inView(x, y)) pressPicker(x, y);
    else if (pressed) pressDrawingButtons(x, y);
  } else if (choosingStation) {
    if (pressed && scanView.inView(x, y)) pressStationPicker(x, y);
    else if (pressed && inside(x, y, PICK_CANCEL_X, PICK_BTN_W)) showStationPicker(false);
    else if (pressed && inside(x, y, ACCEPT_X, PICK_BTN_W)) acceptStation();
  } else if (drawing) {
    penOnDrawing(touched, x, y);
    if (pressed && !scanView.inView(x, y)) pressDrawingButtons(x, y);
  } else if (pressed) {
    pressScanScreen(x, y);
  }
  wasTouched = touched;
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(BAUD);
  lcd.start();
  storeOk = store.begin();
  sexytopo.begin(store, [](const String &scan) { return drawing && shown >= 0 && store.name(shown) == scan; });
  lidar.begin();
  for (int i = 0; i < 10 && !lidarFound; i++) {  // the lidar may still be starting up
    lidarFound = lidar.readInfo(lidarInfo);
    if (!lidarFound) delay(200);
  }
  lcd.fillScreen(TFT_BLACK);
  if (store.count()) showSaved(store.count() - 1);  // newest
  showLidarState();
  drawButtons();
  Serial.println("READY");
  sendLidarInfo();
}

void loop() {
  static unsigned long lastCheck = 0;
  // Not while drawing, as a check with no lidar holds up the pen for 0.3 s
  if (!lidarFound && !drawing && millis() - lastCheck >= LIDAR_CHECK_MS) {  // it may be plugged in later
    lastCheck = millis();
    if (checkLidar()) {
      showLidarState();
      drawButtons();
    }
  }
  readSerial();
  readTouch();
  if (sexytopo.loop() && !drawing && !choosingStation) showLinkState();
  String updated;
  if (sexytopo.takeUpdated(updated) && shown >= 0 && store.name(shown) == updated) reloadShown = true;
  if (reloadShown && !drawing && !choosingStation) {
    reloadShown = false;
    showSaved(shown);
    showMessage("Drawing from SexyTopo");
  }
  delay(10);
}
