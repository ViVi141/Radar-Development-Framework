#!/usr/bin/env python3
"""Golden unit tests for RDF track association / alpha-beta / ballistic fit."""

from __future__ import annotations

import math
import unittest
from dataclasses import dataclass

from rdf_radar_track import (
    BallisticState,
    TrackerConfig,
    associate_and_filter,
    cartesian_to_polar,
    confirmed_tracks,
    fit_ballistic_vacuum,
    polar_to_cartesian,
    predict_ballistic,
)


@dataclass
class FakeDetection:
    time_s: float
    scan_number: int
    target_name: str
    measured_range_m: float
    azimuth_deg: float
    radial_speed_m_s: float
    snr_db: float
    detected: bool = True
    elevation_deg: float = 0.0
    doppler_bin: int = -1


class TestPolarCartesian(unittest.TestCase):
    def test_roundtrip_east(self) -> None:
        x, y, z = polar_to_cartesian(1000.0, 0.0, 0.0)
        self.assertAlmostEqual(x, 1000.0, places=6)
        self.assertAlmostEqual(y, 0.0, places=6)
        self.assertAlmostEqual(z, 0.0, places=6)
        r, az, el = cartesian_to_polar(x, y, z)
        self.assertAlmostEqual(r, 1000.0, places=6)
        self.assertAlmostEqual(az, 0.0, places=6)
        self.assertAlmostEqual(el, 0.0, places=6)

    def test_roundtrip_north_elevated(self) -> None:
        x, y, z = polar_to_cartesian(2000.0, 90.0, 30.0)
        r, az, el = cartesian_to_polar(x, y, z)
        self.assertAlmostEqual(r, 2000.0, places=5)
        self.assertAlmostEqual(az, 90.0, places=5)
        self.assertAlmostEqual(el, 30.0, places=5)

    def test_origin_degenerate(self) -> None:
        r, az, el = cartesian_to_polar(0.0, 0.0, 0.0)
        self.assertEqual((r, az, el), (0.0, 0.0, 0.0))


