#include "sketch_file.h"

#include <ArduinoJson.h>

namespace {
const char *FORMAT = "XScan sketch 1";
const char *SEXYTOPO = "26c93a7a";  // SexyTopo upstream commit the format and drawing follow
const char *COLOUR = "BLACK";       // SexyTopo's default brush colour
constexpr size_t POINT_BYTES = 40, HEADER_BYTES = 300;
}  // namespace

namespace sketch_file {
size_t maxSize(const Sketch &sketch) {
  size_t size = HEADER_BYTES;
  for (const Sketch::Path &p : sketch.paths()) size += 70 + POINT_BYTES * p.points.size();
  return size;
}

void write(Print &out, const Sketch &sketch, const String &scan, int revision) {
  out.printf("{\"format\": \"%s\", \"scan\": \"%s\", \"revision\": %d, \"sexytopo\": \"%s\",\n", FORMAT, scan.c_str(),
             revision, SEXYTOPO);
  out.print("\"sketch\": {\"paths\": [");
  const std::vector<Sketch::Path> &paths = sketch.paths();
  for (size_t i = 0; i < paths.size(); i++) {
    out.printf("%s\n{\"colour\": \"%s\", ", i ? "," : "", COLOUR);
    const LineTypeInfo &type = LineTypeInfo::of(paths[i].type);
    if (type.isSemantic()) out.printf("\"line-type\": \"%s\", ", type.name);  // left out for SKETCH, as SexyTopo
    out.print("\"points\": [");
    for (size_t j = 0; j < paths[i].points.size(); j++) {
      const Coord &c = paths[i].points[j];
      out.printf("%s{\"x\": %.4f, \"y\": %.4f}", j ? ", " : "", c.x, c.y);
    }
    out.print("]}");
  }
  out.print("\n], \"areas\": [], \"labels\": [], \"symbols\": []}}\n");
}

bool read(Stream &in, Sketch &sketch, int &revision) {
  JsonDocument filter;  // keep only what is used, to save memory
  filter["format"] = true;
  filter["revision"] = true;
  filter["sketch"]["paths"][0]["line-type"] = true;
  filter["sketch"]["paths"][0]["points"][0]["x"] = true;
  filter["sketch"]["paths"][0]["points"][0]["y"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, in, DeserializationOption::Filter(filter))) return false;
  if (String(doc["format"] | "") != FORMAT) return false;
  std::vector<Sketch::Path> paths;
  for (JsonObject path : doc["sketch"]["paths"].as<JsonArray>()) {
    Sketch::Path p;
    p.type = LineTypeInfo::fromName(path["line-type"] | (const char *)nullptr);
    for (JsonObject point : path["points"].as<JsonArray>()) p.points.push_back({point["x"], point["y"]});
    if (!p.points.empty()) paths.push_back(p);
  }
  revision = doc["revision"] | 0;
  sketch.setPaths(paths);
  return true;
}
}  // namespace sketch_file
