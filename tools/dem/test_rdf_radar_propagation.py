#!/usr/bin/env python3
"""Unit tests for LOS two-ray / refraction / PRF ambiguity math (no waveform)."""

from __future__ import annotations

import math
import unittest

from rdf_radar_channel import (
    MultipathModel,
    fold_doppler_ambiguous,
    fold_range_ambiguous,
    fresnel_reflection_coeff_h,
    horizon_soft_factor,
    radio_horizon_range_m,
    refraction_elevation_bias_deg,
    roughness_damp_reflection_abs,
    two_ray_multipath_factor,
)
from rdf_radar_physics import get_preset


class TestTwoRayMultipath(unittest.TestCase):
    def test_disabled_is_unity(self) -> None:
        mp = MultipathModel(enabled=False)
        self.assertEqual(mp.power_factor(0.03, 2000.0, 10.0, 20.0), 1.0)

    def test_high_target_skips(self) -> None:
        f = two_ray_multipath_factor(0.03, 2000.0, 10.0, 900.0, max_height_m=600.0)
        self.assertEqual(f, 1.0)

    def test_lobes_vary_with_range(self) -> None:
        a = two_ray_multipath_factor(0.03, 1500.0, 12.0, 40.0, -0.5)
        b = two_ray_multipath_factor(0.03, 2500.0, 12.0, 40.0, -0.5)
        self.assertNotAlmostEqual(a, b, places=6)
        self.assertGreaterEqual(a, 0.08)
        self.assertLessEqual(a, 4.0)

    def test_dielectric_fresnel_negative_for_land(self) -> None:
        gamma = fresnel_reflection_coeff_h(15.0, 0.02)
        self.assertLess(gamma, 0.0)
        self.assertGreater(gamma, -1.01)

    def test_roughness_reduces_abs_gamma(self) -> None:
        smooth = roughness_damp_reflection_abs(0.9, 0.0, 0.05)
        rough = roughness_damp_reflection_abs(0.9, 0.4, 0.05)
        self.assertLess(rough, smooth)

    def test_dielectric_path_runs(self) -> None:
        mp = MultipathModel(enabled=True)
        hw = get_preset("shorad")
        f = mp.power_factor_from_dielectric(
            hw.wavelength_m, 3000.0, 15.0, 50.0, eps_r=15.0, roughness=0.2
        )
        self.assertGreaterEqual(f, mp.min_factor)
        self.assertLessEqual(f, mp.max_factor)


class TestRefraction(unittest.TestCase):
    def test_horizon_grows_with_height(self) -> None:
        low = radio_horizon_range_m(10.0, 10.0)
        high = radio_horizon_range_m(100.0, 100.0)
        self.assertGreater(high, low)

    def test_inside_horizon_unity(self) -> None:
        h = radio_horizon_range_m(20.0, 50.0)
        self.assertEqual(horizon_soft_factor(0.5 * h, h), 1.0)

    def test_beyond_horizon_rolls_off(self) -> None:
        h = radio_horizon_range_m(20.0, 50.0)
        f = horizon_soft_factor(2.0 * h, h)
        self.assertLess(f, 1.0)
        self.assertGreaterEqual(f, 0.02)

    def test_elevation_bias_grows_with_range(self) -> None:
        b1 = refraction_elevation_bias_deg(10000.0)
        b2 = refraction_elevation_bias_deg(50000.0)
        self.assertGreater(b2, b1)
        self.assertGreater(b1, 0.0)


class TestAmbiguityFold(unittest.TestCase):
    def test_range_fold_identity_inside(self) -> None:
        self.assertAlmostEqual(fold_range_ambiguous(5000.0, 37500.0), 5000.0)

    def test_range_fold_wraps(self) -> None:
        runamb = 10000.0
        self.assertAlmostEqual(fold_range_ambiguous(25000.0, runamb), 5000.0)

    def test_doppler_fold_into_half_prf(self) -> None:
        prf = 4000.0
        folded = fold_doppler_ambiguous(4500.0, prf)
        self.assertGreaterEqual(folded, -0.5 * prf)
        self.assertLess(folded, 0.5 * prf)

    def test_shorad_unambiguous_range_large(self) -> None:
        hw = get_preset("shorad")
        # High PRF → fold is a no-op for typical instrumented ranges.
        self.assertGreater(hw.unambiguous_range_m, 10000.0)


if __name__ == "__main__":
    unittest.main()
