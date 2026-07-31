#!/usr/bin/env python3
"""Time-domain mechanical/electronic scan framework for RDF radar models."""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

from rdf_radar_channel import (
    MultipathModel,
    SwerlingModel,
    clutter_sigma0_frequency_scale,
    hardware_at_frequency,
    radar_constant_scale,
)
from rdf_radar_ew import EWContext, EWStack, FrequencyHopSchedule
from rdf_radar_physics import (
    RadarHardware,
    ca_cfar_detections,
    doppler_hz,
    elevation_beam_gains,
    mti_apply_target,
    processed_power_w,
    received_power_w,
)
from rdf_radar_sector_sim import SectorResult
from rdf_radar_targets import TargetTrajectory, radial_speed_toward_radar
from rdf_radar_track import TrackerConfig, TrackerResult, associate_and_filter


def _wrap_pi(angle: float) -> float:
    return float((angle + math.pi) % (2.0 * math.pi) - math.pi)


@dataclass
class ScanConfig:
    duration_s: float = 20.0
    time_step_s: float = 0.0
    start_azimuth_deg: float = 0.0
    direction: int = 1
    ew_stack: EWStack = field(default_factory=EWStack)
    frequency_hop: FrequencyHopSchedule = field(default_factory=FrequencyHopSchedule)
    multipath: MultipathModel = field(default_factory=MultipathModel)
    swerling: SwerlingModel = field(default_factory=lambda: SwerlingModel(model=1))
    tracker: TrackerConfig = field(default_factory=TrackerConfig)
    radar_height_agl_m: float = 25.0


@dataclass
class ScanDetection:
    time_s: float
    scan_number: int
    target_name: str
    measured_range_m: float
    azimuth_deg: float
    elevation_deg: float
    radial_speed_m_s: float
    doppler_hz: float
    beam_name: str
    snr_db: float
    detected: bool
    frequency_hz: float
    rcs_instant_m2: float = 0.0
    multipath_factor: float = 1.0


@dataclass
class ScanHistory:
    times_s: np.ndarray
    boresight_rad: np.ndarray
    scan_number: np.ndarray
    accumulated_power_w: np.ndarray
    accumulated_cfar: np.ndarray
    last_update_s: np.ndarray
    detections: list[ScanDetection]
    target_truth: dict[str, list[tuple[float, float, float]]]
    frequencies_hz: np.ndarray
    tracker: TrackerResult | None = None


def boresight_at(
    time_s: float,
    hardware: RadarHardware,
    start_azimuth_deg: float,
    direction: int,
) -> float:
    """Mechanical scan angle in radians, wrapped to [-π, π)."""
    sign = 1
    if direction < 0:
        sign = -1
    angular_rate = sign * 2.0 * math.pi * hardware.scan_rpm / 60.0
    start = math.radians(start_azimuth_deg)
    return _wrap_pi(start + angular_rate * time_s)


def _terrain_los_clear(
    terrain: np.ndarray,
    cell_m: float,
    radar_ix: int,
    radar_iz: int,
    radar_y: float,
    target_ix: float,
    target_iz: float,
    target_y: float,
) -> bool:
    """Terrain-only LOS for moving tracks; span LOS remains in sector snapshot."""
    height, width = terrain.shape
    dx = target_ix - radar_ix
    dz = target_iz - radar_iz
    steps = int(max(abs(dx), abs(dz)))
    if steps < 2:
        return True
    for step in range(1, steps):
        fraction = step / float(steps)
        ix = int(round(radar_ix + dx * fraction))
        iz = int(round(radar_iz + dz * fraction))
        if ix < 0 or ix >= width or iz < 0 or iz >= height:
            return False
        terrain_y = float(terrain[iz, ix])
        if not np.isfinite(terrain_y):
            continue
        ray_y = radar_y + (target_y - radar_y) * fraction
        if terrain_y > ray_y:
            return False
    return True


