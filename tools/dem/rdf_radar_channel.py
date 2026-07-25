#!/usr/bin/env python3
"""Propagation extras: frequency retune, multipath, and Swerling RCS.

These are framework utilities used by the scan loop. They do not bake into DEM.
"""

from __future__ import annotations

import copy
import math
from dataclasses import dataclass

import numpy as np

from rdf_radar_physics import RadarHardware, atmospheric_one_way_db_per_km, db_to_lin


def band_for_frequency(frequency_hz: float) -> str:
    f_ghz = frequency_hz / 1.0e9
    if f_ghz < 0.3:
        return "VHF"
    if f_ghz < 1.0:
        return "L"
    if f_ghz < 2.0:
        return "L"
    if f_ghz < 4.0:
        return "S"
    if f_ghz < 8.0:
        return "C"
    return "X"


def hardware_at_frequency(
    hardware: RadarHardware,
    frequency_hz: float,
) -> RadarHardware:
    """Clone hardware with retuned carrier (wavelength, atm, band label)."""
    tuned = copy.deepcopy(hardware)
    if frequency_hz <= 0.0:
        return tuned
    tuned.frequency_hz = frequency_hz
    tuned.band = band_for_frequency(frequency_hz)
    tuned.atm_loss_db_per_km_one_way = atmospheric_one_way_db_per_km(frequency_hz)
    return tuned


def radar_constant_scale(
    hardware_base: RadarHardware,
    hardware_tuned: RadarHardware,
) -> float:
    """Scale factor for RF powers when retuning frequency (λ² / L_sys path)."""
    base = hardware_base.radar_constant()
    if base <= 0.0:
        return 1.0
    return hardware_tuned.radar_constant() / base


def spectral_overlap_hz(
    center_a_hz: float,
    bandwidth_a_hz: float,
    center_b_hz: float,
    bandwidth_b_hz: float,
) -> float:
    """Overlap width of two rectangular spectra (Hz)."""
    half_a = 0.5 * max(bandwidth_a_hz, 0.0)
    half_b = 0.5 * max(bandwidth_b_hz, 0.0)
    lo = max(center_a_hz - half_a, center_b_hz - half_b)
    hi = min(center_a_hz + half_a, center_b_hz + half_b)
    width = hi - lo
    if width < 0.0:
        return 0.0
    return width


@dataclass
class MultipathModel:
    """Simple two-ray multipath power factor for low-altitude paths."""

    enabled: bool = True
    reflection_coeff: float = -0.6
    max_height_m: float = 800.0

    def power_factor(
        self,
        wavelength_m: float,
        range_m: float,
        radar_height_agl_m: float,
        target_height_agl_m: float,
    ) -> float:
        if not self.enabled:
            return 1.0
        if wavelength_m <= 0.0 or range_m < 1.0:
            return 1.0
        if radar_height_agl_m < 1.0 or target_height_agl_m < 1.0:
            return 1.0
        if target_height_agl_m > self.max_height_m:
            return 1.0

        # Path length difference ≈ 2 h_r h_t / R
        delta = 2.0 * radar_height_agl_m * target_height_agl_m / range_m
        phase = 2.0 * math.pi * delta / wavelength_m
        # Coherent sum of direct (1) and reflected (Gamma e^{jφ}).
        real = 1.0 + self.reflection_coeff * math.cos(phase)
        imag = self.reflection_coeff * math.sin(phase)
        factor = real * real + imag * imag
        if factor < 0.01:
            factor = 0.01
        if factor > 4.0:
            factor = 4.0
        return factor


@dataclass
class SwerlingModel:
    """RCS fluctuation. Type 0 = non-fluctuating."""

    model: int = 1  # 0,1,2,3,4
    seed: int = 7

    def __post_init__(self) -> None:
        self._rng = np.random.default_rng(self.seed)
        self._scan_cache: dict[tuple[str, int], float] = {}

    def sample(
        self,
        mean_rcs_m2: float,
        target_name: str,
        scan_number: int,
        dwell_index: int,
    ) -> float:
        if mean_rcs_m2 <= 0.0:
            return 0.0
        model = self.model
        if model <= 0:
            return mean_rcs_m2

        # Swerling I/III: constant within a scan, redraw between scans.
        # Swerling II/IV: redraw every dwell/pulse group.
        if model == 1 or model == 3:
            key = (target_name, scan_number)
            if key not in self._scan_cache:
                self._scan_cache[key] = self._draw(mean_rcs_m2, chi4=(model == 3))
            return self._scan_cache[key]

        # II / IV
        _ = dwell_index
        return self._draw(mean_rcs_m2, chi4=(model == 4))

    def _draw(self, mean_rcs_m2: float, chi4: bool) -> float:
        if chi4:
            # Chi-square 4 DOF / Gamma(k=2): mean = 2θ → θ = mean/2
            # Two exponential averages.
            u1 = float(self._rng.random())
            u2 = float(self._rng.random())
            if u1 < 1e-12:
                u1 = 1e-12
            if u2 < 1e-12:
                u2 = 1e-12
            return -0.5 * mean_rcs_m2 * (math.log(u1) + math.log(u2))
        u = float(self._rng.random())
        if u < 1e-12:
            u = 1e-12
        return -mean_rcs_m2 * math.log(u)


def clutter_sigma0_frequency_scale(f_base_hz: float, f_new_hz: float) -> float:
    """Mild empirical σ⁰ frequency scaling between nearby carriers.

    Rough land-clutter rule of thumb: ~f^0.5 over microwave bands when
    roughness is comparable to wavelength. Clamped for large jumps.
    """
    if f_base_hz <= 0.0 or f_new_hz <= 0.0:
        return 1.0
    ratio = f_new_hz / f_base_hz
    if ratio < 0.5:
        ratio = 0.5
    if ratio > 2.0:
        ratio = 2.0
    return math.sqrt(ratio)
