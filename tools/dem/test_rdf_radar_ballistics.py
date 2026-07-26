#!/usr/bin/env python3
"""Unit tests for RDF ballistic integration and WLR ground intersection."""

from __future__ import annotations

import math
import unittest

import numpy as np

from rdf_radar_targets import (
    AIR_DRAG_SHELL_82MM_HE,
    GRAVITY_M_S2,
    GlobalWind,
    dem_ground_y_fn,
    find_ground_intersection,
    integrate_ballistic,
    solve_launch_and_impact,
)


NO_WIND = GlobalWind(0.0, 0.0)


class TestBallistics(unittest.TestCase):
    def test_freefall_impact_time(self) -> None:
        h = 1000.0
        expected_t = math.sqrt(2.0 * h / 9.81)
        hit = find_ground_intersection(
            0.0, h, 0.0, 0.0, 0.0, 0.0, 0.0, air_drag=0.0, wind=NO_WIND
        )
        self.assertIsNotNone(hit)
        assert hit is not None
        self.assertAlmostEqual(hit.time_offset_s, expected_t, delta=0.08)

    def test_drag_shortens_range(self) -> None:
        vac = find_ground_intersection(
            0.0, 50.0, 0.0, 200.0, 80.0, 0.0, 0.0, air_drag=0.0, wind=NO_WIND
        )
        drag = find_ground_intersection(
            0.0,
            50.0,
            0.0,
            200.0,
            80.0,
            0.0,
            0.0,
            air_drag=AIR_DRAG_SHELL_82MM_HE,
            wind=NO_WIND,
        )
        self.assertIsNotNone(vac)
        self.assertIsNotNone(drag)
        assert vac is not None and drag is not None
        vac_r = math.hypot(vac.x_m, vac.z_m)
        drag_r = math.hypot(drag.x_m, drag.z_m)
        self.assertLess(drag_r, vac_r - 5.0)

    def test_wind_shifts_impact_plus_z(self) -> None:
        calm = find_ground_intersection(
            0.0,
            80.0,
            0.0,
            180.0,
            60.0,
            0.0,
            0.0,
            air_drag=AIR_DRAG_SHELL_82MM_HE,
            wind=NO_WIND,
        )
        cross = find_ground_intersection(
            0.0,
            80.0,
            0.0,
            180.0,
            60.0,
            0.0,
            0.0,
            air_drag=AIR_DRAG_SHELL_82MM_HE,
            wind=GlobalWind(12.0, 90.0),
        )
        self.assertIsNotNone(calm)
        self.assertIsNotNone(cross)
        assert calm is not None and cross is not None
        self.assertGreater(cross.z_m - calm.z_m, 5.0)

    def test_backward_recovers_launch(self) -> None:
        launch = (100.0, 0.0, -50.0)
        muzzle = (150.0, 120.0, 40.0)
        mid = integrate_ballistic(
            launch[0],
            launch[1],
            launch[2],
            muzzle[0],
            muzzle[1],
            muzzle[2],
            8.0,
            air_drag=AIR_DRAG_SHELL_82MM_HE,
            wind=NO_WIND,
        )
        recovered = find_ground_intersection(
            mid[0],
            mid[1],
            mid[2],
            mid[3],
            mid[4],
            mid[5],
            0.0,
            air_drag=AIR_DRAG_SHELL_82MM_HE,
            wind=NO_WIND,
            backward=True,
        )
        self.assertIsNotNone(recovered)
        assert recovered is not None
        err = math.hypot(recovered.x_m - launch[0], recovered.z_m - launch[2])
        self.assertLess(err, 25.0)
        self.assertAlmostEqual(recovered.y_m, 0.0, delta=0.1)

    def test_solve_launch_and_impact_pair(self) -> None:
        mid = integrate_ballistic(
            0.0,
            0.0,
            0.0,
            220.0,
            90.0,
            0.0,
            6.0,
            air_drag=AIR_DRAG_SHELL_82MM_HE,
            wind=NO_WIND,
        )
        launch, impact = solve_launch_and_impact(
            mid[0],
            mid[1],
            mid[2],
            mid[3],
            mid[4],
            mid[5],
            0.0,
            air_drag=AIR_DRAG_SHELL_82MM_HE,
            wind=NO_WIND,
        )
        self.assertIsNotNone(launch)
        self.assertIsNotNone(impact)
        assert launch is not None and impact is not None
        self.assertAlmostEqual(launch.time_offset_s, -6.0, delta=0.2)
        self.assertGreater(impact.time_offset_s, 0.0)
        self.assertLess(abs(launch.x_m), 20.0)
        self.assertLess(abs(launch.z_m), 20.0)

    def test_integrate_zero_horizon_identity(self) -> None:
        state = integrate_ballistic(
            1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 0.0, air_drag=AIR_DRAG_SHELL_82MM_HE
        )
        self.assertEqual(state, (1.0, 2.0, 3.0, 4.0, 5.0, 6.0))

    def test_gravity_constant_sign(self) -> None:
        self.assertLess(GRAVITY_M_S2, 0.0)

    def test_dem_ground_intersection_hits_hill(self) -> None:
        terrain = np.zeros((64, 64), dtype=np.float32)
        terrain[40:50, 40:50] = 80.0
        ground_fn = dem_ground_y_fn(terrain, 10.0, 32.0, 32.0, 0.0)
        # Fly toward the hill plateau; flat ground at 0 would land farther.
        hit_flat = find_ground_intersection(
            0.0, 50.0, 0.0, 40.0, 10.0, 40.0, 0.0, air_drag=0.0, wind=NO_WIND
        )
        hit_dem = find_ground_intersection(
            0.0,
            50.0,
            0.0,
            40.0,
            10.0,
            40.0,
            0.0,
            air_drag=0.0,
            wind=NO_WIND,
            ground_y_fn=ground_fn,
        )
        self.assertIsNotNone(hit_flat)
        self.assertIsNotNone(hit_dem)
        assert hit_flat is not None and hit_dem is not None
        self.assertGreater(hit_dem.y_m, 20.0)
        self.assertLess(hit_dem.time_offset_s, hit_flat.time_offset_s)


if __name__ == "__main__":
    unittest.main()
