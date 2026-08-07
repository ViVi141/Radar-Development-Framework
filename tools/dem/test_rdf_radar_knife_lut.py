#!/usr/bin/env python3
"""Unit tests for knife-edge LUT bake / interpolation validation."""

from __future__ import annotations

import unittest

from rdf_radar_diffraction import knife_edge_linear_factor
from rdf_radar_knife_lut import (
    build_geometry_lut,
    build_nu_lut,
    case_knife_lut_accuracy,
    interp_geometry_lut,
    interp_nu_lut,
    run_knife_lut_validation,
    validate_band_nodes,
    validate_geometry_lut,
    validate_nu_lut,
)


class TestNuLut(unittest.TestCase):
    def test_nodes_match_analytic(self) -> None:
        lut = build_nu_lut(n_samples=65)
        for nu, factor in zip(lut["nu"], lut["factor"]):
            self.assertAlmostEqual(
                float(factor),
                knife_edge_linear_factor(float(nu)),
                places=12,
            )

    def test_interp_within_tolerance(self) -> None:
        lut = build_nu_lut()
        check = validate_nu_lut(lut)
        self.assertTrue(check["ok"], msg=str(check))

    def test_interp_midpoint_reasonable(self) -> None:
        lut = build_nu_lut(nu_min=0.0, nu_max=2.0, n_samples=5)
        mid = interp_nu_lut(lut, 1.0)
        truth = knife_edge_linear_factor(1.0)
        self.assertLess(abs(mid - truth), 0.02)


class TestGeometryLut(unittest.TestCase):
    def test_grid_nodes_exact(self) -> None:
        lut = build_geometry_lut()
        check = validate_band_nodes(lut)
        self.assertTrue(check["ok"], msg=str(check))

    def test_interp_within_tolerance(self) -> None:
        lut = build_geometry_lut()
        check = validate_geometry_lut(lut)
        self.assertTrue(check["ok"], msg=str(check))

    def test_interp_at_node(self) -> None:
        lut = build_geometry_lut()
        iw, ih, ir, iu = 2, 2, 1, 2
        lam = float(lut["wavelengths_m"][iw])
        h = float(lut["h_obs_m"][ih])
        r = float(lut["ranges_m"][ir])
        u = float(lut["u_edges"][iu])
        approx = interp_geometry_lut(lut, lam, h, r, u)
        truth = float(lut["factor"][iw, ih, ir, iu])
        self.assertAlmostEqual(approx, truth, places=12)


class TestSuite(unittest.TestCase):
    def test_run_validation(self) -> None:
        report = run_knife_lut_validation()
        self.assertTrue(report["all_ok"], msg=str(report))

    def test_case_wrapper(self) -> None:
        result = case_knife_lut_accuracy()
        self.assertTrue(result.ok, msg=str(result.metrics))


if __name__ == "__main__":
    unittest.main()
