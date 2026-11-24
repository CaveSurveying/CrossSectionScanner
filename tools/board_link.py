#!/usr/bin/env python3
"""Debug link to the scanner over its USB port (firmware/ must be on the board).

  board_link.py                until Ctrl-C, show what the board reports and save each scan it takes
  board_link.py --scan         ask the board for one scan, save it, then keep listening
  board_link.py --status       show whether the board has a lidar, then stop
  board_link.py --list         list the scans saved on the board
  board_link.py --fetch        save the board's scans in scans/ (ones already there are skipped)
  board_link.py --fetch --clear   and then delete them from the board, once each is checked here
  board_link.py --jsonl        only rewrite scans/xscan.jsonl from the board scans already in scans/
  board_link.py --push SURVEY_DIR STATION=SCAN ...   put scans in a survey on the phone or emulator

Board scans are saved as scans/board-<name>.json, e.g. board-b0007-003 is scan 3 after the
board's 7th start-up, and their drawings as scans/board-<name>.sketch.json, as the board wrote them. The board has no clock, so their time is null and received is when they
reached the computer. After --fetch, scans/xscan.jsonl is rewritten with every board scan in scans/,
one per line in the exchange format (PLAN.md, Link with SexyTopo).

--push gives each scan its station by hand until the board records it (stage 4). SURVEY_DIR is the
survey's folder on the device, e.g. /sdcard/Documents/Cave/Trip1, holding Trip1.data.json. The scans
go in Trip1.xscan.jsonl there over adb, one line per station, replacing any line the station had.
"""
import argparse
import json
import math
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

import serial

from scan_once import SCANS_DIR, make_record, samples_of, save

BOARD_PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
BOARD_BAUD = 460800


class Board:
    def __init__(self, port_name=BOARD_PORT):
        self.port = serial.Serial()
        self.port.port, self.port.baudrate, self.port.timeout = port_name, BOARD_BAUD, 0.5
        self.port.dtr = self.port.rts = False
        self.port.open()
        # Opening the port still resets the ESP32 here, so let it boot and drop its boot messages
        time.sleep(1.5)
        self.port.reset_input_buffer()
        self.send("?")
        print("Waiting for the board (finish touch calibration if it is showing)")
        self.wait_for("READY")

    def close(self):
        self.port.close()

    def readline(self):
        return self.port.readline().decode(errors="replace").strip()

    def send(self, line):
        self.port.write(f"{line}\n".encode())

    def wait_for(self, word):
        """Wait for a line starting with word and return it, showing anything else."""
        while True:
            line = self.readline()
            if line.startswith(word):
                return line
            if line:
                print(f"board: {line}")


def parse_lidar_info(line):
    """Lidar details from a LIDAR line, None for LIDAR none or LIDAR failed."""
    fields = line.split()
    if len(fields) < 5:
        return None
    model, firmware, hardware, serial_number = fields[1:5]
    return {"model": int(model), "firmware": firmware, "hardware": int(hardware), "serial": serial_number}


def receive_scan(board):
    """Read a scan (after SCANDATA start) and return it as a record, with the bytes of text read.
    Raises RuntimeError, after reading to the end, if a turn's returns don't match its T line."""
    revolutions, counts, info, extra, size = [], [], None, {}, 0
    while True:
        line = board.readline()
        if line == "SCANDATA end":
            break
        if not line:
            raise RuntimeError("scan stopped part way")
        size += len(line) + 1
        word, *fields = line.split()
        if word == "T":
            revolutions.append([])
            counts.append([int(n) for n in fields[:2]])  # samples and returns, none before FORMAT 2
        elif word == "P" and revolutions:
            deg, mm, quality = fields[:3]
            revolutions[-1].append({"quality": int(quality), "angle_deg": float(deg), "distance_mm": float(mm)})
        elif word == "NAME":
            extra["board_name"] = fields[0]
        elif word == "BOOT":
            extra.update(start_up=int(fields[0]), scan=int(fields[1]), ms_since_start_up=int(fields[2]))
        elif word == "LIDAR":
            info = parse_lidar_info(line)
    if "board_name" not in extra:
        raise RuntimeError("scan has no name")
    for i, (rev, count) in enumerate(zip(revolutions, counts), 1):
        if count and (len(count) != 2 or count[1] != len(rev) or count[0] < count[1]):
            raise RuntimeError(f"{extra['board_name']} turn {i}: {len(rev)} returns read, T line says {count}")
    record = make_record(info, revolutions, "board")
    if all(counts):
        record["samples"] = [count[0] for count in counts]
    record.update(time=None, received=datetime.now().astimezone().isoformat(timespec="seconds"), **extra)
    return record, size


def file_name(board_name):
    return f"board-{board_name}"


def save_board_scan(record):
    path = save(record, file_name(record["board_name"]))
    points = sum(len(r) for r in record["revolutions"])
    print(f"{path.name}: {len(record['revolutions'])} turns, {points} points")


