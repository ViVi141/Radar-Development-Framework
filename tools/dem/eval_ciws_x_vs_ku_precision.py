#!/usr/bin/env python3
"""CIWS realism: Phalanx/Centurion-class RF + X vs Ku precision.

Open literature (Raytheon / NavWeaps brochure):
  - Ku search (digital MTI) + Ku track (PD monopulse)
  - Search antenna ~90 RPM (ATR slaves beam to turret idle yaw)
  - Prime power 18 kW search / 70 kW track (NOT RF peak)

Engineering RF fit (unclassified aperture scaling @ 16 GHz design):
  SEARCH: D~0.55 m → ~2.5° az, G~36 dBi, P_peak~8 kW
  TRACK:  D~0.75 m → ~1.5° az, G~39 dBi, P_peak~20 kW, B=8 MHz

X-band twin uses ScaleApertureToFrequency from the Ku design (same dish).

  python eval_ciws_x_vs_ku_precision.py
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass

from rdf_radar_channel import (
    band_for_frequency,
    scale_aperture_to_frequency,
    scale_rcs_for_wavelength,
)
from rdf_radar_physics import (
    C_LIGHT_M_S,
    K_BOLTZMANN,
    T0_K,
    RadarHardware,
    atmospheric_one_way_db_per_km,
    db_to_lin,
    lin_to_db,
)

DETECT_SNR_DB = 8.0
CRLB_K = 1.6
MEAS_NOISE_SCALE = 1.0
RANGES_M = [500.0, 1000.0, 2000.0, 3500.0, 5000.0]
KU_HZ = 16.0e9
X_HZ = 9.0e9


@dataclass(frozen=True)
class CiwsMode:
    name: str
    peak_w: float
    gain_dbi: float
    az_bw_deg: float
    el_bw_deg: float
    bandwidth_hz: float
    pulse_s: float
    prf_hz: float
    pulses: int
    nf_db: float
    loss_db: float
    note: str


# Matches ATR ApplyCiwsSearchHardware / ApplyCiwsTrackHardware.
SEARCH = CiwsMode(
    name="search",
    peak_w=8000.0,
    gain_dbi=36.0,
    az_bw_deg=2.5,
    el_bw_deg=10.0,
    bandwidth_hz=4.0e6,
    pulse_s=0.5e-6,
    prf_hz=4000.0,
    pulses=24,
    nf_db=4.5,
    loss_db=7.0,
    note="Ku search MTI; brochure 18 kW prime",
)
TRACK = CiwsMode(
    name="track",
    peak_w=20000.0,
    gain_dbi=39.0,
    az_bw_deg=1.5,
    el_bw_deg=1.8,
    bandwidth_hz=8.0e6,
    pulse_s=0.4e-6,
    prf_hz=6000.0,
    pulses=32,
    nf_db=4.0,
    loss_db=6.0,
    note="Ku track monopulse; brochure 70 kW prime",
)


def make_ku_hw(mode: CiwsMode) -> RadarHardware:
    return RadarHardware(
        name=f"ciws_ku_{mode.name}",
        display_name=f"CIWS Ku {mode.name}",
        era="modern",
        band="Ku",
        frequency_hz=KU_HZ,
        peak_power_w=mode.peak_w,
        antenna_gain_dbi=mode.gain_dbi,
        az_beamwidth_deg=mode.az_bw_deg,
        el_beamwidth_deg=mode.el_bw_deg,
        el_boresight_deg=0.0,
        scan_rpm=0.0,
        sidelobe_level_db=-26.0,
        system_loss_db=mode.loss_db,
        noise_figure_db=mode.nf_db,
        pulse_width_s=mode.pulse_s,
        chirp_bandwidth_hz=mode.bandwidth_hz,
        prf_hz=mode.prf_hz,
        pulses_integrated=mode.pulses,
        coherent_integration=True,
        polarization_factor=1.0,
        atm_loss_db_per_km_one_way=atmospheric_one_way_db_per_km(KU_HZ),
        instrumented_range_m=5000.0,
        preferred_range_bin_m=0.0,
        mti_clutter_floor=1.0e-4,
        enable_mti=True,
        enable_pulse_compression=True,
        notes=mode.note,
    )


def thermal_noise_w(hw: RadarHardware) -> float:
    bw = hw.effective_bandwidth_hz
    if bw < 1.0:
        bw = 1.0
    return K_BOLTZMANN * T0_K * bw * db_to_lin(hw.noise_figure_db)


def path_loss_lin(frequency_hz: float, range_m: float) -> float:
    atm = atmospheric_one_way_db_per_km(frequency_hz)
    return db_to_lin(2.0 * (range_m / 1000.0) * atm)


def snr_db_skin(hw: RadarHardware, sigma_m2: float, range_m: float) -> float:
    if range_m < 1.0:
        range_m = 1.0
    if sigma_m2 <= 0.0:
        return -300.0
    lam = C_LIGHT_M_S / hw.frequency_hz
    g = db_to_lin(hw.antenna_gain_dbi)
    four_pi = 4.0 * math.pi
    denom = (four_pi**3) * db_to_lin(hw.system_loss_db)
    const = hw.peak_power_w * g * g * (lam**2) / denom
    pr = const * sigma_m2 / (range_m**4) / path_loss_lin(hw.frequency_hz, range_m)
    if hw.coherent_integration and hw.pulses_integrated > 1:
        pr = pr * float(hw.pulses_integrated)
    else:
        pr = pr * math.sqrt(float(max(hw.pulses_integrated, 1)))
    n = thermal_noise_w(hw)
    if n < 1.0e-30:
        n = 1.0e-30
    return lin_to_db(pr / n)


def precision_from_snr(hw: RadarHardware, snr_db: float, range_m: float) -> dict:
    snr_lin = max(10.0 ** (snr_db / 10.0), 1.0)
    denom = CRLB_K * math.sqrt(2.0 * snr_lin)
    if denom < 0.001:
        denom = 0.001
    scale = MEAS_NOISE_SCALE
    d_r = hw.range_bin_m()
    sig_r = (d_r / denom) * scale
    sig_az = (hw.az_beamwidth_deg / denom) * scale
    sig_el = (hw.el_beamwidth_deg / denom) * scale
    cross_m = range_m * math.radians(sig_az)
    elev_m = range_m * math.radians(sig_el)
    pos_1sigma = math.sqrt(sig_r * sig_r + cross_m * cross_m + elev_m * elev_m)
    t_obs = float(hw.pulses_integrated) / max(hw.prf_hz, 1.0)
    lam = C_LIGHT_M_S / hw.frequency_hz
    sig_fd = (1.0 / (t_obs * denom)) * scale
    sig_vr = 0.5 * lam * sig_fd
    return {
        "sig_range_m": round(sig_r, 3),
        "sig_az_deg": round(sig_az, 4),
        "sig_el_deg": round(sig_el, 4),
        "sig_cross_m": round(cross_m, 2),
        "sig_elev_m": round(elev_m, 2),
        "sig_pos_1s_m": round(pos_1sigma, 2),
        "sig_vr_ms": round(sig_vr, 3),
        "range_bin_m": round(d_r, 2),
    }


TARGETS = [
    ("heli", 5.0, 8.0),
    ("shell", 0.01, 0.15),
]


def evaluate(hw: RadarHardware) -> dict:
    band = band_for_frequency(hw.frequency_hz)
    lam = C_LIGHT_M_S / hw.frequency_hz
    out = {
        "band": band,
        "f_ghz": round(hw.frequency_hz / 1e9, 2),
        "lambda_cm": round(lam * 100.0, 2),
        "peak_kw": hw.peak_power_w / 1000.0,
        "gain_dbi": hw.antenna_gain_dbi,
        "az_bw_deg": hw.az_beamwidth_deg,
        "el_bw_deg": hw.el_beamwidth_deg,
        "range_bin_m": round(hw.range_bin_m(), 2),
        "targets": {},
    }
    for name, optical, length in TARGETS:
        rcs = scale_rcs_for_wavelength(optical, length, lam)
        series = []
        for r in RANGES_M:
            snr = snr_db_skin(hw, rcs, r)
            det = snr >= DETECT_SNR_DB
            entry = {"range_m": r, "snr_db": round(snr, 1), "detect": det}
            if det:
                entry.update(precision_from_snr(hw, snr, r))
            series.append(entry)
        out["targets"][name] = {
            "rcs_eff_m2": float(f"{rcs:.6e}"),
            "series": series,
        }
    return out


def cell(row: dict, key: str, width: int = 7, prec: int = 2) -> str:
    if not row.get("detect"):
        return f"{'n/a':>{width}}"
    return f"{row[key]:{width}.{prec}f}"


def print_mode(mode: CiwsMode, ku: dict, x: dict) -> None:
    print(f"\n=== MODE {mode.name.upper()} — {mode.note} ===")
    print(
        f"Ku design: P={ku['peak_kw']:.1f} kW  G={ku['gain_dbi']:.1f} dBi  "
        f"az={ku['az_bw_deg']:.2f} deg  el={ku['el_bw_deg']:.1f} deg  dR={ku['range_bin_m']:.1f} m"
    )
    print(
        f"X scaled:  P={x['peak_kw']:.1f} kW  G={x['gain_dbi']:.1f} dBi  "
        f"az={x['az_bw_deg']:.2f} deg  el={x['el_bw_deg']:.1f} deg  dR={x['range_bin_m']:.1f} m"
    )
    print("(X = same physical aperture retuned from Ku via ScaleApertureToFrequency)\n")

    for tgt in ("heli", "shell"):
        print(f"-- {tgt} --")
        print(
            f"{'R_m':>6} {'SNR_Ku':>7} {'SNR_X':>7} "
            f"{'posKu':>7} {'posX':>7} "
            f"{'azKu':>7} {'azX':>7} "
            f"{'xKu':>7} {'xX':>7}"
        )
        for a, b in zip(ku["targets"][tgt]["series"], x["targets"][tgt]["series"]):
            print(
                f"{a['range_m']:6.0f} "
                f"{a['snr_db']:7.1f} {b['snr_db']:7.1f} "
                f"{cell(a, 'sig_pos_1s_m', 7, 2)} {cell(b, 'sig_pos_1s_m', 7, 2)} "
                f"{cell(a, 'sig_az_deg', 7, 4)} {cell(b, 'sig_az_deg', 7, 4)} "
                f"{cell(a, 'sig_cross_m', 7, 2)} {cell(b, 'sig_cross_m', 7, 2)}"
            )
        print()

    print("--- Headline @ 2000 m ---")
    for tgt in ("heli", "shell"):
        a = next(s for s in ku["targets"][tgt]["series"] if s["range_m"] == 2000.0)
        b = next(s for s in x["targets"][tgt]["series"] if s["range_m"] == 2000.0)
        if a.get("detect") and b.get("detect"):
            print(
                f"{tgt}: pos1s Ku={a['sig_pos_1s_m']:.2f} m  X={b['sig_pos_1s_m']:.2f} m  "
                f"(X/Ku={b['sig_pos_1s_m']/a['sig_pos_1s_m']:.2f}x)  "
                f"SNR Ku={a['snr_db']:.1f} X={b['snr_db']:.1f}  "
                f"az Ku={a['sig_az_deg']:.4f} X={b['sig_az_deg']:.4f} deg"
            )
        else:
            print(
                f"{tgt}: detect Ku={a['detect']} X={b['detect']} "
                f"SNR Ku={a['snr_db']:.1f} X={b['snr_db']:.1f}"
            )


def main() -> None:
    print("=== Phalanx/Centurion-class CIWS: realistic RF, X vs Ku ===")
    print("Detect threshold", DETECT_SNR_DB, "dB | CRLB meas model (RDF)")
    report = {}
    for mode in (SEARCH, TRACK):
        ku_hw = make_ku_hw(mode)
        x_hw = scale_aperture_to_frequency(ku_hw, X_HZ)
        # Keep peak power / waveform; only G and beam scale with aperture law.
        x_hw.peak_power_w = ku_hw.peak_power_w
        x_hw.chirp_bandwidth_hz = ku_hw.chirp_bandwidth_hz
        x_hw.pulse_width_s = ku_hw.pulse_width_s
        x_hw.prf_hz = ku_hw.prf_hz
        x_hw.pulses_integrated = ku_hw.pulses_integrated
        x_hw.noise_figure_db = ku_hw.noise_figure_db
        x_hw.system_loss_db = ku_hw.system_loss_db
        x_hw.coherent_integration = True
        x_hw.preferred_range_bin_m = 0.0
        # scale_aperture_to_frequency only retunes az HPBW; mirror for elevation.
        freq_ratio = X_HZ / KU_HZ
        if freq_ratio > 0.0:
            x_hw.el_beamwidth_deg = ku_hw.el_beamwidth_deg / freq_ratio
        ku = evaluate(ku_hw)
        x = evaluate(x_hw)
        print_mode(mode, ku, x)
        report[mode.name] = {"ku": ku, "x": x, "mode": mode.note}

    print("\nJSON_BEGIN")
    print(json.dumps(report, indent=2))
    print("JSON_END")


if __name__ == "__main__":
    main()
