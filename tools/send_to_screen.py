#!/usr/bin/env python3
"""Draw scans on the CYD screen (firmware/ must be on the board).

  send_to_screen.py              newest saved scan in scans/
  send_to_screen.py FILE         a saved scan
  send_to_screen.py --scan 5     take a new 5 turn scan (saved as usual), then show it
  send_to_screen.py --listen     until Ctrl-C, save scans the board takes, and scan here
                                 for the board when it has no lidar
"""
import argparse
import json
import time
from pathlib import Path

import serial

from scan_once import DEFAULT_PORT as LIDAR_PORT, SCANS_DIR, make_record, revolutions_of, save, scan

SCREEN_PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
SCREEN_BAUD = 460800
SYNC_EVERY = 100  # lines, the board drops data if sent more without a pause


class Screen:
    def __init__(self, port_name=SCREEN_PORT):
        self.port = serial.Serial()
        self.port.port, self.port.baudrate, self.port.timeout = port_name, SCREEN_BAUD, 0.5
        self.port.dtr = self.port.rts = False
        self.port.open()
        # Opening the port still resets the ESP32 here, so let it boot and drop its boot messages
        time.sleep(1.5)
        self.port.reset_input_buffer()
        self.port.write(b"?\n")
        print("Waiting for the screen (finish touch calibration if it is showing)")
        self.wait_for("READY", timeout=None)

    def close(self):
        self.port.close()

    def readline(self):
        return self.port.readline().decode(errors="replace").strip()

    def wait_for(self, word, timeout=5.0):
        """Wait for a reply containing word, for ever if timeout is None."""
        deadline = None if timeout is None else time.monotonic() + timeout
        while deadline is None or time.monotonic() < deadline:
            line = self.readline()
            if word in line:
                return line
            if line:
                print(f"screen: {line}")
        raise RuntimeError(f"screen did not reply {word}")

    def send(self, record, name):
        lines = [f"B {name}"]
        for rev in revolutions_of(record):
            lines.append("T")
            lines += [f"P {p['angle_deg']:.3f} {p['distance_mm']:.0f}" for p in rev if p["distance_mm"] > 0]
        for i in range(0, len(lines), SYNC_EVERY):
            self.port.write(("\n".join(lines[i:i + SYNC_EVERY]) + "\nS\n").encode())
            self.wait_for("OK")
        self.port.write(b"E\n")
        reply = self.wait_for("DRAWN", timeout=15)
        sent = sum(line.startswith("P") for line in lines)
        if int(reply.split()[-1]) != sent:
            raise RuntimeError(f"sent {sent} points but screen {reply}")
        return reply

    def message(self, text):
        self.port.write(f"M {text}\n".encode())


def receive_board_scan(screen, info):
    """Read a scan the board took itself (after SCANDATA start) and return it as a record."""
    revolutions = []
    while True:
        line = screen.readline()
        if line == "SCANDATA end":
            return make_record(info, revolutions, "board")
        if line == "T":
            revolutions.append([])
        elif line.startswith("P ") and revolutions:
            deg, mm, quality = line.split()[1:4]
            revolutions[-1].append({"quality": int(quality), "angle_deg": float(deg), "distance_mm": float(mm)})
        elif not line:
            raise RuntimeError("board scan stopped part way")


def parse_lidar_info(line):
    model, firmware, hardware, serial_number = line.split()[1:5]
    return {"model": int(model), "firmware": firmware, "hardware": int(hardware), "serial": serial_number}


def listen(screen, turns):
    print("Waiting for the SCAN button, Ctrl-C to stop")
    info = None
    while True:
        line = screen.readline()
        if line.startswith("LIDAR ") and line != "LIDAR failed":
            info = parse_lidar_info(line)
        elif line == "SCANDATA start":
            try:
                record = receive_board_scan(screen, info)
                path = save(record)
                points = sum(len(r) for r in record["revolutions"])
                print(f"{path.name}: board scan, {len(record['revolutions'])} turns, {points} points")
            except Exception as e:
                print(f"board scan not saved: {e}")
        elif line == "SCAN":
            try:
                record = scan(LIDAR_PORT, turns)
                path = save(record)
                print(f"{path.name}: {screen.send(record, path.stem)}")
            except Exception as e:
                print(f"scan failed: {e}")
                screen.message(f"Scan failed: {e}")
        elif line:
            print(f"screen: {line}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", nargs="?", help="saved scan, newest in scans/ if left out")
    parser.add_argument("--scan", type=int, metavar="TURNS", help="take a new scan of this many turns first")
    parser.add_argument("--listen", action="store_true", help="scan each time the SCAN button is pressed")
    parser.add_argument("--turns", type=int, default=1, help="turns per scan with --listen")
    args = parser.parse_args()

    screen = Screen()
    try:
        if args.listen:
            listen(screen, args.turns)
        if args.scan:
            record = scan(LIDAR_PORT, args.scan)
            path = save(record)
        else:
            path = Path(args.file) if args.file else sorted(SCANS_DIR.glob("scan-*.json"))[-1]
            record = json.loads(path.read_text())
        print(f"{path.name}: {screen.send(record, path.stem)}")
    except KeyboardInterrupt:
        pass
    finally:
        screen.close()


if __name__ == "__main__":
    main()
