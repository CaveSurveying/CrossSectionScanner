#!/usr/bin/env python3
"""Stage 2 calibration fit, the alignment between the accelerometer and the lidar.

Each pose gives a scan of a flat floor and an accelerometer reading. The floor
is a line in the scan plane and must be at right angles to gravity, so the fit
finds the rotation R (accelerometer frame to lidar frame) making every floor
line's direction d at right angles to R a. See PLAN.md, Calibration method.

Lidar frame: z along the spin axis (normal to the scan plane), x at 0 degrees.
"""
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation

# Lidar turned 90 degrees for the cable-left mounting, the fit's starting point
NOMINAL = Rotation.from_euler("z", 90, degrees=True)


def line_direction(points):
    """Unit direction of the best straight line through 2D points (total least squares)."""
    centred = points - points.mean(axis=0)
    _, _, vt = np.linalg.svd(centred, full_matrices=False)
    return vt[0]


def fit_alignment(directions, accels, start=NOMINAL):
    """Fit R from floor line directions (N x 2, lidar frame) and accelerometer readings (N x 3).

    Returns the rotation and the residuals in degrees.
    """
    d = np.column_stack([directions, np.zeros(len(directions))])
    a = accels / np.linalg.norm(accels, axis=1, keepdims=True)

    def residuals(w):
        g = (start * Rotation.from_rotvec(w)).apply(a)
        return np.einsum("ij,ij->i", d, g)

    result = least_squares(residuals, np.zeros(3))
    rotation = start * Rotation.from_rotvec(result.x)
    return rotation, np.degrees(np.arcsin(np.clip(result.fun, -1, 1)))


def alignment_error(fitted, true):
    """Errors in degrees of a fitted rotation, as (scan plane tilt, roll about the lidar axis)."""
    err = fitted * true.inv()
    z = err.apply([0, 0, 1])
    x = err.apply([1, 0, 0])
    tilt = np.degrees(np.arccos(np.clip(z[2], -1, 1)))
    roll = np.degrees(np.arctan2(x[1], x[0]))
    return tilt, abs(roll)
