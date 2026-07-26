#!/usr/bin/env python3
"""Sparse airborne / projectile target layer for RDF sector radar prototypes.

Targets are NOT baked into DEM tiles. Injected after clutter raymarch using the
absolute monostatic radar equation (see rdf_radar_physics.received_power_w).
"""

from __future__ import annotations

import math
from collections.abc import Callable
from dataclasses import dataclass, field

import numpy as np


# Reforger ShellMoveComponent.AirDrag priors (prefab values).
AIR_DRAG_SHELL_82MM_HE = 0.000615  # Ammo_Shell_82mm_HE_O832DU
AIR_DRAG_SHELL_81MM_HE = 0.000462  # Ammo_Shell_81mm_HE_M821
GRAVITY_M_S2 = -9.81


@dataclass(frozen=True)
class GlobalWind:
    """Single global horizontal wind, matching Reforger's world-wide wind.

    direction_deg is the bearing the wind blows *towards*, in the same frame as
    radar azimuth (+X east, +Z north, 0 deg = +X).
    """

    speed_m_s: float = 0.0
    direction_deg: float = 0.0

    @property
    def vx_m_s(self) -> float:
        return self.speed_m_s * float(np.cos(np.radians(self.direction_deg)))

    @property
    def vz_m_s(self) -> float:
        return self.speed_m_s * float(np.sin(np.radians(self.direction_deg)))


NO_WIND = GlobalWind()


def random_global_wind(
    rng: np.random.Generator,
    min_speed_m_s: float = 2.0,
    max_speed_m_s: float = 14.0,
) -> GlobalWind:
    """Draw a random global wind (Reforger overrides speed + direction globally)."""
    return GlobalWind(
        speed_m_s=float(rng.uniform(min_speed_m_s, max_speed_m_s)),
        direction_deg=float(rng.uniform(0.0, 360.0)),
    )


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
    """Time-parametric target contract used by scan simulators.

    Without air_drag: closed-form constant-acceleration motion.
    With air_drag > 0: numerical integration using Reforger-style quadratic drag
    on the airspeed vector (velocity relative to the global wind):
        a_drag = -air_drag * |v - w| * (v - w)
    plus constant gravity on Y (ay_m_s2, typically GRAVITY_M_S2).
    """

    initial: RadarTarget
    start_time_s: float = 0.0
    # Optional constant acceleration in world axes (used when air_drag == 0,
    # and as gravity bias when air_drag > 0 — typically ay = GRAVITY_M_S2).
    ax_m_s2: float = 0.0
    ay_m_s2: float = 0.0
    az_m_s2: float = 0.0
    # Reforger ShellMoveComponent.AirDrag. Zero disables drag integration.
    air_drag: float = 0.0
    # Global horizontal wind; only meaningful together with air_drag > 0.
    wind: GlobalWind = NO_WIND
    integrate_dt_s: float = 0.05
    _cache_cell_m: float = field(default=-1.0, repr=False)
    _cache_t: list[float] = field(default_factory=list, repr=False)
    _cache_state: list[tuple[float, float, float, float, float, float]] = field(
        default_factory=list, repr=False
    )

    def sample(self, time_s: float, cell_m: float) -> RadarTarget:
        """Sample position and velocity at time_s without mutating the source."""
        dt = time_s - self.start_time_s
        if dt < 0.0:
            dt = 0.0

        if self.air_drag > 0.0:
            x_m, y_m, z_m, vx, vy, vz = self._sample_with_drag(dt, cell_m)
        else:
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
            vx = self.initial.vx_m_s + self.ax_m_s2 * dt
            vy = self.initial.vy_m_s + self.ay_m_s2 * dt
            vz = self.initial.vz_m_s + self.az_m_s2 * dt

        return RadarTarget(
            name=self.initial.name,
            ix=x_m / cell_m,
            iz=z_m / cell_m,
            y_m=y_m,
            rcs_m2=self.initial.rcs_m2,
            kind=self.initial.kind,
            vx_m_s=vx,
            vy_m_s=vy,
            vz_m_s=vz,
        )

    def _sample_with_drag(
        self, dt: float, cell_m: float
    ) -> tuple[float, float, float, float, float, float]:
        self._ensure_drag_cache(cell_m, dt)
        if not self._cache_t:
            return (
                self.initial.ix * cell_m,
                self.initial.y_m,
                self.initial.iz * cell_m,
                self.initial.vx_m_s,
                self.initial.vy_m_s,
                self.initial.vz_m_s,
            )
        if dt <= self._cache_t[0]:
            return self._cache_state[0]
        if dt >= self._cache_t[-1]:
            return self._cache_state[-1]

        # Linear interpolate between bracketing samples.
        idx = int(dt / self.integrate_dt_s)
        if idx < 0:
            idx = 0
        if idx >= len(self._cache_t) - 1:
            return self._cache_state[-1]
        t0 = self._cache_t[idx]
        t1 = self._cache_t[idx + 1]
        s0 = self._cache_state[idx]
        s1 = self._cache_state[idx + 1]
        if t1 <= t0:
            return s0
        alpha = (dt - t0) / (t1 - t0)
        return (
            s0[0] + (s1[0] - s0[0]) * alpha,
            s0[1] + (s1[1] - s0[1]) * alpha,
            s0[2] + (s1[2] - s0[2]) * alpha,
            s0[3] + (s1[3] - s0[3]) * alpha,
            s0[4] + (s1[4] - s0[4]) * alpha,
            s0[5] + (s1[5] - s0[5]) * alpha,
        )

    def _ensure_drag_cache(self, cell_m: float, dt_needed: float) -> None:
        if abs(cell_m - self._cache_cell_m) > 1e-9:
            self._cache_cell_m = cell_m
            self._cache_t = []
            self._cache_state = []

        if self._cache_t and self._cache_t[-1] + 1e-9 >= dt_needed:
            return

        if not self._cache_t:
            x = self.initial.ix * cell_m
            y = self.initial.y_m
            z = self.initial.iz * cell_m
            vx = self.initial.vx_m_s
            vy = self.initial.vy_m_s
            vz = self.initial.vz_m_s
            self._cache_t = [0.0]
            self._cache_state = [(x, y, z, vx, vy, vz)]

        step = self.integrate_dt_s
        if step < 1e-4:
            step = 1e-4
        x, y, z, vx, vy, vz = self._cache_state[-1]
        t = self._cache_t[-1]
        drag = self.air_drag
        ax0 = self.ax_m_s2
        ay0 = self.ay_m_s2
        az0 = self.az_m_s2
        wx = self.wind.vx_m_s
        wz = self.wind.vz_m_s

        while t + 1e-12 < dt_needed:
            # RK2 (midpoint) with a = g_bias - air_drag * |v - w| * (v - w)
            ax, ay, az = _accel_with_drag(vx, vy, vz, drag, wx, wz, ax0, ay0, az0)
            vx_m = vx + ax * (0.5 * step)
            vy_m = vy + ay * (0.5 * step)
            vz_m = vz + az * (0.5 * step)
            ax_m, ay_m, az_m = _accel_with_drag(
                vx_m, vy_m, vz_m, drag, wx, wz, ax0, ay0, az0
            )

            x = x + vx_m * step
            y = y + vy_m * step
            z = z + vz_m * step
            vx = vx + ax_m * step
            vy = vy + ay_m * step
            vz = vz + az_m * step
            t = t + step
            self._cache_t.append(t)
            self._cache_state.append((x, y, z, vx, vy, vz))


