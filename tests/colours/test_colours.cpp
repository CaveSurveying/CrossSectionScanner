// Checks firmware/src/colours.cpp on the computer, against SexyTopo's Colour.java.
//   g++ -std=c++11 -I../../firmware/src test_colours.cpp ../../firmware/src/colours.cpp -o /tmp/test_colours && /tmp/test_colours
#include <cstdio>
#include <cstdlib>
#include <string>

#include "colours.h"

int main() {
  int failures = 0;
  struct Case {
    const char *name;
    uint32_t rgb;
    uint16_t rgb565;
    const char *why;
  };
  const Case cases[] = {
      {"RED", 0xFF0000, 0xF800, "red"},
      {"GREEN", 0x008000, 0x0400, "SexyTopo's green, not lime"},
      {"SADDLE_BROWN", 0x8B4513, 0x8A22, "a colour off the brush list"},
      {"BLACK", 0xFFFFFF, 0xFFFF, "black as white, SexyTopo's dark mode"},
      {"BLUE", 0x1E90FF, 0x1C9F, "blue as dodger blue, SexyTopo's dark mode"},
      {"LAVA", 0xFFFFFF, 0xFFFF, "an unknown name as white"},
      {"", 0xFFFFFF, 0xFFFF, "no name as white"},
  };
  for (const Case &c : cases) {
    const bool ok = colours::rgbOf(c.name) == c.rgb && colours::of(c.name) == c.rgb565;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", c.why);
    if (!ok) failures++;
  }
  bool brushesKnown = true;
  for (const colours::Brush &b : colours::BRUSHES)
    brushesKnown = brushesKnown && (colours::rgbOf(b.name) != 0xFFFFFF || std::string(b.name) == "BLACK");
  std::printf("%s every brush colour is in the table\n", brushesKnown ? "ok  " : "FAIL");
  if (!brushesKnown) failures++;
  const bool dim = colours::dimmed(0xFFFF) == 0x7BEF && colours::dimmed(0xF800) == 0x7800;
  std::printf("%s dimmed is half as bright\n", dim ? "ok  " : "FAIL");
  if (!dim) failures++;
  std::printf("%d failed\n", failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
