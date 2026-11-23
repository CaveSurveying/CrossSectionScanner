#!/usr/bin/env python3
"""Checks board_link.jsonl_line writes a board scan as one line of the exchange format.

  python3 tests/board_link/test_jsonl.py
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))
import board_link  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(f"{'ok  ' if ok else 'FAIL'} {what}")
    failures += not ok


def point(deg, mm, quality):
    return {"quality": quality, "angle_deg": deg, "distance_mm": mm}


LIDAR = {"model": 65, "firmware": "1.02", "hardware": 18, "serial": "53AB"}
FORMAT_2 = {"format": "CrossSectionScanner raw scan 2", "time": None, "source": "board", "lidar": LIDAR,
            "revolutions": [[point(0.5, 1234.0, 47), point(2.1, 1240.25, 46)]], "samples": [5],
            "received": "2026-11-22T19:35:32+00:00", "board_name": "b0007-003",
            "start_up": 7, "scan": 3, "ms_since_start_up": 12345}
FORMAT_1 = dict(FORMAT_2, revolutions=[[point(0.5, 1234.0, 47), point(1.3, 0.0, 0), point(2.1, 1240.25, 46)]])
del FORMAT_1["samples"]
NO_LIDAR = dict(FORMAT_2, lidar=None)
TWO_TURNS = dict(FORMAT_2, revolutions=FORMAT_2["revolutions"] * 2)

expected = {"format": "XScan scan 1", "scan": "b0007-003",
            "board": {"start_up": 7, "scan": 3, "ms_since_start_up": 12345}, "lidar": LIDAR,
            "samples": 5, "returns": 2, "points": [[0.5, 1234, 47], [2.1, 1240.25, 46]]}

for record, want, what in (
        (FORMAT_2, expected, "FORMAT 2 scan"),
        (FORMAT_1, dict(expected, samples=3), "FORMAT 1 scan, no-returns left out and counted as samples"),
        (NO_LIDAR, {k: v for k, v in expected.items() if k != "lidar"}, "unknown lidar left out")):
    line = board_link.jsonl_line(record)
    check(json.loads(line) == want and "\n" not in line, what)

check(json.dumps(board_link.jsonl_line(FORMAT_2)).count("1234.0") == 0, "whole millimetres written without .0")
check("time" not in json.loads(board_link.jsonl_line(FORMAT_2)), "null time left out")
check(json.loads(board_link.jsonl_line(dict(FORMAT_2, time="2026-11-23T10:00:00+00:00")))["time"]
      == "2026-11-23T10:00:00+00:00", "known time kept")

try:
    board_link.jsonl_line(TWO_TURNS)
    check(False, "a two-turn scan refused")
except ValueError:
    check(True, "a two-turn scan refused")

check(list(json.loads(board_link.jsonl_line(FORMAT_2, "1.3")))[:3] == ["format", "scan", "station"]
      and json.loads(board_link.jsonl_line(FORMAT_2, "1.3"))["station"] == "1.3", "station written after the scan name")

for pairs, want, what in (
        (["1=b0001-001", "2.4=b0002-003"], {"1": "b0001-001", "2.4": "b0002-003"}, "stations paired with scans"),
        (["a=b=b0001-001"], {"a=b": "b0001-001"}, "an = in a station name kept"),
        (["1"], None, "a pair with no ="),
        (["1=b0001-001", "1=b0001-002"], None, "a station given twice"),
        (["1=b0001-001", "2=b0001-001"], None, "a scan given to two stations")):
    try:
        check(board_link.parse_pairs(pairs) == want, what)
    except ValueError:
        check(want is None, what + " refused")

OLD = '{"scan":"b1","station":"1"}\n{"scan":"b2","station":"2"}\n'
merged = board_link.merge_jsonl(OLD, {"2": '{"scan":"b3","station":"2"}', "3": '{"scan":"b4","station":"3"}'})
check(merged == '{"scan":"b1","station":"1"}\n{"scan":"b3","station":"2"}\n{"scan":"b4","station":"3"}\n',
      "a station's line replaced, others kept, new ones added")
check(board_link.merge_jsonl("", {"1": '{"station":"1"}'}) == '{"station":"1"}\n', "no file yet")

print(f"{failures} failed")
sys.exit(failures > 0)
