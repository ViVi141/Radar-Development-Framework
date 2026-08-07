#!/usr/bin/env python3
"""Cold-War-style radar equation, waveform, atmosphere, MTI, and CFAR helpers.

Monostatic received RF power (watts) before processing:

  Pr = Pt * Gt * Gr * λ² * σ * F_pol
       / ((4π)³ * R⁴ * L_sys * L_atm(R))

Processing chain (linear gains, not "display gain"):
  1) pulse compression gain ≈ B * τ  (chirp) or 1 (uncoded)
  2) coherent / non-coherent pulse integration over CPI
  3) MTI: two-pulse |sin(π f_d / PRF)|² (default) or MTD DFT bank max_k |H_k|²
  4) CA-CFAR thresholding for detections

Presets approximate open-literature Cold War search / AD radars.
Instrumented ranges may exceed the DEM map; geometry is clamped to DEM.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

C_LIGHT_M_S = 299792458.0
K_BOLTZMANN = 1.380649e-23
T0_K = 290.0


def db_to_lin(db: float) -> float:
    return 10.0 ** (db / 10.0)


def lin_to_db(linear: float) -> float:
    if linear <= 1e-30:
        return -300.0
    return 10.0 * math.log10(linear)


def watts_to_dbm(watts: float) -> float:
    if watts <= 1e-30:
        return -300.0
    return 10.0 * math.log10(watts * 1000.0)


def dbm_array(watts: np.ndarray) -> np.ndarray:
    return 10.0 * np.log10(np.maximum(watts, 1e-30) * 1000.0)


@dataclass
class ElevationBeam:
    """One independently addressable elevation beam."""

    name: str
    boresight_deg: float
    beamwidth_deg: float
    relative_gain_db: float = 0.0


@dataclass
class RadarHardware:
    """Hardware + waveform parameters (engineering / open-literature order)."""

    name: str = "ad_xband"
    display_name: str = "Generic X-band AD"
    era: str = "modern"
    band: str = "X"  # drives default σ⁰ table if not overridden
    frequency_hz: float = 9.5e9
    peak_power_w: float = 5.0e4
    antenna_gain_dbi: float = 35.0
    az_beamwidth_deg: float = 2.0
    el_beamwidth_deg: float = 10.0
    el_boresight_deg: float = 5.0
    elevation_beams: list[ElevationBeam] = field(default_factory=list)
    scan_rpm: float = 6.0
    sidelobe_level_db: float = -25.0
    system_loss_db: float = 6.0
    noise_figure_db: float = 4.0
    # Waveform
    pulse_width_s: float = 1.0e-6
    # Chirp / coded bandwidth. If 0 → uncoded, B ≈ 1/τ.
    chirp_bandwidth_hz: float = 0.0
    prf_hz: float = 2000.0
    # Integration: pulses in CPI (scan dwell approx).
    pulses_integrated: int = 16
    coherent_integration: bool = False
    # Polarization mode: "H" | "V" | "CIRCULAR".
    polarization_mode: str = "H"
    # Extra polarization trim (linear, ≤1). Multiplies mode match factor.
    polarization_factor: float = 1.0
    # When True, floor two-way pattern to sidelobe_level_db^2.
    enable_sidelobe_floor: bool = True
    # When True, use segmented az pattern LUT (mainlobe + peaks + floor).
    enable_pattern_lut: bool = True
    # When True, apply polarization_mode match table.
    enable_polarization_match: bool = True
    # Two-way atmospheric loss coefficient near surface [dB/km] one-way * 2 applied.
    atm_loss_db_per_km_one_way: float = 0.01
    # Instrumented / advertised max range (m). DEM may be smaller.
    instrumented_range_m: float = 6000.0
    # Preferred range bin for this waveform (m). 0 → derive from bandwidth.
    preferred_range_bin_m: float = 0.0
    mti_clutter_floor: float = 1.0e-4
    mtd_clutter_leakage: float = 1.0e-6
    enable_mti: bool = True
    # "twopulse" | "three_pulse" | "mtd_bank"
    mti_mode: str = "twopulse"
    # Classic canceller: max gain over prf_set_hz when True.
    mti_stagger_deblind: bool = False
    doppler_bin_count: int = 16
    # Optional multi-PRF list; empty → use prf_hz only.
    prf_set_hz: list[float] = field(default_factory=list)
    clutter_sigma_vr_m_s: float = 0.5
    enable_pulse_compression: bool = True
    notes: str = ""

    def active_prf_hz(self, scan_number: int = 0) -> float:
        if not self.prf_set_hz:
            return self.prf_hz
        n = len(self.prf_set_hz)
        if n <= 0:
            return self.prf_hz
        idx = int(scan_number) % n
        prf = float(self.prf_set_hz[idx])
        if prf < 1.0:
            return self.prf_hz
        return prf

    def resolved_elevation_beams(self) -> list[ElevationBeam]:
        if self.elevation_beams:
            return self.elevation_beams
        return [
            ElevationBeam(
                name="main",
                boresight_deg=self.el_boresight_deg,
                beamwidth_deg=self.el_beamwidth_deg,
            )
        ]

    @property
    def scan_period_s(self) -> float:
        if self.scan_rpm <= 0.0:
            return 1.0e9
        return 60.0 / self.scan_rpm

    @property
    def wavelength_m(self) -> float:
        return C_LIGHT_M_S / self.frequency_hz

    @property
    def gain_linear(self) -> float:
        return db_to_lin(self.antenna_gain_dbi)

    @property
    def loss_linear(self) -> float:
        return db_to_lin(self.system_loss_db)

    @property
    def uncompressed_bandwidth_hz(self) -> float:
        if self.pulse_width_s <= 0.0:
            return 1.0e6
        return 1.0 / self.pulse_width_s

    @property
    def effective_bandwidth_hz(self) -> float:
        if self.enable_pulse_compression and self.chirp_bandwidth_hz > 0.0:
            return self.chirp_bandwidth_hz
        return self.uncompressed_bandwidth_hz

    @property
    def pulse_compression_gain(self) -> float:
        """Processing gain ≈ B·τ for chirp; 1 for uncoded long pulse."""
        if not self.enable_pulse_compression:
            return 1.0
        if self.chirp_bandwidth_hz <= 0.0:
            return 1.0
        gain = self.chirp_bandwidth_hz * self.pulse_width_s
        if gain < 1.0:
            return 1.0
        return gain

    @property
    def integration_gain(self) -> float:
        n = self.pulses_integrated
        if n < 1:
            return 1.0
        if self.coherent_integration:
            return float(n)
        return math.sqrt(float(n))

    @property
    def unambiguous_range_m(self) -> float:
        if self.prf_hz <= 0.0:
            return 1.0e9
        return C_LIGHT_M_S / (2.0 * self.prf_hz)

    @property
    def unambiguous_velocity_m_s(self) -> float:
        # |v| < λ·PRF/4
        return self.wavelength_m * self.prf_hz / 4.0

    def range_resolution_m(self) -> float:
        return C_LIGHT_M_S / (2.0 * self.effective_bandwidth_hz)

    def range_bin_m(self) -> float:
        if self.preferred_range_bin_m > 0.0:
            return self.preferred_range_bin_m
        res = self.range_resolution_m()
        if res < 5.0:
            return 5.0
        return res

    def bandwidth_for_noise(self) -> float:
        # Noise bandwidth ≈ matched to compressed pulse / IF.
        return self.effective_bandwidth_hz

    def noise_power_w(self, _range_bin_m: float = 0.0) -> float:
        bandwidth = self.bandwidth_for_noise()
        return K_BOLTZMANN * T0_K * bandwidth * db_to_lin(self.noise_figure_db)

    def radar_constant(self) -> float:
        """Pt Gt Gr λ² F_pol / ((4π)³ L_sys) — atmosphere applied per-range."""
        lam = self.wavelength_m
        g = self.gain_linear
        four_pi = 4.0 * math.pi
        denom = (four_pi**3) * self.loss_linear
        return (
            self.peak_power_w
            * g
            * g
            * (lam**2)
            * self.polarization_factor
            / denom
        )

    def atmospheric_loss_linear(self, range_m: float) -> float:
        # Two-way: 2 * α_dB/km * R_km
        if range_m < 0.0:
            range_m = 0.0
        loss_db = 2.0 * self.atm_loss_db_per_km_one_way * (range_m / 1000.0)
        return db_to_lin(loss_db)

    def processing_gain(self) -> float:
        return self.pulse_compression_gain * self.integration_gain


def atmospheric_one_way_db_per_km(frequency_hz: float) -> float:
    """Rough clear-air one-way loss near sea level (engineering fit)."""
    f_ghz = frequency_hz / 1.0e9
    if f_ghz < 1.0:
        return 0.003
    if f_ghz < 4.0:
        return 0.007
    if f_ghz < 8.0:
        return 0.012
    if f_ghz < 12.0:
        return 0.02
    return 0.04


# ---------------------------------------------------------------------------
# Cold War / tactical presets (order-of-magnitude open literature)
# ---------------------------------------------------------------------------


def preset_p18() -> RadarHardware:
    """P-18 (Spoon Rest D) style: VHF early-warning, long pulse, no PC."""
    freq = 160e6
    return RadarHardware(
        name="p18",
        display_name="P-18 Spoon Rest (VHF EW)",
        era="cold_war",
        band="VHF",
        frequency_hz=freq,
        peak_power_w=2.5e5,
        antenna_gain_dbi=18.0,
        az_beamwidth_deg=6.0,
        el_beamwidth_deg=20.0,
        el_boresight_deg=6.0,
        elevation_beams=[
            ElevationBeam("low", 3.0, 12.0, 0.0),
            ElevationBeam("high", 12.0, 16.0, -1.5),
        ],
        scan_rpm=6.0,
        sidelobe_level_db=-22.0,
        system_loss_db=8.0,
        noise_figure_db=6.0,
        pulse_width_s=6.0e-6,
        chirp_bandwidth_hz=0.0,
        prf_hz=200.0,
        pulses_integrated=8,
        coherent_integration=False,
        polarization_factor=0.9,
        atm_loss_db_per_km_one_way=atmospheric_one_way_db_per_km(freq),
        instrumented_range_m=250000.0,
        preferred_range_bin_m=900.0,  # ~c*τ/2
        mti_clutter_floor=1.0e-2,  # VHF MTI limited
        enable_mti=True,
        enable_pulse_compression=False,
        notes="VHF search; map DEM clamps geometry << instrumented 250 km",
    )


def preset_tps43() -> RadarHardware:
    """AN/TPS-43 style: S-band 3D tactical, pulse Doppler / MTI era."""
    freq = 3.0e9
    return RadarHardware(
        name="tps43",
        display_name="AN/TPS-43-like (S-band 3D)",
        era="cold_war",
        band="S",
        frequency_hz=freq,
        peak_power_w=4.0e6,
        antenna_gain_dbi=36.0,
        az_beamwidth_deg=1.1,
        # Cosecant-squared search coverage approximated as a tall fan for DEM demos.
        el_beamwidth_deg=12.0,
        el_boresight_deg=3.0,
        elevation_beams=[
            ElevationBeam("low", 2.0, 6.0, 0.0),
            ElevationBeam("mid", 8.0, 8.0, -1.0),
            ElevationBeam("high", 18.0, 12.0, -3.0),
        ],
        scan_rpm=6.0,
        sidelobe_level_db=-28.0,
        system_loss_db=7.0,
        noise_figure_db=4.5,
        pulse_width_s=6.5e-6,
        chirp_bandwidth_hz=2.0e6,  # modest coding / PD processing gain
        prf_hz=350.0,
        pulses_integrated=24,
        coherent_integration=True,
        polarization_factor=1.0,
        atm_loss_db_per_km_one_way=atmospheric_one_way_db_per_km(freq),
        instrumented_range_m=450000.0,
        preferred_range_bin_m=150.0,
        mti_clutter_floor=3.16e-5,
        enable_mti=True,
        enable_pulse_compression=True,
        notes="S-band PD/MTI; DEM clamps geometry on small maps",
    )


def preset_shorad_x() -> RadarHardware:
    """Cold-War short-range AD / gun-laying X-band (~6–20 km class)."""
    freq = 9.0e9
    return RadarHardware(
        name="shorad",
        display_name="Cold-War SHORAD X-band",
        era="cold_war",
        band="X",
        frequency_hz=freq,
        peak_power_w=1.2e5,
        antenna_gain_dbi=32.0,
        az_beamwidth_deg=2.5,
        el_beamwidth_deg=8.0,
        el_boresight_deg=4.0,
        scan_rpm=15.0,
        sidelobe_level_db=-25.0,
        system_loss_db=6.0,
        noise_figure_db=5.0,
        pulse_width_s=0.5e-6,
        chirp_bandwidth_hz=4.0e6,
        prf_hz=4000.0,
        pulses_integrated=32,
        coherent_integration=False,
        polarization_factor=1.0,
        atm_loss_db_per_km_one_way=atmospheric_one_way_db_per_km(freq),
        instrumented_range_m=20000.0,
        preferred_range_bin_m=40.0,
        mti_clutter_floor=1.0e-4,
        enable_mti=True,
        enable_pulse_compression=True,
        notes="Near-range AD — fits Everon DEM scale",
    )


def preset_artillery_x() -> RadarHardware:
    freq = 9.0e9
    return RadarHardware(
        name="artillery",
        display_name="Weapon-locating X-band",
        era="cold_war",
        band="X",
        frequency_hz=freq,
        peak_power_w=5.0e4,
        antenna_gain_dbi=38.0,
        az_beamwidth_deg=1.5,
        el_beamwidth_deg=8.0,
        el_boresight_deg=12.0,
        elevation_beams=[
            ElevationBeam("low", 6.0, 6.0, 0.0),
            ElevationBeam("high", 18.0, 10.0, -1.0),
        ],
        scan_rpm=0.0,
        sidelobe_level_db=-30.0,
        system_loss_db=5.0,
        noise_figure_db=3.5,
        pulse_width_s=1.0e-6,
        chirp_bandwidth_hz=8.0e6,
        prf_hz=4000.0,
        pulses_integrated=64,
        coherent_integration=True,
        polarization_factor=1.0,
        atm_loss_db_per_km_one_way=atmospheric_one_way_db_per_km(freq),
        instrumented_range_m=30000.0,
        preferred_range_bin_m=30.0,
        mti_clutter_floor=3.16e-5,
        enable_mti=True,
        enable_pulse_compression=True,
        notes="High-elevation WLR; small RCS shells need PC+integration",
    )


def preset_airborne_x() -> RadarHardware:
    freq = 10.0e9
    return RadarHardware(
        name="airborne",
        display_name="Airborne intercept X-band",
        era="cold_war",
        band="X",
        frequency_hz=freq,
        peak_power_w=1.0e4,
        antenna_gain_dbi=32.0,
        az_beamwidth_deg=3.0,
        el_beamwidth_deg=6.0,
        el_boresight_deg=0.0,
        scan_rpm=0.0,
        sidelobe_level_db=-30.0,
        system_loss_db=7.0,
        noise_figure_db=5.0,
        pulse_width_s=0.4e-6,
        chirp_bandwidth_hz=5.0e6,
        prf_hz=8000.0,
        pulses_integrated=32,
        coherent_integration=True,
        polarization_factor=1.0,
        atm_loss_db_per_km_one_way=atmospheric_one_way_db_per_km(freq),
        instrumented_range_m=40000.0,
        preferred_range_bin_m=40.0,
        mti_clutter_floor=1.0e-3,
        enable_mti=True,
        enable_pulse_compression=True,
        notes="Look-down clutter limited by platform motion",
    )


# Keep short alias "ad" → SHORAD (fits map). Long-range search → p18/tps43.
PRESETS = {
    "p18": preset_p18,
    "tps43": preset_tps43,
    "shorad": preset_shorad_x,
    "ad": preset_shorad_x,
    "artillery": preset_artillery_x,
    "airborne": preset_airborne_x,
}


def get_preset(name: str) -> RadarHardware:
    key = name.lower().strip().replace("-", "").replace("_", "")
    aliases = {
        "spoonrest": "p18",
        "tps": "tps43",
        "tps43like": "tps43",
        "wlr": "artillery",
        "aaa": "shorad",
    }
    if key in aliases:
        key = aliases[key]
    if key not in PRESETS:
        key = "shorad"
    return PRESETS[key]()


def gaussian_beam_gain(offset_deg: float, half_power_beamwidth_deg: float) -> float:
    if half_power_beamwidth_deg <= 1e-6:
        return 1.0
    half = 0.5 * half_power_beamwidth_deg
    x = offset_deg / half
    return math.exp(-0.693147 * (x * x))


def two_way_pattern_gain(
    hardware: RadarHardware,
    az_offset_deg: float,
    el_offset_deg: float,
) -> float:
    gaz = gaussian_beam_gain(az_offset_deg, hardware.az_beamwidth_deg)
    gel = gaussian_beam_gain(el_offset_deg, hardware.el_beamwidth_deg)
    one_way = gaz * gel
    return one_way * one_way


def elevation_beam_gains(
    hardware: RadarHardware,
    elevation_deg: float,
    az_offset_deg: float = 0.0,
) -> dict[str, float]:
    """Return two-way gain for every configured elevation beam."""
    result: dict[str, float] = {}
    if hardware.enable_pattern_lut:
        from rdf_radar_pattern_lut import segmented_one_way_gain

        az_gain = segmented_one_way_gain(
            az_offset_deg,
            hardware.az_beamwidth_deg,
            hardware.sidelobe_level_db,
        )
    else:
        az_gain = gaussian_beam_gain(az_offset_deg, hardware.az_beamwidth_deg)
    for beam in hardware.resolved_elevation_beams():
        el_gain = gaussian_beam_gain(
            elevation_deg - beam.boresight_deg,
            beam.beamwidth_deg,
        )
        relative = db_to_lin(beam.relative_gain_db)
        one_way = az_gain * el_gain * relative
        result[beam.name] = one_way * one_way
    return result


def two_way_sidelobe_floor(sidelobe_level_db: float) -> float:
    one_way = db_to_lin(sidelobe_level_db)
    if one_way < 0.0:
        one_way = 0.0
    return one_way * one_way


def apply_two_way_sidelobe_floor(
    two_way_gain: float,
    sidelobe_level_db: float,
) -> float:
    floor_two = two_way_sidelobe_floor(sidelobe_level_db)
    if two_way_gain < floor_two:
        return floor_two
    return two_way_gain


def combined_two_way_pattern_gain(
    hardware: RadarHardware,
    elevation_deg: float,
    az_offset_deg: float = 0.0,
    apply_sidelobe_floor: bool | None = None,
) -> float:
    """Use the strongest configured elevation beam at a look direction."""
    gains = elevation_beam_gains(hardware, elevation_deg, az_offset_deg)
    if not gains:
        return 0.0
    strongest = max(gains.values())
    use_floor = apply_sidelobe_floor
    if use_floor is None:
        use_floor = hardware.enable_sidelobe_floor
    # Segmented pattern LUT already embeds the one-way floor.
    if use_floor and (not hardware.enable_pattern_lut):
        strongest = apply_two_way_sidelobe_floor(
            strongest,
            hardware.sidelobe_level_db,
        )
    return strongest


def polarization_match_factor(
    mode: str,
    target_type: str,
) -> float:
    """Engineering match table (linear ≤1). Mirrors Enforce ClutterModel."""
    mode_key = str(mode).upper()
    typ = str(target_type).upper()
    if mode_key in ("CIRC", "CIRCULAR", "RHCP", "LHCP"):
        if typ in ("PROJECTILE", "MISSILE", "ROCKET"):
            return 0.95
        if typ in ("VEHICLE", "HELI", "HELICOPTER"):
            return 0.75
        if typ in ("EMITTER", "RADAR_EMITTER"):
            return 0.80
        return 0.85
    if typ in ("PROJECTILE", "MISSILE", "ROCKET"):
        return 1.0
    if typ in ("VEHICLE", "HELI", "HELICOPTER"):
        return 0.65
    if typ in ("EMITTER", "RADAR_EMITTER"):
        return 0.85
    return 0.90


def polarization_clutter_factor(
    mode: str,
    rain_db_per_km_one_way: float,
) -> float:
    mode_key = str(mode).upper()
    if mode_key not in ("CIRC", "CIRCULAR", "RHCP", "LHCP"):
        return 1.0
    if rain_db_per_km_one_way < 0.05:
        return 1.0
    return 0.20


def resolve_polarization_factor(
    hardware: RadarHardware,
    target_type: str,
) -> float:
    factor = float(hardware.polarization_factor)
    if hardware.enable_polarization_match:
        factor = factor * polarization_match_factor(
            hardware.polarization_mode,
            target_type,
        )
    if factor < 0.05:
        factor = 0.05
    if factor > 1.0:
        factor = 1.0
    return factor


def received_power_w(
    hardware: RadarHardware,
    sigma_m2: float,
    range_m: float,
    pattern_two_way: float = 1.0,
) -> float:
    """RF power at receiver input (before PC / integration / MTI)."""
    if range_m < 1.0:
        range_m = 1.0
    if sigma_m2 <= 0.0 or pattern_two_way <= 0.0:
        return 0.0
    latm = hardware.atmospheric_loss_linear(range_m)
    return (
        hardware.radar_constant()
        * pattern_two_way
        * sigma_m2
        / ((range_m**4) * latm)
    )


def processed_power_w(
    hardware: RadarHardware,
    rf_power_w: float,
) -> float:
    """After pulse compression + pulse integration (still before MTI)."""
    return rf_power_w * hardware.processing_gain()


def doppler_hz(radial_speed_m_s: float, wavelength_m: float) -> float:
    if wavelength_m <= 0.0:
        return 0.0
    return 2.0 * radial_speed_m_s / wavelength_m


def mti_two_pulse_gain(doppler_hz_value: float, prf_hz: float) -> float:
    if prf_hz <= 0.0:
        return 1.0
    phase = math.pi * doppler_hz_value / prf_hz
    raw = abs(math.sin(phase))
    return raw * raw


def mti_three_pulse_gain(doppler_hz_value: float, prf_hz: float) -> float:
    g2 = mti_two_pulse_gain(doppler_hz_value, prf_hz)
    return g2 * g2


def mti_canceller_gain(mode: str, doppler_hz_value: float, prf_hz: float) -> float:
    mode_l = (mode or "twopulse").lower()
    if "three" in mode_l:
        return mti_three_pulse_gain(doppler_hz_value, prf_hz)
    return mti_two_pulse_gain(doppler_hz_value, prf_hz)


def max_mti_canceller_spectrum_gain(
    doppler_hz_lines: list[float],
    powers: list[float],
    prf_hz_list: list[float],
    mode: str = "twopulse",
) -> tuple[float, float]:
    """Power-weighted max canceller gain over lines × PRFs. Returns (gain, peak_fd)."""
    if not doppler_hz_lines:
        return 1.0, 0.0
    if not prf_hz_list:
        return 1.0, float(doppler_hz_lines[0])
    weights = []
    for i in range(len(doppler_hz_lines)):
        if i < len(powers):
            weights.append(max(0.0, float(powers[i])))
        else:
            weights.append(1.0)
    power_sum = sum(weights)
    if power_sum <= 0.0:
        power_sum = 1.0
    best = -1.0
    best_fd = float(doppler_hz_lines[0])
    for prf in prf_hz_list:
        prf_f = float(prf)
        if prf_f < 1.0:
            continue
        channel = 0.0
        fd_acc = 0.0
        fd_w = 0.0
        for i, fd in enumerate(doppler_hz_lines):
            w = weights[i]
            g = mti_canceller_gain(mode, float(fd), prf_f)
            contrib = w * g
            channel += contrib
            fd_acc += float(fd) * contrib
            fd_w += contrib
        normalized = channel / power_sum
        if normalized > best:
            best = normalized
            if fd_w > 0.0:
                best_fd = fd_acc / fd_w
    if best < 0.0:
        best = 0.0
    return best, best_fd


def suggest_mti_clutter_floor(
    sigma_vr_m_s: float,
    wavelength_m: float,
    prf_hz: float,
    canceller_order: int = 1,
) -> float:
    """Classic MTI residue ≈ (σ_f/PRF)^{2N}; N=1 two-pulse, N=2 three-pulse."""
    if wavelength_m <= 0.0 or prf_hz <= 0.0:
        return 1.0e-4
    sigma_vr = max(0.05, float(sigma_vr_m_s))
    order = int(canceller_order)
    if order < 1:
        order = 1
    if order > 2:
        order = 2
    sigma_fd = 2.0 * sigma_vr / wavelength_m
    x = abs(sigma_fd / prf_hz)
    residue = x ** (2 * order)
    if residue < 1.0e-6:
        residue = 1.0e-6
    if residue > 0.5:
        residue = 0.5
    return residue


def wrap_normalized_doppler(norm_fd: float) -> float:
    x = float(norm_fd)
    while x >= 0.5:
        x -= 1.0
    while x < -0.5:
        x += 1.0
    return x


def mtd_bin_power_gain(
    doppler_hz_value: float,
    prf_hz: float,
    bin_index: int,
    bin_count: int,
) -> float:
    """Peak-normalized DFT / MTD bin response |H_k(fd)|²."""
    if prf_hz <= 0.0:
        return 1.0
    if bin_count < 2:
        return 1.0
    if bin_index < 0 or bin_index >= bin_count:
        return 0.0
    norm_fd = wrap_normalized_doppler(doppler_hz_value / prf_hz)
    bin_center = bin_index / float(bin_count)
    if bin_center >= 0.5:
        bin_center -= 1.0
    delta = wrap_normalized_doppler(norm_fd - bin_center)
    if abs(delta) < 1.0e-7:
        return 1.0
    n = float(bin_count)
    num = abs(math.sin(math.pi * n * delta))
    den = abs(n * math.sin(math.pi * delta))
    if den < 1.0e-7:
        return 0.0
    h = num / den
    return h * h


def max_mtd_bin_gain(
    doppler_hz_value: float,
    prf_hz: float,
    bin_count: int,
) -> tuple[float, int]:
    best = -1.0
    best_bin = 0
    for k in range(max(2, int(bin_count))):
        g = mtd_bin_power_gain(doppler_hz_value, prf_hz, k, bin_count)
        if g > best:
            best = g
            best_bin = k
    if best < 0.0:
        best = 0.0
    return best, best_bin


def max_mtd_spectrum_gain(
    doppler_hz_lines: list[float],
    powers: list[float],
    prf_hz: float,
    bin_count: int,
    mti_clutter_floor: float = 1.0e-4,
    mtd_clutter_leakage: float = 1.0e-6,
) -> tuple[float, int, float]:
    """Clutter-aware max over body + micro-Doppler lines.

    Returns (power fraction in winning bin, bin index, peak fd).
    Sidebands in non-zero bins win when body vr≈0 because clutter floor ≫ leakage.
    """
    if not doppler_hz_lines:
        return 1.0, 0, 0.0
    if prf_hz <= 0.0 or bin_count < 2:
        return 1.0, 0, float(doppler_hz_lines[0])
    n_lines = len(doppler_hz_lines)
    weights = []
    for i in range(n_lines):
        if i < len(powers):
            weights.append(max(0.0, float(powers[i])))
        else:
            weights.append(1.0)
    power_sum = sum(weights)
    if power_sum <= 0.0:
        power_sum = 1.0

    best_score = -1.0
    best_norm = 0.0
    best_bin = 0
    best_fd = float(doppler_hz_lines[0])
    for k in range(bin_count):
        channel = 0.0
        fd_acc = 0.0
        fd_w = 0.0
        for i, fd in enumerate(doppler_hz_lines):
            w = weights[i]
            if w <= 0.0:
                continue
            g = mtd_bin_power_gain(fd, prf_hz, k, bin_count)
            contrib = w * g
            channel += contrib
            fd_acc += fd * contrib
            fd_w += contrib
        normalized = channel / power_sum
        clutter_gain = mtd_clutter_bin_gain(
            k, bin_count, mti_clutter_floor, mtd_clutter_leakage
        )
        score = normalized / (clutter_gain + 1.0e-7)
        if score > best_score:
            best_score = score
            best_norm = normalized
            best_bin = k
            if fd_w > 0.0:
                best_fd = fd_acc / fd_w
    if best_norm < 0.0:
        best_norm = 0.0
    return best_norm, best_bin, best_fd


def rotor_disk_aspect_scale(los_elevation_deg: float) -> float:
    """Horizon (edge-on disk) → 1; steep look-down/up → ~0.2."""
    el = abs(float(los_elevation_deg))
    if el > 90.0:
        el = 180.0 - el
    el = max(0.0, min(90.0, el))
    edge_on = abs(math.cos(math.radians(el)))
    return float(max(0.2, min(1.0, 0.2 + 0.8 * edge_on)))


def rotor_doppler_spectrum(
    body_doppler_hz: float,
    wavelength_m: float,
    tip_speed_m_s: float = 0.0,
    rotor_rcs_fraction: float = 0.0,
    hub_width_m_s: float = 0.0,
    blade_count: int = 2,
    los_elevation_deg: float = 0.0,
) -> tuple[list[float], list[float]]:
    """Body line + tip sidebands + blade harmonics (+ optional hub lines)."""
    lines = [body_doppler_hz]
    powers = [1.0]
    if tip_speed_m_s <= 0.0 or rotor_rcs_fraction <= 0.0 or wavelength_m <= 0.0:
        return lines, powers
    blades = int(blade_count)
    if blades < 2:
        blades = 2
    if blades > 8:
        blades = 8
    aspect = rotor_disk_aspect_scale(los_elevation_deg)
    body_frac = max(0.05, 1.0 - rotor_rcs_fraction)
    powers[0] = body_frac
    tip_fd = doppler_hz(tip_speed_m_s, wavelength_m)
    tip_share = 0.55
    harm_share = 0.30
    hub_share = 0.15
    tip_p = rotor_rcs_fraction * tip_share * 0.5 * aspect
    lines.extend([body_doppler_hz + tip_fd, body_doppler_hz - tip_fd])
    powers.extend([tip_p, tip_p])

    max_harm = min(3, blades // 2)
    harm_candidates = []
    for h in range(1, max_harm + 1):
        if h >= blades:
            break
        harm_ms = tip_speed_m_s * (h / blades)
        if harm_ms < tip_speed_m_s * 0.12:
            continue
        if harm_ms > tip_speed_m_s * 0.92:
            continue
        harm_candidates.append(harm_ms)
    harm_n = max(1, len(harm_candidates))
    harm_each = rotor_rcs_fraction * harm_share * aspect / (harm_n * 2.0)
    for harm_ms in harm_candidates:
        harm_fd = doppler_hz(harm_ms, wavelength_m)
        lines.extend([body_doppler_hz + harm_fd, body_doppler_hz - harm_fd])
        powers.extend([harm_each, harm_each])

    if hub_width_m_s > 0.0:
        hub_fd = doppler_hz(hub_width_m_s, wavelength_m)
        hub_p = rotor_rcs_fraction * hub_share * 0.5 * aspect
        lines.extend([body_doppler_hz + hub_fd, body_doppler_hz - hub_fd])
        powers.extend([hub_p, hub_p])
    return lines, powers


def mtd_clutter_bin_gain(
    bin_index: int,
    bin_count: int,
    mti_clutter_floor: float,
    mtd_clutter_leakage: float,
) -> float:
    if bin_count < 2 or bin_index == 0:
        return max(1.0e-6, float(mti_clutter_floor))
    return max(1.0e-9, float(mtd_clutter_leakage))


def mti_apply_clutter(
    hardware: RadarHardware,
    clutter_power_w: float,
    clutter_radial_m_s: float = 0.0,
    target_doppler_bin: int = 0,
) -> float:
    if not hardware.enable_mti:
        return clutter_power_w
    if hardware.mti_mode == "mtd_bank":
        gain = mtd_clutter_bin_gain(
            target_doppler_bin,
            hardware.doppler_bin_count,
            hardware.mti_clutter_floor,
            hardware.mtd_clutter_leakage,
        )
        return clutter_power_w * gain
    fd = doppler_hz(clutter_radial_m_s, hardware.wavelength_m)
    gain = mti_two_pulse_gain(fd, hardware.active_prf_hz())
    if gain < hardware.mti_clutter_floor:
        gain = hardware.mti_clutter_floor
    return clutter_power_w * gain


def mti_apply_target(
    hardware: RadarHardware,
    target_power_w: float,
    radial_speed_m_s: float,
    tip_speed_m_s: float = 0.0,
    rotor_rcs_fraction: float = 0.0,
    hub_width_m_s: float = 0.0,
    blade_count: int = 2,
    los_elevation_deg: float = 0.0,
) -> float:
    if not hardware.enable_mti:
        return target_power_w
    prf = hardware.active_prf_hz()
    fd = doppler_hz(radial_speed_m_s, hardware.wavelength_m)
    mode = (hardware.mti_mode or "twopulse").lower()
    if mode == "mtd_bank" or "mtd" in mode:
        lines, powers = rotor_doppler_spectrum(
            fd,
            hardware.wavelength_m,
            tip_speed_m_s=tip_speed_m_s,
            rotor_rcs_fraction=rotor_rcs_fraction,
            hub_width_m_s=hub_width_m_s,
            blade_count=blade_count,
            los_elevation_deg=los_elevation_deg,
        )
        gain, _bin, _peak = max_mtd_spectrum_gain(
            lines,
            powers,
            prf,
            hardware.doppler_bin_count,
            hardware.mti_clutter_floor,
            hardware.mtd_clutter_leakage,
        )
    else:
        lines, powers = rotor_doppler_spectrum(
            fd,
            hardware.wavelength_m,
            tip_speed_m_s=tip_speed_m_s,
            rotor_rcs_fraction=rotor_rcs_fraction,
            hub_width_m_s=hub_width_m_s,
            blade_count=blade_count,
            los_elevation_deg=los_elevation_deg,
        )
        prf_list = [prf]
        if hardware.mti_stagger_deblind and hardware.prf_set_hz:
            prf_list = [float(p) for p in hardware.prf_set_hz if float(p) >= 1.0]
            if not prf_list:
                prf_list = [prf]
        cancel_mode = "three_pulse" if "three" in mode else "twopulse"
        gain, _peak = max_mti_canceller_spectrum_gain(
            lines, powers, prf_list, cancel_mode
        )
    if gain < 1.0e-6:
        gain = 1.0e-6
    return target_power_w * gain


def ca_cfar_detections(
    power_w: np.ndarray,
    noise_w: float,
    guard_cells: int = 2,
    training_cells: int = 8,
    pfa: float = 1.0e-6,
) -> np.ndarray:
    """CA-CFAR along range for each azimuth. Returns bool mask [naz, nbin].

    Threshold scale for Gaussian approx: α = N * (Pfa^(-1/N) - 1).
    Also floors against thermal noise so empty bins don't false-alarm.
    """
    naz, nbin = power_w.shape
    det = np.zeros((naz, nbin), dtype=bool)
    n_train = training_cells
    if n_train < 2:
        n_train = 2
    alpha = float(n_train) * (pfa ** (-1.0 / float(n_train)) - 1.0)
    if alpha < 1.0:
        alpha = 1.0

    half = n_train // 2
    for az_i in range(naz):
        row = power_w[az_i]
        for bin_i in range(nbin):
            left0 = bin_i - guard_cells - half
            left1 = bin_i - guard_cells
            right0 = bin_i + guard_cells + 1
            right1 = bin_i + guard_cells + 1 + half
            cells = []
            if left1 > 0:
                a0 = max(left0, 0)
                a1 = max(left1, 0)
                if a1 > a0:
                    cells.append(row[a0:a1])
            if right0 < nbin:
                b0 = min(right0, nbin)
                b1 = min(right1, nbin)
                if b1 > b0:
                    cells.append(row[b0:b1])
            if not cells:
                local = noise_w
            else:
                train = np.concatenate(cells)
                if train.size == 0:
                    local = noise_w
                else:
                    local = float(np.mean(train))
            if local < noise_w:
                local = noise_w
            threshold = alpha * local
            if row[bin_i] > threshold:
                det[az_i, bin_i] = True
    return det


def summarize_hardware(hardware: RadarHardware) -> list[str]:
    lines = [
        f"{hardware.display_name} [{hardware.name}]",
        f"  f={hardware.frequency_hz/1e9:.3f} GHz  λ={hardware.wavelength_m*100:.2f} cm  "
        f"band={hardware.band}",
        f"  Pt={hardware.peak_power_w/1e3:.0f} kW  G={hardware.antenna_gain_dbi:.0f} dBi  "
        f"Lsys={hardware.system_loss_db:.1f} dB  NF={hardware.noise_figure_db:.1f} dB",
        f"  τ={hardware.pulse_width_s*1e6:.2f} us  B="
        f"{hardware.effective_bandwidth_hz/1e6:.2f} MHz  "
        f"Gpc={lin_to_db(hardware.pulse_compression_gain):.1f} dB",
        f"  PRF={hardware.prf_hz:.0f} Hz  Nint={hardware.pulses_integrated}  "
        f"Gint={lin_to_db(hardware.integration_gain):.1f} dB  "
        f"MTI={'on' if hardware.enable_mti else 'off'}",
        f"  Runamb={hardware.unambiguous_range_m/1000:.1f} km  "
        f"Vunamb={hardware.unambiguous_velocity_m_s:.0f} m/s",
        f"  scan={hardware.scan_rpm:.1f} rpm ({hardware.scan_period_s:.1f}s)  "
        f"beams={','.join(beam.name for beam in hardware.resolved_elevation_beams())}  "
        f"SLL={hardware.sidelobe_level_db:.0f} dB",
        f"  Rinstr={hardware.instrumented_range_m/1000:.0f} km  "
        f"dR={hardware.range_bin_m():.0f} m  "
        f"atm={hardware.atm_loss_db_per_km_one_way:.3f} dB/km 1-way",
    ]
    if hardware.notes:
        lines.append(f"  note: {hardware.notes}")
    return lines
