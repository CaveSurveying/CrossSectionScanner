// Read the display controller ID over SPI, then show it on the screen
#include <Arduino.h>
#include <SPI.h>
#include <cyd_display.h>

using namespace cyd;

SPIClass hspi(HSPI);
String report;

// Send a command and read n bytes back
void readCommand(uint8_t cmd, uint8_t *out, int n) {
  hspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW);
  hspi.transfer(cmd);
  digitalWrite(PIN_DC, HIGH);
  for (int i = 0; i < n; i++) out[i] = hspi.transfer(0);
  digitalWrite(PIN_CS, HIGH);
  hspi.endTransaction();
}

void addLine(const char *name, uint8_t cmd, int n) {
  uint8_t b[5] = {};
  readCommand(cmd, b, n);
  char line[64];
  int len = snprintf(line, sizeof line, "%s (0x%02X):", name, cmd);
  for (int i = 0; i < n; i++) len += snprintf(line + len, sizeof line - len, " %02X", b[i]);
  report += String(line) + "\n";
}

// Name the controller from the raw bytes
String identify() {
  if (report.indexOf("93 41") >= 0) return "ILI9341";
  if (report.indexOf("93 42") >= 0) return "ILI9342";
  if (report.indexOf("85 85 52") >= 0 || report.indexOf("85 52") >= 0) return "ST7789";
  // Single byte reads ID1 ID2 ID3 = 81 81 B3, an ST7789 revision found on CYD2USB boards
  if (report.indexOf("ID1 (0xDA): 81") >= 0 && report.indexOf("ID3 (0xDC): B3") >= 0) return "ST7789 (ID 8181B3)";
  return "unknown, see raw bytes";
}

Display lcd;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_CS, OUTPUT); digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  hspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);

  uint8_t none[1];
  readCommand(0x01, none, 0);  // software reset
  delay(150);
  addLine("ID4", 0xD3, 4);
  addLine("RDDID", 0x04, 4);
  addLine("ID1", 0xDA, 2);
  addLine("ID2", 0xDB, 2);
  addLine("ID3", 0xDC, 2);
  hspi.end();

  Serial.println(report + "Display: " + identify());
  lcd.start();
}

// Draw the result, the rotation number and colour blocks in one orientation
void drawRotation(int r) {
  lcd.setRotation(r);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.setCursor(4, 4);
  lcd.println(identify());
  lcd.setTextSize(1);
  lcd.print(report);
  lcd.setTextSize(6);
  lcd.setCursor(lcd.width() / 2 - 16, lcd.height() / 2 - 10);
  lcd.print(r);
  const int y = lcd.height() - 30, w = lcd.width() / 3;
  lcd.setTextSize(2);
  const uint16_t colours[] = {TFT_RED, TFT_GREEN, TFT_BLUE};
  const char *names[] = {"RED", "GREEN", "BLUE"};
  for (int i = 0; i < 3; i++) {
    lcd.fillRect(i * w, y, w, 30, colours[i]);
    lcd.setTextColor(TFT_BLACK);
    lcd.setCursor(i * w + 4, y + 8);
    lcd.print(names[i]);
  }
  lcd.fillTriangle(lcd.width() - 1, 0, lcd.width() - 20, 0, lcd.width() - 1, 20, TFT_YELLOW);  // top right marker
}

void loop() {
  // Step through all 8 orientations (4 to 7 are mirrored)
  static int r = 0;
  drawRotation(r);
  Serial.printf("Showing rotation %d\n", r);
  r = (r + 1) % 8;
  delay(4000);
}
