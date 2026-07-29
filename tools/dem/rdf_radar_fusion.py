#!/usr/bin/env python3
"""Multi-radar association + horizontal cross-fix helpers (RDF fusion)."""

from __future__ import annotations

import math


def bearing_separation_deg(az_a: float, az_b: float) -> float:
    d = az_a - az_b
    while d > 180.0:
        d -= 360.0
    while d < -180.0:
        d += 360.0
    return abs(d)


def try_cross_fix_horizontal(
    o1_xz: tuple[float, float],
    az1_deg: float,
    o2_xz: tuple[float, float],
    az2_deg: float,
    min_angle_deg: float = 15.0,
    min_range_m: float = 50.0,
) -> tuple[float, float] | None:
    """Intersect two horizontal bearings. Returns (x, z) or None if rejected."""
    angle = bearing_separation_deg(az1_deg, az2_deg)
    if angle < min_angle_deg or angle > 180.0 - min_angle_deg:
        return None

    az1 = math.radians(az1_deg)
    az2 = math.radians(az2_deg)
    dx1 = math.cos(az1)
    dz1 = math.sin(az1)
    dx2 = math.cos(az2)
    dz2 = math.sin(az2)

    det = dx1 * dz2 - dz1 * dx2
    if abs(det) < 1e-4:
        return None

    ox = o2_xz[0] - o1_xz[0]
    oz = o2_xz[1] - o1_xz[1]
    t = (ox * dz2 - oz * dx2) / det
    s = (ox * dz1 - oz * dx1) / det
    if t < min_range_m or s < min_range_m:
        return None
    return (o1_xz[0] + dx1 * t, o1_xz[1] + dz1 * t)


def associate_pair(
    pos_a: tuple[float, float, float],
    vel_a: tuple[float, float, float],
    pos_b: tuple[float, float, float],
    vel_b: tuple[float, float, float],
    gate_m: float = 350.0,
    speed_gate_ms: float = 80.0,
) -> bool:
    dx = pos_b[0] - pos_a[0]
    dy = pos_b[1] - pos_a[1]
    dz = pos_b[2] - pos_a[2]
    dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist > gate_m:
        return False
    dvx = vel_b[0] - vel_a[0]
    dvy = vel_b[1] - vel_a[1]
    dvz = vel_b[2] - vel_a[2]
    speed = math.sqrt(dvx * dvx + dvy * dvy + dvz * dvz)
    if speed > speed_gate_ms:
        return False
    return True
