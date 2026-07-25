#!/usr/bin/env python3
"""Pluggable electronic-warfare effects for RDF radar simulations.

Effects operate after the physical receive chain and before CFAR. Core radar
code does not depend on concrete EW classes; callers pass an EWStack.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Protocol

import numpy as np

from rdf_radar_channel import spectral_overlap_hz
from rdf_radar_physics import RadarHardware, db_to_lin


@dataclass
class EWContext:
    hardware: RadarHardware
    radar_x_m: float
    radar_z_m: float
    az_rad: np.ndarray
    range_centers_m: np.ndarray
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
    """Broadband spot/barrage noise jammer with main/side-lobe coupling."""

    name: str = "noise_jammer"
    x_m: float = 0.0
    z_m: float = 0.0
    erp_w: float = 2.0e4
    bandwidth_hz: float = 5.0e6
    # Jammer RF center; 0 → assume co-tuned with victim radar.
    center_frequency_hz: float = 0.0
    enabled: bool = True

    def apply(self, power_w: np.ndarray, context: EWContext) -> np.ndarray:
        if not self.enabled or self.erp_w <= 0.0:
            return power_w

        dx = self.x_m - context.radar_x_m
        dz = self.z_m - context.radar_z_m
        jammer_range_m = math.hypot(dx, dz)
        if jammer_range_m < 1.0:
            jammer_range_m = 1.0
        jammer_az = math.atan2(dz, dx)

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
            return power_w

        # Received jammer density: ERP / (4πR²B_j), Ae = G λ²/(4π),
        # integrated over overlapping bandwidth.
        flux_density = self.erp_w / (
            4.0 * math.pi * jammer_range_m * jammer_range_m * max(self.bandwidth_hz, 1.0)
        )
        effective_aperture = (
            context.hardware.gain_linear
            * context.hardware.wavelength_m
            * context.hardware.wavelength_m
            / (4.0 * math.pi)
        )
        peak_jammer_w = flux_density * effective_aperture * overlap_hz

        az_delta = np.abs(_wrap_pi(context.az_rad - jammer_az))
        half_beam = math.radians(context.hardware.az_beamwidth_deg * 0.5)
        sidelobe = db_to_lin(context.hardware.sidelobe_level_db)
        coupling = np.full(context.az_rad.shape, sidelobe, dtype=np.float64)
        coupling[az_delta <= half_beam] = 1.0

        return power_w + coupling[:, None] * peak_jammer_w


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
        if context.az_rad.size < 1 or context.range_centers_m.size < 1:
            return output

        for false_target in self.false_targets:
            moved_range = false_target.range_m + false_target.range_rate_m_s * context.time_s
            if moved_range < 0.0:
                continue
            az = math.radians(false_target.azimuth_deg)
            az_delta = np.abs(_wrap_pi(context.az_rad - az))
            az_i = int(np.argmin(az_delta))
            range_i = int(np.argmin(np.abs(context.range_centers_m - moved_range)))
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
