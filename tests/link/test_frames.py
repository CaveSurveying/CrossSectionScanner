#!/usr/bin/env python3
"""Checks tools/xscan_link.py reads the shared frames in tests/link as cases.json says, whole and
split into packets.

  python3 tests/link/test_frames.py
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tools"))
from xscan_link import FrameReader, frame  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(f"{'ok  ' if ok else 'FAIL'} {what}")
    failures += not ok


def read_in_packets(data, size):
    reader, found = FrameReader(), []
    for i in range(0, len(data), size):
        found += reader.feed(data[i:i + size])
    return [list(item) for item in found], reader


cases = json.loads((HERE / "cases.json").read_text())
for case in cases:
    data = (HERE / case["file"]).read_bytes()
    for size, how in ((len(data) or 1, "whole"), (1, "a byte at a time"), (20, "20-byte packets"),
                      (514, "514-byte packets (MTU 517)")):
        found, reader = read_in_packets(data, size)
        check(found == case["read"], f"{case['file']}, {how}")
    if case["file"] == "cut_short.frame":
        check(reader.waiting() > 0, "cut_short.frame is held waiting for the rest")

message = json.loads((HERE / "cases.json").read_text())[0]["read"][0][1]
check(frame(message) == (HERE / cases[0]["file"]).read_bytes(), "frame() writes the shared bytes")

reader = FrameReader()
check(reader.feed(b"x" * 40 + b"XS") == [("dropped", "bad_header")] and reader.waiting() == 2,
      "a long run with no newline is dropped, keeping a possible frame start at its end")
check(reader.feed(frame(cases[0]["read"][0][1])[2:]) == [("message", cases[0]["read"][0][1])],
      "and joined to what follows")

print(f"{failures} failed")
sys.exit(failures > 0)
