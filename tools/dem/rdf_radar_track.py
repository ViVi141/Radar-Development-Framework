#!/usr/bin/env python3
"""Track association and alpha-beta filtering for RDF scan detections.

This is a framework-level track layer, not a specific radar's tracker.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol


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
    history: list[tuple[float, float, float]] = field(default_factory=list)


@dataclass
class TrackerConfig:
    gate_range_m: float = 400.0
    gate_azimuth_deg: float = 4.0
    alpha: float = 0.5
    beta: float = 0.2
    max_misses: int = 3
    confirm_hits: int = 2


@dataclass
class TrackerResult:
    tracks: list[TrackState]
    associations: list[tuple[int, DetectionLike]]


def _angle_diff_deg(a: float, b: float) -> float:
    return (a - b + 180.0) % 360.0 - 180.0


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
            track.time_s = plot.time_s
            track.snr_db = plot.snr_db
            track.hit_count = track.hit_count + 1
            track.miss_count = 0
            track.last_target_name = plot.target_name
            track.history.append(
                (plot.time_s, track.range_m, track.azimuth_deg)
            )
            associations.append((track.track_id, plot))

        for ti, track in enumerate(tracks):
            if ti in assigned_tracks:
                continue
            track.miss_count = track.miss_count + 1

        for pi, plot in enumerate(scan_plots):
            if pi in used:
                continue
            track = TrackState(
                track_id=next_id,
                time_s=plot.time_s,
                range_m=plot.measured_range_m,
                azimuth_deg=plot.azimuth_deg % 360.0,
                range_rate_m_s=plot.radial_speed_m_s,
                snr_db=plot.snr_db,
                hit_count=1,
                miss_count=0,
                last_target_name=plot.target_name,
                history=[(plot.time_s, plot.measured_range_m, plot.azimuth_deg % 360.0)],
            )
            next_id = next_id + 1
            tracks.append(track)
            associations.append((track.track_id, plot))

    alive = []
    for track in tracks:
        if track.miss_count > config.max_misses and track.hit_count < config.confirm_hits:
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