def simulate_scan(
    dem: dict,
    hardware: RadarHardware,
    sector: SectorResult,
    trajectories: list[TargetTrajectory],
    config: ScanConfig,
) -> ScanHistory:
    """Run scan(t), update one azimuth dwell at a time, and produce plots/tracks."""
    terrain = np.asarray(dem["terrain"])
    cell_m = float(dem["cell_m"])
    radar_ix = int(dem["_radar_ix"])
    radar_iz = int(dem["_radar_iz"])
    radar_y = sector.radar_y
    naz, nbin = sector.clutter_mti_w.shape

    time_step_s = config.time_step_s
    if time_step_s <= 0.0:
        if hardware.scan_rpm > 0.0:
            time_step_s = hardware.scan_period_s / float(naz)
        else:
            time_step_s = 0.1
    count = int(math.ceil(config.duration_s / time_step_s)) + 1
    times = np.arange(count, dtype=np.float64) * time_step_s
    boresights = np.zeros(count, dtype=np.float64)
    scans = np.zeros(count, dtype=np.int32)
    frequencies = np.zeros(count, dtype=np.float64)

    accumulated = np.zeros((naz, nbin), dtype=np.float64)
    accumulated_cfar = np.zeros((naz, nbin), dtype=bool)
    last_update = np.full(naz, -1.0, dtype=np.float64)
    detections: list[ScanDetection] = []
    truth: dict[str, list[tuple[float, float, float]]] = {}
    for trajectory in trajectories:
        truth[trajectory.initial.name] = []

    base_noise_proc = sector.noise_proc_w
    base_const = hardware.radar_constant()

    for time_i, time_s in enumerate(times):
        boresight = boresight_at(
            float(time_s),
            hardware,
            config.start_azimuth_deg,
            config.direction,
        )
        boresights[time_i] = boresight
        if hardware.scan_period_s < 1.0e8:
            scans[time_i] = int(time_s / hardware.scan_period_s)
        frequency = config.frequency_hop.frequency_at(
            float(time_s),
            hardware.frequency_hz,
        )
        frequencies[time_i] = frequency

        # Per-dwell retuned hardware: λ, atm, band label.
        dwell_hw = hardware_at_frequency(hardware, frequency)
        rf_scale = radar_constant_scale(hardware, dwell_hw)
        sigma0_scale = clutter_sigma0_frequency_scale(
            hardware.frequency_hz,
            frequency,
        )
        clutter_scale = rf_scale * sigma0_scale
        # Noise scales with λ⁰ here (kTBF); bandwidth unchanged → keep NF path.
        # Processing gain unchanged for fixed waveform. Approximate noise retune
        # via radar-constant-independent thermal term (same B, NF).
        noise_proc = processed_power_w(dwell_hw, dwell_hw.noise_power_w())

        az_delta = (sector.az_rad - boresight + math.pi) % (2.0 * math.pi) - math.pi
        az_i = int(np.argmin(np.abs(az_delta)))
        dwell_power = sector.clutter_mti_w[az_i : az_i + 1].copy() * clutter_scale

        target_cells: list[tuple[int, ScanDetection]] = []
        for trajectory in trajectories:
            target = trajectory.sample(float(time_s), cell_m)
            truth[target.name].append((float(time_s), target.ix, target.iz))
            dx_m = (target.ix - radar_ix) * cell_m
            dz_m = (target.iz - radar_iz) * cell_m
            dy_m = target.y_m - radar_y
            range_m = math.sqrt(dx_m * dx_m + dy_m * dy_m + dz_m * dz_m)
            if range_m < 1.0 or range_m > sector.range_centers_m[-1]:
                continue

            target_az = math.atan2(dz_m, dx_m)
            az_offset = _wrap_pi(target_az - boresight)
            if abs(math.degrees(az_offset)) > dwell_hw.az_beamwidth_deg * 0.5:
                continue
            if not _terrain_los_clear(
                terrain,
                cell_m,
                radar_ix,
                radar_iz,
                radar_y,
                target.ix,
                target.iz,
                target.y_m,
            ):
                continue

            elevation_deg = math.degrees(math.asin(np.clip(dy_m / range_m, -1.0, 1.0)))
            beam_gains = elevation_beam_gains(
                dwell_hw,
                elevation_deg,
                math.degrees(az_offset),
            )
            if not beam_gains:
                continue
            beam_name = max(beam_gains, key=beam_gains.get)
            pattern = beam_gains[beam_name]

            rcs_inst = config.swerling.sample(
                target.rcs_m2,
                target.name,
                int(scans[time_i]),
                time_i,
            )
            ground_y = float(terrain[radar_iz, radar_ix])
            if not np.isfinite(ground_y):
                ground_y = radar_y - config.radar_height_agl_m
            tgt_ix_i = int(round(target.ix))
            tgt_iz_i = int(round(target.iz))
            tgt_ground = ground_y
            if 0 <= tgt_iz_i < terrain.shape[0] and 0 <= tgt_ix_i < terrain.shape[1]:
                g = float(terrain[tgt_iz_i, tgt_ix_i])
                if np.isfinite(g):
                    tgt_ground = g
            multipath = config.multipath.power_factor(
                dwell_hw.wavelength_m,
                range_m,
                max(radar_y - ground_y, 1.0),
                max(target.y_m - tgt_ground, 1.0),
            )

            rf_power = received_power_w(
                dwell_hw,
                rcs_inst,
                range_m,
                pattern,
            )
            rf_power = rf_power * multipath
            proc_power = processed_power_w(dwell_hw, rf_power)
            radial_speed = radial_speed_toward_radar(
                target,
                float(radar_ix),
                float(radar_iz),
                radar_y,
                cell_m,
            )
            tip = float(getattr(target, "rotor_tip_m_s", 0.0) or 0.0)
            rotor_frac = float(getattr(target, "rotor_rcs_fraction", 0.0) or 0.0)
            hub = float(getattr(target, "hub_width_m_s", 0.0) or 0.0)
            if tip <= 0.0 and str(getattr(target, "kind", "")) == "heli":
                tip = 220.0
                rotor_frac = 0.35
                hub = 40.0
            target_power = mti_apply_target(
                dwell_hw,
                proc_power,
                radial_speed,
                tip_speed_m_s=tip,
                rotor_rcs_fraction=rotor_frac,
                hub_width_m_s=hub,
            )
            range_i = int(np.argmin(np.abs(sector.range_centers_m - range_m)))
            dwell_power[0, range_i] = dwell_power[0, range_i] + target_power

            snr = target_power / max(noise_proc, 1e-30)
            measurement = ScanDetection(
                time_s=float(time_s),
                scan_number=int(scans[time_i]),
                target_name=target.name,
                measured_range_m=range_m,
                azimuth_deg=math.degrees(target_az),
                elevation_deg=elevation_deg,
                radial_speed_m_s=radial_speed,
                doppler_hz=doppler_hz(radial_speed, dwell_hw.wavelength_m),
                beam_name=beam_name,
                snr_db=10.0 * math.log10(max(snr, 1e-30)),
                detected=False,
                frequency_hz=frequency,
                rcs_instant_m2=rcs_inst,
                multipath_factor=multipath,
            )
            target_cells.append((range_i, measurement))

        context = EWContext(
            hardware=dwell_hw,
            radar_x_m=radar_ix * cell_m,
            radar_z_m=radar_iz * cell_m,
            az_rad=np.array([boresight], dtype=np.float64),
            range_centers_m=sector.range_centers_m,
            noise_w=noise_proc,
            time_s=float(time_s),
            frequency_hz=frequency,
        )
        dwell_power = config.ew_stack.apply(dwell_power, context)
        dwell_cfar = ca_cfar_detections(
            dwell_power,
            noise_proc,
        )
        accumulated[az_i] = dwell_power[0]
        accumulated_cfar[az_i] = dwell_cfar[0]
        last_update[az_i] = time_s

        for range_i, measurement in target_cells:
            measurement.detected = bool(dwell_cfar[0, range_i])
            detections.append(measurement)

    tracker = associate_and_filter(detections, config.tracker)

    # Silence unused warning for base_noise_proc / base_const if not needed.
    _ = base_noise_proc
    _ = base_const

    return ScanHistory(
        times_s=times,
        boresight_rad=boresights,
        scan_number=scans,
        accumulated_power_w=accumulated,
        accumulated_cfar=accumulated_cfar,
        last_update_s=last_update,
        detections=detections,
        target_truth=truth,
        frequencies_hz=frequencies,
        tracker=tracker,
    )
