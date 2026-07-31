#!/usr/bin/env python3
"""Sector raymarch clutter + target injection on RDF DEM (absolute RF).

Uses:
  Pr = Pt Gt Gr λ² σ / ((4π)³ R⁴ L)     [rdf_radar_physics]
  σ_clutter = σ⁰(surface, grazing) * A_proj
  antenna elevation pattern (scan az on-boresight)
  two-pulse MTI vs Doppler
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from rdf_radar_materials import Sigma0Table, canopy_sigma0_linear, sigma0_linear
from rdf_radar_physics import (
    RadarHardware,
    ca_cfar_detections,
    combined_two_way_pattern_gain,
    get_preset,
    mti_apply_clutter,
    mti_apply_target,
    processed_power_w,
    received_power_w,
)
from rdf_radar_targets import RadarTarget, radial_speed_toward_radar


@dataclass
class SectorConfig:
    radar_ix: int
    radar_iz: int
    radar_height_m: float = 25.0
    max_range_m: float = 6000.0
    range_bin_m: float = 40.0
    # Sector: center and half-width in radians (math angle: 0=+X, CCW).
    az_center_rad: float = 0.0
    az_halfwidth_rad: float = np.pi  # full 360 by default
    azimuth_count: int = 360
    use_spans: bool = True
    hardware: RadarHardware = field(default_factory=lambda: get_preset("ad"))
    sigma0_table: Sigma0Table = field(default_factory=lambda: Sigma0Table.builtin("X", 3))


@dataclass
class SectorResult:
    clutter_power_w: np.ndarray  # RF at Rx, before processing
    target_power_w: np.ndarray
    clutter_proc_w: np.ndarray  # after PC + integration
    target_proc_w: np.ndarray
    clutter_mti_w: np.ndarray
    target_mti_w: np.ndarray
    total_mti_w: np.ndarray
    cfar_mask: np.ndarray  # bool detections after CFAR
    noise_w: float
    noise_proc_w: float  # noise after processing gain (approx)
    snr_mti: np.ndarray
    az_rad: np.ndarray
    range_centers_m: np.ndarray
    radar_y: float
    visible_map: np.ndarray
    hardware_name: str
    sigma0_source: str
    dem_range_limit_m: float
    instrumented_range_m: float

    @property
    def clutter_power(self) -> np.ndarray:
        return self.clutter_mti_w

    @property
    def target_power(self) -> np.ndarray:
        return self.target_mti_w

    @property
    def total_power(self) -> np.ndarray:
        return self.total_mti_w


def _column_top_y(
    terrain_y: float,
    n_spans: int,
    span_lo: np.ndarray,
    span_hi: np.ndarray,
    use_spans: bool,
) -> float:
    top = terrain_y
    if not use_spans or n_spans <= 0:
        return top
    count = int(n_spans)
    if count > span_hi.shape[0]:
        count = span_hi.shape[0]
    for i in range(count):
        hi = float(span_hi[i])
        lo = float(span_lo[i])
        if hi <= lo:
            continue
        if hi > top:
            top = hi
    return top


def _grazing_from_geometry(
    radar_y: float,
    terrain_y: float,
    range_m: float,
    slope_deg: float,
) -> float:
    """Grazing ≈ 90° − incidence; incidence from ray elevation vs local slope."""
    if range_m < 1.0:
        range_m = 1.0
    ray_elev = math_asin(np.clip((terrain_y - radar_y) / range_m, -1.0, 1.0))
    slope_rad = abs(slope_deg) * np.pi / 180.0
    # Local surface tilt in the radial plane (unsigned).
    grazing = (np.pi * 0.5) - abs(ray_elev - slope_rad)
    if grazing < 0.0:
        grazing = 0.0
    if grazing > np.pi * 0.5:
        grazing = np.pi * 0.5
    return float(grazing)


def math_asin(x: float) -> float:
    return float(np.arcsin(x))


def math_hypot(a: float, b: float) -> float:
    return float(np.hypot(a, b))


def math_atan2(y: float, x: float) -> float:
    return float(np.arctan2(y, x))


def math_degrees(rad: float) -> float:
    return float(np.degrees(rad))


def simulate_sector(
    dem: dict,
    config: SectorConfig,
    targets: list[RadarTarget],
) -> SectorResult:
    terrain = np.asarray(dem["terrain"])
    slope = np.asarray(dem["slope"])
    surface = np.asarray(dem["surface"])
    cell_m = float(dem["cell_m"])
    height, width = terrain.shape
    hardware = config.hardware
    table = config.sigma0_table

    n_spans = np.asarray(dem.get("n_spans", np.zeros_like(terrain, dtype=np.uint8)))
    span_lo = dem.get("span_lo")
    span_hi = dem.get("span_hi")
    has_spans = bool(dem.get("has_spans", False)) and span_lo is not None and span_hi is not None
    use_spans = config.use_spans and has_spans

    radar_ix = config.radar_ix
    radar_iz = config.radar_iz
    radar_y = float(terrain[radar_iz, radar_ix]) + config.radar_height_m
    if not np.isfinite(radar_y):
        raise SystemExit("Radar site terrain is invalid")

    n_bins = int(np.ceil(config.max_range_m / config.range_bin_m))
    if n_bins < 1:
        n_bins = 1
    range_centers = (np.arange(n_bins, dtype=np.float64) + 0.5) * config.range_bin_m

    az_start = config.az_center_rad - config.az_halfwidth_rad
    az_end = config.az_center_rad + config.az_halfwidth_rad
    if config.azimuth_count < 1:
        raise SystemExit("azimuth_count must be >= 1")
    az_rad = np.linspace(az_start, az_end, config.azimuth_count, endpoint=False)

    clutter = np.zeros((config.azimuth_count, n_bins), dtype=np.float64)
    target_bins = np.zeros((config.azimuth_count, n_bins), dtype=np.float64)
    clutter_mti = np.zeros_like(clutter)
    target_mti = np.zeros_like(target_bins)
    visible_map = np.zeros((height, width), dtype=np.uint8)

    step_m = cell_m
    max_steps = int(config.max_range_m / step_m)
    cell_area = cell_m * cell_m
    for az_i, angle in enumerate(az_rad):
        direction_x = np.cos(angle)
        direction_z = np.sin(angle)
        max_elev = -1e9

        for step_index in range(1, max_steps + 1):
            range_m = step_index * step_m
            ix = int(round(radar_ix + direction_x * step_index))
            iz = int(round(radar_iz + direction_z * step_index))
            if ix < 0 or ix >= width or iz < 0 or iz >= height:
                break

            terrain_y = terrain[iz, ix]
            if not np.isfinite(terrain_y):
                continue

            if use_spans:
                top_y = _column_top_y(
                    float(terrain_y),
                    int(n_spans[iz, ix]),
                    span_lo[iz, ix],
                    span_hi[iz, ix],
                    True,
                )
            else:
                top_y = float(terrain_y)

            elev = (top_y - radar_y) / range_m
            visible = elev >= max_elev - 1e-4
            if elev > max_elev:
                max_elev = elev

            if not visible:
                continue

            visible_map[iz, ix] = 1
            grazing = _grazing_from_geometry(
                radar_y, float(terrain_y), range_m, float(slope[iz, ix])
            )
            surf = int(surface[iz, ix])
            sigma0 = sigma0_linear(surf, grazing, table)
            projected = cell_area * max(np.sin(grazing), 0.02)
            sigma_m2 = sigma0 * projected

            if use_spans and int(n_spans[iz, ix]) > 1:
                canopy = canopy_sigma0_linear(grazing, table)
                sigma_m2 = sigma_m2 + canopy * projected * 0.35

            # Scan azimuth is on-boresight; elevation offset from beam center.
            el_deg = math_degrees(math_asin(np.clip((float(terrain_y) - radar_y) / range_m, -1.0, 1.0)))
            pattern = combined_two_way_pattern_gain(hardware, el_deg, 0.0)
            power_w = received_power_w(hardware, sigma_m2, range_m, pattern)

            bin_i = int(range_m / config.range_bin_m)
            if bin_i < 0:
                bin_i = 0
            if bin_i >= n_bins:
                bin_i = n_bins - 1
            clutter[az_i, bin_i] = clutter[az_i, bin_i] + power_w

    clutter_proc = np.zeros_like(clutter)
    clutter_mti = np.zeros_like(clutter)
    for az_i in range(config.azimuth_count):
        for bin_i in range(n_bins):
            proc = processed_power_w(hardware, clutter[az_i, bin_i])
            clutter_proc[az_i, bin_i] = proc
            clutter_mti[az_i, bin_i] = mti_apply_clutter(hardware, proc, 0.0)

    target_proc = np.zeros_like(target_bins)
    target_mti = np.zeros_like(target_bins)

    for tgt in targets:
        dx = float(tgt.ix) - radar_ix
        dz = float(tgt.iz) - radar_iz
        dy = float(tgt.y_m) - radar_y
        range_m = float(np.sqrt(dx * dx * cell_m * cell_m + dy * dy + dz * dz * cell_m * cell_m))
        if range_m < 1.0 or range_m > config.max_range_m:
            continue

        az = math_atan2(dz, dx)
        if not _angle_in_sector(az, az_start, az_end):
            continue

        if not _target_los_clear(
            terrain,
            n_spans,
            span_lo,
            span_hi,
            use_spans,
            radar_ix,
            radar_iz,
            radar_y,
            tgt,
            cell_m,
        ):
            continue

        el_deg = math_degrees(math_asin(np.clip(dy / range_m, -1.0, 1.0)))
        az_i = _nearest_az_index(az_rad, az)
        az_off_deg = math_degrees(_wrap_pi(az - az_rad[az_i]))
        pattern = combined_two_way_pattern_gain(hardware, el_deg, az_off_deg)
        power_rf = received_power_w(hardware, tgt.rcs_m2, range_m, pattern)
        power_proc = processed_power_w(hardware, power_rf)

        v_radial = radial_speed_toward_radar(
            tgt, float(radar_ix), float(radar_iz), radar_y, cell_m
        )
        tip = float(getattr(tgt, "rotor_tip_m_s", 0.0) or 0.0)
        rotor_frac = float(getattr(tgt, "rotor_rcs_fraction", 0.0) or 0.0)
        hub = float(getattr(tgt, "hub_width_m_s", 0.0) or 0.0)
        if tip <= 0.0 and str(getattr(tgt, "kind", "")) == "heli":
            tip = 220.0
            rotor_frac = 0.35
            hub = 40.0
        power_mti = mti_apply_target(
            hardware,
            power_proc,
            v_radial,
            tip_speed_m_s=tip,
            rotor_rcs_fraction=rotor_frac,
            hub_width_m_s=hub,
        )

        bin_i = int(range_m / config.range_bin_m)
        if bin_i < 0:
            bin_i = 0
        if bin_i >= n_bins:
            bin_i = n_bins - 1
        target_bins[az_i, bin_i] = target_bins[az_i, bin_i] + power_rf
        target_proc[az_i, bin_i] = target_proc[az_i, bin_i] + power_proc
        target_mti[az_i, bin_i] = target_mti[az_i, bin_i] + power_mti

    noise_w = hardware.noise_power_w(config.range_bin_m)
    noise_proc_w = processed_power_w(hardware, noise_w)
    total_mti = clutter_mti + target_mti
    snr = total_mti / max(noise_proc_w, 1e-30)
    cfar_mask = ca_cfar_detections(total_mti, noise_proc_w)

    # DEM geometric limit: farthest corner distance from radar site.
    dem_limit = cell_m * math_hypot(
        max(radar_ix, width - 1 - radar_ix),
        max(radar_iz, height - 1 - radar_iz),
    )

    return SectorResult(
        clutter_power_w=clutter,
        target_power_w=target_bins,
        clutter_proc_w=clutter_proc,
        target_proc_w=target_proc,
        clutter_mti_w=clutter_mti,
        target_mti_w=target_mti,
        total_mti_w=total_mti,
        cfar_mask=cfar_mask,
        noise_w=noise_w,
        noise_proc_w=noise_proc_w,
        snr_mti=snr,
        az_rad=az_rad,
        range_centers_m=range_centers,
        radar_y=radar_y,
        visible_map=visible_map,
        hardware_name=hardware.name,
        sigma0_source=table.source,
        dem_range_limit_m=dem_limit,
        instrumented_range_m=hardware.instrumented_range_m,
    )


def _wrap_pi(angle: float) -> float:
    return float((angle + np.pi) % (2.0 * np.pi) - np.pi)


def _angle_in_sector(az: float, az_start: float, az_end: float) -> bool:
    width = az_end - az_start
    if width >= 2.0 * np.pi - 1e-9:
        return True
    delta = (az - az_start) % (2.0 * np.pi)
    if delta < 0.0:
        delta = delta + 2.0 * np.pi
    return delta <= width


def _nearest_az_index(az_rad: np.ndarray, az: float) -> int:
    diffs = (az_rad - az + np.pi) % (2.0 * np.pi) - np.pi
    return int(np.argmin(np.abs(diffs)))


def _target_los_clear(
    terrain: np.ndarray,
    n_spans: np.ndarray,
    span_lo,
    span_hi,
    use_spans: bool,
    radar_ix: int,
    radar_iz: int,
    radar_y: float,
    tgt: RadarTarget,
    cell_m: float,
) -> bool:
    height, width = terrain.shape
    dx = float(tgt.ix) - radar_ix
    dz = float(tgt.iz) - radar_iz
    steps = int(max(abs(dx), abs(dz)))
    if steps < 1:
        return True

    ground_range = math_hypot(dx * cell_m, dz * cell_m)
    slant = math_hypot(ground_range, float(tgt.y_m) - radar_y)
    tgt_elev = (float(tgt.y_m) - radar_y) / max(slant, 1.0)

    for s in range(1, steps):
        t = s / float(steps)
        ix = int(round(radar_ix + dx * t))
        iz = int(round(radar_iz + dz * t))
        if ix < 0 or ix >= width or iz < 0 or iz >= height:
            return False
        terrain_y = terrain[iz, ix]
        if not np.isfinite(terrain_y):
            continue
        if use_spans:
            top_y = _column_top_y(
                float(terrain_y),
                int(n_spans[iz, ix]),
                span_lo[iz, ix],
                span_hi[iz, ix],
                True,
            )
        else:
            top_y = float(terrain_y)
        range_m = math_hypot((ix - radar_ix) * cell_m, (iz - radar_iz) * cell_m)
        if range_m < 1.0:
            continue
        elev = (top_y - radar_y) / range_m
        if elev > tgt_elev + 1e-4:
            return False
    return True
