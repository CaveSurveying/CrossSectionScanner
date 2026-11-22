#!/usr/bin/env python3
"""Add a scanner drawing to a SexyTopo survey as a vertical cross section at a station.

  to_sexytopo.py SURVEY_DIR STATION SKETCH          angle worked out as SexyTopo does
  to_sexytopo.py SURVEY_DIR STATION SKETCH --facing back    the scan looked back along the survey
  to_sexytopo.py SURVEY_DIR STATION SKETCH --angle 140      angle given
  to_sexytopo.py --check SKETCH                     only check the drawing

SKETCH is a drawing fetched from the scanner (scans/board-<name>.sketch.json). The section goes in
SURVEY_DIR/<survey>.plan.json, drawn --offset metres from the station on the plan (3 m to the right
unless given), after a backup copy of the plan file is saved beside it. A station that already has
a cross section is left alone unless --replace is given.

Follows SexyTopo upstream main 26c93a7a: SketchJsonTranslater (file format), CrossSectioner and
Space2DUtils.averageAzimuths (angle), Space3DUtils.toCartesian and Projection2D.PLAN (station
positions on the plan).
"""
import argparse
import json
import math
import shutil
import sys
from datetime import datetime
from pathlib import Path

SCANNER_FORMAT = "XScan sketch 1"
SPLAY = "-"  # destination of a splay in SexyTopo's data file
# model/sketch/LineType.java and BrushColour.java at 26c93a7a
LINE_TYPES = {"SKETCH", "WALL", "FLOOR_STEP", "PIT", "CHIMNEY", "CEILING_STEP", "OVERHANG", "SLOPE",
              "ROCK_BORDER", "ROCK_EDGE", "PRESUMED_WALL", "UNDERLYING_WALL"}
BRUSH_COLOURS = {"BLACK", "BROWN", "GREY", "RED", "ORANGE", "GREEN", "BLUE", "PURPLE"}


def check_sketch(doc):
    """Problems with a scanner drawing, as SexyTopo's reader would meet them (empty if none)."""
    problems = []
    if doc.get("format") != SCANNER_FORMAT:
        problems.append(f"format is {doc.get('format')!r}, not {SCANNER_FORMAT!r}")
    sketch = doc.get("sketch")
    if not isinstance(sketch, dict):
        return problems + ["no sketch"]
    for key in ("paths", "areas", "labels", "symbols"):
        if not isinstance(sketch.get(key), list):
            problems.append(f"sketch has no {key} list")
    for i, path in enumerate(sketch.get("paths") or []):
        if path.get("colour") not in BRUSH_COLOURS:
            problems.append(f"path {i} colour {path.get('colour')!r}")
        if "line-type" in path and path["line-type"] not in LINE_TYPES:
            problems.append(f"path {i} line-type {path['line-type']!r}")
        points = path.get("points")
        if not points:
            problems.append(f"path {i} has no points")
        elif not all(isinstance(p.get(k), (int, float)) and math.isfinite(p[k]) for p in points for k in ("x", "y")):
            problems.append(f"path {i} has a point without numbers x and y")
    return problems


def load_survey(survey_dir):
    """The survey's data and plan sketch, and the plan file's path."""
    survey_dir = Path(survey_dir)
    data_files = list(survey_dir.glob("*.data.json"))
    if len(data_files) != 1:
        raise SystemExit(f"{survey_dir}: expected one .data.json, found {len(data_files)}")
    plan_path = data_files[0].with_name(data_files[0].name.replace(".data.json", ".plan.json"))
    if not plan_path.exists():
        raise SystemExit(f"{plan_path} not found, open the survey's plan in SexyTopo once first")
    return json.loads(data_files[0].read_text()), json.loads(plan_path.read_text()), plan_path


def survey_legs(data):
    """(from station, leg) for each leg that isn't a splay, in survey order."""
    return [(st["name"], leg) for st in data["stations"] for leg in st["legs"] if leg["destination"] != SPLAY]


def station_positions(data):
    """Station positions (east, north, up) in metres from the first station."""
    positions = {data["stations"][0]["name"]: (0.0, 0.0, 0.0)}
    for origin, leg in survey_legs(data):
        if origin not in positions:
            continue
        x, y, z = positions[origin]
        r, a, i = leg["distance"], math.radians(leg["azimuth"]), math.radians(leg["inclination"])
        positions[leg["destination"]] = (x + r * math.cos(i) * math.sin(a), y + r * math.cos(i) * math.cos(a),
                                         z + r * math.sin(i))
    return positions