JSONL_FORMAT = "XScan scan 1"
JSONL_PATH = SCANS_DIR / "xscan.jsonl"


def whole_mm(value):
    """A distance to the nearest whole mm, halves up as the board rounds, finer is pointless."""
    return math.floor(value + 0.5)


def jsonl_line(record, station=None):
    """A board scan record as one line of the exchange format. Anything not known is left out."""
    if len(record["revolutions"]) != 1:
        raise ValueError(f"{record['board_name']}: {len(record['revolutions'])} turns, one expected")
    points = [[p["angle_deg"], whole_mm(p["distance_mm"]), p["quality"]]
              for p in record["revolutions"][0] if p["distance_mm"]]
    line = {"format": JSONL_FORMAT, "scan": record["board_name"]}
    if station is not None:
        line["station"] = station
    if record.get("time"):
        line["time"] = record["time"]
    line["board"] = {key: record[key] for key in ("start_up", "scan", "ms_since_start_up") if key in record}
    if record.get("lidar"):
        line["lidar"] = record["lidar"]
    line["samples"] = samples_of(record)[0]
    line.update(returns=len(points), points=points)
    return json.dumps(line, separators=(",", ":"))


def write_jsonl():
    """Rewrite scans/xscan.jsonl from the board scans in scans/, in board order."""
    records = []
    for path in SCANS_DIR.glob(f"{file_name('')}*.json"):
        if not path.name.endswith(".sketch.json"):
            records.append(json.loads(path.read_text()))
    records.sort(key=lambda r: (r.get("start_up", 0), r.get("scan", 0)))
    lines = []
    for record in records:
        try:
            lines.append(jsonl_line(record))
        except ValueError as e:
            print(f"{e}, left out of {JSONL_PATH.name}")
    JSONL_PATH.write_text("".join(f"{line}\n" for line in lines))
    print(f"{JSONL_PATH.name}: {len(lines)} scans")


ADB = shutil.which("adb") or str(Path.home() / "Android/Sdk/platform-tools/adb")


def adb(*args, check=True):
    return subprocess.run([ADB, *args], capture_output=True, text=True, check=check)


def parse_pairs(pairs):
    """{station: scan} from STATION=SCAN arguments, one scan per station and station per scan."""
    chosen = {}
    for pair in pairs:
        station, _, scan = pair.rpartition("=")
        if not station or not scan:
            raise ValueError(f"{pair}: expected STATION=SCAN")
        if station in chosen:
            raise ValueError(f"station {station} given twice")
        if scan in chosen.values():
            raise ValueError(f"scan {scan} given to two stations")
        chosen[station] = scan
    return chosen


def merge_jsonl(existing, new_lines):
    """The survey's JSONL text with new_lines ({station: line}) replacing those stations' lines."""
    kept = [line for line in existing.splitlines()
            if line.strip() and json.loads(line).get("station") not in new_lines]
    return "".join(f"{line}\n" for line in kept + list(new_lines.values()))


def push(survey_dir, pairs):
    survey_dir = survey_dir.rstrip("/")
    survey = survey_dir.rsplit("/", 1)[-1]
    data = adb("shell", f"cat '{survey_dir}/{survey}.data.json'", check=False)
    if data.returncode:
        raise SystemExit(f"{survey_dir}/{survey}.data.json not found on the device: {data.stderr.strip()}")
    stations = {st["name"] for st in json.loads(data.stdout)["stations"]}
    try:
        chosen = parse_pairs(pairs)
    except ValueError as e:
        raise SystemExit(e)
    missing = [st for st in chosen if st not in stations]
    if missing:
        raise SystemExit(f"not stations in {survey}: {', '.join(missing)}")
    new_lines = {}
    for station, scan in chosen.items():
        path = SCANS_DIR / f"{file_name(scan)}.json"
        if not path.exists():
            raise SystemExit(f"{path.name} not found, fetch it first")
        new_lines[station] = jsonl_line(json.loads(path.read_text()), station)
    target = f"{survey_dir}/{survey}.xscan.jsonl"
    existing = adb("shell", f"cat '{target}' 2>/dev/null", check=False).stdout
    text = merge_jsonl(existing, new_lines)
    with tempfile.TemporaryDirectory() as tmp:
        local = Path(tmp) / "xscan.jsonl"
        local.write_text(text)
        adb("push", str(local), target)
    print(f"{target}: {len(text.splitlines())} stations with scans, "
          + ", ".join(f"{st}={scan}" for st, scan in chosen.items()) + " pushed")


def receive_sketch(board):
    """Read a drawing (after SKETCHDATA start) and return its text, as the board saved it."""
    lines = []
    while (line := board.readline()) != "SKETCHDATA end":
        if not line:
            raise RuntimeError("drawing stopped part way")
        lines.append(line)
    return "".join(f"{line}\n" for line in lines)


def sketch_path(board_name):
    return SCANS_DIR / f"{file_name(board_name)}.sketch.json"


