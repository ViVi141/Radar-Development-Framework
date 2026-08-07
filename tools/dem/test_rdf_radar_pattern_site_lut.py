#!/usr/bin/env python3
"""Unit tests for segmented pattern LUT + fixed-site path LUT."""

from __future__ import annotations

import unittest

from rdf_radar_pattern_lut import (
    build_pattern_lut,
    interp_pattern_lut,
    run_pattern_lut_validation,
    segmented_one_way_gain,
)
from rdf_radar_physics import db_to_lin, gaussian_beam_gain
from rdf_radar_site_path_lut import (
    build_synthetic_site_lut,
    interp_site_path_lut,
    run_site_path_validation,
)


class TestPatternLut(unittest.TestCase):
    def test_boresight_unity(self) -> None:
        g = segmented_one_way_gain(0.0, 2.5, -25.0)
        self.assertAlmostEqual(g, 1.0, places=6)

    def test_far_hits_floor(self) -> None:
        floor = db_to_lin(-25.0)
        g = segmented_one_way_gain(90.0, 2.5, -25.0)
        self.assertAlmostEqual(g, floor, places=6)

    def test_sidelobe_above_pure_gauss(self) -> None:
        # At ~2.2*HPBW a segmented peak should beat bare gaussian (near zero).
        off = 2.2 * 2.5
        seg = segmented_one_way_gain(off, 2.5, -25.0)
        gauss = gaussian_beam_gain(off, 2.5)
        self.assertGreater(seg, gauss * 10.0)
        self.assertGreater(seg, db_to_lin(-25.0))

    def test_lut_interp(self) -> None:
        lut = build_pattern_lut(n_samples=721)
        for off in (-12.0, 0.0, 5.5, 40.0):
            self.assertAlmostEqual(
                interp_pattern_lut(lut, off),
                segmented_one_way_gain(off, 2.5, -25.0),
                places=2,
            )

    def test_validation_ok(self) -> None:
        report = run_pattern_lut_validation()
        self.assertTrue(report["all_ok"])


class TestSitePathLut(unittest.TestCase):
    def test_synthetic_ridge_shadow(self) -> None:
        lut = build_synthetic_site_lut()
        clear = interp_site_path_lut(lut, 0.0, 500.0)
        ridge = interp_site_path_lut(lut, 90.0, 800.0)
        self.assertGreater(clear, 0.9)
        self.assertLess(ridge, clear)

    def test_validation_ok(self) -> None:
        report = run_site_path_validation()
        self.assertTrue(report["all_ok"])

    def test_range_clamp(self) -> None:
        lut = build_synthetic_site_lut(max_range_m=2000.0, range_count=20)
        a = interp_site_path_lut(lut, 10.0, 50.0)
        b = interp_site_path_lut(lut, 10.0, 5000.0)
        self.assertGreaterEqual(a, 0.0)
        self.assertLessEqual(a, 1.0)
        self.assertGreaterEqual(b, 0.0)
        self.assertLessEqual(b, 1.0)


if __name__ == "__main__":
    unittest.main()
