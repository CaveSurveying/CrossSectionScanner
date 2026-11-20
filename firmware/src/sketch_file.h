// A drawing as saved and sent, SexyTopo's cross section sub-sketch (control/io/basic/
// SketchJsonTranslater.java toSubSketchJson, upstream main 26c93a7a) inside our own header
//   {"format": "XScan sketch 1", "scan": "b0007-003", "revision": 2, "sexytopo": "26c93a7a",
//    "sketch": {"paths": [{"colour": "BLACK", "points": [{"x": 0.1, "y": -0.2}, ...]}, ...],
//               "areas": [], "labels": [], "symbols": []}}
// Plain sketch lines leave out SexyTopo's line-type, as SexyTopo does.
#pragma once
#include <Arduino.h>

#include "sketch.h"

namespace sketch_file {
// Most bytes the drawing can take when written, for making room before saving
size_t maxSize(const Sketch &sketch);
void write(Print &out, const Sketch &sketch, const String &scan, int revision);
// Replace the sketch's lines with those read, false if it can't be read (or is too big to hold)
bool read(Stream &in, Sketch &sketch, int &revision);
}  // namespace sketch_file
