#!/usr/bin/env python3
"""Propagation extras: frequency retune, multipath, and Swerling RCS.

These are framework utilities used by the scan loop. They do not bake into DEM.

Optional fidelity (LOS two-ray, 4/3 refraction, PRF folds) mirrors in-game
RDF_RadarSettings Enable* APIs — default OFF; call ChannelFidelity helpers.
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
    """Clone hardware with retuned carrier (wavelength, atm, band label).

    Same physical aperture: G and beamwidth stay put. For a one-shot
    same-aperture retune use scale_aperture_to_frequency.
    """
    tuned = copy.deepcopy(hardware)
    if frequency_hz <= 0.0:
        return tuned
    tuned.frequency_hz = frequency_hz
    tuned.band = band_for_frequency(frequency_hz)
    tuned.atm_loss_db_per_km_one_way = atmospheric_one_way_db_per_km(frequency_hz)
    return tuned


KA_OPTICAL = 10.0


def scale_rcs_for_wavelength(
    optical_rcs_m2: float,
    characteristic_length_m: float,
    wavelength_m: float,
    ka_optical: float = KA_OPTICAL,
) -> float:
    """Rayleigh roll-off of optical-region RCS when ka < 10 (a = L/2).

    Mirrors RDF_RadarRcsModel.ScaleRcsForWavelength:
    σ = σ_opt · (ka / 10)^4 for ka < 10, else optical.
    """
    optical = float(optical_rcs_m2)
    if optical <= 0.0:
        return 0.0
    lam = float(wavelength_m)
    if lam <= 0.0:
        return optical
    a = float(characteristic_length_m) * 0.5
    if a < 0.01:
        a = 0.01
    ka = 2.0 * math.pi * a / lam
    if ka >= float(ka_optical):
        return optical
    ratio = ka / float(ka_optical)
    scale = ratio * ratio * ratio * ratio
    rcs = optical * scale
    if rcs < 1.0e-8:
        rcs = 1.0e-8
    return rcs


def scale_aperture_to_frequency(
    hardware: RadarHardware,
    new_frequency_hz: float,
) -> RadarHardware:
    """Clone and retune the same physical aperture (G ∝ f², HPBW ∝ 1/f).

    Dual-band factories must not use this — they are separate antennas.
    Mirrors RDF_RadarHardware.ScaleApertureToFrequency.
    """
    new_hz = float(new_frequency_hz)
    if new_hz < 1.0e6:
        return copy.deepcopy(hardware)
    tuned = hardware_at_frequency(hardware, new_hz)
    old_hz = float(hardware.frequency_hz)
    if old_hz < 1.0:
        return tuned
    ratio = new_hz / old_hz
    if ratio <= 0.0:
        return tuned
    tuned.antenna_gain_dbi = hardware.antenna_gain_dbi + 20.0 * math.log10(ratio)
    beam = hardware.az_beamwidth_deg / ratio
    if beam < 0.1:
        beam = 0.1
    if beam > 360.0:
        beam = 360.0
    tuned.az_beamwidth_deg = beam
    return tuned


def scan_frequency_hz(
    center_hz: float,
    hop_set_hz: list[float] | None,
    scan_number: int,
    hop_enabled: bool,
    hop_stagger_ratio: float = 1.05,
) -> float:
    """Intra-band hop carrier. Empty hop set + ratio synthesizes a 2-tone pair.

    Hop does not re-scale G / beamwidth. Mirrors RDF_RadarHardware.GetScanFrequencyHz.
    """
    center = float(center_hz)
    if not hop_enabled:
        return center
    hops: list[float] = []
    if hop_set_hz:
        hops = [float(x) for x in hop_set_hz]
    if len(hops) <= 0:
        if hop_stagger_ratio <= 1.001:
            return center
        n = int(scan_number)
        if n < 0:
            n = 0
        bit = n - (n // 2) * 2
        if bit == 0:
            return center
        hopped = center * float(hop_stagger_ratio)
        if hopped < 1.0e6:
            hopped = 1.0e6
        return hopped
    n = int(scan_number)
    if n < 0:
        n = 0
    count = len(hops)
    index = 0
    if count > 1:
        index = n - (n // count) * count
        if index < 0:
            index = 0
    freq = hops[index]
    if freq < 1.0e6:
        freq = center
    return freq


def select_band_index(
    kind: int,
    search_index: int,
    track_index: int,
    fire_control_index: int,
    n_channels: int,
) -> int:
    """Channel index for a dwell kind. kind: 0 SEARCH, 1 TRACK, 2 FIRE_CONTROL.

    Mirrors RDF_RadarSettings.SelectBandForDwell.
    """
    idx = int(search_index)
    if kind == 1:
        idx = int(track_index)
    elif kind == 2:
        idx = int(fire_control_index)
    n = int(n_channels)
    if n <= 0:
        return 0
    if idx < 0:
        return 0
    if idx >= n:
        return 0
    return idx


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


def fresnel_reflection_coeff_h(eps_r: float, grazing_rad: float) -> float:
    """Horizontal-pol Fresnel Γ (mirrors RDF_RadarClutterModel)."""
    eps = float(eps_r)
    if eps < 1.0:
        eps = 1.0
    theta = float(grazing_rad)
    if theta < 0.001:
        theta = 0.001
    if theta > 0.5 * math.pi:
        theta = 0.5 * math.pi
    s = math.sin(theta)
    c = math.cos(theta)
    inside = eps - c * c
    if inside < 1.0e-6:
        inside = 1.0e-6
    root = math.sqrt(inside)
    denom = s + root
    if denom < 1.0e-6:
        denom = 1.0e-6
    return (s - root) / denom


def roughness_damp_reflection_abs(
    gamma_abs: float,
    roughness: float,
    grazing_rad: float,
) -> float:
    """Damp |Γ| by roughness; exp(-x) via 0.5^(x/ln2) like Enforce."""
    g = abs(float(gamma_abs))
    r = max(0.0, float(roughness))
    s = max(0.0, math.sin(float(grazing_rad)))
    x = 8.0 * r * s
    if x <= 0.0:
        return g
    damp = math.pow(0.5, x / 0.693147)
    if damp < 0.05:
        damp = 0.05
    if damp > 1.0:
        damp = 1.0
    return g * damp


def two_ray_multipath_factor(
    wavelength_m: float,
    range_m: float,
    radar_height_agl_m: float,
    target_height_agl_m: float,
    reflection_coeff: float = -0.5,
    max_height_m: float = 600.0,
    min_factor: float = 0.08,
    max_factor: float = 4.0,
) -> float:
    """Clear-LOS two-ray power factor (not a waveform simulation)."""
    if wavelength_m <= 0.0 or range_m < 1.0:
        return 1.0
    if radar_height_agl_m < 1.0 or target_height_agl_m < 1.0:
        return 1.0
    if max_height_m > 0.0 and target_height_agl_m > max_height_m:
        return 1.0

    delta = 2.0 * radar_height_agl_m * target_height_agl_m / range_m
    phase = 2.0 * math.pi * delta / wavelength_m
    real = 1.0 + reflection_coeff * math.cos(phase)
    imag = reflection_coeff * math.sin(phase)
    factor = real * real + imag * imag
    lo = min_factor
    if lo < 0.01:
        lo = 0.01
    hi = max_factor
    if hi < lo:
        hi = lo
    if factor < lo:
        factor = lo
    if factor > hi:
        factor = hi
    return factor


def effective_earth_radius_m(earth_radius_factor: float = 4.0 / 3.0) -> float:
    k = float(earth_radius_factor)
    if k < 0.5:
        k = 0.5
    if k > 4.0:
        k = 4.0
    return k * 6371000.0


def radio_horizon_range_m(
    radar_height_agl_m: float,
    target_height_agl_m: float,
    earth_radius_factor: float = 4.0 / 3.0,
) -> float:
    re = effective_earth_radius_m(earth_radius_factor)
    hr = max(0.0, float(radar_height_agl_m))
    ht = max(0.0, float(target_height_agl_m))
    return math.sqrt(2.0 * re * hr) + math.sqrt(2.0 * re * ht)


def horizon_soft_factor(range_m: float, horizon_m: float) -> float:
    if horizon_m <= 1.0 or range_m <= horizon_m:
        return 1.0
    over = (range_m - horizon_m) / horizon_m
    if over < 0.0:
        over = 0.0
    f = 1.0 / (1.0 + 4.0 * over * over)
    if f < 0.02:
        f = 0.02
    return f


def refraction_elevation_bias_deg(
    range_m: float,
    earth_radius_factor: float = 4.0 / 3.0,
) -> float:
    if range_m < 1.0:
        return 0.0
    re = effective_earth_radius_m(earth_radius_factor)
    if re < 1.0:
        return 0.0
    return (range_m / (2.0 * re)) * (180.0 / math.pi)


def fold_range_ambiguous(range_m: float, unambiguous_range_m: float) -> float:
    if unambiguous_range_m <= 1.0:
        return range_m
    if range_m < 0.0:
        return 0.0
    folded = range_m - math.floor(range_m / unambiguous_range_m) * unambiguous_range_m
    if folded < 0.0:
        folded = folded + unambiguous_range_m
    return folded


def fold_doppler_ambiguous(doppler_hz: float, prf_hz: float) -> float:
    if prf_hz <= 0.0:
        return doppler_hz
    half = prf_hz * 0.5
    x = float(doppler_hz)
    while x >= half:
        x = x - prf_hz
    while x < -half:
        x = x + prf_hz
    return x


@dataclass
class ChannelFidelity:
    """Opt-in propagation / measurement extras (mirrors RDF_RadarSettings).

    Defaults match StabilizeForRegression / game defaults: all optional
    fidelity off until Enable* helpers are called.
    """

    enable_los_two_ray_multipath: bool = False
    enable_atmospheric_refraction: bool = False
    enable_range_ambiguity_fold: bool = False
    enable_doppler_ambiguity_fold: bool = False
    earth_radius_factor: float = 4.0 / 3.0
    los_two_ray_reflection_coeff: float = -0.5
    los_two_ray_max_target_agl_m: float = 600.0
    los_two_ray_min_factor: float = 0.08
    los_two_ray_max_factor: float = 4.0
    # When True, MeasurementModel skips Doppler fold (WLR / weapon-locate).
    weapon_locate: bool = False

    def enable_los_two_ray(self) -> "ChannelFidelity":
        self.enable_los_two_ray_multipath = True
        self.los_two_ray_reflection_coeff = -0.5
        self.los_two_ray_max_target_agl_m = 600.0
        self.los_two_ray_min_factor = 0.08
        self.los_two_ray_max_factor = 4.0
        return self

    def enable_refraction(self) -> "ChannelFidelity":
        self.enable_atmospheric_refraction = True
        self.earth_radius_factor = 4.0 / 3.0
        return self

    def enable_prf_ambiguity_folds(
        self,
        fold_range: bool = True,
        fold_doppler: bool = True,
    ) -> "ChannelFidelity":
        self.enable_range_ambiguity_fold = fold_range
        self.enable_doppler_ambiguity_fold = fold_doppler
        return self

    def stabilize_for_regression(self) -> "ChannelFidelity":
        """Turn off optional fidelity extras (AutoTest / golden helper)."""
        self.enable_los_two_ray_multipath = False
        self.enable_atmospheric_refraction = False
        self.enable_range_ambiguity_fold = False
        self.enable_doppler_ambiguity_fold = False
        return self

    def multipath_model(self) -> "MultipathModel":
        return MultipathModel(
            enabled=self.enable_los_two_ray_multipath,
            reflection_coeff=self.los_two_ray_reflection_coeff,
            max_height_m=self.los_two_ray_max_target_agl_m,
            min_factor=self.los_two_ray_min_factor,
            max_factor=self.los_two_ray_max_factor,
        )

    def horizon_power_factor(
        self,
        range_m: float,
        radar_height_agl_m: float,
        target_height_agl_m: float,
    ) -> float:
        """1.0 unless refraction enabled; then soft radio-horizon roll-off."""
        if not self.enable_atmospheric_refraction:
            return 1.0
        horizon = radio_horizon_range_m(
            radar_height_agl_m,
            target_height_agl_m,
            self.earth_radius_factor,
        )
        return horizon_soft_factor(range_m, horizon)


@dataclass
class MultipathModel:
    """Simple two-ray multipath power factor for low-altitude paths.

    Default disabled to match in-game EnableLosTwoRayMultipath() opt-in.
    """

    enabled: bool = False
    reflection_coeff: float = -0.5
    max_height_m: float = 600.0
    min_factor: float = 0.08
    max_factor: float = 4.0

    def power_factor(
        self,
        wavelength_m: float,
        range_m: float,
        radar_height_agl_m: float,
        target_height_agl_m: float,
        reflection_coeff: float | None = None,
    ) -> float:
        if not self.enabled:
            return 1.0
        gamma = self.reflection_coeff
        if reflection_coeff is not None:
            gamma = float(reflection_coeff)
        return two_ray_multipath_factor(
            wavelength_m,
            range_m,
            radar_height_agl_m,
            target_height_agl_m,
            reflection_coeff=gamma,
            max_height_m=self.max_height_m,
            min_factor=self.min_factor,
            max_factor=self.max_factor,
        )

    def power_factor_from_dielectric(
        self,
        wavelength_m: float,
        range_m: float,
        radar_height_agl_m: float,
        target_height_agl_m: float,
        eps_r: float,
        roughness: float = 0.0,
    ) -> float:
        """Fresnel Γ from surface dielectric + roughness damp, then two-ray."""
        if not self.enabled:
            return 1.0
        grazing = math.atan2(
            radar_height_agl_m + target_height_agl_m,
            max(1.0, range_m),
        )
        fresnel = fresnel_reflection_coeff_h(eps_r, grazing)
        abs_g = roughness_damp_reflection_abs(abs(fresnel), roughness, grazing)
        if fresnel < 0.0:
            gamma = -abs_g
        else:
            gamma = abs_g
        return self.power_factor(
            wavelength_m,
            range_m,
            radar_height_agl_m,
            target_height_agl_m,
            reflection_coeff=gamma,
        )


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


def _wrap_delta_deg(delta_deg: float) -> float:
    rel = float(delta_deg)
    while rel > 180.0:
        rel = rel - 360.0
    while rel < -180.0:
        rel = rel + 360.0
    return rel


def aspect_factor(yaw_deg: float, los_azimuth_deg: float) -> float:
    """Horizontal aspect: nose/tail ~1.0, broadside ~0.35."""
    rel = abs(_wrap_delta_deg(los_azimuth_deg - yaw_deg))
    c = math.cos(math.radians(rel))
    return 0.35 + 0.65 * abs(c)


def elevation_factor(pitch_deg: float, los_elevation_deg: float) -> float:
    """Elevation vs body pitch: 1.0 along body axis, 0.5 at 90° look."""
    rel = abs(_wrap_delta_deg(los_elevation_deg - pitch_deg))
    if rel > 90.0:
        rel = 180.0 - rel
    c = math.cos(math.radians(rel))
    return 0.50 + 0.50 * abs(c)


def aspect_factor_3d(
    yaw_deg: float,
    pitch_deg: float,
    los_azimuth_deg: float,
    los_elevation_deg: float,
) -> float:
    return aspect_factor(yaw_deg, los_azimuth_deg) * elevation_factor(
        pitch_deg, los_elevation_deg
    )


def los_unit_from_az_el(azimuth_deg: float, elevation_deg: float) -> tuple[float, float, float]:
    """LOS unit: azimuth atan2(Z,X), elevation from horizon. Matches Enforce."""
    az = math.radians(float(azimuth_deg))
    el = math.radians(float(elevation_deg))
    ce = math.cos(el)
    return (ce * math.cos(az), math.sin(el), ce * math.sin(az))


def axes_from_yaw_pitch(
    yaw_deg: float,
    pitch_deg: float,
) -> tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]:
    """Zero-roll body axes (right, up, forward). Matches RDF_RadarRcsModel."""
    yaw = math.radians(float(yaw_deg))
    pitch = math.radians(float(pitch_deg))
    cy = math.cos(yaw)
    sy = math.sin(yaw)
    cp = math.cos(pitch)
    sp = math.sin(pitch)
    forward = (cp * cy, sp, cp * sy)
    right = (-sy, 0.0, cy)
    up = (
        right[1] * forward[2] - right[2] * forward[1],
        right[2] * forward[0] - right[0] * forward[2],
        right[0] * forward[1] - right[1] * forward[0],
    )
    def _norm(v: tuple[float, float, float], fb: tuple[float, float, float]):
        length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
        if length < 1e-3:
            return fb
        inv = 1.0 / length
        return (v[0] * inv, v[1] * inv, v[2] * inv)

    return (
        _norm(right, (0.0, 0.0, 1.0)),
        _norm(up, (0.0, 1.0, 0.0)),
        _norm(forward, (1.0, 0.0, 0.0)),
    )


def aspect_rcs_from_obb(
    mean_rcs_m2: float,
    size_x: float,
    size_y: float,
    size_z: float,
    axis_right: tuple[float, float, float],
    axis_up: tuple[float, float, float],
    axis_forward: tuple[float, float, float],
    los_azimuth_deg: float,
    los_elevation_deg: float,
) -> float:
    """Mirror of RDF_RadarRcsModel.AspectRcsFromObb."""
    fallback = float(mean_rcs_m2)
    if fallback <= 0.0:
        fallback = 1.0
    if size_x <= 0.01 and size_y <= 0.01 and size_z <= 0.01:
        return fallback

    height = size_y
    if height < 0.1:
        height = 0.1
    length = size_z
    if length < 0.1:
        length = 0.1
    beam = size_x
    if beam < 0.1:
        beam = 0.1

    los = los_unit_from_az_el(los_azimuth_deg, los_elevation_deg)

    def _abs_dot(a, b) -> float:
        return abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2])

    u_f = _abs_dot(los, axis_forward)
    u_s = _abs_dot(los, axis_right)
    u_t = _abs_dot(los, axis_up)
    projected = u_f * beam * height + u_s * length * height + u_t * beam * length
    estimate = projected * 0.25
    lo = fallback * 0.2
    hi = fallback * 4.0
    if estimate < lo:
        estimate = lo
    if estimate > hi:
        estimate = hi
    return estimate


def aspect_rcs_from_extents(
    mean_rcs_m2: float,
    size_x: float,
    size_y: float,
    size_z: float,
    yaw_deg: float,
    los_azimuth_deg: float,
    pitch_deg: float = 0.0,
    los_elevation_deg: float = 0.0,
) -> float:
    """Mirror of RDF_RadarRcsModel.AspectRcsFromExtents3D (zero-roll OBB)."""
    fallback = float(mean_rcs_m2)
    if fallback <= 0.0:
        fallback = 1.0

    if size_x <= 0.01 and size_y <= 0.01 and size_z <= 0.01:
        return fallback * aspect_factor_3d(
            yaw_deg, pitch_deg, los_azimuth_deg, los_elevation_deg
        )

    axis_right, axis_up, axis_forward = axes_from_yaw_pitch(yaw_deg, pitch_deg)
    return aspect_rcs_from_obb(
        mean_rcs_m2,
        size_x,
        size_y,
        size_z,
        axis_right,
        axis_up,
        axis_forward,
        los_azimuth_deg,
        los_elevation_deg,
    )
