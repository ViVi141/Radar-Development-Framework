#!/usr/bin/env python3
"""Track association and alpha-beta filtering for RDF scan detections.

This is a framework-level track layer, not a specific radar's tracker.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Protocol

import numpy as np


class DetectionLike(Protocol):
    time_s: float
    scan_number: int
    target_name: str
    measured_range_m: float
    azimuth_deg: float
    radial_speed_m_s: float
    snr_db: float
    detected: bool


@dataclass
class TrackState:
    track_id: int
    time_s: float
    range_m: float
    azimuth_deg: float
    range_rate_m_s: float
    snr_db: float
    hit_count: int = 1
    miss_count: int = 0
    last_target_name: str = ""
    elevation_deg: float = 0.0
    last_doppler_bin: int = -1
    last_prf_index: int = 0
    last_prf_hz: float = 0.0
    coasting: bool = False
    coast_elapsed_s: float = 0.0
    soft_miss_streak: int = 0
    x_m: float = 0.0
    y_m: float = 0.0
    z_m: float = 0.0
    vx_m_s: float = 0.0
    vy_m_s: float = 0.0
    vz_m_s: float = 0.0
    # (time_s, range_m, azimuth_deg, elevation_deg)
    history: list[tuple[float, float, float, float]] = field(default_factory=list)


@dataclass
class TrackerConfig:
    gate_range_m: float = 400.0
    gate_azimuth_deg: float = 4.0
    alpha: float = 0.5
    beta: float = 0.2
    max_misses: int = 3
    confirm_hits: int = 2
    # Keep confirmed tracks in the result even after they go stale
    # (target left coverage / impacted). Useful for offline analysis.
    keep_confirmed_dead: bool = False
    # On miss, advance filtered kinematics (Enforce m_TrackCoastOnMiss).
    coast_on_miss: bool = False
    # Widen association gates while coasting / after near-zero Doppler hit.
    coast_on_doppler_null: bool = False
    coast_gate_scale: float = 1.5
    coast_gate_grow_per_miss: float = 0.25
    coast_max_s: float = 0.0
    soft_miss_on_blind: bool = True
    # Multi-PRF blind-speed de-blind (λ·PRF/2 notches).
    enable_prf_deblind: bool = False
    wavelength_m: float = 0.0333
    primary_prf_hz: float = 4000.0
    prf_set_hz: list[float] = field(default_factory=list)
    blind_speed_tol_ms: float = 8.0
    blind_extra_misses: int = 2


def near_blind_speed(
    radial_ms: float,
    prf_hz: float,
    wavelength_m: float,
    tol_ms: float = 8.0,
    harmonics: int = 4,
) -> bool:
    """True when |vr| is near n·λ·PRF/2 for n = 0..harmonics."""
    if prf_hz < 1.0 or wavelength_m <= 0.0:
        return False
    step = 0.5 * wavelength_m * prf_hz
    if step < 0.001:
        return False
    tol = tol_ms
    if tol < 0.5:
        tol = 0.5
    vr = abs(float(radial_ms))
    for n in range(0, harmonics + 1):
        if abs(vr - step * n) <= tol:
            return True
    return False


def _resolve_prf_hz(config: TrackerConfig, prf_index: int) -> float:
    prfs = list(config.prf_set_hz) if config.prf_set_hz else []
    if not prfs:
        prfs = [config.primary_prf_hz]
    idx = int(prf_index)
    if idx < 0:
        idx = 0
    prf = float(prfs[idx % len(prfs)])
    if prf < 1.0:
        prf = config.primary_prf_hz
    return prf


def _near_any_prf_blind(config: TrackerConfig, radial_ms: float) -> bool:
    if not config.enable_prf_deblind:
        return False
    prfs = list(config.prf_set_hz) if config.prf_set_hz else [config.primary_prf_hz]
    for prf in prfs:
        if near_blind_speed(
            radial_ms, float(prf), config.wavelength_m, config.blind_speed_tol_ms
        ):
            return True
    return False


def _effective_max_misses(track: TrackState, config: TrackerConfig) -> int:
    max_miss = config.max_misses
    if not config.enable_prf_deblind:
        return max_miss
    prf_hz = track.last_prf_hz
    if prf_hz < 1.0:
        prf_hz = _resolve_prf_hz(config, track.last_prf_index)
    near = near_blind_speed(
        track.range_rate_m_s, prf_hz, config.wavelength_m, config.blind_speed_tol_ms
    )
    if not near:
        near = _near_any_prf_blind(config, track.range_rate_m_s)
    if near:
        max_miss = max_miss + config.blind_extra_misses
    return max_miss


@dataclass
class TrackerResult:
    tracks: list[TrackState]
    associations: list[tuple[int, DetectionLike]]


def _angle_diff_deg(a: float, b: float) -> float:
    return (a - b + 180.0) % 360.0 - 180.0


def _plot_elevation_deg(plot: DetectionLike) -> float:
    return float(getattr(plot, "elevation_deg", 0.0))


def _sync_polar_from_cart(track: TrackState) -> None:
    r, az, el = cartesian_to_polar(track.x_m, track.y_m, track.z_m)
    track.range_m = r
    track.azimuth_deg = az
    track.elevation_deg = el
    if r > 1.0e-3:
        los_x = track.x_m / r
        los_y = track.y_m / r
        los_z = track.z_m / r
        track.range_rate_m_s = (
            track.vx_m_s * los_x + track.vy_m_s * los_y + track.vz_m_s * los_z
        )


def _predict_polar(
    track: TrackState, time_s: float
) -> tuple[float, float, float, float]:
    dt = time_s - track.time_s
    if dt < 0.0:
        dt = 0.0
    x = track.x_m + track.vx_m_s * dt
    y = track.y_m + track.vy_m_s * dt
    z = track.z_m + track.vz_m_s * dt
    r, az, el = cartesian_to_polar(x, y, z)
    rr = track.range_rate_m_s
    if r > 1.0e-3:
        rr = (track.vx_m_s * x + track.vy_m_s * y + track.vz_m_s * z) / r
    return r, az, el, rr


def _set_cart_from_polar(
    track: TrackState,
    range_m: float,
    azimuth_deg: float,
    elevation_deg: float,
    range_rate_m_s: float,
) -> None:
    x, y, z = polar_to_cartesian(range_m, azimuth_deg, elevation_deg)
    track.x_m = x
    track.y_m = y
    track.z_m = z
    r = max(range_m, 1.0e-3)
    track.vx_m_s = range_rate_m_s * (x / r)
    track.vy_m_s = range_rate_m_s * (y / r)
    track.vz_m_s = range_rate_m_s * (z / r)


def _coast_track(track: TrackState, coast_time: float) -> None:
    dt = coast_time - track.time_s
    if dt <= 0.0:
        return
    track.x_m = track.x_m + track.vx_m_s * dt
    track.y_m = track.y_m + track.vy_m_s * dt
    track.z_m = track.z_m + track.vz_m_s * dt
    _sync_polar_from_cart(track)
    track.time_s = coast_time
    track.coasting = True
    track.coast_elapsed_s = track.coast_elapsed_s + dt


def associate_and_filter(
    detections: list[DetectionLike],
    config: TrackerConfig | None = None,
) -> TrackerResult:
    """Nearest-neighbor association of CFAR hits + alpha-beta filter."""
    if config is None:
        config = TrackerConfig()

    plots = [d for d in detections if d.detected]
    plots.sort(key=lambda d: (d.scan_number, d.time_s))

    tracks: list[TrackState] = []
    associations: list[tuple[int, DetectionLike]] = []
    next_id = 1

    scan_numbers = sorted({p.scan_number for p in plots})
    for scan in scan_numbers:
        scan_plots = [p for p in plots if p.scan_number == scan]
        used = set()

        pairs: list[tuple[float, int, int]] = []
        for ti, track in enumerate(tracks):
            if track.miss_count > _effective_max_misses(track, config):
                continue
            if (
                config.coast_max_s > 0.0
                and track.coasting
                and track.coast_elapsed_s > config.coast_max_s
            ):
                continue
            grow = 1.0 + config.coast_gate_grow_per_miss * track.miss_count
            if grow < 1.0:
                grow = 1.0
            gate_range = config.gate_range_m * grow
            gate_az = config.gate_azimuth_deg * grow
            widen = False
            if config.coast_on_doppler_null:
                if track.last_doppler_bin == 0 or track.coasting:
                    widen = True
            if config.enable_prf_deblind:
                track_prf = track.last_prf_hz
                if track_prf < 1.0:
                    track_prf = _resolve_prf_hz(config, track.last_prf_index)
                if near_blind_speed(
                    track.range_rate_m_s,
                    track_prf,
                    config.wavelength_m,
                    config.blind_speed_tol_ms,
                ):
                    widen = True
            if widen:
                gate_range = gate_range * config.coast_gate_scale
                gate_az = gate_az * config.coast_gate_scale
            for pi, plot in enumerate(scan_plots):
                plot_prf = _resolve_prf_hz(
                    config, int(getattr(plot, "prf_index", 0) or 0)
                )
                gate_r = gate_range
                gate_a = gate_az
                if config.enable_prf_deblind and near_blind_speed(
                    plot.radial_speed_m_s,
                    plot_prf,
                    config.wavelength_m,
                    config.blind_speed_tol_ms,
                ):
                    gate_r = gate_r * config.coast_gate_scale
                    gate_a = gate_a * config.coast_gate_scale
                pred_range, pred_az, _pred_el, _pred_rr = _predict_polar(
                    track, plot.time_s
                )
                d_range = abs(plot.measured_range_m - pred_range)
                d_az = abs(_angle_diff_deg(plot.azimuth_deg, pred_az))
                if d_range > gate_r:
                    continue
                if d_az > gate_a:
                    continue
                cost = d_range / max(gate_r, 1.0) + d_az / max(gate_a, 0.1)
                pairs.append((cost, ti, pi))
        pairs.sort(key=lambda item: item[0])

        assigned_tracks = set()
        assigned_plots = set()
        for _cost, ti, pi in pairs:
            if ti in assigned_tracks or pi in assigned_plots:
                continue
            assigned_tracks.add(ti)
            assigned_plots.add(pi)
            used.add(pi)
            track = tracks[ti]
            plot = scan_plots[pi]
            dt = plot.time_s - track.time_s
            if dt < 1e-6:
                dt = 1e-6
            pred_range, pred_az, pred_el, pred_rr = _predict_polar(track, plot.time_s)
            residual = plot.measured_range_m - pred_range
            track.range_m = pred_range + config.alpha * residual
            track.range_rate_m_s = pred_rr + (config.beta / dt) * residual
            az_err = _angle_diff_deg(plot.azimuth_deg, pred_az)
            track.azimuth_deg = (pred_az + config.alpha * az_err) % 360.0
            el_meas = _plot_elevation_deg(plot)
            el_err = el_meas - pred_el
            track.elevation_deg = pred_el + config.alpha * el_err
            track.time_s = plot.time_s
            track.snr_db = plot.snr_db
            track.hit_count = track.hit_count + 1
            track.miss_count = 0
            track.coasting = False
            track.coast_elapsed_s = 0.0
            track.soft_miss_streak = 0
            track.last_doppler_bin = int(getattr(plot, "doppler_bin", -1))
            track.last_prf_index = int(getattr(plot, "prf_index", 0) or 0)
            track.last_prf_hz = _resolve_prf_hz(config, track.last_prf_index)
            track.last_target_name = plot.target_name
            _set_cart_from_polar(
                track,
                track.range_m,
                track.azimuth_deg,
                track.elevation_deg,
                track.range_rate_m_s,
            )
            track.history.append(
                (plot.time_s, track.range_m, track.azimuth_deg, track.elevation_deg)
            )
            associations.append((track.track_id, plot))

        # Nominal scan time for coasting unassigned tracks.
        coast_time = scan_plots[0].time_s if scan_plots else 0.0
        for ti, track in enumerate(tracks):
            if ti in assigned_tracks:
                continue
            soft = False
            if config.coast_on_doppler_null or (
                config.soft_miss_on_blind and config.enable_prf_deblind
            ):
                if track.last_doppler_bin == 0:
                    soft = True
                prf_hz = track.last_prf_hz
                if prf_hz < 1.0:
                    prf_hz = _resolve_prf_hz(config, track.last_prf_index)
                if near_blind_speed(
                    track.range_rate_m_s,
                    prf_hz,
                    config.wavelength_m,
                    config.blind_speed_tol_ms,
                ):
                    soft = True
                if _near_any_prf_blind(config, track.range_rate_m_s):
                    soft = True
            if soft and track.soft_miss_streak < config.blind_extra_misses:
                track.soft_miss_streak = track.soft_miss_streak + 1
            else:
                track.miss_count = track.miss_count + 1
                track.soft_miss_streak = 0
            if config.coast_on_miss and coast_time > track.time_s:
                _coast_track(track, coast_time)

        for pi, plot in enumerate(scan_plots):
            if pi in used:
                continue
            el0 = _plot_elevation_deg(plot)
            az0 = plot.azimuth_deg % 360.0
            prf_idx = int(getattr(plot, "prf_index", 0) or 0)
            track = TrackState(
                track_id=next_id,
                time_s=plot.time_s,
                range_m=plot.measured_range_m,
                azimuth_deg=az0,
                range_rate_m_s=plot.radial_speed_m_s,
                snr_db=plot.snr_db,
                hit_count=1,
                miss_count=0,
                last_target_name=plot.target_name,
                elevation_deg=el0,
                last_prf_index=prf_idx,
                last_prf_hz=_resolve_prf_hz(config, prf_idx),
                history=[(plot.time_s, plot.measured_range_m, az0, el0)],
            )
            _set_cart_from_polar(
                track,
                plot.measured_range_m,
                az0,
                el0,
                plot.radial_speed_m_s,
            )
            next_id = next_id + 1
            tracks.append(track)
            associations.append((track.track_id, plot))

    alive = []
    for track in tracks:
        confirmed = track.hit_count >= config.confirm_hits
        max_miss = _effective_max_misses(track, config)
        if (
            config.coast_max_s > 0.0
            and track.coasting
            and track.coast_elapsed_s > config.coast_max_s
        ):
            if not (config.keep_confirmed_dead and confirmed):
                continue
        if config.keep_confirmed_dead and confirmed:
            alive.append(track)
            continue
        if track.miss_count > max_miss and not confirmed:
            continue
        if track.miss_count > max_miss + 2:
            continue
        alive.append(track)

    return TrackerResult(tracks=alive, associations=associations)


def confirmed_tracks(
    result: TrackerResult,
    min_hits: int = 2,
) -> list[TrackState]:
    out = []
    for track in result.tracks:
        if track.hit_count >= min_hits:
            out.append(track)
    return out


def polar_to_cartesian(
    range_m: float,
    azimuth_deg: float,
    elevation_deg: float,
) -> tuple[float, float, float]:
    """Radar-centered Cartesian: +X east, +Y up, +Z north."""
    az = math.radians(azimuth_deg)
    el = math.radians(elevation_deg)
    horiz = range_m * math.cos(el)
    x = horiz * math.cos(az)
    y = range_m * math.sin(el)
    z = horiz * math.sin(az)
    return x, y, z


def cartesian_to_polar(x: float, y: float, z: float) -> tuple[float, float, float]:
    range_m = math.sqrt(x * x + y * y + z * z)
    if range_m < 1e-6:
        return 0.0, 0.0, 0.0
    az = math.degrees(math.atan2(z, x)) % 360.0
    el = math.degrees(math.atan2(y, math.hypot(x, z)))
    return range_m, az, el


@dataclass
class BallisticState:
    """Ballistic state at an absolute time origin (t=0 at anchor)."""

    t_anchor_s: float
    x_m: float
    y_m: float
    z_m: float
    vx_m_s: float
    vy_m_s: float
    vz_m_s: float
    ay_m_s2: float = -9.81
    # Reforger ShellMoveComponent.AirDrag; 0 = vacuum propagation.
    air_drag: float = 0.0
    fit_rms_m: float = 0.0
    fit_span_s: float = 0.0
    fit_points: int = 0


def fit_ballistic_vacuum(
    history: list[tuple[float, float, float, float]],
    anchor_index: int,
    gravity_m_s2: float = -9.81,
    min_points: int = 5,
    min_span_s: float = 1.0,
    max_fit_rms_m: float = 80.0,
    air_drag: float = 0.0,
    window_points: int = 20,
) -> BallisticState | None:
    """Least-squares vacuum ballistic fit using history up to anchor_index.

    Horizontal axes: constant velocity. Vertical: fixed gravity acceleration.
    Instantaneous state is estimated under vacuum; forward prediction may then
    apply Reforger-style AirDrag via predict_ballistic(air_drag=...).
    """
    if anchor_index < 0 or anchor_index >= len(history):
        return None
    if anchor_index + 1 < min_points:
        return None

    if window_points < min_points:
        window_points = min_points
    window = history[max(0, anchor_index - window_points + 1) : anchor_index + 1]
    if len(window) < min_points:
        return None
    span = window[-1][0] - window[0][0]
    if span < min_span_s:
        return None

    t_anchor = window[-1][0]
    times = []
    xs = []
    ys = []
    zs = []
    for t, r, az, el in window:
        x, y, z = polar_to_cartesian(r, az, el)
        times.append(t - t_anchor)
        xs.append(x)
        ys.append(y)
        zs.append(z)

    # Vertical: y = y0 + vy*t + 0.5*g*t^2  =>  y - 0.5*g*t^2 = y0 + vy*t
    # Horizontal: x = x0 + vx*t, z = z0 + vz*t
    t_arr = np.asarray(times, dtype=float)
    x_arr = np.asarray(xs, dtype=float)
    y_arr = np.asarray(ys, dtype=float)
    z_arr = np.asarray(zs, dtype=float)
    y_lin = y_arr - 0.5 * gravity_m_s2 * t_arr * t_arr

    a_h = np.column_stack([np.ones_like(t_arr), t_arr])
    try:
        bx, *_ = np.linalg.lstsq(a_h, x_arr, rcond=None)
        bz, *_ = np.linalg.lstsq(a_h, z_arr, rcond=None)
        by, *_ = np.linalg.lstsq(a_h, y_lin, rcond=None)
    except np.linalg.LinAlgError:
        return None

    x_hat = a_h @ bx
    y_hat = a_h @ by + 0.5 * gravity_m_s2 * t_arr * t_arr
    z_hat = a_h @ bz
    resid = np.sqrt((x_arr - x_hat) ** 2 + (y_arr - y_hat) ** 2 + (z_arr - z_hat) ** 2)
    rms = float(np.sqrt(np.mean(resid * resid)))
    if rms > max_fit_rms_m:
        return None

    return BallisticState(
        t_anchor_s=t_anchor,
        x_m=float(bx[0]),
        y_m=float(by[0]),
        z_m=float(bz[0]),
        vx_m_s=float(bx[1]),
        vy_m_s=float(by[1]),
        vz_m_s=float(bz[1]),
        ay_m_s2=gravity_m_s2,
        air_drag=air_drag,
        fit_rms_m=rms,
        fit_span_s=span,
        fit_points=len(window),
    )


def predict_ballistic(
    state: BallisticState,
    horizon_s: float,
    air_drag: float | None = None,
    wind=None,
) -> tuple[float, float, float]:
    """Return (range_m, az_deg, el_deg) at anchor + horizon.

    If air_drag (or state.air_drag) > 0, integrate Reforger-style quadratic drag
    on airspeed (optionally against a known global wind) plus gravity; otherwise
    closed-form vacuum parabola.
    """
    from rdf_radar_targets import NO_WIND, integrate_ballistic

    drag = state.air_drag if air_drag is None else air_drag
    if drag is None:
        drag = 0.0
    if drag <= 0.0:
        t = horizon_s
        x = state.x_m + state.vx_m_s * t
        y = state.y_m + state.vy_m_s * t + 0.5 * state.ay_m_s2 * t * t
        z = state.z_m + state.vz_m_s * t
        return cartesian_to_polar(x, y, z)

    if wind is None:
        wind = NO_WIND
    x, y, z, _vx, _vy, _vz = integrate_ballistic(
        state.x_m,
        state.y_m,
        state.z_m,
        state.vx_m_s,
        state.vy_m_s,
        state.vz_m_s,
        horizon_s,
        air_drag=drag,
        gravity_m_s2=state.ay_m_s2,
        wind=wind,
    )
    return cartesian_to_polar(x, y, z)
