// CrossSectionScanner, stand-alone unit: SCAN takes a lidar scan and shows it.
// A computer on the USB port can watch and ask for scans for debugging (tools/board_link.py)
//
// Board to computer
//   READY           after boot and in reply to ?
//   LIDAR <model> <firmware> <hardware> <serial>   lidar details, in reply to ? and before each scan
//   LIDAR none      no lidar answers
//   SCANDATA start  a scan follows, then T and P <deg> <mm> <quality> lines, raw as the lidar sent them
//   SCANDATA end
//   LIDAR failed    a scan did not complete
//   TOUCH x y rx ry each new touch, screen and raw (for checking calibration)
//   CAL ...         new touch calibration, or why it failed
// Computer to board
//   ?               reply READY and the lidar details
//   SCAN            scan as if SCAN were pressed
#include <Arduino.h>
#include <cyd_display.h>

#include "lidar.h"
#include "scan_view.h"

constexpr long BAUD = 460800;
constexpr int VIEW = 240;  // square scan view at the top of the portrait screen
constexpr int BTN_Y = 272, BTN_H = 42;
constexpr int SCAN_X = 10, SCAN_W = 150;
constexpr int CAL_X = 170, CAL_W = 60;
constexpr unsigned long CAL_HOLD_MS = 1000;     // so it is not started by accident
constexpr unsigned long LIDAR_CHECK_MS = 2000;  // how often to look for a lidar while none is found
constexpr int SCAN_TURNS = 1;

cyd::Display lcd;
ScanView scanView;
Lidar lidar;
Lidar::Info lidarInfo;
bool lidarFound = false;
bool scanning = false;
int scans = 0;

bool inside(int x, int y, int bx, int bw) { return x >= bx && x < bx + bw && y >= BTN_Y && y < BTN_Y + BTN_H; }

void drawButtons() {
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.fillRoundRect(SCAN_X, BTN_Y, SCAN_W, BTN_H, 8, lidarFound && !scanning ? TFT_DARKGREEN : TFT_DARKGREY);
  lcd.setTextSize(2);
  lcd.drawString(scanning ? "Scanning" : lidarFound ? "SCAN" : "No lidar", SCAN_X + SCAN_W / 2, BTN_Y + BTN_H / 2);
  lcd.fillRoundRect(CAL_X, BTN_Y, CAL_W, BTN_H, 8, TFT_NAVY);
  lcd.setTextSize(1);
  lcd.drawString("Cal", CAL_X + CAL_W / 2, BTN_Y + BTN_H / 2 - 6);
  lcd.drawString("(hold)", CAL_X + CAL_W / 2, BTN_Y + BTN_H / 2 + 6);
  lcd.setTextDatum(top_left);
}

void showMessage(const String &text) {
  lcd.fillRect(0, VIEW + 2, VIEW, BTN_Y - VIEW - 4, TFT_BLACK);
  lcd.setTextColor(TFT_YELLOW);
  lcd.setTextSize(1);
  lcd.setCursor(4, VIEW + 6);
  lcd.print(text);
}

void showLidarState() { showMessage(lidarFound ? "Lidar found, press SCAN" : "No lidar found"); }

// Look for the lidar, true if that changed whether it is found
bool checkLidar() {
  const bool was = lidarFound;
  lidarFound = lidar.readInfo(lidarInfo);
  return lidarFound != was;
}

void sendLidarInfo() {
  if (!lidarFound) {
    Serial.println("LIDAR none");
    return;
  }
  char serial[33];
  for (int i = 0; i < 16; i++) sprintf(serial + 2 * i, "%02X", lidarInfo.serial[i]);
  Serial.printf("LIDAR %d %d.%02d %d %s\n", lidarInfo.model, lidarInfo.firmwareMajor, lidarInfo.firmwareMinor,
                lidarInfo.hardware, serial);
}

// Raw scan to the computer, for saving and checking there
void sendScan() {
  sendLidarInfo();
  Serial.println("SCANDATA start");
  for (int t = 0; t < lidar.turnCount(); t++) {
    Serial.println("T");
    for (int i = lidar.turnStart(t); i < lidar.turnStart(t + 1); i++) {
      const Lidar::Point &p = lidar.points()[i];
      Serial.printf("P %.3f %.2f %d\n", p.angleDeg(), p.distanceMm(), p.quality);
    }
  }
  Serial.println("SCANDATA end");
}

void startScan() {
  if (!lidarFound || scanning) {
    sendLidarInfo();
    return;
  }
  scanning = true;
  drawButtons();
  if (lidar.capture(SCAN_TURNS)) {
    scanView.clear(String("Scan ") + ++scans);
    for (int t = 0; t < lidar.turnCount(); t++) {
      scanView.nextTurn();
      for (int i = lidar.turnStart(t); i < lidar.turnStart(t + 1); i++) {
        const Lidar::Point &p = lidar.points()[i];
        if (p.distanceQ2 > 0) scanView.addPoint(p.angleDeg(), p.distanceMm());
      }
    }
    scanView.draw(lcd, VIEW);
    sendScan();
  } else {
    Serial.println("LIDAR failed");
    checkLidar();  // it may have been unplugged
    showMessage(lidarFound ? "Lidar scan failed" : "Lidar scan failed, no lidar found");
  }
  scanning = false;
  drawButtons();
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
    if (line == "?") {
      Serial.println("READY");
      sendLidarInfo();
    } else if (line == "SCAN") {
      startScan();
    }
    line = "";
  }
}

// Whole screen after start-up or calibration
void drawScreen() {
  lcd.fillScreen(TFT_BLACK);
  if (scanView.count() > 0) scanView.draw(lcd, VIEW);
  else showLidarState();
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
