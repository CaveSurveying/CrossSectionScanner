// Touch map test: press each of a 4 x 5 grid of crosses, raw readings go to serial as
//   MAP <screen x> <screen y> <raw x> <raw y>
// tools/touch_map.py reads them and plots the bend
#include <Arduino.h>
#include <cyd_display.h>

constexpr int COLS = 4, ROWS = 5, INSET = 20;

cyd::Display lcd;

void setup() {
  Serial.begin(460800);
  lcd.start(false);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.drawString("Touch map: press and hold each cross", cyd::WIDTH / 2, cyd::HEIGHT / 2 + 40);
  lcd.setTextDatum(top_left);
  Serial.println("MAP start");
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      const int x = INSET + c * (cyd::WIDTH - 1 - 2 * INSET) / (COLS - 1);
      const int y = INSET + r * (cyd::HEIGHT - 1 - 2 * INSET) / (ROWS - 1);
      float rx, ry;
      lcd.readTouchTarget(x, y, rx, ry);
      Serial.printf("MAP %d %d %.0f %.0f\n", x, y, rx, ry);
      lcd.fillCircle(x, y, 2, TFT_DARKGREY);  // done
    }
  }
  Serial.println("MAP done");
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.drawString("Touch map done", cyd::WIDTH / 2, cyd::HEIGHT / 2);
}

void loop() {}
