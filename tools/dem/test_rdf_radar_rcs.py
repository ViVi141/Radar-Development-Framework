#!/usr/bin/env python3
"""OBB aspect RCS parity with RDF_RadarRcsModel (zero-roll + rolled)."""

from __future__ import annotations

import unittest

from rdf_radar_channel import (
    aspect_rcs_from_extents,
    aspect_rcs_from_obb,
    axes_from_yaw_pitch,
    los_unit_from_az_el,
)


class TestObbRcs(unittest.TestCase):
    def test_default_axes_match_yaw0(self) -> None:
        right, up, forward = axes_from_yaw_pitch(0.0, 0.0)
        self.assertAlmostEqual(forward[0], 1.0, places=6)
        self.assertAlmostEqual(right[2], 1.0, places=6)
        self.assertAlmostEqual(up[1], 1.0, places=6)

    def test_zero_roll_matches_extents_helper(self) -> None:
        kwargs = dict(
            mean_rcs_m2=20.0,
            size_x=2.0,
            size_y=4.0,
            size_z=8.0,
            yaw_deg=25.0,
            pitch_deg=10.0,
            los_azimuth_deg=40.0,
            los_elevation_deg=15.0,
        )
        via_ext = aspect_rcs_from_extents(**kwargs)
        right, up, forward = axes_from_yaw_pitch(kwargs["yaw_deg"], kwargs["pitch_deg"])
        via_obb = aspect_rcs_from_obb(
            kwargs["mean_rcs_m2"],
            kwargs["size_x"],
            kwargs["size_y"],
            kwargs["size_z"],
            right,
            up,
            forward,
            kwargs["los_azimuth_deg"],
            kwargs["los_elevation_deg"],
        )
        self.assertAlmostEqual(via_ext, via_obb, places=6)

    def test_roll_changes_broadside(self) -> None:
        mean = 20.0
        level = aspect_rcs_from_obb(
            mean, 2.0, 4.0, 8.0,
            (0.0, 0.0, 1.0), (0.0, 1.0, 0.0), (1.0, 0.0, 0.0),
            90.0, 0.0,
        )
        rolled = aspect_rcs_from_obb(
            mean, 2.0, 4.0, 8.0,
            (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0),
            90.0, 0.0,
        )
        self.assertGreater(abs(level - rolled), 0.5)

    def test_los_unit_horizon_east(self) -> None:
        x, y, z = los_unit_from_az_el(90.0, 0.0)
        self.assertAlmostEqual(x, 0.0, places=6)
        self.assertAlmostEqual(y, 0.0, places=6)
        self.assertAlmostEqual(z, 1.0, places=6)


if __name__ == "__main__":
    unittest.main()