def average_azimuths(*azimuths):
    """Space2DUtils.averageAzimuths, so {359, 1} averages to 0, not 180."""
    split_over_zero = max(azimuths) - min(azimuths) > 180
    total = sum(a + 360 if split_over_zero and a < 180 else a for a in azimuths)
    return (total / len(azimuths)) % 360


def section_angle(data, station):
    """CrossSectioner.getAngleOfSection, the way a vertical section at the station faces."""
    legs = survey_legs(data)
    incoming = [leg for _, leg in legs if leg["destination"] == station]
    if station == data["stations"][0]["name"]:
        incoming = []
    outgoing = [leg for origin, leg in legs if origin == station]
    if incoming and len(outgoing) == 1:
        return average_azimuths(incoming[0]["azimuth"], outgoing[0]["azimuth"])
    if incoming:
        return incoming[0]["azimuth"]
    if len(outgoing) == 1:
        return outgoing[0]["azimuth"]
    return 0.0


def add_section(data, plan, station, sketch_doc, angle=None, facing_back=False, offset=(3.0, 0.0), replace=False):
    """Add the drawing to the plan as a cross section, and return its angle."""
    positions = station_positions(data)
    if station not in positions:
        raise SystemExit(f"station {station} is not in the survey")
    sections = plan.setdefault("x-sections", [])
    existing = [s for s in sections if s["station-id"] == station]
    if existing and not replace:
        raise SystemExit(f"station {station} already has a cross section, use --replace to replace it")
    if angle is None:
        angle = section_angle(data, station)
        if facing_back:
            angle = (angle + 180) % 360
    east, north, _ = positions[station]
    # Projection2D.PLAN puts north up the screen, so y is minus north
    location = {"x": east + offset[0], "y": -north + offset[1]}
    sections[:] = [s for s in sections if s["station-id"] != station]
    sections.append({"station-id": station, "location": location, "angle": angle, "sketch": sketch_doc["sketch"]})
    return angle


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("survey", nargs="?", help="SexyTopo survey folder")
    parser.add_argument("station", nargs="?")
    parser.add_argument("sketch", help="drawing fetched from the scanner")
    parser.add_argument("--check", action="store_true", help="only check the drawing")
    parser.add_argument("--angle", type=float, help="the way the section faces, degrees")
    parser.add_argument("--facing", choices=("on", "back"), default="on", help="the way the scan looked along the survey")
    parser.add_argument("--offset", type=float, nargs=2, default=(3.0, 0.0), metavar=("DX", "DY"),
                        help="where to draw it on the plan, metres from the station (y down the screen)")
    parser.add_argument("--replace", action="store_true", help="replace the station's cross section")
    args = parser.parse_args()

    sketch_doc = json.loads(Path(args.sketch).read_text())
    problems = check_sketch(sketch_doc)
    for problem in problems:
        print(f"{args.sketch}: {problem}")
    if problems:
        sys.exit(1)
    if args.check:
        print(f"{args.sketch}: fine for SexyTopo")
        return
    if not args.survey or not args.station:
        parser.error("give SURVEY_DIR STATION SKETCH, or --check SKETCH")

    data, plan, plan_path = load_survey(args.survey)
    angle = add_section(data, plan, args.station, sketch_doc, args.angle, args.facing == "back", tuple(args.offset),
                        args.replace)
    stamp = f"{datetime.now():%Y%m%d-%H%M%S}"
    backup, n = plan_path.with_name(f"{plan_path.name}.{stamp}.bak"), 1
    while backup.exists():  # never overwrite an earlier backup
        n += 1
        backup = plan_path.with_name(f"{plan_path.name}.{stamp}-{n}.bak")
    shutil.copy2(plan_path, backup)
    plan_path.write_text(json.dumps(plan, indent=2))
    print(f"{plan_path.name}: cross section at {args.station} facing {angle:.0f}°, "
          f"from {sketch_doc['scan']} revision {sketch_doc['revision']}, backup {backup.name}")


if __name__ == "__main__":
    main()
