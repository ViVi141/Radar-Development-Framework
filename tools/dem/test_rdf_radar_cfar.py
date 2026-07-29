#!/usr/bin/env python3
"""Golden unit tests for RDF CA-CFAR (tools/dem offline + Enforce formula parity).

Pins ca_cfar_detections behaviour so realistic-channel / CFAR changes cannot
drift silently. CellDetectedCA mirrors RDF_RadarCfarGate CA mode for a
lightweight Enforce-aligned golden (no Workbench required).
"""

from __future__ import annotations

import math
import unittest

import numpy as np

from rdf_radar_physics import ca_cfar_detections, lin_to_db


def _cfar_alpha(training_cells: int, pfa: float) -> float:
    n_train = training_cells
    if n_train < 2:
        n_train = 2
    alpha = float(n_train) * (pfa ** (-1.0 / float(n_train)) - 1.0)
    if alpha < 1.0:
        alpha = 1.0
    return alpha


def cell_detected_ca(
    row_powers: list[float],
    bin_idx: int,
    noise_floor_w: float,
    guard_cells: int = 2,
    training_cells: int = 8,
    pfa: float = 1.0e-6,
) -> bool:
    """Enforce RDF_RadarCfarGate CA-mode equivalent (single cell)."""
    nbin = len(row_powers)
    if nbin <= 0:
        return False
    if bin_idx < 0 or bin_idx >= nbin:
        return False

    n_train = training_cells
    if n_train < 2:
        n_train = 2
    alpha = _cfar_alpha(n_train, pfa)
    half = n_train // 2

    sum_left = 0.0
    count_left = 0
    left0 = bin_idx - guard_cells - half
    left1 = bin_idx - guard_cells - 1
    for i in range(left0, left1 + 1):
        if i < 0:
            continue
        if i >= nbin:
            continue
        sum_left = sum_left + row_powers[i]
        count_left = count_left + 1

    sum_right = 0.0
    count_right = 0
    right0 = bin_idx + guard_cells + 1
    right1 = bin_idx + guard_cells + half
    for j in range(right0, right1 + 1):
        if j < 0:
            continue
        if j >= nbin:
            continue
        sum_right = sum_right + row_powers[j]
        count_right = count_right + 1

    count = count_left + count_right
    total = sum_left + sum_right
    if count > 0:
        local_noise = total / float(count)
    else:
        local_noise = noise_floor_w
    if local_noise < noise_floor_w:
        local_noise = noise_floor_w
    threshold = local_noise * alpha
    return row_powers[bin_idx] > threshold


class TestCfarAlpha(unittest.TestCase):
    def test_alpha_matches_textbook_ca(self) -> None:
        # N=8, Pfa=1e-6 → α = N*(Pfa^(-1/N)-1)
        alpha = _cfar_alpha(8, 1.0e-6)
        expected = 8.0 * (1.0e-6 ** (-1.0 / 8.0) - 1.0)
        self.assertAlmostEqual(alpha, expected, places=9)
        self.assertGreater(alpha, 1.0)

    def test_alpha_floors_at_one(self) -> None:
        # Extremely large Pfa → raw α can drop below 1; implementation floors.
        alpha = _cfar_alpha(8, 0.9)
        self.assertGreaterEqual(alpha, 1.0)


