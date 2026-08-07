#!/usr/bin/env python3
"""Unit tests for offline 3D voxel EM power-field validation."""

from __future__ import annotations

import unittest

import numpy as np

from rdf_voxel_em import (
    EmSource,
    VoxelGrid,
    case_free_space_agreement,
    case_occlusion_shadow,
    case_scale_report,
    deposit_power_ray_march,
    estimate_dense_cost,
    estimate_fdtd_cost,
    fill_power_field_los,
    free_space_power_density_w_m2,
    probe_power,
    relative_error,
    run_validation_suite,
    sample_unit_directions,
)


class TestVoxelGrid(unittest.TestCase):
    def test_index_roundtrip_center(self) -> None:
        grid = VoxelGrid(origin_m=(0.0, 0.0, 0.0), cell_m=2.0, shape=(8, 8, 8))
        cx, cy, cz = grid.index_to_center(3, 4, 5)
        ix, iy, iz = grid.world_to_index(cx, cy, cz)
        self.assertEqual((ix, iy, iz), (3, 4, 5))

    def test_fill_box_marks_voxels(self) -> None:
        grid = VoxelGrid(origin_m=(0.0, 0.0, 0.0), cell_m=1.0, shape=(20, 20, 20))
        n = grid.fill_box_obstacle(5.0, 5.0, 5.0, 8.0, 8.0, 8.0, atten_per_m=10.0)
        self.assertGreater(n, 0)
        self.assertEqual(int(grid.occupancy[6, 6, 6]), 1)
        self.assertGreater(float(grid.atten_per_m[6, 6, 6]), 0.0)


class TestFreeSpaceMath(unittest.TestCase):
    def test_density_falls_with_range(self) -> None:
        near = free_space_power_density_w_m2(1000.0, 10.0)
        far = free_space_power_density_w_m2(1000.0, 40.0)
        self.assertGreater(near, far)
        # 4x range -> 16x weaker.
        self.assertAlmostEqual(near / far, 16.0, places=6)

    def test_relative_error(self) -> None:
        self.assertAlmostEqual(relative_error(1.1, 1.0), 0.1, places=9)


class TestRayDeposit(unittest.TestCase):
    def test_directions_are_unit(self) -> None:
        dirs = sample_unit_directions(12, 5)
        norms = np.linalg.norm(dirs, axis=1)
        self.assertTrue(np.allclose(norms, 1.0, atol=1e-9))

    def test_power_decreases_with_range(self) -> None:
        grid = VoxelGrid(origin_m=(-40.0, -40.0, -40.0), cell_m=4.0, shape=(20, 20, 20))
        source = EmSource(0.0, 0.0, 0.0, 500.0)
        fill_power_field_los(grid, source)
        near = probe_power(grid, 8.0, 0.0, 0.0)
        far = probe_power(grid, 28.0, 0.0, 0.0)
        self.assertGreater(near, 0.0)
        self.assertGreater(near, far)

    def test_ray_march_runs(self) -> None:
        grid = VoxelGrid(origin_m=(-20.0, -20.0, -20.0), cell_m=4.0, shape=(10, 10, 10))
        source = EmSource(0.0, 0.0, 0.0, 100.0)
        info = deposit_power_ray_march(grid, source, n_az=24, n_el=13)
        self.assertGreater(info["steps_total"], 0)
        self.assertGreater(float(grid.power_w_m2.max()), 0.0)


class TestValidationCases(unittest.TestCase):
    def test_free_space_case(self) -> None:
        result = case_free_space_agreement()
        self.assertTrue(result.ok, msg=str(result.metrics))

    def test_occlusion_case(self) -> None:
        result = case_occlusion_shadow()
        self.assertTrue(result.ok, msg=str(result.metrics))

    def test_scale_case(self) -> None:
        result = case_scale_report()
        self.assertTrue(result.ok, msg=str(result.metrics))
        fdtd = result.metrics["fdtd_xband_1us"]
        self.assertGreater(fdtd["gib_fields"], 1.0)

    def test_suite_all_ok(self) -> None:
        report = run_validation_suite()
        self.assertTrue(report["all_ok"], msg=str(report))
        names = {row["name"] for row in report["results"]}
        self.assertIn("free_space_agreement", names)
        self.assertIn("occlusion_shadow", names)
        self.assertIn("scale_report", names)


class TestCostEstimates(unittest.TestCase):
    def test_finer_cells_cost_more(self) -> None:
        coarse = estimate_dense_cost((1000.0, 200.0, 1000.0), 20.0)
        fine = estimate_dense_cost((1000.0, 200.0, 1000.0), 5.0)
        self.assertGreater(fine["voxels"], coarse["voxels"])

    def test_fdtd_finer_than_power_grid(self) -> None:
        fdtd = estimate_fdtd_cost((500.0, 100.0, 500.0), 9.5e9, sim_time_s=1e-7)
        power = estimate_dense_cost((500.0, 100.0, 500.0), 10.0)
        self.assertGreater(fdtd["voxels"], power["voxels"])


if __name__ == "__main__":
    unittest.main()
