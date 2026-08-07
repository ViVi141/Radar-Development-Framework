#!/usr/bin/env python3
"""Unit tests for sidelobe floor + polarization match tables."""

from __future__ import annotations

import math
import unittest

import numpy as np

from rdf_radar_ew import EWContext, NoiseJamCoupling, NoiseJammer
from rdf_radar_physics import (
    apply_two_way_sidelobe_floor,
    combined_two_way_pattern_gain,
    db_to_lin,
    gaussian_beam_gain,
    polarization_clutter_factor,
    polarization_match_factor,
    preset_shorad_x,
    resolve_polarization_factor,
    two_way_sidelobe_floor,
)


class TestSidelobeFloor(unittest.TestCase):
    def test_floor_matches_one_way_squared(self) -> None:
        sll_db = -25.0
        expected = db_to_lin(sll_db) ** 2
        self.assertAlmostEqual(two_way_sidelobe_floor(sll_db), expected, places=12)

    def test_boresight_unchanged(self) -> None:
        hw = preset_shorad_x()
        hw.sidelobe_level_db = -25.0
        hw.enable_sidelobe_floor = True
        g = combined_two_way_pattern_gain(hw, elevation_deg=hw.el_boresight_deg, az_offset_deg=0.0)
        self.assertAlmostEqual(g, 1.0, places=6)

    def test_far_off_axis_hits_floor(self) -> None:
        hw = preset_shorad_x()
        hw.sidelobe_level_db = -25.0
        hw.enable_sidelobe_floor = True
        g = combined_two_way_pattern_gain(hw, elevation_deg=hw.el_boresight_deg, az_offset_deg=90.0)
        floor = two_way_sidelobe_floor(-25.0)
        self.assertAlmostEqual(g, floor, places=10)

    def test_disable_floor_allows_tiny_gauss(self) -> None:
        hw = preset_shorad_x()
        hw.sidelobe_level_db = -25.0
        hw.enable_sidelobe_floor = False
        g = combined_two_way_pattern_gain(hw, elevation_deg=hw.el_boresight_deg, az_offset_deg=90.0)
        floor = two_way_sidelobe_floor(-25.0)
        self.assertLess(g, floor * 0.01)

    def test_apply_helper_monotonic(self) -> None:
        raw = gaussian_beam_gain(20.0, 2.0) ** 2
        floored = apply_two_way_sidelobe_floor(raw, -30.0)
        self.assertGreaterEqual(floored, raw)
        self.assertGreaterEqual(floored, two_way_sidelobe_floor(-30.0))


class TestPolarizationMatch(unittest.TestCase):
    def test_projectile_linear_keeps_match(self) -> None:
        self.assertAlmostEqual(polarization_match_factor("H", "PROJECTILE"), 1.0)

    def test_vehicle_linear_depolarizes(self) -> None:
        self.assertAlmostEqual(polarization_match_factor("V", "VEHICLE"), 0.65)

    def test_circular_rain_clutter_reject(self) -> None:
        self.assertAlmostEqual(polarization_clutter_factor("CIRCULAR", 0.0), 1.0)
        self.assertAlmostEqual(polarization_clutter_factor("CIRCULAR", 0.5), 0.20)
        self.assertAlmostEqual(polarization_clutter_factor("H", 0.5), 1.0)

    def test_resolve_multiplies_trim(self) -> None:
        hw = preset_shorad_x()
        hw.polarization_mode = "H"
        hw.polarization_factor = 0.9
        hw.enable_polarization_match = True
        factor = resolve_polarization_factor(hw, "VEHICLE")
        self.assertAlmostEqual(factor, 0.9 * 0.65, places=8)

    def test_resolve_can_disable_match(self) -> None:
        hw = preset_shorad_x()
        hw.polarization_factor = 0.9
        hw.enable_polarization_match = False
        factor = resolve_polarization_factor(hw, "VEHICLE")
        self.assertAlmostEqual(factor, 0.9, places=8)


class TestSlb(unittest.TestCase):
    def setUp(self) -> None:
        self.hw = preset_shorad_x()
        self.az = np.linspace(-math.pi, math.pi, 72, endpoint=False)
        self.ranges = np.linspace(100.0, 4000.0, 8)
        self.ctx = EWContext(
            hardware=self.hw,
            radar_x_m=0.0,
            radar_z_m=0.0,
            az_rad=self.az,
            range_centers_m=self.ranges,
            noise_w=self.hw.noise_power_w(),
        )
        self.jammer = NoiseJammer(x_m=2000.0, z_m=0.0, erp_w=1000.0)

    def test_slb_zeros_beam_sidelobes(self) -> None:
        self.jammer.configure_physics_beam(sidelobe_level_db=-25.0)
        self.jammer.enable_sidelobe_blanking(True)
        coupling = self.jammer.coupling_for_azimuths(self.ctx)
        opp_i = int(np.argmin(np.abs(self.az - math.pi)))
        self.assertEqual(float(coupling[opp_i]), 0.0)
        on_i = int(np.argmin(np.abs(self.az - 0.0)))
        self.assertEqual(float(coupling[on_i]), 1.0)

    def test_slb_search_avg_drops_side_term(self) -> None:
        self.jammer.configure_gameplay_search_avg(sidelobe_level_db=-40.0)
        self.jammer.enable_sidelobe_blanking(True)
        coupling = self.jammer.coupling_for_azimuths(self.ctx)
        duty = self.hw.az_beamwidth_deg / 360.0
        self.assertAlmostEqual(float(coupling[0]), duty, places=8)
        self.assertEqual(self.jammer.coupling_mode, NoiseJamCoupling.SEARCH_AVG)


if __name__ == "__main__":
    unittest.main()
