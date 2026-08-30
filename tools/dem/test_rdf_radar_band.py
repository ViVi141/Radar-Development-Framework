#!/usr/bin/env python3
"""Band tag, per-band σ⁰, rain, and foliage-attenuation parity with Enforce."""

from __future__ import annotations

import unittest

from rdf_radar_channel import band_for_frequency
from rdf_radar_materials import (
    SURF_HARD,
    SURF_VEGETATION,
    SURF_WATER,
    Sigma0Table,
    attenuation_scale_for_band,
    sigma0_ref_linear,
)
from rdf_radar_physics import atmospheric_one_way_db_per_km, get_preset
from rdf_radar_systems import rain_one_way_db_per_km


class TestBandForFrequency(unittest.TestCase):
    def test_vhf_p18(self) -> None:
        self.assertEqual(band_for_frequency(160e6), "VHF")

    def test_uhf_vs_l(self) -> None:
        self.assertEqual(band_for_frequency(0.4e9), "UHF")
        self.assertEqual(band_for_frequency(1.5e9), "L")

    def test_s_c_x(self) -> None:
        self.assertEqual(band_for_frequency(3.0e9), "S")
        self.assertEqual(band_for_frequency(5.5e9), "C")
        self.assertEqual(band_for_frequency(9.0e9), "X")
        self.assertEqual(band_for_frequency(10.5e9), "X")

    def test_ku_k_ka(self) -> None:
        self.assertEqual(band_for_frequency(16.0e9), "Ku")
        self.assertEqual(band_for_frequency(24.0e9), "K")
        self.assertEqual(band_for_frequency(35.0e9), "Ka")

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

    def test_ku_water_higher_than_x(self) -> None:
        ku = Sigma0Table.builtin("Ku", 3)
        x = Sigma0Table.builtin("X", 3)
        self.assertEqual(ku.band, "KU")
        self.assertGreater(
            sigma0_ref_linear(SURF_WATER, ku),
            sigma0_ref_linear(SURF_WATER, x),
        )

    def test_ka_hard_higher_than_ku(self) -> None:
        ka = Sigma0Table.builtin("Ka", 3)
        ku = Sigma0Table.builtin("Ku", 3)
        self.assertGreater(
            sigma0_ref_linear(SURF_HARD, ka),
            sigma0_ref_linear(SURF_HARD, ku),
        )

    def test_unknown_band_falls_back_to_x(self) -> None:
        fallback = Sigma0Table.builtin("ZZ", 3)
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

    def test_rain_ku_stronger_than_x(self) -> None:
        x = rain_one_way_db_per_km(25.0, 9.0e9)
        ku = rain_one_way_db_per_km(25.0, 16.0e9)
        ka = rain_one_way_db_per_km(25.0, 35.0e9)
        self.assertGreater(ku, x)
        self.assertGreater(ka, ku)

    def test_foliage_scale_monotonic(self) -> None:
        self.assertAlmostEqual(attenuation_scale_for_band("X"), 1.0)
        self.assertAlmostEqual(attenuation_scale_for_band("VHF"), 0.20)
        self.assertAlmostEqual(attenuation_scale_for_band("Ku"), 1.35)
        self.assertAlmostEqual(attenuation_scale_for_band("Ka"), 2.40)
        self.assertLess(
            attenuation_scale_for_band("VHF") * 0.5,
            attenuation_scale_for_band("X") * 0.5,
        )
        self.assertLess(
            attenuation_scale_for_band("X"),
            attenuation_scale_for_band("Ku"),
        )

    def test_atmosphere_ku_vs_x_and_k_bump(self) -> None:
        x = atmospheric_one_way_db_per_km(9.0e9)
        ku = atmospheric_one_way_db_per_km(16.0e9)
        k_low = atmospheric_one_way_db_per_km(20.0e9)
        k_line = atmospheric_one_way_db_per_km(24.0e9)
        ka = atmospheric_one_way_db_per_km(35.0e9)
        self.assertAlmostEqual(x, 0.020)
        self.assertAlmostEqual(ku, 0.045)
        self.assertGreater(k_line, k_low)
        self.assertGreater(ka, k_line)


if __name__ == "__main__":
    unittest.main()
