#!/usr/bin/env python3
"""Record the touch map test (tests/touch_map on the board) and plot how the touch layer bends.

  touch_map.py           run the test, save the readings, plot them
  touch_map.py FILE      plot saved readings
"""
import sys
import time
from datetime import datetime
from pathlib import Path

import numpy as np
import serial

from board_link import BOARD_PORT

RESULTS = Path(__file__).resolve().parent.parent / "tests" / "touch_map" / "results"


def record():
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = BOARD_PORT, 460800, 1
    port.dtr = port.rts = False
    port.open()  # restarts the board, which starts the test
    rows = []
    print("Press and hold each cross on the screen")
    with port:
        while True:
            line = port.readline().decode(errors="replace").strip()
            if line.startswith("MAP "):
                parts = line.split()
                if parts[1] == "done":
                    break
                if parts[1] != "start":
                    rows.append([float(v) for v in parts[1:5]])
                    print(f"{len(rows)}/20 {line}")
    RESULTS.mkdir(parents=True, exist_ok=True)
    path = RESULTS / f"touch-map-{datetime.now():%Y%m%d-%H%M%S}.txt"
    np.savetxt(path, rows, fmt="%.0f", header="screen_x screen_y raw_x raw_y")
    print(f"Saved {path}")
    return path


def plot(path):
    import matplotlib.pyplot as plt

    d = np.loadtxt(path)
    sx, sy, rx, ry = d.T
    # Best straight line (affine) fit from raw to screen, and what it leaves over
    a = np.column_stack([rx, ry, np.ones_like(rx)])
    fx, *_ = np.linalg.lstsq(a, sx, rcond=None)
    fy, *_ = np.linalg.lstsq(a, sy, rcond=None)
    px, py = a @ fx, a @ fy
    err = np.hypot(px - sx, py - sy)

    fig, (left, right) = plt.subplots(1, 2, figsize=(12, 7))
    # Raw readings joined along each row and column of crosses
    for key in (sy, sx):
        for v in np.unique(key):
            m = key == v
            order = np.argsort(sx[m] + sy[m])
            left.plot(rx[m][order], ry[m][order], "-o", color="tab:blue", markersize=4)
    left.set_title("Raw readings, joined along rows and columns of crosses")
    left.set_xlabel("raw x")
    left.set_ylabel("raw y")
    left.invert_xaxis()  # raw x runs right to left on the screen
    left.invert_yaxis()
    left.set_aspect("equal")
    left.grid(True)

    right.quiver(sx, sy, px - sx, py - sy, angles="xy", scale_units="xy", scale=1, color="tab:red")
    right.scatter(sx, sy, s=10, color="black")
    for x, y, e in zip(sx, sy, err):
        right.annotate(f"{e:.0f}", (x, y), (4, 4), textcoords="offset points", fontsize=8)
    right.set_xlim(-10, 250)
    right.set_ylim(330, -10)
    right.set_aspect("equal")
    right.grid(True)
    right.set_title(f"Straight line fit error (px), worst {err.max():.0f}, mean {err.mean():.0f}")
    right.set_xlabel("screen x")
    right.set_ylabel("screen y")
    fig.suptitle(path.name)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    plot(Path(sys.argv[1]) if len(sys.argv) > 1 else record())
