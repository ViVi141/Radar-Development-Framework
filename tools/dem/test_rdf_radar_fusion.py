#!/usr/bin/env python3
"""Unit tests for RDF multi-radar fusion geometry helpers."""

from __future__ import annotations

import math
import unittest

from rdf_radar_fusion import (
    associate_pair,
    bearing_separation_deg,
    try_cross_fix_horizontal,
)


class TestBearing(unittest.TestCase):
    def test_wrap(self) -> None:
        self.assertAlmostEqual(bearing_separation_deg(10.0, 350.0), 20.0, places=5)
        self.assertAlmostEqual(bearing_separation_deg(0.0, 90.0), 90.0, places=5)


class TestCrossFix(unittest.TestCase):
    def test_orthogonal_intersection(self) -> None:
        # Radar A at origin looking +X; B at (0,1000) looking +X from north... 
        # A at (0,0) az=0 (east/+X), B at (0,1000) az=-90 or 270 (west? )
        # Enfusion az: Cos(az)*X + Sin(az)*Z — az=0 → +X, az=90° → +Z
        # Target at (1000, 0, 1000): from A bearing atan2(1000,1000)=45°, from B(0,0,0)? 
        # Place A at (0,0), B at (0,2000) in XZ as (x,z).
        # Target (1000, 1000): from A az=45°, from B at (0,2000): dx=1000,dz=-1000 → az=-45°
        hit = try_cross_fix_horizontal((0.0, 0.0), 45.0, (0.0, 2000.0), -45.0)
        self.assertIsNotNone(hit)
        assert hit is not None
        self.assertAlmostEqual(hit[0], 1000.0, delta=1.0)
        self.assertAlmostEqual(hit[1], 1000.0, delta=1.0)

    def test_acute_rejected(self) -> None:
        hit = try_cross_fix_horizontal((0.0, 0.0), 0.0, (10.0, 0.0), 1.0, min_angle_deg=15.0)
        self.assertIsNone(hit)

    def test_parallel_rejected(self) -> None:
        hit = try_cross_fix_horizontal((0.0, 0.0), 0.0, (0.0, 100.0), 0.0)
        self.assertIsNone(hit)


class TestAssociate(unittest.TestCase):
    def test_near_pair(self) -> None:
        self.assertTrue(
            associate_pair(
                (0.0, 100.0, 0.0),
                (10.0, 0.0, 0.0),
                (50.0, 100.0, 10.0),
                (12.0, 0.0, 0.0),
                gate_m=350.0,
            )
        )

    def test_far_rejected(self) -> None:
        self.assertFalse(
            associate_pair(
                (0.0, 0.0, 0.0),
                (0.0, 0.0, 0.0),
                (2000.0, 0.0, 0.0),
                (0.0, 0.0, 0.0),
                gate_m=350.0,
            )
        )


if __name__ == "__main__":
    unittest.main()
