#!/usr/bin/env python3
"""Checks board_link.receive_scan reads the board's scan text and its counts.

  python3 tests/board_link/test_receive_scan.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))
import board_link  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(f"{'ok  ' if ok else 'FAIL'} {what}")
    failures += not ok


class FakeBoard:
    """Gives the lines of a scan as the board sends them after SCANDATA start."""

    def __init__(self, text):
        self.lines = text.strip().split("\n") + ["SCANDATA end"]

    def readline(self):
        return self.lines.pop(0) if self.lines else ""


HEAD = "FORMAT 2\nNAME b0007-003\nBOOT 7 3 12345\nLIDAR none\n"
GOOD = HEAD + "T 5 3\nP 0.500 1234.00 47\nP 2.100 1240.25 46\nP 2.900 980.50 12\n"

record, size = board_link.receive_scan(FakeBoard(GOOD))
check(record["samples"] == [5] and len(record["revolutions"][0]) == 3, "samples kept, only the returns as points")
check(size == len(GOOD), "bytes read counted")

for text, what in ((GOOD.replace("T 5 3", "T 5 4"), "a missing P line"),
                   (GOOD.replace("T 5 3", "T 2 3"), "more returns than samples"),
                   (GOOD.replace("T 5 3", "T 5"), "a T line with one count")):
    try:
        board_link.receive_scan(FakeBoard(text))
        check(False, f"{what} is reported")
    except RuntimeError:
        check(True, f"{what} is reported")

old = board_link.receive_scan(FakeBoard(GOOD.replace("FORMAT 2", "FORMAT 1").replace("T 5 3", "T")
                                        + "P 3.700 0.00 0\n"))[0]
check("samples" not in old and len(old["revolutions"][0]) == 4, "FORMAT 1 scans, with their zeros, still read")

print(f"{failures} failed")
sys.exit(1 if failures else 0)
