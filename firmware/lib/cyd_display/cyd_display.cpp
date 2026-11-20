#include "cyd_display.h"

#include <Preferences.h>

namespace cyd {
namespace {
// Starting values until the first calibration, from PlayDisplay-touch.yaml (raw at the screen edges)
constexpr float RAW_X_LEFT = 3800, RAW_X_RIGHT = 260, RAW_Y_TOP = 250, RAW_Y_BOTTOM = 3750;
constexpr TouchCalibration DEFAULT_CAL = {
    (WIDTH - 1) / (RAW_X_RIGHT - RAW_X_LEFT), 0, -(WIDTH - 1) / (RAW_X_RIGHT - RAW_X_LEFT) * RAW_X_LEFT,
    0, (HEIGHT - 1) / (RAW_Y_BOTTOM - RAW_Y_TOP), -(HEIGHT - 1) / (RAW_Y_BOTTOM - RAW_Y_TOP) * RAW_Y_TOP};

constexpr const char *PREFS = "touch", *PREFS_KEY = "cal";
constexpr int INSET = 20;           // px from the screen edge to the corner crosses
constexpr float CHECK_LIMIT = 8;    // px the centre cross may be out by
constexpr int SETTLE_SAMPLES = 5;   // dropped at the start of a press, where readings are noisy
constexpr int NEEDED_SAMPLES = 20;  // kept, 10 ms apart, so a press must be held about a quarter second
constexpr int ALLOWED_GAP = 5;      // readings in a row with no touch before a press counts as released

struct Target {
  int x, y;
};
constexpr Target CORNERS[4] = {{INSET, INSET}, {WIDTH - 1 - INSET, INSET},
                               {WIDTH - 1 - INSET, HEIGHT - 1 - INSET}, {INSET, HEIGHT - 1 - INSET}};
constexpr Target CENTRE = {WIDTH / 2, HEIGHT / 2};

// Least squares fit of v = p rx + q ry + r over n points (3x3 normal equations, Cramer's rule)
bool fitPlane(const float *rx, const float *ry, const float *v, int n, float &p, float &q, float &r) {
  double sxx = 0, sxy = 0, sx = 0, syy = 0, sy = 0, svx = 0, svy = 0, sv = 0;
  for (int i = 0; i < n; i++) {
    sxx += rx[i] * rx[i]; sxy += rx[i] * ry[i]; sx += rx[i];
    syy += ry[i] * ry[i]; sy += ry[i];
    svx += v[i] * rx[i]; svy += v[i] * ry[i]; sv += v[i];
  }
  auto det3 = [](double a, double b, double c, double d, double e, double f, double g, double h, double i) {
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  };
  const double det = det3(sxx, sxy, sx, sxy, syy, sy, sx, sy, n);
  if (fabs(det) < 1e-6) return false;
  p = det3(svx, sxy, sx, svy, syy, sy, sv, sy, n) / det;
  q = det3(sxx, svx, sx, sxy, svy, sy, sx, sv, n) / det;
  r = det3(sxx, sxy, svx, sxy, syy, svy, sx, sy, sv) / det;
  return true;
}

void drawCross(Display &lcd, Target t, uint16_t colour) {
  lcd.drawFastHLine(t.x - 10, t.y, 21, colour);
  lcd.drawFastVLine(t.x, t.y - 10, 21, colour);
  lcd.drawCircle(t.x, t.y, 4, colour);
}

void waitForRelease(Display &lcd) {
  int rx, ry;
  for (int clear = 0; clear < 10;) {  // 100 ms with no touch
    clear = lcd.touchedRaw(rx, ry) ? 0 : clear + 1;
    delay(10);
  }
}

int median(int *v, int n) {
  std::sort(v, v + n);
  return v[n / 2];
}

// Wait for a steady press on the target and return its raw position.
// Medians, as the chip gives some wild readings on one axis during a press.
void readTarget(Display &lcd, Target t, float &rx, float &ry) {
  drawCross(lcd, t, TFT_WHITE);
  for (;;) {
    waitForRelease(lcd);
    int x, y, z;
    while (!lcd.touchedRaw(x, y)) delay(10);
    int xs[NEEDED_SAMPLES], ys[NEEDED_SAMPLES], zs[NEEDED_SAMPLES];
    int samples = 0, gap = 0;
    // The touch reading drops out briefly during a press, so short gaps are allowed
    while (gap < ALLOWED_GAP) {
      if (!lcd.touchedRaw(x, y, &z)) {
        gap++;
        delay(10);
        continue;
      }
      gap = 0;
      if (++samples > SETTLE_SAMPLES) {
        const int i = samples - SETTLE_SAMPLES - 1;
        xs[i] = x, ys[i] = y, zs[i] = z;
      }
      if (samples == SETTLE_SAMPLES + NEEDED_SAMPLES) {
        String all = "";
        for (int i = 0; i < NEEDED_SAMPLES; i++) all += String(" ") + xs[i] + "," + ys[i] + "," + zs[i];
        Serial.printf("CAL samples x,y,pressure%s\n", all.c_str());
        rx = median(xs, NEEDED_SAMPLES);
        ry = median(ys, NEEDED_SAMPLES);
        Serial.printf("CAL target %d %d raw %.0f %.0f\n", t.x, t.y, rx, ry);
        drawCross(lcd, t, TFT_GREEN);
        waitForRelease(lcd);
        drawCross(lcd, t, TFT_BLACK);
        return;
      }
      delay(10);
    }
    Serial.printf("CAL press too short, %d readings\n", samples);
  }
}

void prompt(Display &lcd, const char *line1, const char *line2, uint16_t colour) {
  lcd.fillRect(0, HEIGHT / 2 + 20, WIDTH, 40, TFT_BLACK);
  lcd.setTextColor(colour);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_center);
  lcd.drawString(line1, WIDTH / 2, HEIGHT / 2 + 28);
  lcd.drawString(line2, WIDTH / 2, HEIGHT / 2 + 44);
  lcd.setTextDatum(top_left);
}
}  // namespace