class TestAssociateAndFilter(unittest.TestCase):
    def test_single_target_confirms_across_scans(self) -> None:
        dets = []
        for scan in range(1, 5):
            t = float(scan) * 2.0
            # Closing slowly: range decreases ~50 m per scan.
            rng = 5000.0 - 50.0 * float(scan - 1)
            dets.append(
                FakeDetection(
                    time_s=t,
                    scan_number=scan,
                    target_name="T1",
                    measured_range_m=rng,
                    azimuth_deg=45.0,
                    radial_speed_m_s=-25.0,
                    snr_db=18.0,
                )
            )
        result = associate_and_filter(dets, TrackerConfig(confirm_hits=2, max_misses=3))
        confirmed = confirmed_tracks(result, min_hits=2)
        self.assertEqual(len(confirmed), 1)
        track = confirmed[0]
        self.assertEqual(track.hit_count, 4)
        self.assertEqual(track.last_target_name, "T1")
        self.assertLess(track.miss_count, 1)
        # Filtered range should stay near the last measurement.
        self.assertAlmostEqual(track.range_m, dets[-1].measured_range_m, delta=80.0)

    def test_two_well_separated_targets_two_tracks(self) -> None:
        dets = []
        for scan in range(1, 4):
            t = float(scan)
            dets.append(
                FakeDetection(
                    time_s=t,
                    scan_number=scan,
                    target_name="A",
                    measured_range_m=3000.0,
                    azimuth_deg=10.0,
                    radial_speed_m_s=0.0,
                    snr_db=15.0,
                )
            )
            dets.append(
                FakeDetection(
                    time_s=t,
                    scan_number=scan,
                    target_name="B",
                    measured_range_m=6000.0,
                    azimuth_deg=100.0,
                    radial_speed_m_s=0.0,
                    snr_db=14.0,
                )
            )
        result = associate_and_filter(dets)
        confirmed = confirmed_tracks(result, min_hits=2)
        self.assertEqual(len(confirmed), 2)
        names = sorted(tr.last_target_name for tr in confirmed)
        self.assertEqual(names, ["A", "B"])

    def test_undetected_plots_ignored(self) -> None:
        dets = [
            FakeDetection(
                time_s=0.0,
                scan_number=1,
                target_name="ghost",
                measured_range_m=1000.0,
                azimuth_deg=0.0,
                radial_speed_m_s=0.0,
                snr_db=0.0,
                detected=False,
            )
        ]
        result = associate_and_filter(dets)
        self.assertEqual(len(result.tracks), 0)
        self.assertEqual(len(result.associations), 0)

    def test_gate_rejects_far_azimuth(self) -> None:
        dets = [
            FakeDetection(
                time_s=0.0,
                scan_number=1,
                target_name="T",
                measured_range_m=4000.0,
                azimuth_deg=0.0,
                radial_speed_m_s=0.0,
                snr_db=12.0,
            ),
            FakeDetection(
                time_s=2.0,
                scan_number=2,
                target_name="T",
                measured_range_m=4000.0,
                azimuth_deg=30.0,  # outside default 4° gate
                radial_speed_m_s=0.0,
                snr_db=12.0,
            ),
        ]
        cfg = TrackerConfig(gate_azimuth_deg=4.0, confirm_hits=2, max_misses=3)
        result = associate_and_filter(dets, cfg)
        # Second plot starts a new tentative track rather than updating the first.
        self.assertGreaterEqual(len(result.tracks), 2)
        for track in result.tracks:
            self.assertEqual(track.hit_count, 1)

    def test_alpha_beta_pulls_toward_measurement(self) -> None:
        # First hit establishes track; second has a range step inside gate.
        dets = [
            FakeDetection(
                time_s=0.0,
                scan_number=1,
                target_name="T",
                measured_range_m=5000.0,
                azimuth_deg=20.0,
                radial_speed_m_s=0.0,
                snr_db=16.0,
            ),
            FakeDetection(
                time_s=1.0,
                scan_number=2,
                target_name="T",
                measured_range_m=5100.0,
                azimuth_deg=20.0,
                radial_speed_m_s=100.0,
                snr_db=16.0,
            ),
        ]
        cfg = TrackerConfig(alpha=0.5, beta=0.2, gate_range_m=400.0)
        result = associate_and_filter(dets, cfg)
        confirmed = confirmed_tracks(result, min_hits=2)
        self.assertEqual(len(confirmed), 1)
        track = confirmed[0]
        # pred=5000, residual=100 → range = 5000 + 0.5*100 = 5050
        self.assertAlmostEqual(track.range_m, 5050.0, places=3)
        # range_rate = 0 + (0.2/1)*100 = 20
        self.assertAlmostEqual(track.range_rate_m_s, 20.0, places=3)

    def test_azimuth_wrap_association(self) -> None:
        dets = [
            FakeDetection(
                time_s=0.0,
                scan_number=1,
                target_name="WRAP",
                measured_range_m=2000.0,
                azimuth_deg=359.0,
                radial_speed_m_s=0.0,
                snr_db=10.0,
            ),
            FakeDetection(
                time_s=1.0,
                scan_number=2,
                target_name="WRAP",
                measured_range_m=2000.0,
                azimuth_deg=1.0,
                radial_speed_m_s=0.0,
                snr_db=10.0,
            ),
        ]
        result = associate_and_filter(
            dets, TrackerConfig(gate_azimuth_deg=4.0, confirm_hits=2)
        )
        confirmed = confirmed_tracks(result, min_hits=2)
        self.assertEqual(len(confirmed), 1)
        self.assertEqual(confirmed[0].hit_count, 2)

    def test_coast_on_miss_advances_range(self) -> None:
        # Two hits, one miss scan (unassociated distant plot), then reacquire.
        dets = [
            FakeDetection(0.0, 1, "C", 3000.0, 10.0, 50.0, 14.0, doppler_bin=4),
            FakeDetection(1.0, 2, "C", 3050.0, 10.0, 50.0, 14.0, doppler_bin=4),
            FakeDetection(2.0, 3, "NOISE", 9000.0, 90.0, 0.0, 5.0, doppler_bin=0),
            FakeDetection(3.0, 4, "C", 3150.0, 10.0, 50.0, 14.0, doppler_bin=4),
        ]
        cfg = TrackerConfig(
            confirm_hits=2,
            max_misses=4,
            coast_on_miss=True,
            coast_on_doppler_null=True,
            gate_range_m=200.0,
        )
        result = associate_and_filter(dets, cfg)
        coasted = [t for t in result.tracks if t.last_target_name == "C"]
        self.assertTrue(coasted)
        track = max(coasted, key=lambda t: t.hit_count)
        self.assertGreaterEqual(track.hit_count, 3)
        self.assertFalse(track.coasting)