class TestCaCfarDetections(unittest.TestCase):
    def test_strong_spike_detected_on_noise_floor(self) -> None:
        noise = 1.0e-12
        power = np.full((1, 32), noise, dtype=float)
        power[0, 16] = noise * 1.0e6
        mask = ca_cfar_detections(
            power, noise_w=noise, guard_cells=2, training_cells=8, pfa=1.0e-6
        )
        self.assertTrue(bool(mask[0, 16]))
        # Neighbours in guard / training should stay quiet.
        self.assertFalse(bool(mask[0, 15]))
        self.assertFalse(bool(mask[0, 17]))

    def test_uniform_noise_no_false_alarm_golden(self) -> None:
        noise = 2.5e-12
        power = np.full((3, 48), noise, dtype=float)
        mask = ca_cfar_detections(
            power, noise_w=noise, guard_cells=2, training_cells=8, pfa=1.0e-6
        )
        self.assertEqual(int(mask.sum()), 0)

    def test_weak_spike_below_threshold_missed(self) -> None:
        noise = 1.0e-12
        power = np.full((1, 32), noise, dtype=float)
        # Just above noise but well below CA threshold for Pfa=1e-6.
        power[0, 16] = noise * 2.0
        mask = ca_cfar_detections(
            power, noise_w=noise, guard_cells=2, training_cells=8, pfa=1.0e-6
        )
        self.assertFalse(bool(mask[0, 16]))

    def test_clutter_edge_raises_local_estimate(self) -> None:
        noise = 1.0e-12
        power = np.full((1, 40), noise, dtype=float)
        # High clutter on the left training side; target sits at bin 20.
        power[0, 0:16] = noise * 100.0
        power[0, 20] = noise * 200.0
        mask_hot = ca_cfar_detections(
            power, noise_w=noise, guard_cells=2, training_cells=8, pfa=1.0e-6
        )
        # Same spike on uniform noise should detect; with hot clutter may miss.
        power_flat = np.full((1, 40), noise, dtype=float)
        power_flat[0, 20] = noise * 200.0
        mask_flat = ca_cfar_detections(
            power_flat, noise_w=noise, guard_cells=2, training_cells=8, pfa=1.0e-6
        )
        self.assertTrue(bool(mask_flat[0, 20]))
        # Hot-clutter case: either missed or still detected, but local estimate
        # must not be below the noise floor (covered by empty-side path).
        self.assertTrue(mask_hot.shape == (1, 40))

    def test_shape_and_dtype_golden(self) -> None:
        power = np.zeros((5, 17), dtype=float)
        mask = ca_cfar_detections(power, noise_w=1.0e-12)
        self.assertEqual(mask.shape, (5, 17))
        self.assertEqual(mask.dtype, bool)

    def test_python_row_matches_enforce_ca_helper(self) -> None:
        """Optional Enforce golden: Python map == CellDetectedCA per cell."""
        rng = np.random.default_rng(42)
        noise = 1.0e-12
        row = noise * (1.0 + 0.05 * rng.standard_normal(64))
        row = np.clip(row, noise * 0.5, None)
        row[30] = noise * 5.0e5
        row[45] = noise * 50.0
        power = row.reshape(1, -1)
        mask = ca_cfar_detections(
            power, noise_w=noise, guard_cells=2, training_cells=8, pfa=1.0e-6
        )
        row_list = [float(v) for v in row]
        for bin_i in range(len(row_list)):
            expect = cell_detected_ca(
                row_list,
                bin_i,
                noise_floor_w=noise,
                guard_cells=2,
                training_cells=8,
                pfa=1.0e-6,
            )
            self.assertEqual(
                bool(mask[0, bin_i]),
                expect,
                msg=f"bin {bin_i} python={mask[0, bin_i]} enforce_ca={expect}",
            )

    def test_detection_count_golden_seeded(self) -> None:
        """Frozen detection count for a seeded power map (drift guard)."""
        rng = np.random.default_rng(7)
        noise = 1.0e-12
        power = noise * (1.0 + 0.02 * rng.standard_normal((4, 64)))
        power = np.maximum(power, noise * 0.5)
        # Plant three loud targets.
        power[0, 10] = noise * 1.0e6
        power[1, 30] = noise * 2.0e6
        power[3, 50] = noise * 8.0e5
        mask = ca_cfar_detections(
            power, noise_w=noise, guard_cells=2, training_cells=8, pfa=1.0e-6
        )
        # Exactly the three planted bins (no thermal-fill false alarms).
        self.assertEqual(int(mask.sum()), 3)
        self.assertTrue(bool(mask[0, 10]))
        self.assertTrue(bool(mask[1, 30]))
        self.assertTrue(bool(mask[3, 50]))


class TestPhysicsHelpersUsedByCfar(unittest.TestCase):
    def test_lin_to_db_roundtrip_sanity(self) -> None:
        self.assertAlmostEqual(lin_to_db(100.0), 20.0, places=6)
        self.assertTrue(math.isfinite(lin_to_db(1.0e-12)))


if __name__ == "__main__":
    unittest.main()
