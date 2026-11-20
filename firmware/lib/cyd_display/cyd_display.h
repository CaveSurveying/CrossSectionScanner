// Display and touch for the supported boards, chosen with a build flag
//   default         CYD ESP32-2432S028R (2 USB), 2.8", ST7789 ID 8181B3, checked 2026-11-19
//   BOARD_E32R24P   LCDwiki E32R24P, 2.4" IPS ST7789, same display and touch pins (lcdwiki.com pin table)
#pragma once
#include <LovyanGFX.hpp>

namespace cyd {
#ifdef BOARD_E32R24P
constexpr int PIN_MISO = -1;        // display not wired for reading back
constexpr bool INVERT_COLOURS = true;  // IPS panel, to be checked by eye
#else
constexpr int PIN_MISO = 12;
constexpr bool INVERT_COLOURS = false;
#endif
constexpr int PIN_SCK = 14, PIN_MOSI = 13, PIN_CS = 15, PIN_DC = 2, PIN_BL = 21;
constexpr int ROTATION_USB_DOWN = 0;  // portrait 240x320, USB at the bottom
constexpr int WIDTH = 240, HEIGHT = 320;
// Touch (XPT2046) has its own SPI pins
constexpr int PIN_T_SCK = 25, PIN_T_MOSI = 32, PIN_T_MISO = 39, PIN_T_CS = 33, PIN_T_IRQ = 36;

// Raw touch to screen pixels, x = a rx + b ry + c, y = d rx + e ry + f
struct TouchCalibration {
  float a, b, c, d, e, f;
};

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI bus;
  lgfx::Touch_XPT2046 touch;
  TouchCalibration cal;

 public:
  Display();

  // Backlight on, panel the right way up, touch calibration loaded.
  // Calibrates first (unless told not to) if none is stored or the screen is held at power-on.
  void start(bool allowCalibration = true);

  // Raw touch reading, false when not touched
  bool touchedRaw(int &rx, int &ry, int *pressure = nullptr);
  // Touch position in portrait screen pixels, false when not touched
  bool touchedAt(int &x, int &y, int *rawX = nullptr, int *rawY = nullptr);

  // Five cross touch calibration, saved on the board. Repeats until the centre check passes.
  void calibrateTouch();
  const TouchCalibration &touchCalibration() const { return cal; }
  // Show a cross, wait for a steady press on it and return the raw position (as calibration does)
  void readTouchTarget(int x, int y, float &rx, float &ry);
};
}  // namespace cyd
