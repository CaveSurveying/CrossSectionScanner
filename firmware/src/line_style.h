// Lines drawn on the screen in the style of their type, after SexyTopo's LineType.java (upstream
// main 26c93a7a): wider walls, dashes, and ticks, hachures or teeth repeated along the line, a
// positive tick on the right of the way the line runs. Sized in screen pixels, as the view has
// no zoom.
#pragma once
#include <LovyanGFX.hpp>

#include <vector>

#include "sketch.h"

namespace line_style {
constexpr float ORNAMENT_PX = 7;  // SexyTopo's ornament size, the unit of its dashes and ticks

struct Point {
  float x, y;
};

// The line through the screen points, a dot if they are all one point
void draw(lgfx::LGFXBase &lcd, const std::vector<Point> &points, LineType type, uint16_t colour);
}  // namespace line_style
