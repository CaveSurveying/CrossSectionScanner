// What the board knows of the link with SexyTopo (PLAN.md, Link with SexyTopo): the survey's
// stations, newest last, and each scan's station, how it was taken and how far it has got.
// Plain C++ so it can be checked on the computer (tests/firmware_link). Kept in flash as one small
// JSON file, so stations can still be picked when the link is down.
#pragma once
#include <ArduinoJson.h>

#include <map>
#include <string>
#include <vector>

enum class ScanState { NEW, ACCEPTED, SENT, ACKNOWLEDGED, ERROR, CONFLICT };

// What to do with a drawing SexyTopo sends (docs/LINK.md, drawing)
enum class DrawingAnswer {
  TAKE,     // the drawing here is the one SexyTopo's was based on
  ALREADY,  // taken before, its reply lost
  HOLD,     // being drawn here, so wait for Save or Cancel
  CLASH,    // changed here since, so send ours and SexyTopo asks
  NO_SCAN   // the scan isn't saved here
};

struct ScanLink {
  std::string station;  // empty for none
  bool horizontal = false;
  bool back = false;  // the user faced back along the survey
  ScanState state = ScanState::NEW;
  int acknowledged = 0;  // the revision SexyTopo last said ok to, sent as based_on
  std::string reason;    // of an error
};

class LinkModel {
 public:
  static const char *stateName(ScanState state);
  // current is the revision of the drawing saved here, 0 for none
  static DrawingAnswer answerDrawing(bool saved, bool beingDrawn, int current, int revision, int basedOn);
  // Drawing revisions made here are odd and SexyTopo's even, so the two never share a number
  static int nextRevision(int current) { return (current + 1) | 1; }

  // A survey from SexyTopo, its stations oldest first and the station of each scan it holds.
  // Within the same survey, an acknowledged scan SexyTopo no longer holds loses its station.
  void setSurvey(const std::string &name, const std::vector<std::string> &stations,
                 const std::map<std::string, std::string> &scanStations);
  void stationAdded(const std::string &name);
  void stationRenamed(const std::string &from, const std::string &to);
  void stationDeleted(const std::string &name);  // its scan loses its station and is kept
  // SexyTopo has the scan at this station now, or at none if empty (its section undone)
  void scanStation(const std::string &scan, const std::string &station);

  // The scan at a station, empty if none
  std::string scanAt(const std::string &station) const;
  // Give a scan its station, to be sent. Another scan at that station loses it, named in displaced.
  void accept(const std::string &scan, const std::string &station, bool horizontal, bool back, std::string &displaced);
  void clearStation(const std::string &scan);
  // A new revision of the scan's drawing, to be sent again if it has a station
  void drawingSaved(const std::string &scan);
  // SexyTopo's drawing taken, so that revision is agreed
  void drawingAgreed(const std::string &scan, int revision);
  void forget(const std::string &scan);  // the scan was deleted
  void keepOnly(const std::vector<std::string> &saved);  // forget scans no longer saved

  // The next scan to send (oldest name first), with a station and accepted or sent without reply, or empty
  std::string nextToSend() const;
  void sent(const std::string &scan);
  // SexyTopo's reply for a scan at a revision. A reply for an older revision changes nothing.
  void reply(const std::string &scan, int revision, int currentRevision, const std::string &result,
             const std::string &reason);

  const ScanLink *find(const std::string &scan) const;
  const std::string &survey() const { return surveyName; }
  const std::vector<std::string> &stations() const { return stationList; }

  void toJson(JsonDocument &doc) const;
  void fromJson(const JsonDocument &doc);

 private:
  std::string surveyName;
  std::vector<std::string> stationList;
  std::map<std::string, ScanLink> scans;  // by scan name, which sorts oldest first
};