def _accel_with_drag(
    vx: float,
    vy: float,
    vz: float,
    air_drag: float,
    wind_x_m_s: float,
    wind_z_m_s: float,
    ax_bias: float,
    ay_bias: float,
    az_bias: float,
) -> tuple[float, float, float]:
    """Gravity/bias plus quadratic drag on airspeed (velocity minus wind)."""
    rx = vx - wind_x_m_s
    ry = vy
    rz = vz - wind_z_m_s
    airspeed = math_hypot3(rx, ry, rz)
    return (
        ax_bias - air_drag * airspeed * rx,
        ay_bias - air_drag * airspeed * ry,
        az_bias - air_drag * airspeed * rz,
    )


def drag_accel(
    vx: float,
    vy: float,
    vz: float,
    air_drag: float,
    wind: GlobalWind = NO_WIND,
) -> tuple[float, float, float]:
    """Reforger-style quadratic drag on airspeed: -AirDrag * |v-w| * (v-w)."""
    if air_drag <= 0.0:
        return 0.0, 0.0, 0.0
    return _accel_with_drag(
        vx, vy, vz, air_drag, wind.vx_m_s, wind.vz_m_s, 0.0, 0.0, 0.0
    )


def integrate_ballistic(
    x: float,
    y: float,
    z: float,
    vx: float,
    vy: float,
    vz: float,
    horizon_s: float,
    air_drag: float = 0.0,
    gravity_m_s2: float = GRAVITY_M_S2,
    dt_s: float = 0.05,
    wind: GlobalWind = NO_WIND,
) -> tuple[float, float, float, float, float, float]:
    """Forward-integrate gravity + optional AirDrag/wind; return final state."""
    if horizon_s <= 0.0:
        return x, y, z, vx, vy, vz
    if dt_s < 1e-4:
        dt_s = 1e-4
    wx = wind.vx_m_s
    wz = wind.vz_m_s
    t = 0.0
    while t + 1e-12 < horizon_s:
        step = dt_s
        if t + step > horizon_s:
            step = horizon_s - t
        ax, ay, az = _accel_with_drag(
            vx, vy, vz, air_drag, wx, wz, 0.0, gravity_m_s2, 0.0
        )
        vx_m = vx + ax * (0.5 * step)
        vy_m = vy + ay * (0.5 * step)
        vz_m = vz + az * (0.5 * step)
        ax_m, ay_m, az_m = _accel_with_drag(
            vx_m, vy_m, vz_m, air_drag, wx, wz, 0.0, gravity_m_s2, 0.0
        )

        x = x + vx_m * step
        y = y + vy_m * step
        z = z + vz_m * step
        vx = vx + ax_m * step
        vy = vy + ay_m * step
        vz = vz + az_m * step
        t = t + step
    return x, y, z, vx, vy, vz


