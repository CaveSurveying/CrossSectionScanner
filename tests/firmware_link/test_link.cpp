// Checks the firmware's link code on the computer: frames against the shared examples in tests/link,
// the link state, and the exchange line against tools/board_link.py (make_expected.py).
//   ./run.sh
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "exchange.h"
#include "link_frame.h"
#include "link_model.h"

namespace {
int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

// Same JSON, numbers compared as numbers and object keys in any order
bool same(JsonVariantConst a, JsonVariantConst b) {
  if (a.is<JsonObjectConst>()) {
    if (!b.is<JsonObjectConst>() || a.size() != b.size()) return false;
    for (JsonPairConst p : a.as<JsonObjectConst>())
      if (!same(p.value(), b[p.key()])) return false;
    return true;
  }
  if (a.is<JsonArrayConst>()) {
    if (!b.is<JsonArrayConst>() || a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
      if (!same(a[i], b[i])) return false;
    return true;
  }
  if (a.is<double>() && b.is<double>()) return std::fabs(a.as<double>() - b.as<double>()) < 1e-9;
  return a == b;
}

// Each shared example frame file, read whole and a byte at a time, gives what cases.json says
void testFrames() {
  JsonDocument cases;
  deserializeJson(cases, readFile("../link/cases.json"), DeserializationOption::NestingLimit(32));
  for (JsonObjectConst c : cases.as<JsonArrayConst>()) {
    const std::string file = c["file"].as<const char *>();
    const std::string bytes = readFile("../link/" + file);
    for (int byByte = 0; byByte < 2; byByte++) {
      link_frame::Reader reader;
      std::vector<link_frame::Result> results;
      if (byByte) {
        for (char ch : bytes)
          for (auto &r : reader.feed((const uint8_t *)&ch, 1)) results.push_back(std::move(r));
      } else {
        results = reader.feed((const uint8_t *)bytes.data(), bytes.size());
      }
      JsonArrayConst expected = c["read"];
      bool ok = results.size() == expected.size();
      for (size_t i = 0; ok && i < results.size(); i++) {
        const std::string kind = expected[i][0].as<const char *>();
        if (kind == "dropped") ok = results[i].dropped && expected[i][1] == results[i].dropped;
        else ok = !results[i].dropped && same(expected[i][1], results[i].message.as<JsonVariantConst>());
      }
      check(ok, file + (byByte ? " a byte at a time" : " whole"));
    }
  }
}

void testFrameWritten() {
  const std::string json = "{\"type\":\"hello\",\"protocol\":1,\"app\":\"SexyTopo 1.15.0\"}";
  check(link_frame::frame(json) == readFile("../link/hello_sexytopo.frame"), "frame written as xscan_link.py");
  const std::string accented = readFile("../link/accented.frame");
  link_frame::Reader reader;
  auto results = reader.feed((const uint8_t *)accented.data(), accented.size());
  std::string again;
  serializeJson(results[0].message, again);
  check(link_frame::frame(again) == accented, "accented frame written back the same");
}

void testModel() {
  LinkModel m;
  m.setSurvey("Trip1", {"1", "2", "3"}, {});
  std::string displaced;
  m.accept("b1", "2", false, false, displaced);
  check(displaced.empty() && m.find("b1")->state == ScanState::ACCEPTED, "accept gives a station to send");
  check(m.nextToSend() == "b1", "accepted scan is next to send");
  m.accept("b2", "2", true, true, displaced);
  check(displaced == "b1" && m.find("b1")->station.empty() && m.find("b1")->state == ScanState::NEW,
        "a second scan at a station takes it from the first");
  check(m.scanAt("2") == "b2", "station 2 has b2");

  m.sent("b2");
  m.reply("b2", 1, 2, "ok", "");
  check(m.find("b2")->state == ScanState::SENT, "reply for an older revision changes nothing");
  m.reply("b2", 2, 2, "ok", "");
  check(m.find("b2")->state == ScanState::ACKNOWLEDGED && m.find("b2")->acknowledged == 2, "ok acknowledges");
  check(m.nextToSend().empty(), "nothing to send once acknowledged");
  m.drawingSaved("b2");
  check(m.nextToSend() == "b2", "a new drawing is sent again");
  m.reply("b2", 2, 2, "conflict", "");
  check(m.find("b2")->state == ScanState::CONFLICT, "conflict kept");
  m.reply("b2", 2, 2, "error", "no_station");
  check(m.find("b2")->state == ScanState::ERROR && m.find("b2")->reason == "no_station", "error with reason");

  m.stationRenamed("2", "2a");
  check(m.find("b2")->station == "2a" && m.stations()[1] == "2a", "rename follows to the scan and list");
  m.stationAdded("4");
  m.stationAdded("4");
  check(m.stations().size() == 4 && m.stations().back() == "4", "added once, newest last");
  m.stationDeleted("2a");
  check(m.find("b2")->station.empty() && m.stations().size() == 3, "delete clears the scan's station");

  m.accept("b1", "3", false, false, displaced);
  m.setSurvey("Trip1", {"1", "3x"}, {{"b1", "3x"}, {"b9", "1"}});
  check(m.find("b1")->station == "3x" && !m.find("b9"), "survey brings station names up to date");

  JsonDocument doc;
  m.toJson(doc);
  LinkModel back;
  back.fromJson(doc);
  JsonDocument again;
  back.toJson(again);
  check(same(doc.as<JsonVariantConst>(), again.as<JsonVariantConst>()), "saved and read back the same");
  m.forget("b1");
  check(!m.find("b1"), "forgotten");
  m.keepOnly({"b3"});
  check(!m.find("b2"), "scans no longer saved forgotten");
}

void testExchange() {
  const std::string line = exchange::line(readFile("scan.txt"), "5a", true, true);
  JsonDocument got, expected;
  check(!deserializeJson(got, line), "exchange line is JSON");
  deserializeJson(expected, readFile("expected.json"));
  check(same(expected.as<JsonVariantConst>(), got.as<JsonVariantConst>()), "exchange line as board_link.py");
  check(exchange::line("FORMAT 2\n", "", false, false).empty(), "no name or turn gives nothing");
}
}  // namespace

int main() {
  testFrames();
  testFrameWritten();
  testModel();
  testExchange();
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
