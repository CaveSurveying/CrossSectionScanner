#include "scan_store.h"

#include <LittleFS.h>
#include <Preferences.h>

#include <algorithm>

namespace {
constexpr int FORMAT = 1;
constexpr size_t SPARE = 8192;  // file system blocks and metadata beyond the text itself

String pathOf(const String &name) { return "/" + name + ".txt"; }
}  // namespace

bool ScanStore::begin() {
  Preferences prefs;
  prefs.begin("scanner", false);
  boot = prefs.getUInt("boot", 0) + 1;
  prefs.putUInt("boot", boot);
  prefs.end();
  if (!LittleFS.begin(true)) return false;
  refresh();
  return true;
}

void ScanStore::refresh() {
  names.clear();
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String n = f.name();
    if (n.endsWith(".txt")) names.push_back(n.substring(0, n.length() - 4));
  }
  std::sort(names.begin(), names.end());  // names sort oldest first
}

String ScanStore::record(const Lidar &lidar, const Lidar::Info &info, String &name) {
  char line[64];
  snprintf(line, sizeof line, "b%04u-%03d", boot % 10000, ++scansThisBoot);
  name = line;
  String text;
  text.reserve(200 + 24 * lidar.turnStart(lidar.turnCount()));
  text += "FORMAT " + String(FORMAT) + "\nNAME " + name + "\n";
  snprintf(line, sizeof line, "BOOT %u %d %lu\n", boot, scansThisBoot, millis());
  text += line;
  text += Lidar::infoLine(info) + "\n";
  for (int t = 0; t < lidar.turnCount(); t++) {
    text += "T\n";
    for (int i = lidar.turnStart(t); i < lidar.turnStart(t + 1); i++) {
      const Lidar::Point &p = lidar.points()[i];
      snprintf(line, sizeof line, "P %.3f %.2f %d\n", p.angleDeg(), p.distanceMm(), p.quality);
      text += line;
    }
  }
  return text;
}

int ScanStore::save(const String &name, const String &text) {
  int overwritten = 0;
  while (LittleFS.totalBytes() - LittleFS.usedBytes() < text.length() + SPARE && !names.empty()) {
    if (!remove(names.front())) return -1;
    overwritten++;
  }
  File f = LittleFS.open(pathOf(name), "w");
  if (!f) return -1;
  const size_t written = f.print(text);
  f.close();
  if (written != text.length()) {
    LittleFS.remove(pathOf(name));
    return -1;
  }
  names.push_back(name);
  return overwritten;
}

bool ScanStore::load(int i, ScanView &view) {
  if (i < 0 || i >= count()) return false;
  File f = LittleFS.open(pathOf(names[i]), "r");
  if (!f) return false;
  view.clear(names[i] + "   " + (i + 1) + " of " + count());
  while (f.available()) view.addLine(f.readStringUntil('\n'));
  return true;
}

bool ScanStore::send(const String &name, Print &out) {
  File f = LittleFS.open(pathOf(name), "r");
  if (!f) return false;
  uint8_t buf[512];
  while (const size_t n = f.read(buf, sizeof buf)) out.write(buf, n);
  return true;
}

bool ScanStore::remove(const String &name) {
  if (!LittleFS.remove(pathOf(name))) return false;
  names.erase(std::remove(names.begin(), names.end(), name), names.end());
  return true;
}

void ScanStore::list(Print &out) {
  for (const String &n : names) {
    File f = LittleFS.open(pathOf(n), "r");
    out.printf("FILE %s %u\n", n.c_str(), f ? f.size() : 0);
  }
  out.printf("LIST end %d %u %u\n", count(), LittleFS.usedBytes(), LittleFS.totalBytes());
}

int ScanStore::percentFull() const { return 100 * LittleFS.usedBytes() / max<size_t>(LittleFS.totalBytes(), 1); }
