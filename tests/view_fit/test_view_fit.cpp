// Checks firmware/src/view_fit.cpp on the computer, the fit, zoom and move of the scan view.
//   g++ -std=c++11 -I../../firmware/src test_view_fit.cpp ../../firmware/src/view_fit.cpp ../../firmware/src/sketch.cpp -o /tmp/test_view_fit && /tmp/test_view_fit
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "view_fit.h"

namespace {
int failures = 0;

void check(bool ok, const char *what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

ViewFit::Box box(float minX, float maxX, float minY, float maxY) {
  ViewFit::Box b;
  b.add(minX, minY);
  b.add(maxX, maxY);
  return b;
}

// A 4 m by 2 m scan, centred on (1000, 500) mm, in a 240 px view with no margin
ViewFit fitted() {
  ViewFit v;
  v.fit(240, box(-1000, 3000, -500, 1500), 0);
  return v;
}
}  // namespace

int main() {
  {
    const ViewFit v = fitted();
    int px, py;
    v.toScreen({1, -0.5}, px, py);
    check(px == 120 && py == 120, "the scan's centre is the view's centre");
    check(near(v.metresPerPixel(), 4.0f / 240), "the longer side fills the view");
  }
  {
    Sketch sketch;  // from (4, 0.5) to (5, 1.5) m on the screen, so (4000, -500) to (5000, -1500) mm
    sketch.startPath({4, 0.5});
    sketch.lineTo({5, 1.5});
    sketch.finishPath(0);
    ViewFit::Box b = box(-1000, 3000, -500, 1500);
    b.add(sketch);
    check(near(b.maxX, 5000) && near(b.minY, -1500), "the fit takes in lines drawn beyond the scan");
  }
  struct Zoom {
    const char *what;
    int ins, outs, step;
  };
  const Zoom zooms[] = {
      {"in to x8 and no further", 5, 0, 3},
      {"out to x1/4 and no further", 0, 4, -2},
      {"in then out back to x1", 2, 2, 0},
  };
  for (const Zoom &z : zooms) {
    ViewFit v = fitted();
    for (int i = 0; i < z.ins; i++) v.zoomIn();
    for (int i = 0; i < z.outs; i++) v.zoomOut();
    const float span = 4000 / std::ldexp(1.0f, z.step);
    check(v.zoomStep() == z.step && near(v.visibleSpan(), span) && near(v.mmX(120), 1000) && near(v.mmY(120), 500),
          z.what);
  }
  struct Move {
    const char *what;
    int ins;
    float dx, dy;            // px
    float centreX, centreY;  // mm at the view centre after
  };
  const Move moves[] = {
      {"dragging right shows what was to the left", 0, 60, 0, 0, 500},
      {"dragging down shows what was above", 0, 0, 30, 1000, 1000},
      {"zoomed x2, the same drag moves half as far", 1, 60, 30, 500, 750},
      {"no further than a quarter of the view beyond the scan", 0, 1000, -1000, -2000, -1500},
      {"zoomed x4 the limit is closer", 2, -1000, 0, 3250, 500},
  };
  for (const Move &m : moves) {
    ViewFit v = fitted();
    for (int i = 0; i < m.ins; i++) v.zoomIn();
    v.moveBy(m.dx, m.dy);
    check(near(v.mmX(120), m.centreX) && near(v.mmY(120), m.centreY), m.what);
  }
  {
    ViewFit v = fitted();
    v.moveBy(1000, 0);  // centre at the limit, 1000 mm left of the scan
    v.zoomIn();
    check(near(v.mmX(120), -1500), "zooming in keeps the scan in view");
  }
  {
    ViewFit v = fitted();
    v.zoomOut();
    v.moveBy(-37, 23);
    bool roundTrip = true;
    for (int px = 0; px < 240; px += 37)
      for (int py = 0; py < 240; py += 41) {
        int bx, by;
        v.toScreen(v.toSketch(px, py), bx, by);
        roundTrip = roundTrip && bx == px && by == py;
      }
    check(roundTrip, "zoomed out and moved, a pixel goes to the sketch and back to itself");
    v.fit(240, box(-1000, 3000, -500, 1500), 0);
    check(v.zoomStep() == 0 && near(v.mmX(120), 1000) && near(v.mmY(120), 500), "a new fit is x1, centred");
  }
  std::printf("%d failed\n", failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
