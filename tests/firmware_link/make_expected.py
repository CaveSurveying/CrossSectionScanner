#!/usr/bin/env python3
"""Makes scan.txt, a board scan in the board's own text (firmware/src/scan_store.h, FORMAT 2), from
a scan fetched earlier, and expected.json, the exchange line tools/board_link.py writes for it with
a station, orientation and facing, for test_link.cpp to compare with firmware/src/exchange.cpp."""
import json
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import board_link  # noqa: E402

SOURCE = ROOT / "scans" / "board-b0002-003.json"


class TextBoard:
    """Stands in for the board, giving the lines of a scan's text then SCANDATA end."""

    def __init__(self, text):
        self.lines = text.splitlines() + ["SCANDATA end"]

    def readline(self):
        return self.lines.pop(0)


def board_text(record):
    points = record["revolutions"][0]
    returned = [p for p in points if p["distance_mm"]]
    lidar = record["lidar"]
    rows = ["FORMAT 2", f"NAME {record['board_name']}",
            f"BOOT {record['start_up']} {record['scan']} {record['ms_since_start_up']}",
            f"LIDAR {lidar['model']} {lidar['firmware']} {lidar['hardware']} {lidar['serial']}",
            f"T {len(points)} {len(returned)}"]
    rows += [f"P {p['angle_deg']:.3f} {p['distance_mm']:.2f} {p['quality']}" for p in returned]
    return "".join(f"{row}\n" for row in rows)


def main():
    text = board_text(json.loads(SOURCE.read_text()))
    (HERE / "scan.txt").write_text(text)
    record, _ = board_link.receive_scan(TextBoard(text))
    line = json.loads(board_link.jsonl_line(record, station="5a"))
    line.update(orientation="horizontal", facing="back")
    (HERE / "expected.json").write_text(json.dumps(line) + "\n")
    print(f"scan.txt and expected.json from {SOURCE.name}, {line['returns']} points")


if __name__ == "__main__":
    main()