def save_board_sketch(text):
    """Save a drawing unless the same one is already here, and return its scan name."""
    sketch = json.loads(text)
    if not sketch.get("format", "").startswith("XScan sketch "):
        raise RuntimeError(f"not a drawing: {sketch.get('format')}")
    path = sketch_path(sketch["scan"])
    if path.exists() and path.read_text() == text:
        return sketch["scan"]
    state = "updated" if path.exists() else "new"
    path.write_text(text)
    points = sum(len(p["points"]) for p in sketch["sketch"]["paths"])
    print(f"{path.name}: {state}, revision {sketch['revision']}, {len(sketch['sketch']['paths'])} lines, {points} points")
    return sketch["scan"]


def listen(board):
    print("Listening, Ctrl-C to stop")
    while True:
        line = board.readline()
        if line == "SCANDATA start":
            try:
                save_board_scan(receive_scan(board)[0])
            except Exception as e:
                print(f"scan not saved: {e}")
        elif line == "SKETCHDATA start":
            try:
                save_board_sketch(receive_sketch(board))
            except Exception as e:
                print(f"drawing not saved: {e}")
        elif line:
            print(f"board: {line}")


def board_list(board):
    """Saved scans on the board as {name: (scan bytes, drawing bytes)}, and the LIST end line."""
    board.send("LIST")
    files = {}
    while True:
        line = board.wait_for("")
        if line.startswith("FILE "):
            _, name, *sizes = line.split()
            files[name] = (int(sizes[0]), int(sizes[1]) if len(sizes) > 1 else 0)
        elif line.startswith("LIST end"):
            return files, line


def fetch(board, clear):
    files, _ = board_list(board)
    fetched = skipped = deleted = drawings = 0
    for name, (size, sketch_size) in files.items():
        path = SCANS_DIR / f"{file_name(name)}.json"
        if path.exists():
            skipped += 1
        else:
            board.send(f"GET {name}")
            board.wait_for("SCANDATA start")
            try:
                record, got = receive_scan(board)
            except RuntimeError as e:
                print(f"{name}: {e}, not saved")
                continue
            if got != size or record["board_name"] != name:
                print(f"{name}: got {got} of {size} bytes, not saved")
                continue
            save_board_scan(record)
            fetched += 1
        if sketch_size:
            board.send(f"GETSKETCH {name}")
            board.wait_for("SKETCHDATA start")
            text = receive_sketch(board)
            if len(text) != sketch_size:
                print(f"{name} drawing: got {len(text)} of {sketch_size} bytes, not saved")
                continue
            save_board_sketch(text)
            drawings += 1
        # Delete only what is safely here and reads back
        if (clear and json.loads(path.read_text()).get("board_name") == name
                and (not sketch_size or json.loads(sketch_path(name).read_text()).get("scan") == name)):
            board.send(f"DELETE {name}")
            if board.wait_for("DELETE") == f"DELETED {name}":
                deleted += 1
    print(f"{len(files)} on the board, {fetched} fetched, {skipped} already here, {drawings} drawings checked, "
          f"{deleted} deleted from the board")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scan", action="store_true", help="ask the board for a scan")
    parser.add_argument("--status", action="store_true", help="show the lidar details and stop")
    parser.add_argument("--list", action="store_true", help="list the scans saved on the board and stop")
    parser.add_argument("--fetch", action="store_true", help="save the board's scans in scans/ and stop")
    parser.add_argument("--clear", action="store_true", help="with --fetch, delete them from the board")
    parser.add_argument("--jsonl", action="store_true", help="only rewrite scans/xscan.jsonl, no board needed")
    parser.add_argument("--push", metavar="SURVEY_DIR", help="survey folder on the device, no board needed")
    parser.add_argument("pairs", nargs="*", metavar="STATION=SCAN", help="with --push, the scan for each station")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)  # show each line at once, even when piped
    if args.clear and not args.fetch:
        parser.error("--clear only works with --fetch")
    if args.pairs and not args.push:
        parser.error("STATION=SCAN only works with --push")
    if args.push:
        if not args.pairs:
            parser.error("--push needs at least one STATION=SCAN")
        push(args.push, args.pairs)
        return
    if args.jsonl:
        write_jsonl()
        return

    board = Board()
    try:
        if args.status:
            print(f"board: {board.wait_for('LIDAR')}")
        elif args.list:
            files, end = board_list(board)
            for name, (size, sketch_size) in files.items():
                print(f"{name} {size} bytes" + (f", drawing {sketch_size} bytes" if sketch_size else ""))
            count, used, total = end.split()[2:5]
            print(f"{count} scans, {int(used) * 100 // max(int(total), 1)}% full")
        elif args.fetch:
            fetch(board, args.clear)
            write_jsonl()
        else:
            if args.scan:
                board.send("SCAN")
            listen(board)
    except KeyboardInterrupt:
        pass
    finally:
        board.close()


if __name__ == "__main__":
    main()