@dataclass
class GroundHit:
    """Point where a ballistic trajectory meets the ground surface."""

    time_offset_s: float
    x_m: float
    y_m: float
    z_m: float
    vx_m_s: float
    vy_m_s: float
    vz_m_s: float


GroundYFn = Callable[[float, float], float]


def flat_ground_y_fn(ground_y_m: float) -> GroundYFn:
    """Constant-altitude ground plane (legacy WLR / unit tests)."""

    def _fn(_x_m: float, _z_m: float) -> float:
        return float(ground_y_m)

    return _fn


def sample_terrain_bilinear(
    terrain: np.ndarray,
    ix: float,
    iz: float,
    fallback_y_m: float,
) -> float:
    """Bilinear sample of terrain[iz, ix] with clamp-to-edge."""
    height, width = terrain.shape
    if height < 1 or width < 1:
        return float(fallback_y_m)
    if ix < 0.0:
        ix = 0.0
    if iz < 0.0:
        iz = 0.0
    if ix > width - 1.0000001:
        ix = width - 1.0000001
    if iz > height - 1.0000001:
        iz = height - 1.0000001
    ix0 = int(math.floor(ix))
    iz0 = int(math.floor(iz))
    ix1 = ix0 + 1
    iz1 = iz0 + 1
    if ix1 >= width:
        ix1 = width - 1
    if iz1 >= height:
        iz1 = height - 1
    tx = ix - ix0
    tz = iz - iz0
    v00 = float(terrain[iz0, ix0])
    v10 = float(terrain[iz0, ix1])
    v01 = float(terrain[iz1, ix0])
    v11 = float(terrain[iz1, ix1])
    if not np.isfinite(v00):
        v00 = fallback_y_m
    if not np.isfinite(v10):
        v10 = fallback_y_m
    if not np.isfinite(v01):
        v01 = fallback_y_m
    if not np.isfinite(v11):
        v11 = fallback_y_m
    v0 = v00 * (1.0 - tx) + v10 * tx
    v1 = v01 * (1.0 - tx) + v11 * tx
    return v0 * (1.0 - tz) + v1 * tz


def dem_ground_y_fn(
    terrain: np.ndarray,
    cell_m: float,
    origin_ix: float,
    origin_iz: float,
    fallback_y_m: float,
) -> GroundYFn:
    """Map radar-relative (x_m, z_m) onto a DEM grid and return absolute terrain Y.

    Radar sits at grid (origin_ix, origin_iz). Ballistic X/Z are metres relative
    to that site (same frame as WLR fit / mass-battle tracks).
    """
    if cell_m <= 1e-6:
        cell_m = 1.0

    def _fn(x_m: float, z_m: float) -> float:
        ix = origin_ix + x_m / cell_m
        iz = origin_iz + z_m / cell_m
        return sample_terrain_bilinear(terrain, ix, iz, fallback_y_m)

    return _fn


