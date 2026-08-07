#!/usr/bin/env python3
"""Pluggable electronic-warfare effects for RDF radar simulations.

Effects operate after the physical receive chain and before CFAR. Core radar
code does not depend on concrete EW classes; callers pass an EWStack.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum
from typing import Protocol

import numpy as np

from rdf_radar_channel import spectral_overlap_hz
from rdf_radar_physics import RadarHardware, db_to_lin, lin_to_db


class NoiseJamCoupling(str, Enum):
    """Antenna coupling of a noise jammer into the victim receiver."""

    BEAM = "beam"
    SEARCH_AVG = "search_avg"
    MAINLOBE_ONLY = "mainlobe_only"


def burn_through_range_m(
    clean_instrumented_range_m: float,
    thermal_noise_w: float,
    jam_noise_w: float,
) -> float:
    """Reff = R0 · (N / (N+J))^(1/4) for noise jamming."""
    r0 = max(float(clean_instrumented_range_m), 1.0)
    n = max(float(thermal_noise_w), 0.0)
    j = max(float(jam_noise_w), 0.0)
    denom = n + j
    if denom <= 0.0:
        return r0
    factor = n / denom
    if factor <= 0.0:
        return 0.0
    if factor >= 1.0:
        return r0
    return r0 * (factor**0.25)


@dataclass
class EWContext:
    hardware: RadarHardware
    radar_x_m: float
    radar_z_m: float
    az_rad: np.ndarray
    range_centers_m: float | np.ndarray
    noise_w: float
    time_s: float = 0.0
    # Tuned carrier for this dwell (frequency agility).
    frequency_hz: float = 0.0


class EWEffect(Protocol):
    """Interface for noise, deception, and future DRFM/chaff effects."""

    name: str

    def apply(
        self,
        power_w: np.ndarray,
        context: EWContext,
    ) -> np.ndarray:
        ...


def _wrap_pi(angle: np.ndarray | float) -> np.ndarray | float:
    return (angle + math.pi) % (2.0 * math.pi) - math.pi


@dataclass
class NoiseJammer:
    """Broadband spot/barrage noise jammer with tunable beam coupling.

    Defaults match Enforce ``RDF_RadarNoiseJammerEffect`` gameplay soft path:
    SEARCH_AVG + -40 dB sidelobe. Call ``configure_physics_beam()`` for the
    legacy instantaneous main/side-lobe stare model.
    """

    name: str = "noise_jammer"
    x_m: float = 0.0
    z_m: float = 0.0
    erp_w: float = 2.0e4
    bandwidth_hz: float = 5.0e6
    # Jammer RF center; 0 → assume co-tuned with victim radar.
    center_frequency_hz: float = 0.0
    enabled: bool = True
    coupling_mode: NoiseJamCoupling = NoiseJamCoupling.SEARCH_AVG
    sidelobe_level_db: float = -40.0
    coupling_gain: float = 1.0
    # SEARCH_AVG duty; None → az_beamwidth_deg / 360.
    search_duty: float | None = None
    # Sidelobe blanking (default off; mirrors Enforce m_EnableSlb).
    enable_slb: bool = False

    def configure_physics_beam(self, sidelobe_level_db: float = -25.0) -> None:
        self.coupling_mode = NoiseJamCoupling.BEAM
        self.sidelobe_level_db = sidelobe_level_db
        self.coupling_gain = 1.0
        self.search_duty = None

    def configure_gameplay_search_avg(
        self,
        sidelobe_level_db: float = -40.0,
        coupling_gain: float = 1.0,
    ) -> None:
        self.coupling_mode = NoiseJamCoupling.SEARCH_AVG
        self.sidelobe_level_db = sidelobe_level_db
        self.coupling_gain = coupling_gain
        self.search_duty = None

    def configure_mainlobe_only(self) -> None:
        self.coupling_mode = NoiseJamCoupling.MAINLOBE_ONLY
        self.coupling_gain = 1.0
        self.search_duty = None

    def enable_sidelobe_blanking(self, enable: bool = True) -> None:
        self.enable_slb = enable

    def peak_jammer_power_w(self, context: EWContext) -> float:
        """Isotropic peak jammer power into the receiver (coupling = 1)."""
        if not self.enabled or self.erp_w <= 0.0 or self.coupling_gain <= 0.0:
            return 0.0

        dx = self.x_m - context.radar_x_m
        dz = self.z_m - context.radar_z_m
        jammer_range_m = math.hypot(dx, dz)
        if jammer_range_m < 1.0:
            jammer_range_m = 1.0

        rx_center = context.frequency_hz
        if rx_center <= 0.0:
            rx_center = context.hardware.frequency_hz
        jam_center = self.center_frequency_hz
        if jam_center <= 0.0:
            jam_center = rx_center

        rx_bandwidth = context.hardware.bandwidth_for_noise()
        overlap_hz = spectral_overlap_hz(
            jam_center,
            self.bandwidth_hz,
            rx_center,
            rx_bandwidth,
        )
        if overlap_hz <= 0.0:
            return 0.0

        flux_density = self.erp_w / (
            4.0
            * math.pi
            * jammer_range_m
            * jammer_range_m
            * max(self.bandwidth_hz, 1.0)
        )
        effective_aperture = (
            context.hardware.gain_linear
            * context.hardware.wavelength_m
            * context.hardware.wavelength_m
            / (4.0 * math.pi)
        )
        return flux_density * effective_aperture * overlap_hz * self.coupling_gain

    def coupling_for_azimuths(self, context: EWContext) -> np.ndarray:
        """Per-azimuth coupling factors in [0, 1] (or sidelobe floor)."""
        sidelobe = db_to_lin(self.sidelobe_level_db)
        az = np.asarray(context.az_rad, dtype=np.float64)
        if az.size < 1:
            return np.zeros(0, dtype=np.float64)

        if self.coupling_mode == NoiseJamCoupling.SEARCH_AVG:
            duty = self.search_duty
            if duty is None:
                duty = context.hardware.az_beamwidth_deg / 360.0
            duty = float(np.clip(duty, 0.0, 1.0))
            if self.enable_slb:
                value = duty
            else:
                value = duty * 1.0 + (1.0 - duty) * sidelobe
            return np.full(az.shape, value, dtype=np.float64)

        dx = self.x_m - context.radar_x_m
        dz = self.z_m - context.radar_z_m
        jammer_az = math.atan2(dz, dx)
        half_beam = math.radians(context.hardware.az_beamwidth_deg * 0.5)
        az_delta = np.abs(_wrap_pi(az - jammer_az))
        in_main = az_delta <= half_beam

        if self.coupling_mode == NoiseJamCoupling.MAINLOBE_ONLY:
            coupling = np.zeros(az.shape, dtype=np.float64)
            coupling[in_main] = 1.0
            return coupling

        if self.enable_slb:
            coupling = np.zeros(az.shape, dtype=np.float64)
            coupling[in_main] = 1.0
            return coupling

        coupling = np.full(az.shape, sidelobe, dtype=np.float64)
        coupling[in_main] = 1.0
        return coupling

    def apply(self, power_w: np.ndarray, context: EWContext) -> np.ndarray:
        peak = self.peak_jammer_power_w(context)
        if peak <= 0.0:
            return power_w
        coupling = self.coupling_for_azimuths(context)
        if coupling.size < 1:
            return power_w
        return power_w + coupling[:, None] * peak


@dataclass
class FalseTarget:
    range_m: float
    azimuth_deg: float
    power_w: float
    range_rate_m_s: float = 0.0


@dataclass
class DeceptionJammer:
    """Inject deterministic false plots (range-gate pull-off building block)."""

    name: str = "deception_jammer"
    false_targets: list[FalseTarget] = field(default_factory=list)
    enabled: bool = True

    def apply(self, power_w: np.ndarray, context: EWContext) -> np.ndarray:
        if not self.enabled:
            return power_w
        output = power_w.copy()
        if context.az_rad.size < 1 or np.asarray(context.range_centers_m).size < 1:
            return output

        range_centers = np.asarray(context.range_centers_m, dtype=np.float64)
        for false_target in self.false_targets:
            moved_range = (
                false_target.range_m
                + false_target.range_rate_m_s * context.time_s
            )
            if moved_range < 0.0:
                continue
            az = math.radians(false_target.azimuth_deg)
            az_delta = np.abs(_wrap_pi(context.az_rad - az))
            az_i = int(np.argmin(az_delta))
            range_i = int(np.argmin(np.abs(range_centers - moved_range)))
            output[az_i, range_i] = output[az_i, range_i] + false_target.power_w
        return output


@dataclass
class FrequencyHopSchedule:
    """Frequency-agility contract; returns one channel per dwell."""

    channels_hz: list[float] = field(default_factory=list)
    dwell_s: float = 0.1

    def frequency_at(self, time_s: float, fallback_hz: float) -> float:
        if not self.channels_hz:
            return fallback_hz
        dwell = self.dwell_s
        if dwell <= 0.0:
            dwell = 0.1
        index = int(time_s / dwell) % len(self.channels_hz)
        return self.channels_hz[index]


@dataclass
class EWStack:
    """Ordered composition of optional EW effects."""

    effects: list[EWEffect] = field(default_factory=list)

    def apply(self, power_w: np.ndarray, context: EWContext) -> np.ndarray:
        output = power_w
        for effect in self.effects:
            output = effect.apply(output, context)
        return output

    def names(self) -> list[str]:
        return [effect.name for effect in self.effects]

    def total_noise_jammer_peak_w(self, context: EWContext) -> float:
        """Sum of isotropic peak powers from noise jammers (coupling ignored)."""
        total = 0.0
        for effect in self.effects:
            if isinstance(effect, NoiseJammer):
                total += effect.peak_jammer_power_w(context)
        return total

    def status_short(
        self,
        context: EWContext,
        clean_range_m: float,
    ) -> str:
        """Compact JN / Reff line mirroring Enforce GetEwStatsShort."""
        peak = self.total_noise_jammer_peak_w(context)
        if peak <= 0.0:
            return "ew=0"
        mean_j = 0.0
        count = 0
        for effect in self.effects:
            if not isinstance(effect, NoiseJammer):
                continue
            coupling = effect.coupling_for_azimuths(context)
            if coupling.size < 1:
                continue
            mean_j += effect.peak_jammer_power_w(context) * float(
                np.mean(coupling)
            )
            count += 1
        if count < 1 or mean_j <= 0.0:
            return "ew=0"
        jn_db = lin_to_db(mean_j / max(context.noise_w, 1e-30))
        reff = burn_through_range_m(clean_range_m, context.noise_w, mean_j)
        return f"ewJN={jn_db:.1f}dB Reff={reff:.0f}m"
