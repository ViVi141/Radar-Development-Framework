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


@dataclass
class TrackerResult:
    tracks: list[TrackState]
    associations: list[tuple[int, DetectionLike]]


def _angle_diff_deg(a: float, b: float) -> float:
    return (a - b + 180.0) % 360.0 - 180.0


def _plot_elevation_deg(plot: DetectionLike) -> float:
    return float(getattr(plot, "elevation_deg", 0.0))


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
            if track.miss_count > config.max_misses:
                continue
            for pi, plot in enumerate(scan_plots):
                dt = plot.time_s - track.time_s
                if dt < 0.0:
                    dt = 0.0
                pred_range = track.range_m + track.range_rate_m_s * dt
                d_range = abs(plot.measured_range_m - pred_range)
                d_az = abs(_angle_diff_deg(plot.azimuth_deg, track.azimuth_deg))
                if d_range > config.gate_range_m:
                    continue
                if d_az > config.gate_azimuth_deg:
                    continue
                cost = d_range / max(config.gate_range_m, 1.0) + d_az / max(
                    config.gate_azimuth_deg, 0.1
                )
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
            pred_range = track.range_m + track.range_rate_m_s * dt
            residual = plot.measured_range_m - pred_range
            track.range_m = pred_range + config.alpha * residual
            track.range_rate_m_s = track.range_rate_m_s + (config.beta / dt) * residual
            az_err = _angle_diff_deg(plot.azimuth_deg, track.azimuth_deg)
            track.azimuth_deg = (track.azimuth_deg + config.alpha * az_err) % 360.0
            el_meas = _plot_elevation_deg(plot)
            el_err = el_meas - track.elevation_deg
            track.elevation_deg = track.elevation_deg + config.alpha * el_err
            track.time_s = plot.time_s
            track.snr_db = plot.snr_db
            track.hit_count = track.hit_count + 1
            track.miss_count = 0
            track.last_target_name = plot.target_name
            track.history.append(
                (plot.time_s, track.range_m, track.azimuth_deg, track.elevation_deg)
            )
            associations.append((track.track_id, plot))

        for ti, track in enumerate(tracks):
            if ti in assigned_tracks:
                continue
            track.miss_count = track.miss_count + 1

        for pi, plot in enumerate(scan_plots):
            if pi in used:
                continue
            el0 = _plot_elevation_deg(plot)
            az0 = plot.azimuth_deg % 360.0
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
                history=[(plot.time_s, plot.measured_range_m, az0, el0)],
            )
            next_id = next_id + 1
            tracks.append(track)
            associations.append((track.track_id, plot))

    alive = []
    for track in tracks:
        confirmed = track.hit_count >= config.confirm_hits
        if config.keep_confirmed_dead and confirmed:
            alive.append(track)
            continue
        if track.miss_count > config.max_misses and not confirmed:
            continue
        if track.miss_count > config.max_misses + 2:
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
