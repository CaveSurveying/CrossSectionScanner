#!/usr/bin/env python3
"""Checks tools/to_sexytopo.py on a made-up survey (Test/) and drawing (sample.sketch.json).

  python3 tests/sexytopo/test_to_sexytopo.py

sample.sketch.json is written as the scanner writes drawings (firmware/src/sketch_file.cpp), a
closed square and a dot. Test/ is a small SexyTopo survey: 1 north 10 m to 2 (a splay there),
east 10 m to 3, which branches to 4 and 5 (on to 6).
"""
import copy
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tools"))
import to_sexytopo as ts  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(f"{'ok  ' if ok else 'FAIL'} {what}")
    failures += not ok


def close(a, b):
    return abs(a - b) < 1e-4


sketch = json.loads((HERE / "sample.sketch.json").read_text())
data = json.loads((HERE / "Test" / "Test.data.json").read_text())
plan = json.loads((HERE / "Test" / "Test.plan.json").read_text())

check(ts.check_sketch(sketch) == [], "the sample drawing is fine for SexyTopo")
bad = copy.deepcopy(sketch)
bad["sketch"]["paths"][0]["colour"] = "MAUVE"
bad["sketch"]["paths"][1]["line-type"] = "LAVA"
check(len(ts.check_sketch(bad)) == 2, "an unknown colour and line type are reported")

check(close(ts.average_azimuths(350, 10), 0) and close(ts.average_azimuths(80, 100), 90), "azimuths average across 0")
check(close(ts.section_angle(data, "1"), 0), "first station faces along its one onward leg")
check(close(ts.section_angle(data, "2"), 45), "through station faces between its legs (0 and 90)")
check(close(ts.section_angle(data, "3"), 90), "branching station faces along its incoming leg")
check(close(ts.section_angle(data, "5"), 0), "station with one leg each way averages across 0 (10 and 350)")
check(close(ts.section_angle(data, "4"), 350), "end station faces along its incoming leg")

p = copy.deepcopy(plan)
angle = ts.add_section(data, p, "2", sketch)
section = p["x-sections"][0]
check(close(angle, 45) and section["station-id"] == "2" and close(section["angle"], 45), "section added at 2 facing 45")
check(close(section["location"]["x"], 3) and close(section["location"]["y"], -10), "drawn 3 m right of 2 on the plan, north up")
check(section["sketch"] == sketch["sketch"], "the drawing goes in unchanged")
check(close(ts.add_section(data, p, "2", sketch, facing_back=True, replace=True), 225) and len(p["x-sections"]) == 1,
      "facing back turns it round, and replace keeps one section")
try:
    ts.add_section(data, p, "2", sketch)
    check(False, "a second section at a station is refused")
except SystemExit:
    check(True, "a second section at a station is refused")

with tempfile.TemporaryDirectory() as tmp:
    survey = Path(tmp) / "Test"
    shutil.copytree(HERE / "Test", survey)
    run = subprocess.run([sys.executable, str(HERE.parent.parent / "tools" / "to_sexytopo.py"), str(survey), "3",
                          str(HERE / "sample.sketch.json")], capture_output=True, text=True)
    written = json.loads((survey / "Test.plan.json").read_text())
    check(run.returncode == 0 and written["x-sections"][0]["station-id"] == "3", "command line adds the section")
    check(len(list(survey.glob("Test.plan.json.*.bak"))) == 1, "and keeps a backup of the plan file")
    subprocess.run([sys.executable, str(HERE.parent.parent / "tools" / "to_sexytopo.py"), str(survey), "3",
                    str(HERE / "sample.sketch.json"), "--replace"], capture_output=True, text=True)
    check(len(list(survey.glob("Test.plan.json.*.bak"))) == 2, "a second run in the same second keeps both backups")

print(f"{failures} failed")
sys.exit(1 if failures else 0)
