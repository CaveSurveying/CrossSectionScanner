#!/usr/bin/env python3
"""Writes the example frames in tests/link and cases.json, what each must give when read.

  python3 tests/link/make_frames.py

Run again only if docs/LINK.md changes. The frames are shared, so SexyTopo's and the firmware's
tests read these same files and must get the same results as cases.json.
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "tools"))
from xscan_link import frame  # noqa: E402

HELLO_SEXYTOPO = {"type": "hello", "protocol": 1, "app": "SexyTopo 1.15.0"}
HELLO_XSCAN = {"type": "hello", "protocol": 1, "firmware": "0.4", "board": "A1B2C3"}
SURVEY = {"type": "survey", "name": "Trip1", "stations": ["1", "2", "3", "5a"],
          "scans": [{"scan": "b0004-001", "station": "5a"}]}
STATION_ADDED = {"type": "station_added", "name": "6"}
STATION_RENAMED = {"type": "station_renamed", "from": "5", "to": "5a"}
STATION_DELETED = {"type": "station_deleted", "name": "6"}
SCAN = {"format": "XScan scan 1", "scan": "b0004-001", "station": "5", "orientation": "vertical",
        "facing": "on", "board": {"start_up": 4, "scan": 1, "ms_since_start_up": 54326},
        "lidar": {"model": 65, "firmware": "1.02", "hardware": 18, "serial": "53ABFC8D"},
        "samples": 6, "returns": 4,
        "points": [[0.5, 1500, 47], [90.2, 2000.25, 46], [180, 1250, 40], [270.4, 1999.75, 45]]}
SKETCH = {"format": "XScan sketch 1", "scan": "b0004-001", "revision": 2, "sexytopo": "26c93a7a",
          "sketch": {"paths": [{"colour": "BLACK", "line-type": "WALL",
                                "points": [{"x": -2, "y": 0}, {"x": 0, "y": -1.5}, {"x": 2, "y": 0},
                                           {"x": 0, "y": 1.25}, {"x": -2, "y": 0}]}],
                     "areas": [], "labels": [], "symbols": []}}
SECTION = {"type": "section", "scan": SCAN, "sketch": SKETCH, "based_on": 1}
SECTION_NO_SKETCH = {"type": "section", "scan": dict(SCAN, scan="b0004-002", facing="back"),
                     "based_on": 0}
REPLY_OK = {"type": "reply", "scan": "b0004-001", "revision": 2, "result": "ok"}
REPLY_ERROR = {"type": "reply", "scan": "b0004-001", "revision": 2, "result": "error",
               "reason": "no_station"}
REPLY_CONFLICT = {"type": "reply", "scan": "b0004-001", "revision": 2, "result": "conflict"}
REPLY_UNREAD = {"type": "reply", "result": "error", "reason": "bad_checksum"}
UNKNOWN_TYPE = {"type": "later_feature", "anything": [1, 2]}
ACCENTED = {"type": "station_added", "name": "Salle Cañón"}

GOOD = {
    "hello_sexytopo": HELLO_SEXYTOPO, "hello_xscan": HELLO_XSCAN, "survey": SURVEY,
    "station_added": STATION_ADDED, "station_renamed": STATION_RENAMED,
    "station_deleted": STATION_DELETED, "section": SECTION, "section_no_sketch": SECTION_NO_SKETCH,
    "reply_ok": REPLY_OK, "reply_error": REPLY_ERROR, "reply_conflict": REPLY_CONFLICT,
    "reply_unread": REPLY_UNREAD, "unknown_type": UNKNOWN_TYPE, "accented": ACCENTED,
}


def corrupt_crc(data):
    header, body = data.split(b"\n", 1)
    crc = header[-8:]
    flipped = b"%08x" % (int(crc, 16) ^ 1)
    return header[:-8] + flipped + b"\n" + body


def main():
    cases = []
    for name, message in GOOD.items():
        (HERE / f"{name}.frame").write_bytes(frame(message))
        cases.append({"file": f"{name}.frame", "read": [["message", message]]})

    broken = {
        "two_frames": (frame(STATION_ADDED) + frame(STATION_DELETED),
                       [["message", STATION_ADDED], ["message", STATION_DELETED]]),
        "bad_checksum": (corrupt_crc(frame(STATION_ADDED)), [["dropped", "bad_checksum"]]),
        "bad_checksum_then_good": (corrupt_crc(frame(STATION_ADDED)) + frame(STATION_DELETED),
                                   [["dropped", "bad_checksum"], ["message", STATION_DELETED]]),
        "junk_then_good": (b"noise\n" + frame(STATION_ADDED),
                           [["dropped", "bad_header"], ["message", STATION_ADDED]]),
        "not_json": (b"XS1 3 %08x\nabc" % __import__("zlib").crc32(b"abc"),
                     [["dropped", "unknown_format"]]),
        "no_type": (frame({"name": "6"}), [["dropped", "unknown_format"]]),
        "too_long": (b"XS1 262145 00000000\n" + frame(STATION_ADDED),
                     [["dropped", "bad_header"], ["message", STATION_ADDED]]),
        "cut_short": (frame(SURVEY)[:-5], []),
    }
    for name, (data, read) in broken.items():
        (HERE / f"{name}.frame").write_bytes(data)
        cases.append({"file": f"{name}.frame", "read": read})

    (HERE / "cases.json").write_text(json.dumps(cases, indent=1, ensure_ascii=False) + "\n")
    print(f"{len(cases)} frame files and cases.json written")


if __name__ == "__main__":
    main()
