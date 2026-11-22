// CrossSectionScanner, stand-alone unit: SCAN takes a lidar scan, shows it and saves it in flash.
// ‹ and › step through saved scans, Draw opens the drawing screen for the scan shown, where
// Type picks the kind of line (as SexyTopo's), Save keeps the drawing and Cancel puts it back as
// it was.
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
#include <Arduino.h>
#include <cyd_display.h>

#include "lidar.h"
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
constexpr int PREV_X = 4, NEXT_X = 152, STEP_W = 44;
constexpr int SCAN_X = 52, SCAN_W = 96;
constexpr int DRAW_X = 200, DRAW_W = 36;
// Drawing screen buttons
constexpr int UNDO_X = 2, CLEAR_X = 45, TYPE_X = 88, CANCEL_X = 157, SAVE_X = 200;
constexpr int EDIT_W = 40, TYPE_W = 66;
// Line type picker over the view, two columns
constexpr int PICK_W = VIEW / 2, PICK_H = VIEW / ((LineTypeInfo::COUNT + 1) / 2);
constexpr unsigned long LIDAR_CHECK_MS = 2000;  // how often to look for a lidar while none is found
constexpr int SCAN_TURNS = 1;
constexpr uint16_t LINE_COLOUR = TFT_WHITE;
constexpr int SNAP_PX = 10;  // SexyTopo's SNAP_TO_LINE_SENSITIVITY_DP, about 1 px per dp on this screen
constexpr int MIN_LINE_PX = 10;  // SexyTopo's MINIMUM_DETAIL_SIZE_DP, smaller typed lines are dropped

cyd::Display lcd;
ScanView scanView;
ScanStore store;
Sketch sketch;
Lidar lidar;
Lidar::Info lidarInfo;
bool lidarFound = false;
bool storeOk = false;
bool scanning = false;
bool drawing = false;  // drawing screen open
bool picking = false;  // line type picker open on the drawing screen
LineType lineType = LineType::SKETCH;  // for the next line drawn
int shown = -1;        // saved scan on the screen, -1 for none
int sketchRevision = 0;  // of the drawing shown, 0 if it has never been saved

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
    drawButton(CANCEL_X, EDIT_W, !picking, TFT_NAVY, "Cancel");
    drawButton(SAVE_X, EDIT_W, !picking, TFT_DARKGREEN, "Save");
  } else {
    drawButton(PREV_X, STEP_W, shown > 0, TFT_NAVY, "<");
    drawButton(SCAN_X, SCAN_W, lidarFound && !scanning, TFT_DARKGREEN,
               scanning ? "Scanning" : lidarFound ? "SCAN" : "No lidar");
    drawButton(NEXT_X, STEP_W, shown < store.count() - 1, TFT_NAVY, ">");
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
  line_style::draw(lcd, points, path.type, LINE_COLOUR);
}

// Scan with its sketch over it, in the view
void drawView() {
  scanView.draw(lcd, VIEW);
  lcd.setClipRect(1, 1, VIEW - 2, VIEW - 2);
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
}

// The line types over the view, each with a sample, the one in use highlighted
void drawPicker() {
  lcd.fillRect(0, 0, VIEW, VIEW, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE);
  for (int i = 0; i < LineTypeInfo::COUNT; i++) {
    const int x = i % 2 * PICK_W, y = i / 2 * PICK_H;
    if (i == (int)lineType) lcd.fillRoundRect(x + 2, y + 2, PICK_W - 4, PICK_H - 4, 6, TFT_NAVY);
    else lcd.drawRoundRect(x + 2, y + 2, PICK_W - 4, PICK_H - 4, 6, TFT_DARKGREY);
    lcd.drawString(LineTypeInfo::of((LineType)i).label, x + 8, y + 6);
    const float lineY = y + PICK_H * 0.62f;
    line_style::draw(lcd, {{x + 12.0f, lineY}, {x + PICK_W - 12.0f, lineY}}, (LineType)i, LINE_COLOUR);
  }
}

void showPicker(bool open) {
  picking = open;
  if (open) {
    drawPicker();
    showMessage("Pick the type of line");
  } else {
    drawView();
    showMessage(String(LineTypeInfo::of(lineType).label) + " lines");
  }
  drawButtons();
}

void pressPicker(int x, int y) {
  const int i = y / PICK_H * 2 + x / PICK_W;
  if (i < LineTypeInfo::COUNT) lineType = (LineType)i;
  showPicker(false);
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
}

// Save the drawing if it changed, undo goes back only to here
void saveDrawing() {
  drawing = false;
  drawView();
  drawButtons();
  if (!sketch.changed()) {
    showLidarState();
    return;
  }
  sketch.forgetHistory();
  if (shown < 0) {
    showMessage("Drawing not saved, its scan was not saved");
    return;
  }
  const String &name = store.name(shown);
  const int overwritten = storeOk ? store.saveSketch(name, sketch, sketchRevision + 1) : -1;
  if (overwritten < 0) {
    Serial.printf("SKETCH failed %s\n", name.c_str());
    showMessage("Drawing not saved, flash error");
    return;
  }
  sketchRevision++;
  Serial.printf("SKETCH SAVED %s %d %d\n", name.c_str(), sketchRevision, overwritten);
  sendSketch(name);
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
  if (drawing && (line == "SCAN" || line.startsWith("DELETE "))) {
    Serial.println("BUSY drawing");
  } else if (line == "?") {
    Serial.println("READY");
    sendLidarInfo();
  } else if (line == "SCAN") {
    startScan();
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
    sketch.startPath(scanView.toSketch(x, y), lineType);
    lcd.drawPixel(x, y, LINE_COLOUR);
  } else if (onView && (x != lastX || y != lastY)) {
    sketch.lineTo(scanView.toSketch(x, y));
    lcd.drawLine(lastX, lastY, x, y, LINE_COLOUR);
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
  delay(10);
}