Display::Display() : cal(DEFAULT_CAL) {
  auto b = bus.config();
  b.spi_host = HSPI_HOST;
  b.freq_write = 40000000;
  b.pin_sclk = PIN_SCK; b.pin_mosi = PIN_MOSI; b.pin_miso = PIN_MISO; b.pin_dc = PIN_DC;
  bus.config(b);
  panel.setBus(&bus);
  auto p = panel.config();
  p.pin_cs = PIN_CS; p.pin_rst = -1;
  p.invert = INVERT_COLOURS;
  panel.config(p);
  auto t = touch.config();
  t.spi_host = VSPI_HOST;
  t.freq = 1000000;
  t.pin_sclk = PIN_T_SCK; t.pin_mosi = PIN_T_MOSI; t.pin_miso = PIN_T_MISO; t.pin_cs = PIN_T_CS; t.pin_int = PIN_T_IRQ;
  t.bus_shared = false;
  touch.config(t);
  panel.setTouch(&touch);
  setPanel(&panel);
}

void Display::start(bool allowCalibration) {
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  init();
  setRotation(ROTATION_USB_DOWN);

  Preferences prefs;
  prefs.begin(PREFS, true);
  const bool stored = prefs.getBytes(PREFS_KEY, &cal, sizeof cal) == sizeof cal;
  prefs.end();
  if (!stored) cal = DEFAULT_CAL;
  int rx, ry;
  if (allowCalibration && (!stored || touchedRaw(rx, ry))) calibrateTouch();
}

void Display::readTouchTarget(int x, int y, float &rx, float &ry) { readTarget(*this, {x, y}, rx, ry); }

bool Display::touchedRaw(int &rx, int &ry, int *pressure) {
  lgfx::touch_point_t tp;
  if (!getTouchRaw(&tp, 1)) return false;
  rx = tp.x;
  ry = tp.y;
  if (pressure) *pressure = tp.size;
  return true;
}

bool Display::touchedAt(int &x, int &y, int *rawX, int *rawY) {
  int rx, ry;
  if (!touchedRaw(rx, ry)) return false;
  x = constrain(lroundf(cal.a * rx + cal.b * ry + cal.c), 0, WIDTH - 1);
  y = constrain(lroundf(cal.d * rx + cal.e * ry + cal.f), 0, HEIGHT - 1);
  if (rawX) *rawX = rx;
  if (rawY) *rawY = ry;
  return true;
}

void Display::calibrateTouch() {
  fillScreen(TFT_BLACK);
  prompt(*this, "Touch calibration", "Press and hold each cross", TFT_WHITE);
  waitForRelease(*this);
  for (;;) {
    float rx[4], ry[4], tx[4], ty[4];
    for (int i = 0; i < 4; i++) {
      readTarget(*this, CORNERS[i], rx[i], ry[i]);
      tx[i] = CORNERS[i].x;
      ty[i] = CORNERS[i].y;
    }
    TouchCalibration fit;
    const bool solved = fitPlane(rx, ry, tx, 4, fit.a, fit.b, fit.c) && fitPlane(rx, ry, ty, 4, fit.d, fit.e, fit.f);

    // Centre cross checks the fit
    float cx, cy;
    readTarget(*this, CENTRE, cx, cy);
    const float ex = fit.a * cx + fit.b * cy + fit.c - CENTRE.x, ey = fit.d * cx + fit.e * cy + fit.f - CENTRE.y;
    const float error = sqrtf(ex * ex + ey * ey);
    if (solved && error <= CHECK_LIMIT) {
      cal = fit;
      Preferences prefs;
      prefs.begin(PREFS, false);
      prefs.putBytes(PREFS_KEY, &cal, sizeof cal);
      prefs.end();
      Serial.printf("CAL %.5f %.5f %.1f %.5f %.5f %.1f centre off %.1f px\n", cal.a, cal.b, cal.c, cal.d, cal.e, cal.f, error);
      fillScreen(TFT_BLACK);
      return;
    }
    Serial.printf("CAL failed, centre off %.1f px\n", error);
    char msg[40];
    snprintf(msg, sizeof msg, "Centre was %.0f px out, limit %.0f", error, CHECK_LIMIT);
    prompt(*this, msg, "Again, press and hold each cross", TFT_YELLOW);
  }
}
}  // namespace cyd
