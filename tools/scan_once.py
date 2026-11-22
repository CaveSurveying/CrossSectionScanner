#!/usr/bin/env python3
"""Take one or more revolutions from an RPLIDAR C1 over USB, save them as JSON and plot them.

  scan_once.py                 one turn, save to scans/, plot
  scan_once.py --turns 5       five consecutive turns, one colour each
  scan_once.py --plot FILE     plot a saved scan
"""
import argparse
import json
import math
import time
from datetime import datetime
from pathlib import Path

import serial

DEFAULT_PORT = "/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_804a46d8d831f111a17b935f30d20014-if00-port0"
BAUD = 460800
SCANS_DIR = Path(__file__).resolve().parent.parent / "scans"

CMD_STOP, CMD_SCAN, CMD_INFO = 0x25, 0x20, 0x50
SCAN_DESCRIPTOR = bytes.fromhex("a55a0500004081")
SETTLE_REVOLUTIONS = 3  # skipped while the motor gets up to speed


def command(port, cmd):
    port.write(bytes([0xA5, cmd]))


def read_info(port):
    command(port, CMD_INFO)
    reply = port.read(27)
    if len(reply) != 27 or reply[:2] != b"\xa5\x5a":
        raise RuntimeError(f"no GET_INFO reply: {reply.hex()}")
    d = reply[7:]
    return {"model": d[0], "firmware": f"{d[2]}.{d[1]:02d}", "hardware": d[3], "serial": d[4:20].hex().upper()}


def decode_node(b):
    """Decode one 5 byte standard scan node, or None if the check bits are wrong."""
    start = b[0] & 1
    if start == (b[0] >> 1) & 1 or not b[1] & 1:
        return None
    return {
        "start": bool(start),
        "quality": b[0] >> 2,
        "angle_deg": ((b[1] >> 1) | (b[2] << 7)) / 64.0,
        "distance_mm": (b[3] | (b[4] << 8)) / 4.0,
    }


def capture_revolutions(port, turns, timeout=5.0):
    """Return a list of consecutive full revolutions taken after the motor has settled."""
    command(port, CMD_SCAN)
    descriptor = port.read(7)
    if descriptor != SCAN_DESCRIPTOR:
        raise RuntimeError(f"unexpected scan descriptor: {descriptor.hex()}")
    revolutions, seen, current = [], 0, None
    deadline = time.monotonic() + timeout + turns / 8
    while time.monotonic() < deadline:
        raw = port.read(5)
        node = decode_node(raw) if len(raw) == 5 else None
        if node is None:
            port.read(1)  # out of step, slip a byte to resync
            continue
        if node.pop("start"):
            if current is not None and seen > SETTLE_REVOLUTIONS:
                revolutions.append(current)
                if len(revolutions) == turns:
                    return revolutions
            seen += 1
            current = []
        if current is not None:
            current.append(node)
    raise RuntimeError(f"timed out after {len(revolutions)} of {turns} revolutions")


def revolutions_of(record):
    """Revolutions in a record, reading the older one-turn format too."""
    return record["revolutions"] if "revolutions" in record else [record["points"]]


def samples_of(record):
    """Samples in each turn. Board scans from FORMAT 2 keep only the returns and give the count."""
    return record.get("samples") or [len(rev) for rev in revolutions_of(record)]


def summarise(record):
    for i, (rev, samples) in enumerate(zip(revolutions_of(record), samples_of(record)), 1):
        returns = sum(p["distance_mm"] > 0 for p in rev)
        print(f"turn {i}: {samples} points, {samples - returns} with no return")
    if "samples" in record:
        return  # no-return samples not kept, so their angles aren't known
    # Angles (to the nearest degree) with no return on every turn
    sets = [{round(p["angle_deg"]) % 360 for p in rev if p["distance_mm"] == 0} for rev in revolutions_of(record)]
    always = sorted(set.intersection(*sets)) if sets else []
    print(f"no return on every turn at {len(always)} degrees: {always}")


def scan(port_name, turns):
    with serial.Serial(port_name, BAUD, timeout=1) as port:
        port.dtr = False
        command(port, CMD_STOP)
        time.sleep(0.1)
        port.reset_input_buffer()
        info = read_info(port)
        try:
            revolutions = capture_revolutions(port, turns)
        finally:
            command(port, CMD_STOP)
    return make_record(info, revolutions, "computer")


def make_record(info, revolutions, source):
    """A scan record as saved in scans/. source is where the lidar was read, computer or board."""
    return {
        "format": "CrossSectionScanner raw scan 2",
        "time": datetime.now().astimezone().isoformat(timespec="seconds"),
        "source": source,
        "lidar": info,
        "revolutions": revolutions,
    }


def save(record, name=None):
    """Save a scan record to scans/ (named by the time unless a name is given) and return its path."""
    SCANS_DIR.mkdir(exist_ok=True)
    path = SCANS_DIR / f"{name or f'scan-{datetime.now():%Y%m%d-%H%M%S}'}.json"
    path.write_text(json.dumps(record, indent=1))
    return path


def plot(record, title):
    import matplotlib.pyplot as plt

    # 0° up, angles clockwise as the lidar reports them, metres, one colour per turn
    fig, ax = plt.subplots(figsize=(8, 8))
    revolutions = revolutions_of(record)
    total = valid = 0
    for i, (rev, samples) in enumerate(zip(revolutions, samples_of(record)), 1):
        pts = [p for p in rev if p["distance_mm"] > 0]
        total += samples
        valid += len(pts)
        xs = [p["distance_mm"] / 1000 * math.sin(math.radians(p["angle_deg"])) for p in pts]
        ys = [p["distance_mm"] / 1000 * math.cos(math.radians(p["angle_deg"])) for p in pts]
        ax.scatter(xs, ys, s=4, label=f"turn {i} ({len(pts)}/{samples})")
    if len(revolutions) > 1:
        ax.legend(loc="upper right", fontsize="small")
    ax.plot(0, 0, "r+", markersize=15)
    ax.annotate("0°", (0, 0), (0, 25), textcoords="offset points", ha="center", color="r",
                arrowprops={"arrowstyle": "<-", "color": "r"})
    ax.set_aspect("equal")
    ax.grid(True)
    ax.set_xlabel("m")
    ax.set_ylabel("m")
    ax.set_title(f"{title}\n{len(revolutions)} turns, {valid} points ({total - valid} with no return)")
    plt.show()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--plot", metavar="FILE", help="plot a saved scan instead of scanning")
    parser.add_argument("--no-plot", action="store_true", help="save without plotting")
    parser.add_argument("--turns", type=int, default=1, help="consecutive revolutions to take")
    args = parser.parse_args()

    if args.plot:
        path = Path(args.plot)
        record = json.loads(path.read_text())
    else:
        record = scan(args.port, args.turns)
        path = save(record)
        print(f"Saved {args.turns} turns to {path}")
    summarise(record)
    if not args.no_plot:
        plot(record, path.name)


if __name__ == "__main__":
    main()
