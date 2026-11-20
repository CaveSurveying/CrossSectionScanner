// Scans saved in the board's flash (LittleFS), one text file each, oldest overwritten when full.
// The text is the same as sent over USB:
//   FORMAT 1
//   NAME b0007-003                start-up 7, scan 3 of that start-up
//   BOOT <start-up> <scan> <ms>   ms since start-up, the board has no clock
//   LIDAR <model> <firmware> <hardware> <serial>
//   T                             each turn, then P <deg> <mm> <quality> lines, raw
#pragma once
#include <Arduino.h>

#include <vector>

#include "lidar.h"
#include "scan_view.h"

class ScanStore {
 public:
  // Mount (formatting a new partition), count this start-up, list the saved scans
  bool begin();
  // Text of a new scan, with its name
  String record(const Lidar &lidar, const Lidar::Info &info, String &name);
  // Save, overwriting the oldest scans until it fits. How many were overwritten, -1 if not saved
  int save(const String &name, const String &text);

  int count() const { return names.size(); }
  const String &name(int i) const { return names[i]; }  // oldest first
  // Load saved scan i into the view, labelled with its name and place in the list
  bool load(int i, ScanView &view);
  bool send(const String &name, Print &out);  // the saved text as it is
  bool remove(const String &name);
  // FILE <name> <bytes> for each scan, then LIST end <count> <used bytes> <total bytes>
  void list(Print &out);
  int percentFull() const;

 private:
  void refresh();
  std::vector<String> names;
  uint32_t boot = 0;
  int scansThisBoot = 0;
};
