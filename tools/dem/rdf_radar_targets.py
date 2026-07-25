#!/usr/bin/env python3
"""Sparse airborne / projectile target layer for RDF sector radar prototypes.

Targets are NOT baked into DEM tiles. Injected after clutter raymarch using the
absolute monostatic radar equation (see rdf_radar_physics.received_power_w).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class RadarTarget:
    name: str
    # World grid indices (same [iz, ix] as DEM stitch).
    ix: float
    iz: float
    # Absolute world Y (m).
    y_m: float
    # Radar cross section (m²).
    rcs_m2: float
    kind: str  # "heli" | "aircraft" | "shell" | "other"
    # Velocity in world XZ/Y (m/s). Used for radial Doppler vs radar site.
    vx_m_s: float = 0.0
    vy_m_s: float = 0.0
    vz_m_s: float = 0.0


@dataclass
class TargetTrajectory:
    """Time-parametric target contract used by scan simulators."""

    initial: RadarTarget
    start_time_s: float = 0.0
    # Optional constant acceleration in world axes.
    ax_m_s2: float = 0.0
    ay_m_s2: float = 0.0
    az_m_s2: float = 0.0

    def sample(self, time_s: float, cell_m: float) -> RadarTarget:
        """Sample position and velocity at time_s without mutating the source."""
        dt = time_s - self.start_time_s
        if dt < 0.0:
            dt = 0.0
        x_m = (
            self.initial.ix * cell_m
            + self.initial.vx_m_s * dt
            + 0.5 * self.ax_m_s2 * dt * dt
        )
        z_m = (
            self.initial.iz * cell_m
            + self.initial.vz_m_s * dt
            + 0.5 * self.az_m_s2 * dt * dt
        )
        y_m = (
            self.initial.y_m
            + self.initial.vy_m_s * dt
            + 0.5 * self.ay_m_s2 * dt * dt
        )
        return RadarTarget(
            name=self.initial.name,
            ix=x_m / cell_m,
            iz=z_m / cell_m,
            y_m=y_m,
            rcs_m2=self.initial.rcs_m2,
            kind=self.initial.kind,
            vx_m_s=self.initial.vx_m_s + self.ax_m_s2 * dt,
            vy_m_s=self.initial.vy_m_s + self.ay_m_s2 * dt,
            vz_m_s=self.initial.vz_m_s + self.az_m_s2 * dt,
        )


# Engineering defaults (optical-region order-of-magnitude).
RCS_HELICOPTER_M2 = 5.0
RCS_FIGHTER_M2 = 3.0
RCS_TRANSPORT_M2 = 10.0
RCS_ARTILLERY_SHELL_M2 = 0.01
RCS_MORTAR_BOMB_M2 = 0.005


def radial_speed_toward_radar(
    tgt: RadarTarget,
    radar_ix: float,
    radar_iz: float,
    radar_y: float,
    cell_m: float,
) -> float:
    """Positive when closing on the radar (standard monostatic convention)."""
    dx = (tgt.ix - radar_ix) * cell_m
    dy = tgt.y_m - radar_y
    dz = (tgt.iz - radar_iz) * cell_m
    range_m = math_hypot3(dx, dy, dz)
    if range_m < 1.0:
        return 0.0
    # Unit vector from radar to target.
    ux = dx / range_m
    uy = dy / range_m
    uz = dz / range_m
    # Radial component of target velocity along line-of-sight.
    v_los = tgt.vx_m_s * ux + tgt.vy_m_s * uy + tgt.vz_m_s * uz
    # Closing speed = -v_los (target flying toward radar ⇒ negative outward).
    return -v_los


def math_hypot3(a: float, b: float, c: float) -> float:
    return float(np.sqrt(a * a + b * b + c * c))


def default_scenario_targets(
    terrain: np.ndarray,
    cell_m: float,
    radar_ix: int,
    radar_iz: int,
) -> list[RadarTarget]:
    """Place demo targets for AD / airborne / artillery with Doppler velocities."""
    height, width = terrain.shape
    radar_y = float(terrain[radar_iz, radar_ix])
    if not np.isfinite(radar_y):
        radar_y = 0.0

    targets: list[RadarTarget] = []

    # Helicopter ~2.5 km NE, nap-of-earth + 80 m AGL, inbound ~40 m/s.
    heli_ix = radar_ix + int(1800.0 / cell_m)
    heli_iz = radar_iz + int(1800.0 / cell_m)
    if 0 <= heli_ix < width and 0 <= heli_iz < height:
        ground = terrain[heli_iz, heli_ix]
        if np.isfinite(ground):
            # Velocity toward radar (approx -X/-Z).
            targets.append(
                RadarTarget(
                    name="heli_noe",
                    ix=float(heli_ix),
                    iz=float(heli_iz),
                    y_m=float(ground) + 80.0,
                    rcs_m2=RCS_HELICOPTER_M2,
                    kind="heli",
                    vx_m_s=-28.0,
                    vy_m_s=0.0,
                    vz_m_s=-28.0,
                )
            )

    # Aircraft ~4.5 km E, medium altitude, crossing+inbound ~200 m/s.
    ac_ix = radar_ix + int(4500.0 / cell_m)
    ac_iz = radar_iz + int(500.0 / cell_m)
    if 0 <= ac_ix < width and 0 <= ac_iz < height:
        ground = terrain[ac_iz, ac_ix]
        if not np.isfinite(ground):
            ground = radar_y
        targets.append(
            RadarTarget(
                name="fighter",
                ix=float(ac_ix),
                iz=float(ac_iz),
                y_m=float(ground) + 1200.0,
                rcs_m2=RCS_FIGHTER_M2,
                kind="aircraft",
                vx_m_s=-180.0,
                vy_m_s=0.0,
                vz_m_s=50.0,
            )
        )

    # Artillery shell apex ~3 km NW, mostly vertical velocity near apex.
    shell_ix = radar_ix - int(2200.0 / cell_m)
    shell_iz = radar_iz + int(2200.0 / cell_m)
    if 0 <= shell_ix < width and 0 <= shell_iz < height:
        ground = terrain[shell_iz, shell_ix]
        if not np.isfinite(ground):
            ground = radar_y
        targets.append(
            RadarTarget(
                name="shell_apex",
                ix=float(shell_ix),
                iz=float(shell_iz),
                y_m=float(ground) + 800.0,
                rcs_m2=RCS_ARTILLERY_SHELL_M2,
                kind="shell",
                vx_m_s=80.0,
                vy_m_s=20.0,
                vz_m_s=-80.0,
            )
        )

    return targets


def trajectories_from_targets(targets: list[RadarTarget]) -> list[TargetTrajectory]:
    return [TargetTrajectory(initial=target) for target in targets]
