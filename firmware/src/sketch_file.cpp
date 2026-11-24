#include "sketch_file.h"

#include <ArduinoJson.h>

namespace {
const char *FORMAT = "XScan sketch 1";
const char *SEXYTOPO = "26c93a7a";  // SexyTopo upstream commit the format and drawing follow
constexpr size_t POINT_BYTES = 40, HEADER_BYTES = 300;
}  // namespace

namespace sketch_file {
size_t maxSize(const Sketch &sketch) {
  size_t size = HEADER_BYTES;
  for (const Sketch::Path &p : sketch.paths()) size += 70 + POINT_BYTES * p.points.size();
  const Sketch::Extras &e = sketch.extras();
  return size + e.areas.size() + e.labels.size() + e.symbols.size();
}

void write(Print &out, const Sketch &sketch, const String &scan, int revision) {
  out.printf("{\"format\": \"%s\", \"scan\": \"%s\", \"revision\": %d, \"sexytopo\": \"%s\",\n", FORMAT, scan.c_str(),
             revision, SEXYTOPO);
  out.print("\"sketch\": {\"paths\": [");
  const std::vector<Sketch::Path> &paths = sketch.paths();
  for (size_t i = 0; i < paths.size(); i++) {
    out.printf("%s\n{\"colour\": \"%s\", ", i ? "," : "", paths[i].colour.c_str());
    const LineTypeInfo &type = LineTypeInfo::of(paths[i].type);
    if (type.isSemantic()) out.printf("\"line-type\": \"%s\", ", type.name);  // left out for SKETCH, as SexyTopo
    out.print("\"points\": [");
    for (size_t j = 0; j < paths[i].points.size(); j++) {
      const Coord &c = paths[i].points[j];
      out.printf("%s{\"x\": %.3f, \"y\": %.3f}", j ? ", " : "", c.x, c.y);  // 1 mm, finer is pointless
    }
    out.print("]}");
  }
  const Sketch::Extras &e = sketch.extras();
  out.printf("\n], \"areas\": %s, \"labels\": %s, \"symbols\": %s}}\n", e.areas.c_str(), e.labels.c_str(),
             e.symbols.c_str());
}

bool read(Stream &in, Sketch &sketch, int &revision) {
  JsonDocument filter;  // keep only what is used, to save memory
  filter["format"] = true;
  filter["revision"] = true;
  filter["sketch"]["paths"][0]["colour"] = true;
  filter["sketch"]["paths"][0]["line-type"] = true;
  filter["sketch"]["paths"][0]["points"][0]["x"] = true;
  filter["sketch"]["paths"][0]["points"][0]["y"] = true;
  filter["sketch"]["areas"] = true;
  filter["sketch"]["labels"] = true;
  filter["sketch"]["symbols"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, in, DeserializationOption::Filter(filter))) return false;
  return fromJson(doc.as<JsonVariantConst>(), sketch, revision);
}

namespace {
Coord coordOf(JsonVariantConst point) { return {point["x"] | 0.0f, point["y"] | 0.0f}; }

std::string textOf(JsonVariantConst array) {
  std::string text;
  if (array.is<JsonArrayConst>()) serializeJson(array, text);
  return text.empty() ? "[]" : text;
}

// A symbol's id shortened to fit beside its marker, STALACTITE as stal
std::string shortName(std::string id) {
  id = id.substr(0, 4);
  for (char &c : id) c = tolower(c);
  return id;
}
}  // namespace

bool fromJson(JsonVariantConst doc, Sketch &sketch, int &revision) {
  if (String(doc["format"] | "") != FORMAT) return false;
  JsonVariantConst drawing = doc["sketch"];
  std::vector<Sketch::Path> paths;
  for (JsonObjectConst path : drawing["paths"].as<JsonArrayConst>()) {
    Sketch::Path p;
    p.type = LineTypeInfo::fromName(path["line-type"] | (const char *)nullptr);
    p.colour = path["colour"] | "BLACK";
    for (JsonObjectConst point : path["points"].as<JsonArrayConst>()) p.points.push_back(coordOf(point));
    if (!p.points.empty()) paths.push_back(p);
  }
  Sketch::Extras extras;
  extras.areas = textOf(drawing["areas"]);
  extras.labels = textOf(drawing["labels"]);
  extras.symbols = textOf(drawing["symbols"]);
  for (JsonObjectConst area : drawing["areas"].as<JsonArrayConst>()) {
    Sketch::Extras::Outline outline;
    for (JsonObjectConst point : area["points"].as<JsonArrayConst>()) outline.points.push_back(coordOf(point));
    outline.colour = area["colour"] | "BLACK";
    extras.outlines.push_back(outline);
  }
  // sizes default as SexyTopo reads them (SketchJsonTranslater)
  for (JsonObjectConst label : drawing["labels"].as<JsonArrayConst>())
    extras.marks.push_back(
        {coordOf(label["location"]), label["text"] | "", false, label["colour"] | "BLACK", label["size"] | 0.0f});
  for (JsonObjectConst symbol : drawing["symbols"].as<JsonArrayConst>())
    extras.marks.push_back({coordOf(symbol["location"]), shortName(symbol["symbol-id"] | ""), true,
                            symbol["colour"] | "BLACK", symbol["size"] | 1.0f});
  revision = doc["revision"] | 0;
  sketch.setPaths(paths);
  sketch.setExtras(extras);
  return true;
}
}  // namespace sketch_file
