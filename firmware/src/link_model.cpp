#include "link_model.h"

#include <algorithm>

namespace {
const char *const STATE_NAMES[] = {"new", "accepted", "sent", "acknowledged", "error", "conflict"};
}

const char *LinkModel::stateName(ScanState state) { return STATE_NAMES[(int)state]; }

void LinkModel::setSurvey(const std::string &name, const std::vector<std::string> &stations,
                          const std::map<std::string, std::string> &scanStations) {
  surveyName = name;
  stationList = stations;
  // SexyTopo's names win, so a rename made while apart reaches here too
  for (const auto &s : scanStations) {
    auto scan = scans.find(s.first);
    if (scan != scans.end()) scan->second.station = s.second;
  }
}

void LinkModel::stationAdded(const std::string &name) {
  if (std::find(stationList.begin(), stationList.end(), name) == stationList.end()) stationList.push_back(name);
}

void LinkModel::stationRenamed(const std::string &from, const std::string &to) {
  std::replace(stationList.begin(), stationList.end(), from, to);
  for (auto &s : scans)
    if (s.second.station == from) s.second.station = to;
}

void LinkModel::stationDeleted(const std::string &name) {
  stationList.erase(std::remove(stationList.begin(), stationList.end(), name), stationList.end());
  for (auto &s : scans)
    if (s.second.station == name) clearStation(s.first);
}

std::string LinkModel::scanAt(const std::string &station) const {
  for (const auto &s : scans)
    if (!station.empty() && s.second.station == station) return s.first;
  return "";
}

void LinkModel::accept(const std::string &scan, const std::string &station, bool horizontal, bool back,
                       std::string &displaced) {
  displaced = scanAt(station);
  if (displaced == scan) displaced.clear();
  if (!displaced.empty()) clearStation(displaced);
  ScanLink &link = scans[scan];
  link.station = station;
  link.horizontal = horizontal;
  link.back = back;
  link.state = ScanState::ACCEPTED;
  link.reason.clear();
}

void LinkModel::clearStation(const std::string &scan) {
  auto s = scans.find(scan);
  if (s == scans.end()) return;
  s->second.station.clear();
  s->second.state = ScanState::NEW;
  s->second.reason.clear();
}

void LinkModel::drawingSaved(const std::string &scan) {
  auto s = scans.find(scan);
  if (s != scans.end() && !s->second.station.empty()) s->second.state = ScanState::ACCEPTED;
}

void LinkModel::forget(const std::string &scan) { scans.erase(scan); }

void LinkModel::keepOnly(const std::vector<std::string> &saved) {
  for (auto s = scans.begin(); s != scans.end();)
    s = std::find(saved.begin(), saved.end(), s->first) == saved.end() ? scans.erase(s) : std::next(s);
}

std::string LinkModel::nextToSend() const {
  for (const auto &s : scans)
    if (!s.second.station.empty() && (s.second.state == ScanState::ACCEPTED || s.second.state == ScanState::SENT))
      return s.first;
  return "";
}

void LinkModel::sent(const std::string &scan) {
  auto s = scans.find(scan);
  if (s != scans.end()) s->second.state = ScanState::SENT;
}

void LinkModel::reply(const std::string &scan, int revision, int currentRevision, const std::string &result,
                      const std::string &reason) {
  auto s = scans.find(scan);
  if (s == scans.end() || revision != currentRevision) return;  // the drawing has moved on since
  ScanLink &link = s->second;
  if (result == "ok") {
    link.state = ScanState::ACKNOWLEDGED;
    link.acknowledged = revision;
    link.reason.clear();
  } else if (result == "conflict") {
    link.state = ScanState::CONFLICT;
  } else {
    link.state = ScanState::ERROR;
    link.reason = reason;
  }
}

const ScanLink *LinkModel::find(const std::string &scan) const {
  auto s = scans.find(scan);
  return s == scans.end() ? nullptr : &s->second;
}

void LinkModel::toJson(JsonDocument &doc) const {
  doc.clear();
  doc["survey"] = surveyName;
  JsonArray stations = doc["stations"].to<JsonArray>();
  for (const std::string &s : stationList) stations.add(s);
  JsonObject out = doc["scans"].to<JsonObject>();
  for (const auto &s : scans) {
    JsonObject scan = out[s.first].to<JsonObject>();
    if (!s.second.station.empty()) scan["station"] = s.second.station;
    scan["orientation"] = s.second.horizontal ? "horizontal" : "vertical";
    scan["facing"] = s.second.back ? "back" : "on";
    scan["state"] = stateName(s.second.state);
    scan["acknowledged"] = s.second.acknowledged;
    if (!s.second.reason.empty()) scan["reason"] = s.second.reason;
  }
}

void LinkModel::fromJson(const JsonDocument &doc) {
  surveyName = doc["survey"] | "";
  stationList.clear();
  for (JsonVariantConst s : doc["stations"].as<JsonArrayConst>()) stationList.push_back(s | "");
  scans.clear();
  for (JsonPairConst s : doc["scans"].as<JsonObjectConst>()) {
    ScanLink link;
    link.station = s.value()["station"] | "";
    link.horizontal = std::string(s.value()["orientation"] | "") == "horizontal";
    link.back = std::string(s.value()["facing"] | "") == "back";
    const std::string state = s.value()["state"] | "new";
    for (int i = 0; i < 6; i++)
      if (state == STATE_NAMES[i]) link.state = (ScanState)i;
    link.acknowledged = s.value()["acknowledged"] | 0;
    link.reason = s.value()["reason"] | "";
    scans[s.key().c_str()] = link;
  }
}
