// Lines drawn round a scan, following SexyTopo's sketch model, upstream main 26c93a7a
// (model/sketch/Sketch.java, PathDetail.java, LineType.java, control/util/Space2DUtils.java,
// LineOrienter.java, LoopOrienter.java and control/sketch/SketchView.java handleLine). Coordinates
// are metres from the scanner in SexyTopo's cross section view, x to the right and y down, as on
// the screen. The scanner is taken to be at the station.
#pragma once
#include <string>
#include <vector>

// LineType.java, in its order, with what the drawing and finishing of a line need
enum class LineType { SKETCH, WALL, FLOOR_STEP, PIT, CHIMNEY, CEILING_STEP, OVERHANG, SLOPE, ROCK_BORDER, ROCK_EDGE,
                      PRESUMED_WALL, UNDERLYING_WALL };

struct LineTypeInfo {
  enum Stamp { NONE, TICKED, HACHURED, TOOTHED };
  const char *name;   // as written in the file
  const char *label;  // on the screen
  bool isWall, isHoleEdge, isOutline;
  float widthFactor;
  float dashOn, dashOff;  // in ornament sizes, 0 for solid
  Stamp stamp;
  float advance, barLength, tick, shortTick;  // the stamp in ornament sizes, the sign of a tick picks its side

  static constexpr int COUNT = 12;
  static const LineTypeInfo &of(LineType type);
  static LineType fromName(const char *name);  // SKETCH for none or an unknown name

  bool isSemantic() const;  // all but SKETCH, the types with a meaning in Therion
  bool isOriented() const { return isWall || stamp != NONE || isOutline; }
  bool hasTicksOnRight() const { return stamp != NONE && tick > 0; }  // seen on screen, y down
  bool isOrientedByLoop() const { return isHoleEdge || isOutline; }
  bool hasLoopInsideOnRight() const { return isOutline || hasTicksOnRight(); }
};

struct Coord {
  float x, y;
  bool operator==(const Coord &o) const { return x == o.x && y == o.y; }
};

class Sketch {
 public:
  static constexpr int MAX_POINTS = 6000;  // all lines together, to keep within the board's memory

  struct Path {
    std::vector<Coord> points;
    LineType type = LineType::SKETCH;
    std::string colour = "BLACK";  // SexyTopo's, kept as it came
    const Coord &start() const { return points.front(); }
    const Coord &end() const { return points.back(); }
    bool isClosed() const { return points.size() > 2 && start() == end(); }
  };

  // What SexyTopo drew that can't be edited here, shown and written back as it came
  struct Extras {
    struct Mark {
      Coord at;
      std::string text;
      bool symbol;  // a symbol shown by a short name, otherwise text
      std::string colour;
      float size;  // metres, the text's height or the symbol's width
    };
    struct Outline {
      std::vector<Coord> points;
      std::string colour;
    };
    std::string areas = "[]", labels = "[]", symbols = "[]";  // SexyTopo's JSON
    std::vector<Outline> outlines;                            // of the areas
    std::vector<Mark> marks;
  };

  void startPath(Coord start, LineType type = LineType::SKETCH, const std::string &colour = "BLACK");
  void lineTo(Coord point);
  // Finish the line as SexyTopo does with Snap to Lines and Auto-Orientation on. An outline whose
  // ends come within snapDistance (metres) is closed, a wall is turned to have the passage (the
  // station) on its interior side and a pit, chimney or outline drawn round something to face it,
  // the ends snap to the ends of lines of the same type within snapDistance, and it is simplified.
  // A tap leaves a dot, but a line of any type but SKETCH smaller than minimumSize is dropped.
  void finishPath(float snapDistance, float minimumSize = 0);
  // Undo the last step (a line or a join), false if there is nothing to undo
  bool undo();
  // Put back the last step undone, false if there is none. A new line empties redo.
  bool redo();
  bool canRedo() const { return !undone.empty(); }
  void forget();  // drop everything, history too, for a different scan
  void setPaths(const std::vector<Path> &paths);  // lines read back, with no history
  void setExtras(const Extras &extras) { kept = extras; }
  const Extras &extras() const { return kept; }
  bool changed() const { return !history.empty(); }
  void forgetHistory() {  // undo goes back only to here
    history.clear();
    undone.clear();
  }
  void revert() {  // back to how it was when the history began
    while (undo()) {}
    undone.clear();
  }

  const std::vector<Path> &paths() const { return finished; }
  const Path *activePath() const { return active ? &current : nullptr; }

 private:
  // One undoable step. The lines it removed, with where they were, and how many it added at the end
  struct Step {
    std::vector<std::pair<int, Path>> removed;
    int added = 0;
  };
  int pointCount() const;
  void snapActivePath(float delta, int &startJoin, int &endJoin);
  int findLineToSnapTo(const Coord &point, float delta, bool atStart, const Coord *excluded) const;
  void orientActivePath(float closeDistance);
  void apply(Step step, const Path &added);

  std::vector<Path> finished;
  Extras kept;
  std::vector<Step> history;
  std::vector<std::pair<Step, std::vector<Path>>> undone;  // each with the lines it had added
  Path current;
  bool active = false;
};