def find_ground_intersection(
    x: float,
    y: float,
    z: float,
    vx: float,
    vy: float,
    vz: float,
    ground_y_m: float,
    air_drag: float = 0.0,
    gravity_m_s2: float = GRAVITY_M_S2,
    wind: GlobalWind = NO_WIND,
    *,
    backward: bool = False,
    max_time_s: float = 90.0,
    dt_s: float = 0.05,
    ground_y_fn: GroundYFn | None = None,
) -> GroundHit | None:
    """Integrate until altitude crosses the local ground surface.

    forward  → impact point (weapon impact)
    backward → launch point (weapon origin)
    ground_y_fn(x, z) returns absolute terrain Y; when None, uses flat ground_y_m.
    Returns None if no crossing within max_time_s.
    """
    if ground_y_fn is None:
        ground_y_fn = flat_ground_y_fn(ground_y_m)
    if dt_s < 1e-4:
        dt_s = 1e-4
    step_mag = dt_s
    direction = -1.0 if backward else 1.0
    wx = wind.vx_m_s
    wz = wind.vz_m_s
    t = 0.0

    ground0 = ground_y_fn(x, z)
    height0 = y - ground0
    if height0 <= 0.05:
        if not backward and vy <= 0.0:
            return GroundHit(0.0, x, ground0, z, vx, vy, vz)
        if backward and vy >= 0.0:
            return GroundHit(0.0, x, ground0, z, vx, vy, vz)

    while abs(t) + 1e-12 < max_time_s:
        step = direction * step_mag
        y_prev = y
        x_prev = x
        z_prev = z
        vx_prev = vx
        vy_prev = vy
        vz_prev = vz
        t_prev = t
        ground_prev = ground_y_fn(x_prev, z_prev)
        height_prev = y_prev - ground_prev

        ax, ay, az = _accel_with_drag(
            vx, vy, vz, air_drag, wx, wz, 0.0, gravity_m_s2, 0.0
        )
        vx_m = vx + ax * (0.5 * step)
        vy_m = vy + ay * (0.5 * step)
        vz_m = vz + az * (0.5 * step)
        ax_m, ay_m, az_m = _accel_with_drag(
            vx_m, vy_m, vz_m, air_drag, wx, wz, 0.0, gravity_m_s2, 0.0
        )
        x = x + vx_m * step
        y = y + vy_m * step
        z = z + vz_m * step
        vx = vx + ax_m * step
        vy = vy + ay_m * step
        vz = vz + az_m * step
        t = t + step

        ground = ground_y_fn(x, z)
        height = y - ground
        crossed = (height_prev > 0.0 and height <= 0.0) or (
            height_prev < 0.0 and height >= 0.0
        )
        if not crossed:
            continue

        denom = height_prev - height
        if abs(denom) < 1e-9:
            alpha = 1.0
        else:
            alpha = height_prev / denom
        if alpha < 0.0:
            alpha = 0.0
        if alpha > 1.0:
            alpha = 1.0
        x_hit = x_prev + (x - x_prev) * alpha
        z_hit = z_prev + (z - z_prev) * alpha
        y_hit = ground_y_fn(x_hit, z_hit)
        return GroundHit(
            time_offset_s=t_prev + alpha * step,
            x_m=x_hit,
            y_m=y_hit,
            z_m=z_hit,
            vx_m_s=vx_prev + (vx - vx_prev) * alpha,
            vy_m_s=vy_prev + (vy - vy_prev) * alpha,
            vz_m_s=vz_prev + (vz - vz_prev) * alpha,
        )
    return None


def solve_launch_and_impact(
    x: float,
    y: float,
    z: float,
    vx: float,
    vy: float,
    vz: float,
    ground_y_m: float,
    air_drag: float = 0.0,
    gravity_m_s2: float = GRAVITY_M_S2,
    wind: GlobalWind = NO_WIND,
    max_time_s: float = 90.0,
    ground_y_fn: GroundYFn | None = None,
) -> tuple[GroundHit | None, GroundHit | None]:
    """From a mid-flight ballistic state, recover launch and impact points."""
    launch = find_ground_intersection(
        x,
        y,
        z,
        vx,
        vy,
        vz,
        ground_y_m,
        air_drag=air_drag,
        gravity_m_s2=gravity_m_s2,
        wind=wind,
        backward=True,
        max_time_s=max_time_s,
        ground_y_fn=ground_y_fn,
    )
    impact = find_ground_intersection(
        x,
        y,
        z,
        vx,
        vy,
        vz,
        ground_y_m,
        air_drag=air_drag,
        gravity_m_s2=gravity_m_s2,
        wind=wind,
        backward=False,
        max_time_s=max_time_s,
        ground_y_fn=ground_y_fn,
    )
    return launch, impact


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
