#!/usr/bin/env python3
"""Band tag, per-band σ⁰, rain, and foliage-attenuation parity with Enforce."""

from __future__ import annotations

import unittest

from rdf_radar_channel import band_for_frequency
from rdf_radar_materials import (
    SURF_VEGETATION,
    SURF_WATER,
    Sigma0Table,
    attenuation_scale_for_band,
    sigma0_ref_linear,
)
from rdf_radar_physics import get_preset
from rdf_radar_systems import rain_one_way_db_per_km


class TestBandForFrequency(unittest.TestCase):
    def test_vhf_p18(self) -> None:
        self.assertEqual(band_for_frequency(160e6), "VHF")

    def test_l_uhf(self) -> None:
        self.assertEqual(band_for_frequency(0.4e9), "L")
        self.assertEqual(band_for_frequency(1.5e9), "L")

    def test_s_c_x(self) -> None:
        self.assertEqual(band_for_frequency(3.0e9), "S")
        self.assertEqual(band_for_frequency(5.5e9), "C")
        self.assertEqual(band_for_frequency(9.0e9), "X")
        self.assertEqual(band_for_frequency(10.5e9), "X")

    def test_presets_match_carrier(self) -> None:
        self.assertEqual(band_for_frequency(get_preset("p18").frequency_hz), "VHF")
        self.assertEqual(band_for_frequency(get_preset("tps43").frequency_hz), "S")
        self.assertEqual(band_for_frequency(get_preset("shorad").frequency_hz), "X")


class TestBandSigma0(unittest.TestCase):
    def test_vhf_vegetation_higher_than_x(self) -> None:
        vhf = Sigma0Table.builtin("VHF", 3)
        x = Sigma0Table.builtin("X", 3)
        self.assertGreater(
            sigma0_ref_linear(SURF_VEGETATION, vhf),
            sigma0_ref_linear(SURF_VEGETATION, x),
        )

    def test_vhf_water_lower_than_x(self) -> None:
        vhf = Sigma0Table.builtin("VHF", 3)
        x = Sigma0Table.builtin("X", 3)
        self.assertLess(
            sigma0_ref_linear(SURF_WATER, vhf),
            sigma0_ref_linear(SURF_WATER, x),
        )

    def test_unknown_band_falls_back_to_x(self) -> None:
        fallback = Sigma0Table.builtin("Ku", 3)
        x = Sigma0Table.builtin("X", 3)
        self.assertEqual(fallback.band, "X")
        self.assertAlmostEqual(
            sigma0_ref_linear(SURF_VEGETATION, fallback),
            sigma0_ref_linear(SURF_VEGETATION, x),
        )


class TestBandRainAndAttenuation(unittest.TestCase):
    def test_rain_vhf_weaker_than_x(self) -> None:
        x = rain_one_way_db_per_km(25.0, 9.0e9)
        vhf = rain_one_way_db_per_km(25.0, 160e6)
        self.assertGreater(x, 0.0)
        self.assertLess(vhf, x)
        scale = vhf / x
        self.assertAlmostEqual(scale, (1.0**1.2) / (9.0**1.2), places=6)

    def test_shorad_rain_scale_is_unity(self) -> None:
        x_ref = rain_one_way_db_per_km(25.0, 9.0e9)
        shorad = rain_one_way_db_per_km(25.0, get_preset("shorad").frequency_hz)
        self.assertAlmostEqual(shorad / x_ref, 1.0, places=6)

    def test_foliage_attenuation_vhf_is_weaker(self) -> None:
        self.assertAlmostEqual(attenuation_scale_for_band("X"), 1.0)
        self.assertAlmostEqual(attenuation_scale_for_band("VHF"), 0.20)
        self.assertLess(
            attenuation_scale_for_band("VHF") * 0.5,
            attenuation_scale_for_band("X") * 0.5,
        )


if __name__ == "__main__":
    unittest.main()
