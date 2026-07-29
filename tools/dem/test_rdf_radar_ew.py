#!/usr/bin/env python3
"""Unit tests for RDF noise-jammer coupling and burn-through helpers."""

from __future__ import annotations

import math
import unittest

import numpy as np

from rdf_radar_ew import (
    EWContext,
    NoiseJamCoupling,
    NoiseJammer,
    burn_through_range_m,
)
from rdf_radar_physics import preset_shorad_x


class TestBurnThrough(unittest.TestCase):
    def test_no_jam_keeps_clean_range(self) -> None:
        self.assertAlmostEqual(
            burn_through_range_m(4000.0, 1e-13, 0.0),
            4000.0,
            places=6,
        )

    def test_equal_jam_halves_fourth_root(self) -> None:
        # N/(N+J)=0.5 → Reff = R0 * 0.5^0.25
        expected = 4000.0 * (0.5**0.25)
        self.assertAlmostEqual(
            burn_through_range_m(4000.0, 1.0, 1.0),
            expected,
            places=6,
        )

    def test_dominating_jam_collapses_range(self) -> None:
        reff = burn_through_range_m(4000.0, 1e-13, 1.0)
        self.assertLess(reff, 50.0)


class TestNoiseJamCoupling(unittest.TestCase):
    def setUp(self) -> None:
        self.hw = preset_shorad_x()
        self.az = np.linspace(-math.pi, math.pi, 72, endpoint=False)
        self.ranges = np.linspace(100.0, 4000.0, 16)
        self.ctx = EWContext(
            hardware=self.hw,
            radar_x_m=0.0,
            radar_z_m=0.0,
            az_rad=self.az,
            range_centers_m=self.ranges,
            noise_w=self.hw.noise_power_w(),
        )
        self.jammer = NoiseJammer(
            x_m=2000.0,
            z_m=0.0,
            erp_w=1000.0,
            bandwidth_hz=5.0e6,
        )

    def test_search_avg_is_isotropic(self) -> None:
        self.jammer.configure_gameplay_search_avg(sidelobe_level_db=-40.0)
        coupling = self.jammer.coupling_for_azimuths(self.ctx)
        self.assertEqual(self.jammer.coupling_mode, NoiseJamCoupling.SEARCH_AVG)
        self.assertTrue(np.allclose(coupling, coupling[0]))
        duty = self.hw.az_beamwidth_deg / 360.0
        side = 10.0 ** (-40.0 / 10.0)
        expected = duty + (1.0 - duty) * side
        self.assertAlmostEqual(float(coupling[0]), expected, places=8)

    def test_mainlobe_only_zero_off_boresight(self) -> None:
        self.jammer.configure_mainlobe_only()
        coupling = self.jammer.coupling_for_azimuths(self.ctx)
        # Jammer at az=0; opposite side must be zero.
        opp_i = int(np.argmin(np.abs(self.az - math.pi)))
        self.assertEqual(float(coupling[opp_i]), 0.0)
        on_i = int(np.argmin(np.abs(self.az - 0.0)))
        self.assertEqual(float(coupling[on_i]), 1.0)

    def test_beam_sidelobe_floor(self) -> None:
        self.jammer.configure_physics_beam(sidelobe_level_db=-25.0)
        coupling = self.jammer.coupling_for_azimuths(self.ctx)
        opp_i = int(np.argmin(np.abs(self.az - math.pi)))
        side = 10.0 ** (-25.0 / 10.0)
        self.assertAlmostEqual(float(coupling[opp_i]), side, places=8)

    def test_coupling_gain_scales_peak(self) -> None:
        self.jammer.configure_gameplay_search_avg()
        base = self.jammer.peak_jammer_power_w(self.ctx)
        self.jammer.coupling_gain = 0.25
        scaled = self.jammer.peak_jammer_power_w(self.ctx)
        self.assertAlmostEqual(scaled, base * 0.25, places=12)

    def test_search_avg_softer_than_mainlobe_stare(self) -> None:
        power = np.zeros((self.az.size, self.ranges.size), dtype=np.float64)
        self.jammer.configure_physics_beam(sidelobe_level_db=-25.0)
        hard = self.jammer.apply(power, self.ctx)
        self.jammer.configure_gameplay_search_avg(sidelobe_level_db=-40.0)
        soft = self.jammer.apply(power, self.ctx)
        # On jammer boresight, search-avg is << full mainlobe stare.
        on_i = int(np.argmin(np.abs(self.az - 0.0)))
        self.assertLess(
            float(soft[on_i].mean()),
            float(hard[on_i].mean()) * 0.05,
        )


if __name__ == "__main__":
    unittest.main()