class TestBallisticFit(unittest.TestCase):
    def test_vacuum_fit_recovers_state(self) -> None:
        # Synthetic vacuum trajectory from known state, observed in polar.
        x0, y0, z0 = 1000.0, 800.0, 500.0
        vx, vy, vz = 120.0, 40.0, -30.0
        g = -9.81
        history = []
        t0 = 10.0
        for i in range(12):
            t = t0 + 0.25 * float(i)
            dt = t - t0
            x = x0 + vx * dt
            y = y0 + vy * dt + 0.5 * g * dt * dt
            z = z0 + vz * dt
            r, az, el = cartesian_to_polar(x, y, z)
            history.append((t, r, az, el))

        state = fit_ballistic_vacuum(
            history,
            anchor_index=len(history) - 1,
            gravity_m_s2=g,
            min_points=5,
            min_span_s=1.0,
            max_fit_rms_m=5.0,
        )
        self.assertIsNotNone(state)
        assert state is not None
        self.assertLess(state.fit_rms_m, 1.0)
        # Fit state is at the last sample (anchor), not at t0.
        dt_anchor = 0.25 * 11.0
        expect_x = x0 + vx * dt_anchor
        expect_y = y0 + vy * dt_anchor + 0.5 * g * dt_anchor * dt_anchor
        expect_z = z0 + vz * dt_anchor
        expect_vy = vy + g * dt_anchor
        self.assertAlmostEqual(state.x_m, expect_x, delta=2.0)
        self.assertAlmostEqual(state.y_m, expect_y, delta=2.0)
        self.assertAlmostEqual(state.z_m, expect_z, delta=2.0)
        self.assertAlmostEqual(state.vx_m_s, vx, delta=2.0)
        self.assertAlmostEqual(state.vy_m_s, expect_vy, delta=2.0)
        self.assertAlmostEqual(state.vz_m_s, vz, delta=2.0)

    def test_fit_rejects_short_span(self) -> None:
        history = []
        for i in range(6):
            t = 0.01 * float(i)
            history.append((t, 1000.0 + 10.0 * float(i), 45.0, 5.0))
        state = fit_ballistic_vacuum(
            history,
            anchor_index=5,
            min_points=5,
            min_span_s=1.0,
        )
        self.assertIsNone(state)

    def test_predict_vacuum_matches_closed_form(self) -> None:
        state = BallisticState(
            t_anchor_s=0.0,
            x_m=0.0,
            y_m=1000.0,
            z_m=0.0,
            vx_m_s=100.0,
            vy_m_s=0.0,
            vz_m_s=0.0,
            ay_m_s2=-9.81,
            air_drag=0.0,
        )
        r, az, el = predict_ballistic(state, horizon_s=2.0, air_drag=0.0)
        x = 200.0
        y = 1000.0 + 0.5 * (-9.81) * 4.0
        z = 0.0
        expect_r, expect_az, expect_el = cartesian_to_polar(x, y, z)
        self.assertAlmostEqual(r, expect_r, places=5)
        self.assertAlmostEqual(az, expect_az, places=5)
        self.assertAlmostEqual(el, expect_el, places=5)


if __name__ == "__main__":
    unittest.main()
