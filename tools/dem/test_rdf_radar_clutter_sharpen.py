#!/usr/bin/env python3
"""Unit tests for clutter-boundary sharpening helpers."""

from __future__ import annotations

import math
import unittest

from rdf_radar_clutter_sharpen import (
    asymmetric_ema_series,
    asymmetric_ema_step,
    case_asymmetric_ema_drops_faster,
    case_footprint_mix_land_water,
    case_sea_state_water_scale,
    footprint_mix_sigma0,
    run_clutter_sharpen_validation,
)
from rdf_radar_materials import (
    SURF_VEGETATION,
    SURF_WATER,
    Sigma0Table,
    sigma0_linear,
    water_offset_db,
)


class TestAsymmetricEma(unittest.TestCase):
    def test_step_uses_alpha_down_on_fall(self) -> None:
        prev = 1.0
        sample = 0.0
        up = asymmetric_ema_step(prev, 2.0, 0.15, 0.45)
        down = asymmetric_ema_step(prev, sample, 0.15, 0.45)
        # Rise: 1*(1-0.15)+2*0.15 = 1.15
        self.assertAlmostEqual(up, 1.15, places=9)
        # Fall: 1*(1-0.45)+0 = 0.55
        self.assertAlmostEqual(down, 0.55, places=9)

    def test_series_asymmetric_below_symmetric(self) -> None:
        report = case_asymmetric_ema_drops_faster()
        self.assertTrue(report.ok, msg=str(report.metrics))
        self.assertLess(
            report.metrics["asymmetric_final"],
            report.metrics["symmetric_final"],
        )


class TestFootprintMix(unittest.TestCase):
    def test_mix_between_veg_and_water(self) -> None:
        report = case_footprint_mix_land_water()
        self.assertTrue(report.ok, msg=str(report.metrics))
        self.assertGreater(report.metrics["point_over_mixed_db"], 0.5)

    def test_single_class_equals_point(self) -> None:
        table = Sigma0Table.builtin("X", 3)
        g = math.radians(12.0)
        point = sigma0_linear(SURF_VEGETATION, g, table)
        mixed = footprint_mix_sigma0([SURF_VEGETATION] * 5, g, table)
        self.assertAlmostEqual(point, mixed, places=12)


class TestSeaState(unittest.TestCase):
    def test_water_offsets(self) -> None:
        self.assertEqual(water_offset_db(3), 0.0)
        self.assertEqual(water_offset_db(5), 4.0)
        self.assertEqual(water_offset_db(0), -8.0)
        report = case_sea_state_water_scale()
        self.assertTrue(report.ok, msg=str(report.metrics))

    def test_surf_table_json_keeps_sea_state(self) -> None:
        # Packaged table has water=-22 @ ss3; loading with ss5 must raise water.
        import json
        import os
        import tempfile

        root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        path = os.path.join(root, "RadarData", "SurfaceTable.json")
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        data["sea_state"] = 5
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "surf.json")
            with open(out, "w", encoding="utf-8") as handle:
                json.dump(data, handle)
            table = Sigma0Table.from_json(out)
        g = math.radians(30.0)
        water = sigma0_linear(SURF_WATER, g, table)
        base = Sigma0Table.builtin("X", 3)
        water3 = sigma0_linear(SURF_WATER, g, base)
        self.assertGreater(water / water3, 2.0)


class TestSuite(unittest.TestCase):
    def test_all_ok(self) -> None:
        report = run_clutter_sharpen_validation()
        self.assertTrue(report["all_ok"], msg=str(report))


if __name__ == "__main__":
    unittest.main()
