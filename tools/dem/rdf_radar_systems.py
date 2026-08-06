#!/usr/bin/env python3
"""Offline models for RDF gameplay systems not covered by sector/scan alone.

Mirrors Enforce concepts at engineering fidelity (not bit-identical):
  GO/SO-CFAR, measurement noise scale, lock FSM, ESM/RWR/ARM, RGPO-like
  deception hooks, and Network bandwidth adaptation (Reliable summary /
  Unreliable plots / caps / throttle / interest / fingerprint skip).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum, IntEnum

import numpy as np

from rdf_radar_channel import ChannelFidelity
from rdf_radar_physics import (
    RadarHardware,
    ca_cfar_detections,
    db_to_lin,
    doppler_hz,
    lin_to_db,
)


# ---------------------------------------------------------------------------
# CFAR modes (CA / GO / SO)
# ---------------------------------------------------------------------------


def _cfar_train_mean(
    row: np.ndarray,
    bin_i: int,
    guard_cells: int,
    half: int,
    noise_floor_w: float,
    reduce: str,
) -> float:
    nbin = row.shape[0]
    left0 = bin_i - guard_cells - half
    left1 = bin_i - guard_cells
    right0 = bin_i + guard_cells + 1
    right1 = bin_i + guard_cells + 1 + half
    left_m = noise_floor_w
    right_m = noise_floor_w
    if left1 > 0:
        a0 = max(left0, 0)
        a1 = max(left1, 0)
        if a1 > a0:
            left_m = float(np.mean(row[a0:a1]))
    if right0 < nbin:
        b0 = min(right0, nbin)
        b1 = min(right1, nbin)
        if b1 > b0:
            right_m = float(np.mean(row[b0:b1]))
    if reduce == "go":
        local = max(left_m, right_m)
    elif reduce == "so":
        local = min(left_m, right_m)
    else:
        local = 0.5 * (left_m + right_m)
    if local < noise_floor_w:
        local = noise_floor_w
    return local


def cfar_detections(
    power_w: np.ndarray,
    noise_w: float,
    mode: str = "ca",
    guard_cells: int = 2,
    training_cells: int = 8,
    pfa: float = 1.0e-6,
) -> np.ndarray:
    """CFAR along range. mode: ca | go | so."""
    mode_l = mode.lower().strip()
    if mode_l == "ca":
        return ca_cfar_detections(
            power_w,
            noise_w,
            guard_cells=guard_cells,
            training_cells=training_cells,
            pfa=pfa,
        )

    naz, nbin = power_w.shape
    det = np.zeros((naz, nbin), dtype=bool)
    n_train = max(int(training_cells), 2)
    alpha = float(n_train) * (pfa ** (-1.0 / float(n_train)) - 1.0)
    if alpha < 1.0:
        alpha = 1.0
    half = n_train // 2
    reduce = "go"
    if mode_l == "so":
        reduce = "so"
    for az_i in range(naz):
        row = power_w[az_i]
        for bin_i in range(nbin):
            local = _cfar_train_mean(row, bin_i, guard_cells, half, noise_w, reduce)
            if row[bin_i] > alpha * local:
                det[az_i, bin_i] = True
    return det


def fill_thermal_noise(
    power_w: np.ndarray,
    noise_w: float,
    rng: np.random.Generator,
) -> np.ndarray:
    """Fill empty / weak cells with exponential thermal samples (mean = noise_w)."""
    out = np.array(power_w, dtype=float, copy=True)
    samples = rng.exponential(scale=max(noise_w, 1.0e-30), size=out.shape)
    mask = out < (0.25 * noise_w)
    out = np.where(mask, samples, out)
    return out


# ---------------------------------------------------------------------------
# Atmosphere / weather
# ---------------------------------------------------------------------------


def rain_one_way_db_per_km(rain_mm_h: float, frequency_hz: float) -> float:
    """Very simplified ITU-ish rain loss (one-way dB/km)."""
    r = max(float(rain_mm_h), 0.0)
    if r <= 0.0:
        return 0.0
    f_ghz = max(frequency_hz / 1.0e9, 1.0)
    # Rough: a * R^b with a growing with frequency.
    a = 0.0001 * (f_ghz**1.2)
    b = 1.0
    return a * (r**b)


def two_way_path_loss_db(
    range_m: float,
    atm_db_per_km_one_way: float,
    rain_mm_h: float,
    frequency_hz: float,
) -> float:
    km = max(range_m, 0.0) / 1000.0
    rain = rain_one_way_db_per_km(rain_mm_h, frequency_hz)
    return 2.0 * km * (atm_db_per_km_one_way + rain)


# ---------------------------------------------------------------------------
# Measurement model
# ---------------------------------------------------------------------------


@dataclass
class MeasurementModel:
    """SNR-scaled range/az/el/radial measurement noise (MeasNoiseScale).

    Optional PRF folds / refraction elevation bias mirror RDF_RadarMeasurement
    when ChannelFidelity flags are set (default off).
    """

    noise_scale: float = 1.0
    range_bias_m: float = 0.0
    az_bias_deg: float = 0.0
    el_bias_deg: float = 0.0
    fidelity: ChannelFidelity | None = None

    @classmethod
    def from_fidelity(
        cls,
        fidelity: ChannelFidelity,
        noise_scale: float = 1.0,
        range_bias_m: float = 0.0,
        az_bias_deg: float = 0.0,
        el_bias_deg: float = 0.0,
    ) -> "MeasurementModel":
        return cls(
            noise_scale=noise_scale,
            range_bias_m=range_bias_m,
            az_bias_deg=az_bias_deg,
            el_bias_deg=el_bias_deg,
            fidelity=fidelity,
        )

    def synthesize(
        self,
        hardware: RadarHardware,
        true_range_m: float,
        true_az_deg: float,
        true_el_deg: float,
        true_radial_m_s: float,
        snr_db: float,
        rng: np.random.Generator,
        scan_number: int = 0,
    ) -> tuple[float, float, float, float]:
        from rdf_radar_channel import (
            fold_doppler_ambiguous,
            fold_range_ambiguous,
            refraction_elevation_bias_deg,
        )

        snr_lin = max(10.0 ** (snr_db / 10.0), 1.0)
        denom = 1.6 * math.sqrt(2.0 * snr_lin)
        if denom < 0.001:
            denom = 0.001
        scale = max(self.noise_scale, 0.0)

        work_range = true_range_m
        fid = self.fidelity
        if fid is not None:
            if fid.enable_range_ambiguity_fold:
                work_range = fold_range_ambiguous(
                    true_range_m,
                    hardware.unambiguous_range_m,
                )

        range_bin = hardware.range_bin_m()
        range_sigma = (range_bin / denom) * scale
        qbin = math.floor(work_range / range_bin)
        if qbin < 0:
            qbin = 0
        quantized = (qbin + 0.5) * range_bin
        meas_range = quantized + self.range_bias_m + float(rng.normal(0.0, range_sigma))
        if meas_range < 50.0:
            meas_range = 50.0

        az_sigma = (hardware.az_beamwidth_deg / denom) * scale
        el_sigma = (hardware.el_beamwidth_deg / denom) * scale
        meas_az = true_az_deg + self.az_bias_deg + float(rng.normal(0.0, az_sigma))
        meas_el = true_el_deg + self.el_bias_deg
        if fid is not None:
            if fid.enable_atmospheric_refraction:
                meas_el = meas_el + refraction_elevation_bias_deg(
                    true_range_m,
                    fid.earth_radius_factor,
                )
        meas_el = meas_el + float(rng.normal(0.0, el_sigma))

        rr_sigma = max(0.5, 8.0 / denom) * scale
        meas_rr = true_radial_m_s + float(rng.normal(0.0, rr_sigma))
        if fid is not None:
            fold_doppler = fid.enable_doppler_ambiguity_fold
            if fold_doppler:
                if fid.weapon_locate:
                    fold_doppler = False
            if fold_doppler:
                fd = doppler_hz(meas_rr, hardware.wavelength_m)
                fd = fold_doppler_ambiguous(fd, hardware.active_prf_hz(scan_number))
                if hardware.wavelength_m > 0.0:
                    meas_rr = fd * hardware.wavelength_m * 0.5
        return meas_range, meas_az, meas_el, meas_rr


# ---------------------------------------------------------------------------
# Lock FSM
# ---------------------------------------------------------------------------


class LockState(IntEnum):
    SEARCH = 0
    ACQUIRING = 1
    TRACKING = 2
    COAST = 3


@dataclass
class LockManager:
    """Simplified SEARCH → ACQUIRE → TRACK → COAST lock."""

    acquire_hits: int = 3
    coast_misses: int = 4
    state: LockState = LockState.SEARCH
    track_id: int = -1
    hit_count: int = 0
    miss_count: int = 0
    aim_x: float = 0.0
    aim_y: float = 0.0
    aim_z: float = 0.0

    def unlock(self) -> None:
        self.state = LockState.SEARCH
        self.track_id = -1
        self.hit_count = 0
        self.miss_count = 0

    def update(
        self,
        confirmed_track_ids: list[int],
        positions: dict[int, tuple[float, float, float]],
        preferred_id: int | None = None,
    ) -> None:
        if self.state == LockState.SEARCH:
            if preferred_id is not None and preferred_id in confirmed_track_ids:
                self.track_id = preferred_id
            elif confirmed_track_ids:
                self.track_id = confirmed_track_ids[0]
            else:
                return
            self.state = LockState.ACQUIRING
            self.hit_count = 1
            self.miss_count = 0
            pos = positions.get(self.track_id)
            if pos is not None:
                self.aim_x, self.aim_y, self.aim_z = pos
            return

        if self.track_id in confirmed_track_ids:
            self.hit_count += 1
            self.miss_count = 0
            pos = positions.get(self.track_id)
            if pos is not None:
                self.aim_x, self.aim_y, self.aim_z = pos
            if self.state == LockState.ACQUIRING and self.hit_count >= self.acquire_hits:
                self.state = LockState.TRACKING
            elif self.state == LockState.COAST:
                self.state = LockState.TRACKING
            return

        self.miss_count += 1
        if self.state == LockState.TRACKING:
            self.state = LockState.COAST
        if self.miss_count >= self.coast_misses:
            self.unlock()


# ---------------------------------------------------------------------------
# ESM / RWR / ARM
# ---------------------------------------------------------------------------


def friis_receive_power_w(
    erp_w: float,
    frequency_hz: float,
    range_m: float,
    rx_gain_lin: float = 1.0,
) -> float:
    """One-way Friis receive power (W)."""
    r = max(range_m, 1.0)
    wavelength = 299792458.0 / max(frequency_hz, 1.0)
    return erp_w * rx_gain_lin * (wavelength**2) / ((4.0 * math.pi * r) ** 2)


class RwrAlert(str, Enum):
    NONE = "none"
    SEARCH = "search"
    TRACK = "track"
    LOCK = "lock"


def rwr_alert_from_lock(state: LockState, illuminated: bool) -> RwrAlert:
    if not illuminated:
        return RwrAlert.NONE
    if state == LockState.TRACKING:
        return RwrAlert.LOCK
    if state == LockState.ACQUIRING or state == LockState.COAST:
        return RwrAlert.TRACK
    return RwrAlert.SEARCH


@dataclass
class ArmAim:
    valid: bool
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0


def arm_aim_from_emitter(
    emitter_pos: tuple[float, float, float],
    emitting: bool,
) -> ArmAim:
    if not emitting:
        return ArmAim(valid=False)
    return ArmAim(valid=True, x=emitter_pos[0], y=emitter_pos[1], z=emitter_pos[2])


# ---------------------------------------------------------------------------
# Deception extensions (RGPO / angular / intermittent)
# ---------------------------------------------------------------------------


@dataclass
class RangePullOff:
    """Simple RGPO: walks false range away from true target."""

    pull_m_s: float = 80.0
    max_offset_m: float = 2000.0
    elapsed_s: float = 0.0

    def offset_m(self, dt_s: float) -> float:
        self.elapsed_s += max(dt_s, 0.0)
        offset = self.pull_m_s * self.elapsed_s
        if offset > self.max_offset_m:
            offset = self.max_offset_m
        return offset


@dataclass
class AngularScintillation:
    """Adds angular wander (deg) for false / corrupted plots."""

    sigma_deg: float = 1.5

    def sample(self, rng: np.random.Generator) -> float:
        return float(rng.normal(0.0, self.sigma_deg))


@dataclass
class IntermittentFalsePlots:
    """Duty-cycled false plot generator."""

    duty: float = 0.35
    period_s: float = 2.0

    def active(self, time_s: float) -> bool:
        if self.period_s <= 0.0:
            return self.duty > 0.0
        phase = (time_s % self.period_s) / self.period_s
        return phase < self.duty


# ---------------------------------------------------------------------------
# Network bandwidth adaptation (official-aligned)
# ---------------------------------------------------------------------------


@dataclass
class NetworkSyncConfig:
    max_tracks: int = 32
    max_plots: int = 24
    min_reliable_interval_s: float = 0.15
    min_plot_interval_s: float = 0.05
    skip_unchanged: bool = True
    interest_radius_m: float = 12000.0
    sync_plots_unreliable: bool = True


@dataclass
class NetworkSyncState:
    last_reliable_s: float = -1.0e6
    last_plot_s: float = -1.0e6
    last_fingerprint: int = 0
    reliable_sends: int = 0
    plot_sends: int = 0
    skipped_unchanged: int = 0
    skipped_interest: int = 0
    skipped_throttle: int = 0


def fingerprint_tracks(
    track_ids: list[int],
    positions_xz: list[tuple[float, float]],
    lock_state: int,
    lock_track_id: int,
) -> int:
    h = 17
    h = h * 31 + int(lock_state)
    h = h * 31 + int(lock_track_id)
    h = h * 31 + len(track_ids)
    for i, tid in enumerate(track_ids):
        h = h * 31 + int(tid)
        if i < len(positions_xz):
            h = h * 31 + int(round(positions_xz[i][0]))
            h = h * 31 + int(round(positions_xz[i][1]))
    return h


def network_should_send(
    cfg: NetworkSyncConfig,
    state: NetworkSyncState,
    time_s: float,
    fingerprint: int,
    lock_state: int,
    audience_distance_m: float | None,
    plots: bool,
) -> bool:
    """Return True if a Broadcast should go out for summary (plots=False) or plots."""
    if audience_distance_m is not None and cfg.interest_radius_m > 0.0:
        if audience_distance_m > cfg.interest_radius_m:
            state.skipped_interest += 1
            return False

    if plots:
        if not cfg.sync_plots_unreliable:
            return False
        if (time_s - state.last_plot_s) < cfg.min_plot_interval_s:
            state.skipped_throttle += 1
            return False
        state.last_plot_s = time_s
        state.plot_sends += 1
        return True

    allow = True
    if (time_s - state.last_reliable_s) < cfg.min_reliable_interval_s:
        allow = False
        state.skipped_throttle += 1
    changed = fingerprint != state.last_fingerprint
    if cfg.skip_unchanged and not changed:
        allow = False
        state.skipped_unchanged += 1
    if changed and lock_state > 0:
        allow = True
    if not allow:
        return False
    state.last_reliable_s = time_s
    state.last_fingerprint = fingerprint
    state.reliable_sends += 1
    return True


def cap_list(items: list, max_n: int) -> list:
    if max_n <= 0:
        return list(items)
    return list(items[:max_n])


# ---------------------------------------------------------------------------
# Light IFF
# ---------------------------------------------------------------------------


class Iff(IntEnum):
    UNKNOWN = 0
    FRIEND = 1
    FOE = 2
    NEUTRAL = 3


def resolve_iff(faction_a: str, faction_b: str) -> Iff:
    if not faction_a or not faction_b:
        return Iff.UNKNOWN
    if faction_a == faction_b:
        return Iff.FRIEND
    if faction_a == "neutral" or faction_b == "neutral":
        return Iff.NEUTRAL
    return Iff.FOE


@dataclass
class FeatureCoverage:
    """Names of feature scenarios exercised by the full sim."""

    names: list[str] = field(default_factory=list)

    def add(self, name: str) -> None:
        if name not in self.names:
            self.names.append(name)
