#!/usr/bin/env python3
"""Debug link to the scanner over its USB port (firmware/ must be on the board).

  board_link.py              until Ctrl-C, show what the board reports and save each scan it takes
  board_link.py --scan       ask the board for one scan, save it, then keep listening
  board_link.py --status     show whether the board has a lidar, then stop

Opening the port resets the board, so it starts from its start-up screen.
"""
import argparse
import time

import serial

from scan_once import make_record, save

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
        self.port.write(b"?\n")
        print("Waiting for the board (finish touch calibration if it is showing)")
        self.wait_for("READY")

    def close(self):
        self.port.close()

    def readline(self):
        return self.port.readline().decode(errors="replace").strip()

    def send(self, line):
        self.port.write(f"{line}\n".encode())

    def wait_for(self, word):
        while True:
            line = self.readline()
            if word in line:
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


def receive_scan(board, info):
    """Read a scan (after SCANDATA start) and return it as a record."""
    revolutions = []
    while True:
        line = board.readline()
        if line == "SCANDATA end":
            return make_record(info, revolutions, "board")
        if line == "T":
            revolutions.append([])
        elif line.startswith("P ") and revolutions:
            deg, mm, quality = line.split()[1:4]
            revolutions[-1].append({"quality": int(quality), "angle_deg": float(deg), "distance_mm": float(mm)})
        elif not line:
            raise RuntimeError("scan stopped part way")


def listen(board):
    print("Listening, Ctrl-C to stop")
    info = None
    while True:
        line = board.readline()
        if not line:
            continue
        if line == "SCANDATA start":
            try:
                record = receive_scan(board, info)
                path = save(record)
                points = sum(len(r) for r in record["revolutions"])
                print(f"{path.name}: {len(record['revolutions'])} turns, {points} points")
            except Exception as e:
                print(f"scan not saved: {e}")
            continue
        if line.startswith("LIDAR "):
            info = parse_lidar_info(line) or info
        print(f"board: {line}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scan", action="store_true", help="ask the board for a scan")
    parser.add_argument("--status", action="store_true", help="show the lidar details and stop")
    args = parser.parse_args()

    board = Board()
    try:
        if args.status:
            print(f"board: {board.wait_for('LIDAR')}")
            return
        if args.scan:
            board.send("SCAN")
        listen(board)
    except KeyboardInterrupt:
        pass
    finally:
        board.close()


if __name__ == "__main__":
    main()
