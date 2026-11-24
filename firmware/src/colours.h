// SexyTopo's colours (model/sketch/Colour.java) as drawn on a dark screen, and the brush colours its
// sketch toolbar offers (BrushColour.java). Plain C++, so it can be checked on the computer.
#pragma once
#include <cstdint>
#include <string>

namespace colours {
// 0xRRGGBB to draw a colour named as in SexyTopo's files. Black shows as white and blue as dodger
// blue, as in SexyTopo's dark mode (Colour.getDrawColour). White for a name not known.
uint32_t rgbOf(const std::string &name);
uint16_t to565(uint32_t rgb);
inline uint16_t of(const std::string &name) { return to565(rgbOf(name)); }
// Half as bright, for lines too thin to draw at their width
inline uint16_t dimmed(uint16_t c) { return (c >> 1) & 0x7BEF; }

struct Brush {
  const char *name;   // as written in the file
  const char *label;  // on the screen
};
constexpr int BRUSH_COUNT = 8;
extern const Brush BRUSHES[BRUSH_COUNT];
}  // namespace colours
