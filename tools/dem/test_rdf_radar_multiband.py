#!/usr/bin/env python3
"""Multi-band channel, RCS vs λ, aperture retune, and hop parity with Enforce."""

from __future__ import annotations

import math
import unittest

from rdf_radar_channel import (
    scale_aperture_to_frequency,
    scale_rcs_for_wavelength,
    scan_frequency_hz,
    select_band_index,
)
from rdf_radar_physics import C_LIGHT_M_S, get_preset


class TestScaleRcsForWavelength(unittest.TestCase):
    def test_x_band_shell_stays_optical(self) -> None:
        lam = C_LIGHT_M_S / 9.0e9
        optical = 0.01
        scaled = scale_rcs_for_wavelength(optical, 0.15, lam)
        self.assertAlmostEqual(scaled, optical, places=8)

    def test_vhf_shell_drops_into_rayleigh(self) -> None:
        lam = C_LIGHT_M_S / 160.0e6
        optical = 0.01
        scaled = scale_rcs_for_wavelength(optical, 0.15, lam)
        a = 0.075
        ka = 2.0 * math.pi * a / lam
        expected = optical * (ka / 10.0) ** 4
        if expected < 1.0e-8:
            expected = 1.0e-8
        self.assertAlmostEqual(scaled, expected, places=12)
        self.assertLess(scaled, optical * 0.001)

    def test_vhf_vehicle_near_optical(self) -> None:
        lam = C_LIGHT_M_S / 160.0e6
        optical = 20.0
        scaled = scale_rcs_for_wavelength(optical, 5.0, lam)
        self.assertGreater(scaled, optical * 0.3)
        self.assertLess(scaled, optical)

    def test_zero_wavelength_passthrough(self) -> None:
        self.assertAlmostEqual(scale_rcs_for_wavelength(12.0, 5.0, 0.0), 12.0)


class TestScaleApertureToFrequency(unittest.TestCase):
    def test_double_frequency_adds_six_db(self) -> None:
        hw = get_preset("shorad")
        doubled = scale_aperture_to_frequency(hw, hw.frequency_hz * 2.0)
        self.assertAlmostEqual(
            doubled.antenna_gain_dbi,
            hw.antenna_gain_dbi + 6.020599913,
            places=5,
        )
        self.assertAlmostEqual(
            doubled.az_beamwidth_deg,
            hw.az_beamwidth_deg * 0.5,
            places=6,
        )

    def test_hardware_at_frequency_does_not_scale_gain(self) -> None:
        from rdf_radar_channel import hardware_at_frequency

        hw = get_preset("shorad")
        tuned = hardware_at_frequency(hw, 160.0e6)
        self.assertAlmostEqual(tuned.antenna_gain_dbi, hw.antenna_gain_dbi)
        self.assertAlmostEqual(tuned.az_beamwidth_deg, hw.az_beamwidth_deg)


class TestScanFrequencyHop(unittest.TestCase):
    def test_hop_off_stays_on_center(self) -> None:
        f = scan_frequency_hz(9.0e9, [9.3e9], 7, False)
        self.assertAlmostEqual(f, 9.0e9)

    def test_hop_set_cycles(self) -> None:
        hops = [9.0e9, 9.3e9, 9.6e9]
        self.assertAlmostEqual(scan_frequency_hz(9.0e9, hops, 0, True), 9.0e9)
        self.assertAlmostEqual(scan_frequency_hz(9.0e9, hops, 1, True), 9.3e9)
        self.assertAlmostEqual(scan_frequency_hz(9.0e9, hops, 2, True), 9.6e9)
        self.assertAlmostEqual(scan_frequency_hz(9.0e9, hops, 3, True), 9.0e9)

    def test_empty_set_synthesizes_pair(self) -> None:
        center = 9.0e9
        even = scan_frequency_hz(center, None, 0, True, 1.05)
        odd = scan_frequency_hz(center, None, 1, True, 1.05)
        self.assertAlmostEqual(even, center)
        self.assertAlmostEqual(odd, center * 1.05)


class TestSelectBandIndex(unittest.TestCase):
    def test_search_track_fire_control(self) -> None:
        self.assertEqual(select_band_index(0, 0, 1, 1, 2), 0)
        self.assertEqual(select_band_index(1, 0, 1, 1, 2), 1)
        self.assertEqual(select_band_index(2, 0, 1, 1, 2), 1)

    def test_out_of_range_falls_back_to_zero(self) -> None:
        self.assertEqual(select_band_index(2, 0, 5, 9, 2), 0)


if __name__ == "__main__":
    unittest.main()
