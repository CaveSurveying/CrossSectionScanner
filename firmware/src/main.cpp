// CrossSectionScanner, stand-alone unit: SCAN takes a lidar scan, shows it and saves it in flash.
// ‹ and › step through saved scans. A computer on the USB port can watch, ask for scans and
// fetch saved ones for debugging (tools/board_link.py)
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
//   TOUCH x y rx ry each new touch, screen and raw (for checking calibration)
//   CAL ...         new touch calibration, or why it failed
// Computer to board
//   ?               reply READY and the lidar details
//   SCAN            scan as if SCAN were pressed
//   LIST            list the saved scans
//   GET <name>      send a saved scan
//   DELETE <name>   delete a saved scan
#include <Arduino.h>
#include <cyd_display.h>

#include "lidar.h"
#include "scan_store.h"
#include "scan_view.h"

constexpr long BAUD = 460800;
constexpr int VIEW = 240;  // square scan view at the top of the portrait screen
constexpr int MSG_Y = VIEW + ScanView::NOTES_H;
constexpr int BTN_Y = 272, BTN_H = 42;
constexpr int PREV_X = 4, NEXT_X = 152, STEP_W = 44;
constexpr int SCAN_X = 52, SCAN_W = 96;
constexpr int CAL_X = 200, CAL_W = 36;
constexpr unsigned long CAL_HOLD_MS = 1000;     // so it is not started by accident
constexpr unsigned long LIDAR_CHECK_MS = 2000;  // how often to look for a lidar while none is found
constexpr int SCAN_TURNS = 1;

cyd::Display lcd;
ScanView scanView;
ScanStore store;
Lidar lidar;
Lidar::Info lidarInfo;
bool lidarFound = false;
bool storeOk = false;
bool scanning = false;
int shown = -1;  // saved scan on the screen, -1 for none

bool inside(int x, int y, int bx, int bw) { return x >= bx && x < bx + bw && y >= BTN_Y && y < BTN_Y + BTN_H; }

void drawButton(int x, int w, bool active, uint16_t colour, const char *text, int textSize) {
  lcd.fillRoundRect(x, BTN_Y, w, BTN_H, 8, active ? colour : TFT_DARKGREY);
  lcd.setTextSize(textSize);
  lcd.drawString(text, x + w / 2, BTN_Y + BTN_H / 2);
}

void drawButtons() {
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  drawButton(PREV_X, STEP_W, shown > 0, TFT_NAVY, "<", 2);
  drawButton(SCAN_X, SCAN_W, lidarFound && !scanning, TFT_DARKGREEN, scanning ? "Scanning" : lidarFound ? "SCAN" : "No lidar",
             scanning || !lidarFound ? 1 : 2);
  drawButton(NEXT_X, STEP_W, shown < store.count() - 1, TFT_NAVY, ">", 2);
  drawButton(CAL_X, CAL_W, true, TFT_NAVY, "", 1);
  lcd.drawString("Cal", CAL_X + CAL_W / 2, BTN_Y + BTN_H / 2 - 6);
  lcd.drawString("hold", CAL_X + CAL_W / 2, BTN_Y + BTN_H / 2 + 6);
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

void showSaved(int i) {
  if (!store.load(i, scanView)) return;
  shown = i;
  scanView.draw(lcd, VIEW);
  drawButtons();
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
  scanView.draw(lcd, VIEW);
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
  if (line == "?") {
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

// Whole screen after start-up or calibration
void drawScreen() {
  lcd.fillScreen(TFT_BLACK);
  if (shown >= 0) showSaved(shown);
  showLidarState();
  drawButtons();
}

void readTouch() {
  static bool wasTouched = false;
  static unsigned long calPressedAt = 0;  // 0 when Cal is not being held
  int x, y, rx, ry;
  const bool touched = lcd.touchedAt(x, y, &rx, &ry);
  if (touched && !wasTouched) {
    Serial.printf("TOUCH %d %d %d %d\n", x, y, rx, ry);
    lcd.fillCircle(x, y, 3, TFT_YELLOW);  // shows where the touch landed
    if (inside(x, y, SCAN_X, SCAN_W) && lidarFound) startScan();
    if (inside(x, y, PREV_X, STEP_W) && shown > 0) showSaved(shown - 1);
    if (inside(x, y, NEXT_X, STEP_W) && shown < store.count() - 1) showSaved(shown + 1);
    if (inside(x, y, CAL_X, CAL_W)) calPressedAt = millis();
  }
  if (!touched || !inside(x, y, CAL_X, CAL_W)) calPressedAt = 0;
  if (calPressedAt && millis() - calPressedAt >= CAL_HOLD_MS) {
    calPressedAt = 0;
    lcd.calibrateTouch();
    drawScreen();
  }
  wasTouched = touched;
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(BAUD);
  lcd.start();
  storeOk = store.begin();
  shown = store.count() - 1;  // newest
  lidar.begin();
  for (int i = 0; i < 10 && !lidarFound; i++) {  // the lidar may still be starting up
    lidarFound = lidar.readInfo(lidarInfo);
    if (!lidarFound) delay(200);
  }
  drawScreen();
  Serial.println("READY");
  sendLidarInfo();
}

void loop() {
  static unsigned long lastCheck = 0;
  if (!lidarFound && millis() - lastCheck >= LIDAR_CHECK_MS) {  // it may be plugged in later
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
