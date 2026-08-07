#!/usr/bin/env python3
"""Tests for XZ quadtree power-voxel memory measurement."""

from __future__ import annotations

import unittest

from rdf_voxel_quadtree import (
    ObstacleBox,
    QuadBuildConfig,
    build_power_quadtree,
    case_quadtree_memory,
    measure_quadtree_vs_dense,
)


class TestQuadtreeBuild(unittest.TestCase):
    def test_splits_obstacles_finer_than_air(self) -> None:
        cfg = QuadBuildConfig(
            extent_x=640.0,
            extent_z=640.0,
            extent_y=200.0,
            min_leaf_m=20.0,
            max_leaf_m=160.0,
            roi_radius_m=0.0,
            obstacles=[
                ObstacleBox(300.0, 300.0, 360.0, 500.0, 40.0),
            ],
        )
        built = build_power_quadtree(cfg)
        self.assertGreater(built["leaf_count"], 4)
        self.assertGreater(built["n_obstacle"], 0)
        self.assertGreater(built["n_air"], 0)
        # Obstacle leaves should reach near min size.
        sides_obs = [
            (leaf.x1 - leaf.x0)
            for leaf in built["leaves"]
            if leaf.kind == "obstacle"
        ]
        self.assertTrue(sides_obs)
        self.assertLessEqual(min(sides_obs), cfg.min_leaf_m + 1e-6)

    def test_roi_marks_fine_leaves(self) -> None:
        cfg = QuadBuildConfig(
            extent_x=800.0,
            extent_z=800.0,
            min_leaf_m=25.0,
            max_leaf_m=200.0,
            roi_x=100.0,
            roi_z=100.0,
            roi_radius_m=120.0,
            obstacles=[],
        )
        built = build_power_quadtree(cfg)
        self.assertGreater(built["n_roi"], 0)


class TestQuadtreeMeasure(unittest.TestCase):
    def test_measure_saves_memory(self) -> None:
        report = measure_quadtree_vs_dense()
        self.assertLess(report["quad_over_dense_min_leaf"], 0.25)
        self.assertGreater(report["savings_frac_vs_dense_min"], 0.75)

    def test_validation_case(self) -> None:
        result = case_quadtree_memory()
        self.assertTrue(result.ok, msg=str(result.metrics))


if __name__ == "__main__":
    unittest.main()
