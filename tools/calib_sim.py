#!/usr/bin/env python3
"""Simulate stage 2 calibration (lidar alignment from a flat floor) to see how good it can be.

  calib_sim.py                  sweep pose counts, tilts, pose schemes and hand jitter
  calib_sim.py --trials 500     more trials per case

Each trial makes a random true alignment, takes poses over a floor with a small
slope, adds lidar, accelerometer and hand errors, fits with calib_stage2 and
reports the error left in the scan plane tilt and the roll about the lidar axis.
"""
import argparse

import numpy as np
from scipy.spatial.transform import Rotation

from calib_stage2 import NOMINAL, alignment_error, fit_alignment, line_direction

LIDAR_STEP = 0.72        # degrees between samples
RANGE_MIN, RANGE_MAX = 0.05, 12.0
RANGE_SIGMA = 0.010      # m, datasheet gives about +-30 mm
ROOM_RADIUS = 3.0        # m, floor seen only this far out (walls beyond)
MIN_POINTS = 20          # fewer floor points and the pose is taken again
MOUNT_SIGMA = 3.0        # degrees per axis, lidar and IMU mounting error
FLOOR_SLOPE = 0.5        # degrees
ACC_BIAS = 0.002         # g per axis, left over from stage 1
ACC_SCALE = 0.002        # per axis, left over from stage 1
ACC_NOISE = 0.001        # g per reading, after averaging

# Lidar frame to a level frame with the lidar axis along world X and lidar x up
BASE = Rotation.from_matrix([[0, 0, 1], [0, -1, 0], [1, 0, 0]])
ANGLES = np.radians(np.arange(0, 360, LIDAR_STEP))
RAYS = np.column_stack([np.cos(ANGLES), np.sin(ANGLES), np.zeros_like(ANGLES)])


def pose(yaw, tilt, roll):
    """Lidar frame to world, with the scan plane tilted from vertical by tilt (degrees)."""
    return (Rotation.from_euler("ZY", [yaw, -tilt], degrees=True) * BASE
            * Rotation.from_euler("z", roll, degrees=True))


def poses(scheme, count, max_tilt, rng):
    """(yaw, tilt, roll) for each pose. Guided ones alternate +-max_tilt, step the roll and turn round."""
    if scheme == "random":
        return np.column_stack([rng.uniform(0, 360, count),
                                rng.uniform(-max_tilt, max_tilt, count),
                                rng.uniform(0, 360, count)])
    half = count // 2
    rolls = np.arange(half) * 360 / half + rng.normal(0, 10, half)
    tilts = np.where(np.arange(half) % 2, max_tilt, -max_tilt) + rng.normal(0, 5, half)
    yaws = rng.uniform(0, 360, half) + rng.normal(0, 10, half)
    turned = np.column_stack([yaws + 180, tilts, rolls])
    return np.vstack([np.column_stack([yaws, tilts, rolls]), turned])


def floor_scan(q, height, normal, rng):
    """Floor points (lidar frame, 2D) seen in one turn, or None if too few."""
    rays = q.apply(RAYS)
    down = rays @ normal
    with np.errstate(divide="ignore"):
        t = -height / down          # floor through (0, 0, -height) with this normal
    world = rays * t[:, None]
    ok = (down < 0) & (t > RANGE_MIN) & (t < RANGE_MAX) & (np.hypot(world[:, 0], world[:, 1]) < ROOM_RADIUS)
    if ok.sum() < MIN_POINTS:
        return None
    r = t[ok] + rng.normal(0, RANGE_SIGMA, ok.sum())
    return RAYS[ok, :2] * r[:, None]


def trial(scheme, count, max_tilt, jitter, rng):
    true = NOMINAL * Rotation.from_rotvec(np.radians(rng.normal(0, MOUNT_SIGMA, 3)))
    slope_dir = rng.uniform(0, 2 * np.pi)
    s = np.radians(FLOOR_SLOPE)
    normal = np.array([np.sin(s) * np.cos(slope_dir), np.sin(s) * np.sin(slope_dir), np.cos(s)])
    bias = rng.normal(0, ACC_BIAS, 3)
    scale = 1 + rng.normal(0, ACC_SCALE, 3)
    directions, accels, retaken = [], [], 0
    for yaw, tilt, roll in poses(scheme, count, max_tilt, rng):
        q = pose(yaw, tilt, roll)
        points = None
        while points is None:
            points = floor_scan(q, rng.uniform(0.6, 1.4), normal, rng)
            if points is None:
                retaken += 1
                q = pose(yaw + rng.normal(0, 20), tilt * 0.8, roll)
        directions.append(line_direction(points))
        # hand moves a little between the turn and the accelerometer reading
        held = q * Rotation.from_rotvec(np.radians(rng.normal(0, jitter, 3)))
        g_lidar = held.inv().apply([0, 0, -1])
        accels.append(true.inv().apply(g_lidar) * scale + bias + rng.normal(0, ACC_NOISE, 3))
    fitted, _ = fit_alignment(np.array(directions), np.array(accels))
    return (*alignment_error(fitted, true), retaken)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--trials", type=int, default=200)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    rng = np.random.default_rng(args.seed)
    print(f"{args.trials} trials per case, 95th percentile errors in degrees\n")
    print(f"{'scheme':8} {'jitter':>6} {'poses':>5} {'tilt':>5} | {'plane tilt':>10} {'roll':>6} {'retakes':>7}")
    for scheme in ("random", "guided"):
        for jitter in (0.0, 0.3):
            for count in (12, 24, 48):
                for max_tilt in (15, 30, 45, 60):
                    results = np.array([trial(scheme, count, max_tilt, jitter, rng) for _ in range(args.trials)])
                    tilt95, roll95 = np.percentile(results[:, :2], 95, axis=0)
                    print(f"{scheme:8} {jitter:6.1f} {count:5d} {max_tilt:5d} | {tilt95:10.2f} {roll95:6.2f}"
                          f" {results[:, 2].mean():7.1f}")
            print()


if __name__ == "__main__":
    main()
