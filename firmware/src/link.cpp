#include "link.h"

#include <LittleFS.h>
#include <StreamString.h>

#include "exchange.h"
#include "link_ble.h"
#include "sketch_file.h"

namespace {
const char *PATH = "/link.json";
constexpr int PROTOCOL = 2;
constexpr unsigned long PART_FRAME_MS = 10000;  // a part frame is dropped when nothing more comes
constexpr unsigned long REPLY_MS = 30000;       // a section with no reply by then is sent again
}  // namespace

void Link::begin(ScanStore &scans, bool (*drawingOpen)(const String &scan)) {
  store = &scans;
  beingDrawn = drawingOpen;
  File f = LittleFS.open(PATH, "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) state.fromJson(doc);
  }
  scansChanged();
  link_ble::begin();
}

bool Link::connected() const { return link_ble::connected(); }

bool Link::loop() {
  bool changed = false;
  const int change = link_ble::takeConnectionChange();
  if (change) {
    reader.clear();
    held.clear();  // SexyTopo sends them again on the next connection
    surveyed = false;
    inFlight.clear();  // sent again on the next connection, as it had no reply
    changed = true;
  }
  const std::string bytes = link_ble::takeReceived();
  if (!bytes.empty()) {
    if (reader.waiting() && millis() - lastBytesAt > PART_FRAME_MS) reader.clear();
    lastBytesAt = millis();
    for (link_frame::Result &r : reader.feed((const uint8_t *)bytes.data(), bytes.size())) {
      if (r.dropped) Serial.printf("LINK dropped %s\n", r.dropped);
      else handle(r.message);
      changed = true;
    }
  }
  if (!inFlight.empty() && millis() - sentAt > REPLY_MS) {
    Serial.printf("LINK no reply %s\n", inFlight.c_str());
    inFlight.clear();
    sendNext();
  }
  link_ble::pump();
  return changed;
}

void Link::handle(JsonDocument &message) {
  const String type = message["type"] | "";
  if (type == "hello") {
    if ((message["protocol"] | 0) != PROTOCOL) {
      Serial.printf("LINK protocol %d, ours %d\n", (int)(message["protocol"] | 0), PROTOCOL);
      return;  // SexyTopo disconnects
    }
    JsonDocument hello;
    hello["type"] = "hello";
    hello["protocol"] = PROTOCOL;
    hello["firmware"] = FIRMWARE;
    hello["board"] = link_ble::boardId();
    std::string json;
    serializeJson(hello, json);
    sendMessage(json);
  } else if (type == "survey") {
    std::vector<std::string> stations;
    for (JsonVariant s : message["stations"].as<JsonArray>()) stations.push_back(s | "");
    std::map<std::string, std::string> scanStations;
    for (JsonObject s : message["scans"].as<JsonArray>()) scanStations[s["scan"] | ""] = s["station"] | "";
    state.setSurvey(message["name"] | "", stations, scanStations);
    surveyed = true;
    save();
    sendNext();
  } else if (type == "station_added") {
    state.stationAdded(message["name"] | "");
    save();
  } else if (type == "station_renamed") {
    state.stationRenamed(message["from"] | "", message["to"] | "");
    save();
  } else if (type == "station_deleted") {
    state.stationDeleted(message["name"] | "");
    save();
  } else if (type == "scan_station") {
    state.scanStation(message["scan"] | "", message["station"] | "");
    save();
  } else if (type == "drawing") {
    onDrawing(message);
  } else if (type == "reply") {
    const std::string scan = message["scan"] | "";
    if (scan.empty()) {  // our frame was unreadable, so send it again
      inFlight.clear();
      sendNext();
      return;
    }
    const String result = message["result"] | "";
    Serial.printf("LINK reply %s %d %s %s\n", scan.c_str(), (int)(message["revision"] | 0), result.c_str(),
                  (const char *)(message["reason"] | ""));
    state.reply(scan, message["revision"] | -1, revisionOf(scan.c_str()), result.c_str(), message["reason"] | "");
    save();
    if (scan == inFlight) {
      inFlight.clear();
      sendNext();
    }
  }
  // anything else is from a later protocol, so ignored
}

// A drawing edited in SexyTopo, taken if ours is the one it was based on
void Link::onDrawing(JsonDocument &message) {
  const std::string scan = message["scan"] | "";
  const int revision = message["revision"] | 0;
  const bool open = beingDrawn && beingDrawn(scan.c_str());
  const DrawingAnswer answer = LinkModel::answerDrawing(isSaved(scan), open, revisionOf(scan.c_str()), revision,
                                                        message["based_on"] | 0);
  Serial.printf("LINK drawing %s %d answer %d\n", scan.c_str(), revision, (int)answer);
  if (answer == DrawingAnswer::NO_SCAN) {
    replyDrawing(scan, revision, "error", "no_scan");
  } else if (answer == DrawingAnswer::HOLD) {
    held[scan].clear();
    serializeJson(message, held[scan]);
  } else if (answer == DrawingAnswer::CLASH) {
    replyDrawing(scan, revision, "conflict");
    state.drawingSaved(scan);  // ours goes to SexyTopo, which asks which to keep
    save();
    sendNext();
  } else if (answer == DrawingAnswer::ALREADY) {
    state.drawingAgreed(scan, revision);
    save();
    replyDrawing(scan, revision, "ok");
  } else {
    Sketch drawing;
    int ignored;
    if (!sketch_file::fromJson(message["sketch"], drawing, ignored)) {
      replyDrawing(scan, revision, "error", "bad_sketch");
    } else if (store->saveSketch(scan.c_str(), drawing, revision) < 0) {
      replyDrawing(scan, revision, "error", "flash");
    } else {
      state.drawingAgreed(scan, revision);
      save();
      updated = scan;
      replyDrawing(scan, revision, "ok");
    }
  }
}

