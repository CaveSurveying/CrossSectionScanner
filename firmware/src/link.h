// The board's side of the link with SexyTopo (docs/LINK.md): hello, the survey's stations, and
// each accepted scan sent as a section, one at a time, waiting for SexyTopo's reply.
#pragma once
#include <Arduino.h>

#include "link_frame.h"
#include "link_model.h"
#include "scan_store.h"

class Link {
 public:
  static constexpr const char *FIRMWARE = "0.6";

  void begin(ScanStore &store);
  // Call from loop(), true if what the screen shows of the link has changed
  bool loop();

  const LinkModel &model() const { return state; }
  bool connected() const;
  bool ready() const { return surveyed; }  // hello and survey received this connection
  // Give a scan its station, to be sent. Another scan at that station loses it, named in displaced.
  void accept(const String &scan, const String &station, bool horizontal, bool back, String &displaced);
  void clearStation(const String &scan);
  void drawingSaved(const String &scan);
  void scansChanged();  // after scans are saved, overwritten or deleted
  // The scan's station and how far it has got, for the screen
  String describe(const String &scan) const;
  void print(Print &out) const;  // the saved state, for the computer

 private:
  void handle(JsonDocument &message);
  void sendMessage(const std::string &json);
  void sendNext();
  int revisionOf(const String &scan) const;
  void save();

  ScanStore *store = nullptr;
  LinkModel state;
  link_frame::Reader reader;
  bool surveyed = false;
  std::string inFlight;  // scan sent and waiting for its reply
  unsigned long sentAt = 0, lastBytesAt = 0;
};
