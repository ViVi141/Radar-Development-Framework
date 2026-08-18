#!/usr/bin/env python3
"""Pulse-eclipsing minimum range (近程盲区) matches RDF_RadarHardware."""

from __future__ import annotations

import unittest

from rdf_radar_physics import C_LIGHT_M_S, RadarHardware, get_preset


class TestPulseBlindZone(unittest.TestCase):
    def test_shorad_half_us_pulse(self) -> None:
        hw = RadarHardware(pulse_width_s=0.5e-6, receiver_recovery_s=0.0)
        expected = C_LIGHT_M_S * 0.5e-6 * 0.5
        self.assertAlmostEqual(hw.min_detectable_range_m(), expected, places=6)
        self.assertGreater(hw.min_detectable_range_m(), 74.0)
        self.assertLess(hw.min_detectable_range_m(), 76.0)

    def test_p18_six_us_pulse(self) -> None:
        hw = get_preset("p18")
        expected = C_LIGHT_M_S * hw.pulse_width_s * 0.5
        self.assertAlmostEqual(hw.min_detectable_range_m(), expected, places=4)
        self.assertGreater(hw.min_detectable_range_m(), 890.0)
        self.assertLess(hw.min_detectable_range_m(), 910.0)

    def test_recovery_adds_to_pulse(self) -> None:
        hw = RadarHardware(pulse_width_s=1.0e-6, receiver_recovery_s=0.5e-6)
        expected = C_LIGHT_M_S * 1.5e-6 * 0.5
        self.assertAlmostEqual(hw.min_detectable_range_m(), expected, places=6)

    def test_chirp_does_not_shrink_rmin(self) -> None:
        wide = RadarHardware(
            pulse_width_s=6.0e-6,
            chirp_bandwidth_hz=2.0e6,
            enable_pulse_compression=True,
        )
        uncoded = RadarHardware(
            pulse_width_s=6.0e-6,
            chirp_bandwidth_hz=0.0,
            enable_pulse_compression=False,
        )
        self.assertAlmostEqual(
            wide.min_detectable_range_m(),
            uncoded.min_detectable_range_m(),
            places=6,
        )
        self.assertLess(wide.range_resolution_m(), uncoded.range_resolution_m())


if __name__ == "__main__":
    unittest.main()
