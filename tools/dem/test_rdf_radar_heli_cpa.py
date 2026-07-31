#!/usr/bin/env python3
"""End-to-end UH-1 CPA flyby: TwoPulse vs MtdBank paint probability.

Simulates a constant-speed tangential flyby past a SHORAD site. At CPA the
body radial speed is ~0; TwoPulse MTI nulls the fuselage while MtdBank keeps
rotor sidebands in a clear Doppler bin.
"""

from __future__ import annotations

import math
import unittest

from rdf_radar_physics import (
    doppler_hz,
    get_preset,
    max_mtd_spectrum_gain,
    mti_apply_clutter,
    mti_apply_target,
    mti_two_pulse_gain,
    processed_power_w,
    received_power_w,
    rotor_doppler_spectrum,
)


def _flyby_samples(
    *,
    cpa_range_m: float = 2500.0,
    speed_m_s: float = 55.0,
    altitude_m: float = 120.0,
    half_span_m: float = 4000.0,
    step_m: float = 80.0,
) -> list[tuple[float, float, float]]:
    """Return (range_m, radial_m_s, along_track_m) samples through CPA."""
    samples: list[tuple[float, float, float]] = []
    s = -half_span_m
    while s <= half_span_m + 0.1:
        range_m = math.sqrt(cpa_range_m * cpa_range_m + s * s + altitude_m * altitude_m)
        radial = -(s * speed_m_s) / max(range_m, 1.0)
        samples.append((range_m, radial, s))
        s += step_m
    return samples


def _detect_fraction(
    mti_mode: str,
    *,
    tip_m_s: float,
    rotor_frac: float,
    hub_m_s: float,
    snr_gate_db: float = 8.0,
    clutter_sigma_m2: float = 800.0,
    cpa_window_m: float = 200.0,
) -> float:
    """Paint fraction inside the CPA window (|along-track| ≤ cpa_window_m)."""
    hw = get_preset("shorad")
    hw.enable_mti = True
    hw.mti_mode = mti_mode
    hw.doppler_bin_count = 16
    hits = 0
    total = 0
    for range_m, radial, along in _flyby_samples():
        if abs(along) > cpa_window_m:
            continue
        pattern = 1.0
        rf = received_power_w(hw, 16.0, range_m, pattern)
        proc = processed_power_w(hw, rf)
        tgt = mti_apply_target(
            hw,
            proc,
            radial,
            tip_speed_m_s=tip_m_s,
            rotor_rcs_fraction=rotor_frac,
            hub_width_m_s=hub_m_s,
        )

        body_fd = doppler_hz(radial, hw.wavelength_m)
        bin_i = 0
        if mti_mode == "mtd_bank":
            lines, powers = rotor_doppler_spectrum(
                body_fd,
                hw.wavelength_m,
                tip_m_s,
                rotor_frac,
                hub_m_s,
            )
            _g, bin_i, _fd = max_mtd_spectrum_gain(
                lines,
                powers,
                hw.prf_hz,
                hw.doppler_bin_count,
                hw.mti_clutter_floor,
                hw.mtd_clutter_leakage,
            )
        else:
            bin_i = 0
            _ = mti_two_pulse_gain(body_fd, hw.prf_hz)

        clut_rf = received_power_w(hw, clutter_sigma_m2, range_m, pattern)
        clut_proc = processed_power_w(hw, clut_rf)
        clut = mti_apply_clutter(hw, clut_proc, 0.0, target_doppler_bin=bin_i)
        noise = processed_power_w(hw, hw.noise_power_w())
        snr = tgt / max(noise + clut, 1e-30)
        snr_db = 10.0 * math.log10(max(snr, 1e-30))
        total += 1
        if snr_db >= snr_gate_db:
            hits += 1
    if total <= 0:
        return 0.0
    return hits / float(total)


class TestHeliCpaPaint(unittest.TestCase):
    def test_twopulse_collapses_near_cpa(self) -> None:
        pd = _detect_fraction(
            "twopulse", tip_m_s=220.0, rotor_frac=0.35, hub_m_s=40.0
        )
        self.assertLess(pd, 0.15)

    def test_mtd_bank_keeps_usable_paint(self) -> None:
        pd = _detect_fraction(
            "mtd_bank", tip_m_s=220.0, rotor_frac=0.35, hub_m_s=40.0
        )
        self.assertGreater(pd, 0.45)

    def test_mtd_beats_twopulse_on_same_flyby(self) -> None:
        pd_tp = _detect_fraction(
            "twopulse", tip_m_s=220.0, rotor_frac=0.35, hub_m_s=40.0
        )
        pd_mtd = _detect_fraction(
            "mtd_bank", tip_m_s=220.0, rotor_frac=0.35, hub_m_s=40.0
        )
        self.assertGreater(pd_mtd, pd_tp + 0.30)

    def test_stationary_truck_still_clutter_limited(self) -> None:
        hw = get_preset("shorad")
        hw.enable_mti = True
        hw.mti_mode = "mtd_bank"
        tgt = mti_apply_target(hw, 1.0, 0.0)
        clut = mti_apply_clutter(hw, 1.0, 0.0, target_doppler_bin=0)
        self.assertGreater(tgt, 0.99)
        self.assertAlmostEqual(clut, hw.mti_clutter_floor, places=9)


if __name__ == "__main__":
    unittest.main()
