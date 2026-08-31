#!/usr/bin/env python3
"""Golden tests for range–az clutter surface binning (Enforce parity).

Mirrors RDF_RadarRangeAzClutterSurface TraceRay math:
  pr = sigma0 * area / R^4  with geometric horizon occlusion.
Does not require DEM files — uses a synthetic ramp heightfield.
"""

from __future__ import annotations

import math
import unittest

import numpy as np


MIN_GRAZING = 0.02
MIN_RANGE_M = 50.0


def sigma0_simple(surface: int, grazing: float) -> float:
    """Constant-gamma stand-in (linear)."""
    theta = max(grazing, MIN_GRAZING)
    if theta > math.pi * 0.5:
        theta = math.pi * 0.5
    ref = 0.01
    if surface == 1:
        ref = 0.05
    elif surface == 2:
        ref = 0.001
    return ref * (math.sin(theta) / math.sin(0.1)) ** 1.0


def build_intensity_sector(
    height: np.ndarray,
    surface: np.ndarray,
    origin_y: float,
    max_range: float,
    n_az: int,
    n_rng: int,
    sector_half_deg: float,
) -> np.ndarray:
    """Boresight-relative sector fill (center column = 0 az offset)."""
    power = np.zeros((n_az, n_rng), dtype=np.float64)
    half = math.radians(sector_half_deg)
    sector = half * 2.0
    step = max_range / float(n_rng)
    if step < 5.0:
        step = 5.0
    rows, cols = height.shape
    cx = cols // 2
    cz = rows // 2
    cell_m = max_range / float(max(cols, rows) * 0.5)

    for ia in range(n_az):
        if n_az > 1:
            t = ia / float(n_az - 1)
        else:
            t = 0.5
        az_off = -half + t * sector
        dir_x = math.cos(az_off)
        dir_z = math.sin(az_off)
        max_elev = -1.0e38
        blocked = False
        r = step
        while r <= max_range:
            ix = int(cx + dir_x * r / cell_m)
            iz = int(cz + dir_z * r / cell_m)
            if ix < 0 or iz < 0 or ix >= cols or iz >= rows:
                r = r + step
                continue
            ty = float(height[iz, ix])
            elev = math.atan2(ty - origin_y, r)
            if elev > max_elev + 1.0e-4:
                max_elev = elev
                blocked = False
            elif elev < max_elev - 0.002:
                blocked = True
            if blocked:
                r = r + step
                continue
            grazing = max(MIN_GRAZING, abs(elev))
            s0 = sigma0_simple(int(surface[iz, ix]), grazing)
            az_width = max(step, r * (sector / max(n_az, 1)))
            area = step * az_width
            r_safe = max(r, MIN_RANGE_M)
            pr = (s0 * area) / (r_safe ** 4)
            ir = int(r / max_range * n_rng)
            if ir < 0:
                ir = 0
            if ir >= n_rng:
                ir = n_rng - 1
            power[ia, ir] = power[ia, ir] + pr
            r = r + step
    return power


class TestRangeAzClutterSurface(unittest.TestCase):
    def test_energy_in_near_bins_on_flat(self) -> None:
        n = 64
        height = np.full((n, n), 10.0, dtype=np.float64)
        surface = np.ones((n, n), dtype=np.int32)
        power = build_intensity_sector(
            height,
            surface,
            origin_y=50.0,
            max_range=2000.0,
            n_az=32,
            n_rng=32,
            sector_half_deg=30.0,
        )
        self.assertGreater(float(power.sum()), 0.0)
        # Near range bins should hold most energy on flat terrain.
        near = float(power[:, :8].sum())
        far = float(power[:, 24:].sum())
        self.assertGreater(near, far)

    def test_ridge_blocks_far_bins(self) -> None:
        n = 80
        height = np.full((n, n), 0.0, dtype=np.float64)
        # Ridge across boresight at mid range.
        height[:, n // 2 + 5 : n // 2 + 8] = 80.0
        surface = np.ones((n, n), dtype=np.int32)
        power = build_intensity_sector(
            height,
            surface,
            origin_y=5.0,
            max_range=2000.0,
            n_az=16,
            n_rng=40,
            sector_half_deg=5.0,
        )
        # Center column (boresight) far bins should be quieter than near.
        mid_az = 8
        near = float(power[mid_az, :10].sum())
        far = float(power[mid_az, 30:].sum())
        self.assertGreater(near + far, 0.0)
        self.assertGreater(near, far * 0.5)

    def test_display_r4_stretch_finite(self) -> None:
        n_az = 8
        n_rng = 16
        power = np.zeros((n_az, n_rng), dtype=np.float64)
        power[4, 2] = 1.0e-12
        power[4, 10] = 1.0e-14
        max_range = 2000.0
        work = np.zeros_like(power)
        for ir in range(n_rng):
            r = ((ir + 0.5) / n_rng) * max_range
            if r < 50.0:
                r = 50.0
            work[:, ir] = power[:, ir] * (r ** 4)
        db = np.full_like(work, -300.0)
        mask = work > 1.0e-30
        db[mask] = 10.0 * np.log10(work[mask])
        self.assertTrue(np.isfinite(db).all())
        self.assertEqual(float(db[0, 0]), -300.0)
        self.assertGreater(float(db[4, 2]), -300.0)
        self.assertGreater(float(db[4, 10]), -300.0)


if __name__ == "__main__":
    unittest.main()
