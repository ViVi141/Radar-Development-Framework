#!/usr/bin/env python3
"""Unit tests for rdf_radar_systems + full capability simulation."""

from __future__ import annotations

import unittest

import numpy as np

from rdf_radar_full_sim import run_full_sim
from rdf_radar_physics import get_preset
from rdf_radar_systems import (
    LockManager,
    LockState,
    MeasurementModel,
    NetworkSyncConfig,
    NetworkSyncState,
    cfar_detections,
    fill_thermal_noise,
    fingerprint_tracks,
    friis_receive_power_w,
    network_should_send,
    resolve_iff,
    rwr_alert_from_lock,
)


class TestCfarModes(unittest.TestCase):
    def test_go_detects_peak(self) -> None:
        noise = 1.0e-12
        power = np.full((1, 48), noise, dtype=float)
        power[0, 24] = noise * 100.0
        det = cfar_detections(power, noise, mode="go", pfa=1.0e-4)
        self.assertTrue(bool(det[0, 24]))

    def test_thermal_fill_raises_floor(self) -> None:
        rng = np.random.default_rng(0)
        power = np.zeros((2, 16), dtype=float)
        filled = fill_thermal_noise(power, 1.0e-12, rng)
        self.assertGreater(float(np.mean(filled)), 0.0)


class TestLockAndEsm(unittest.TestCase):
    def test_lock_acquire_and_coast(self) -> None:
        lock = LockManager(acquire_hits=2, coast_misses=2)
        pos = {1: (10.0, 20.0, 30.0)}
        lock.update([1], pos)
        self.assertEqual(lock.state, LockState.ACQUIRING)
        lock.update([1], pos)
        self.assertEqual(lock.state, LockState.TRACKING)
        self.assertEqual(rwr_alert_from_lock(lock.state, True).value, "lock")
        lock.update([], pos)
        self.assertEqual(lock.state, LockState.COAST)
        lock.update([], pos)
        self.assertEqual(lock.state, LockState.SEARCH)

    def test_friis_range_law(self) -> None:
        p1 = friis_receive_power_w(1000.0, 9.5e9, 1000.0)
        p2 = friis_receive_power_w(1000.0, 9.5e9, 2000.0)
        self.assertAlmostEqual(p1 / p2, 4.0, places=5)


class TestMeasurementAndIff(unittest.TestCase):
    def test_noise_scale_spreads(self) -> None:
        hw = get_preset("shorad")
        mm = MeasurementModel(noise_scale=2.0)
        rng = np.random.default_rng(1)
        errs = []
        for _ in range(30):
            r, _, _, _ = mm.synthesize(hw, 4000.0, 0.0, 5.0, 50.0, 8.0, rng)
            errs.append(abs(r - 4000.0))
        self.assertGreater(float(np.mean(errs)), 1.0)

    def test_iff(self) -> None:
        self.assertEqual(int(resolve_iff("a", "a")), 1)
        self.assertEqual(int(resolve_iff("a", "b")), 2)


class TestNetworkPolicy(unittest.TestCase):
    def test_throttle_and_interest(self) -> None:
        cfg = NetworkSyncConfig(
            min_reliable_interval_s=0.2,
            skip_unchanged=True,
            interest_radius_m=1000.0,
        )
        st = NetworkSyncState()
        fp = fingerprint_tracks([1], [(0.0, 0.0)], 1, 1)
        self.assertTrue(network_should_send(cfg, st, 0.0, fp, 1, 100.0, False))
        self.assertFalse(network_should_send(cfg, st, 0.05, fp, 1, 100.0, False))
        self.assertFalse(network_should_send(cfg, st, 1.0, fp, 1, 5000.0, False))


class TestFullSim(unittest.TestCase):
    def test_all_scenarios_pass(self) -> None:
        report = run_full_sim()
        failed = [r.name for r in report.results if not r.ok]
        self.assertTrue(report.all_ok, f"failed={failed}")
        self.assertGreaterEqual(len(report.coverage), 20)
        self.assertEqual(len(report.results), 16)


if __name__ == "__main__":
    unittest.main()