void Link::replyDrawing(const std::string &scan, int revision, const char *result, const char *reason) {
  JsonDocument reply;
  reply["type"] = "drawing_reply";
  reply["scan"] = scan;
  reply["revision"] = revision;
  reply["result"] = result;
  if (reason) reply["reason"] = reason;
  std::string json;
  serializeJson(reply, json);
  sendMessage(json);
}

bool Link::isSaved(const std::string &scan) const {
  for (int i = 0; i < store->count(); i++)
    if (store->name(i) == scan.c_str()) return true;
  return false;
}

void Link::drawingClosed(const String &scan, bool saved) {
  auto h = held.find(scan.c_str());
  if (h == held.end()) return;
  JsonDocument message;
  const bool readable = !deserializeJson(message, h->second);
  held.erase(h);
  if (!readable) return;
  if (saved) replyDrawing(scan.c_str(), message["revision"] | 0, "conflict");  // ours is on its way
  else onDrawing(message);
}

bool Link::takeUpdated(String &scan) {
  if (updated.empty()) return false;
  scan = updated.c_str();
  updated.clear();
  return true;
}

void Link::sendMessage(const std::string &json) { link_ble::send(link_frame::frame(json)); }

// The next accepted scan as a section, if connected, surveyed and nothing waiting for a reply
void Link::sendNext() {
  if (!connected() || !surveyed || !inFlight.empty()) return;
  const std::string scan = state.nextToSend();
  if (scan.empty()) return;
  const ScanLink *link = state.find(scan);
  StreamString text, sketch;
  store->send(scan.c_str(), text);
  const std::string line = exchange::line(text.c_str(), link->station, link->horizontal, link->back);
  if (line.empty()) {  // unreadable, so it can't be sent
    state.reply(scan, 0, 0, "error", "bad_points");
    save();
    return;
  }
  std::string json = "{\"type\":\"section\",\"scan\":" + line;
  if (store->sendSketch(scan.c_str(), sketch)) json += std::string(",\"sketch\":") + sketch.c_str();
  json += ",\"based_on\":" + std::to_string(link->acknowledged) + "}";
  sendMessage(json);
  inFlight = scan;
  sentAt = millis();
  state.sent(scan);
  save();
  Serial.printf("LINK sent %s at %s, %u bytes\n", scan.c_str(), link->station.c_str(), (unsigned)json.size());
}

int Link::revisionOf(const String &scan) const {
  StreamString sketch;
  if (!store->sendSketch(scan, sketch)) return 0;
  JsonDocument filter, doc;
  filter["revision"] = true;
  deserializeJson(doc, sketch.c_str(), DeserializationOption::Filter(filter));
  return doc["revision"] | 0;
}

void Link::accept(const String &scan, const String &station, bool horizontal, bool back, String &displaced) {
  std::string other;
  state.accept(scan.c_str(), station.c_str(), horizontal, back, other);
  displaced = other.c_str();
  save();
  sendNext();
}

void Link::clearStation(const String &scan) {
  state.clearStation(scan.c_str());
  if (inFlight == scan.c_str()) inFlight.clear();
  save();
}

void Link::drawingSaved(const String &scan) {
  state.drawingSaved(scan.c_str());
  save();
  sendNext();
}

void Link::scansChanged() {
  std::vector<std::string> saved;
  for (int i = 0; i < store->count(); i++) saved.push_back(store->name(i).c_str());
  state.keepOnly(saved);
  save();
}

String Link::describe(const String &scan) const {
  const ScanLink *link = state.find(scan.c_str());
  if (!link || link->station.empty()) return "No station";
  String text = String("Stn ") + link->station.c_str() + (link->horizontal ? " horiz " : " vert ") +
                (link->back ? "back" : "on") + ", " + LinkModel::stateName(link->state);
  if (!link->reason.empty()) text += String(" ") + link->reason.c_str();
  return text;
}

void Link::print(Print &out) const {
  JsonDocument doc;
  state.toJson(doc);
  serializeJson(doc, out);
  out.println();
}

void Link::save() {
  JsonDocument doc;
  state.toJson(doc);
  File f = LittleFS.open(PATH, "w");
  if (f) serializeJson(doc, f);
}
