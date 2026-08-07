#!/usr/bin/env python3
"""Unit tests for RDF knife-edge diffraction helpers."""

from __future__ import annotations

import unittest

from rdf_radar_diffraction import (
    dual_knife_edge_factor_from_geometry,
    fresnel_nu,
    knife_edge_factor_from_geometry,
    knife_edge_linear_factor,
    obstacle_height_above_los,
    select_dual_dominant_edges,
)


class TestKnifeEdgeLinearFactor(unittest.TestCase):
    def test_clearance_near_unity(self) -> None:
        self.assertAlmostEqual(knife_edge_linear_factor(-0.8), 1.0, places=6)
        self.assertAlmostEqual(knife_edge_linear_factor(-1.5), 1.0, places=6)

    def test_grazing_weaker_than_clear(self) -> None:
        clear = knife_edge_linear_factor(-0.8)
        grazing = knife_edge_linear_factor(0.0)
        self.assertLess(grazing, clear)
        self.assertGreater(grazing, 0.2)

    def test_deep_obstruction_near_zero(self) -> None:
        deep = knife_edge_linear_factor(5.0)
        self.assertLess(deep, 0.05)

    def test_monotone_decreasing_in_nu(self) -> None:
        prev = knife_edge_linear_factor(-0.5)
        for nu in (-0.2, 0.0, 0.5, 1.0, 2.0, 4.0):
            cur = knife_edge_linear_factor(nu)
            self.assertLessEqual(cur, prev + 1e-12)
            prev = cur


class TestFresnelGeometry(unittest.TestCase):
    def test_taller_obstacle_larger_nu(self) -> None:
        n_low = fresnel_nu(5.0, 1000.0, 1000.0, 0.03)
        n_high = fresnel_nu(50.0, 1000.0, 1000.0, 0.03)
        self.assertGreater(n_high, n_low)

    def test_obstacle_height_sign(self) -> None:
        origin = (0.0, 100.0, 0.0)
        target = (2000.0, 100.0, 0.0)
        # Flat LOS at y=100; terrain 110 → +10 above LOS before slack.
        h = obstacle_height_above_los(origin, target, 0.5, 110.0, 0.0)
        self.assertAlmostEqual(h, 10.0, places=6)
        h_slack = obstacle_height_above_los(origin, target, 0.5, 110.0, 2.0)
        self.assertAlmostEqual(h_slack, 8.0, places=6)

    def test_geometry_clear_returns_zero(self) -> None:
        # Terrain below LOS → no knife path (entity occlusion must not get factor 1).
        f = knife_edge_factor_from_geometry(-5.0, 4000.0, 0.5, 0.03)
        self.assertEqual(f, 0.0)

    def test_geometry_deep_block_rejects(self) -> None:
        f = knife_edge_factor_from_geometry(80.0, 2000.0, 0.5, 0.03, min_factor=0.008)
        self.assertEqual(f, 0.0)


class TestDualKnifeEdge(unittest.TestCase):
    def test_select_requires_separation(self) -> None:
        edges = [(0.3, 10.0), (0.32, 9.0), (0.7, 8.0)]
        picked = select_dual_dominant_edges(edges, min_u_sep=0.10)
        self.assertEqual(len(picked), 2)
        self.assertAlmostEqual(picked[0][0], 0.3, places=6)
        self.assertAlmostEqual(picked[1][0], 0.7, places=6)

    def test_select_single_when_clustered(self) -> None:
        edges = [(0.4, 12.0), (0.42, 11.0), (0.45, 10.0)]
        picked = select_dual_dominant_edges(edges, min_u_sep=0.10)
        self.assertEqual(len(picked), 1)

    def test_dual_weaker_than_single_main(self) -> None:
        single = knife_edge_factor_from_geometry(3.0, 10000.0, 0.35, 0.3)
        dual = dual_knife_edge_factor_from_geometry(
            3.0, 0.35, 2.5, 0.70, 10000.0, 0.3
        )
        self.assertGreater(single, 0.0)
        self.assertGreater(dual, 0.0)
        self.assertLess(dual, single)

    def test_dual_falls_back_when_not_separated(self) -> None:
        single = knife_edge_factor_from_geometry(20.0, 4000.0, 0.40, 0.03)
        dual = dual_knife_edge_factor_from_geometry(
            20.0, 0.40, 18.0, 0.45, 4000.0, 0.03, min_u_sep=0.10
        )
        self.assertAlmostEqual(dual, single, places=9)


if __name__ == "__main__":
    unittest.main()
