// The board's side of the link with SexyTopo (docs/LINK.md): hello, the survey's stations, each
// accepted scan sent as a section, one at a time, waiting for SexyTopo's reply, and drawings edited
// in SexyTopo taken in.
#pragma once
#include <Arduino.h>

#include "link_frame.h"
#include "link_model.h"
#include "scan_store.h"

class Link {
 public:
  static constexpr const char *FIRMWARE = "0.7.1";

  // beingDrawn says whether a scan's drawing screen is open, so SexyTopo's drawing waits
  void begin(ScanStore &store, bool (*beingDrawn)(const String &scan));
  // Call from loop(), true if what the screen shows of the link has changed
  bool loop();

  const LinkModel &model() const { return state; }
  bool connected() const;
  bool ready() const { return surveyed; }  // hello and survey received this connection
  // Give a scan its station, to be sent. Another scan at that station loses it, named in displaced.
  void accept(const String &scan, const String &station, bool horizontal, bool back, String &displaced);
  void clearStation(const String &scan);
  void drawingSaved(const String &scan);
  // The drawing screen closed, saved or not, so a drawing from SexyTopo held meanwhile is answered
  void drawingClosed(const String &scan, bool saved);
  // A scan whose drawing SexyTopo has replaced since last asked, to show it again
  bool takeUpdated(String &scan);
  void scansChanged();  // after scans are saved, overwritten or deleted
  // The scan's station and how far it has got, for the screen
  String describe(const String &scan) const;
  void print(Print &out) const;  // the saved state, for the computer

 private:
  void handle(JsonDocument &message);
  void onDrawing(JsonDocument &message);
  void replyDrawing(const std::string &scan, int revision, const char *result, const char *reason = nullptr);
  bool isSaved(const std::string &scan) const;
  void sendMessage(const std::string &json);
  void sendNext();
  int revisionOf(const String &scan) const;
  void save();

  ScanStore *store = nullptr;
  bool (*beingDrawn)(const String &scan) = nullptr;
  std::map<std::string, std::string> held;  // drawings from SexyTopo waiting while drawing, by scan
  std::string updated;
  LinkModel state;
  link_frame::Reader reader;
  bool surveyed = false;
  std::string inFlight;  // scan sent and waiting for its reply
  unsigned long sentAt = 0, lastBytesAt = 0;
};
