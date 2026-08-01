#!/usr/bin/env python3
"""Unit tests for MTD Doppler bank + rotor micro-Doppler spectrum."""

from __future__ import annotations

import unittest

from rdf_radar_physics import (
    get_preset,
    max_mtd_bin_gain,
    max_mtd_spectrum_gain,
    max_mti_canceller_spectrum_gain,
    mti_apply_clutter,
    mti_apply_target,
    mti_three_pulse_gain,
    mti_two_pulse_gain,
    mtd_bin_power_gain,
    rotor_doppler_spectrum,
    suggest_mti_clutter_floor,
)


class TestMtdBank(unittest.TestCase):
    def test_two_pulse_zero_radial_null(self) -> None:
        g = mti_two_pulse_gain(0.0, 4000.0)
        self.assertLess(g, 1.0e-6)

    def test_mtd_zero_body_picks_bin_zero(self) -> None:
        gain, bin_i = max_mtd_bin_gain(0.0, 4000.0, 16)
        self.assertEqual(bin_i, 0)
        self.assertGreater(gain, 0.99)

    def test_mtd_moving_picks_nonzero_bin(self) -> None:
        # fd ≈ PRF/4 → mid-band
        gain, bin_i = max_mtd_bin_gain(1000.0, 4000.0, 16)
        self.assertNotEqual(bin_i, 0)
        self.assertGreater(gain, 0.5)

    def test_bin_peak_normalized(self) -> None:
        # Exact bin centre for k=4 of N=16 → fd/PRF = 4/16 = 0.25
        g = mtd_bin_power_gain(1000.0, 4000.0, 4, 16)
        self.assertAlmostEqual(g, 1.0, places=5)

    def test_tangential_heli_sideband_survives_mtd(self) -> None:
        hw = get_preset("shorad")
        hw.enable_mti = True
        hw.mti_mode = "mtd_bank"
        hw.doppler_bin_count = 16
        # Body vr=0 but UH-1-class tip speed → non-zero bin power.
        tgt = mti_apply_target(
            hw,
            1.0,
            0.0,
            tip_speed_m_s=220.0,
            rotor_rcs_fraction=0.35,
            hub_width_m_s=40.0,
            blade_count=2,
        )
        self.assertGreater(tgt, 0.05)

        lines, powers = rotor_doppler_spectrum(
            0.0, hw.wavelength_m, 220.0, 0.35, 40.0, blade_count=2
        )
        gain, bin_i, _fd = max_mtd_spectrum_gain(
            lines,
            powers,
            hw.prf_hz,
            hw.doppler_bin_count,
            hw.mti_clutter_floor,
            hw.mtd_clutter_leakage,
        )
        self.assertNotEqual(bin_i, 0)
        self.assertGreater(gain, 0.05)

        # Clutter in that non-zero bin is leakage, not the global floor path.
        clut = mti_apply_clutter(hw, 1.0, 0.0, target_doppler_bin=bin_i)
        self.assertLess(clut, hw.mti_clutter_floor * 10.0)
        # Sideband fraction is smaller than body, but SINR wins in clear bins.
        self.assertGreater(tgt, clut * 10.0)

    def test_blade_harmonics_add_interior_lines(self) -> None:
        from rdf_radar_physics import rotor_disk_aspect_scale

        lines2, _ = rotor_doppler_spectrum(
            0.0, 0.03, 220.0, 0.35, 40.0, blade_count=2
        )
        lines5, _ = rotor_doppler_spectrum(
            0.0, 0.03, 220.0, 0.35, 40.0, blade_count=5
        )
        self.assertGreater(len(lines5), len(lines2))
        self.assertAlmostEqual(rotor_disk_aspect_scale(0.0), 1.0, places=3)
        self.assertLess(rotor_disk_aspect_scale(80.0), 0.45)

    def test_stationary_vehicle_stays_clutter_limited(self) -> None:
        hw = get_preset("shorad")
        hw.enable_mti = True
        hw.mti_mode = "mtd_bank"
        tgt = mti_apply_target(hw, 1.0, 0.0)
        self.assertGreater(tgt, 0.99)
        clut = mti_apply_clutter(hw, 1.0, 0.0, target_doppler_bin=0)
        self.assertAlmostEqual(clut, hw.mti_clutter_floor, places=9)

    def test_legacy_twopulse_default_unchanged(self) -> None:
        hw = get_preset("shorad")
        hw.enable_mti = True
        hw.mti_mode = "twopulse"
        self.assertLess(mti_apply_target(hw, 1.0, 0.0), 1.0e-5)

    def test_three_pulse_deeper_null_near_zero(self) -> None:
        # At small fd, three-pulse (sin^4) is much smaller than two-pulse (sin^2).
        prf = 4000.0
        fd = 40.0
        g2 = mti_two_pulse_gain(fd, prf)
        g3 = mti_three_pulse_gain(fd, prf)
        self.assertAlmostEqual(g3, g2 * g2, places=9)
        self.assertLess(g3, g2 * 0.1)

    def test_stagger_max_deblinds_classic(self) -> None:
        lam = 0.03
        prf0 = 4000.0
        prf1 = 4800.0
        blind0 = 0.5 * lam * prf0
        fd = 2.0 * blind0 / lam  # body on PRF0 blind
        lines = [fd]
        powers = [1.0]
        g0, _ = max_mti_canceller_spectrum_gain(lines, powers, [prf0], "twopulse")
        g_max, _ = max_mti_canceller_spectrum_gain(
            lines, powers, [prf0, prf1], "twopulse"
        )
        self.assertLess(g0, 1.0e-4)
        self.assertGreater(g_max, 0.05)

    def test_suggest_mti_clutter_floor_order(self) -> None:
        f2 = suggest_mti_clutter_floor(0.5, 0.03, 4000.0, 1)
        f3 = suggest_mti_clutter_floor(0.5, 0.03, 4000.0, 2)
        self.assertGreater(f2, f3)
        self.assertGreaterEqual(f2, 1.0e-6)
        self.assertLessEqual(f2, 0.5)

    def test_prf_stagger_cycles(self) -> None:
        hw = get_preset("shorad")
        hw.prf_hz = 4000.0
        hw.prf_set_hz = [4000.0, 4800.0]
        self.assertAlmostEqual(hw.active_prf_hz(0), 4000.0)
        self.assertAlmostEqual(hw.active_prf_hz(1), 4800.0)
        self.assertAlmostEqual(hw.active_prf_hz(2), 4000.0)
        blind0 = hw.wavelength_m * 4000.0 / 2.0
        blind1 = hw.wavelength_m * 4800.0 / 2.0
        self.assertGreater(abs(blind0 - blind1), 1.0)


if __name__ == "__main__":
    unittest.main()
