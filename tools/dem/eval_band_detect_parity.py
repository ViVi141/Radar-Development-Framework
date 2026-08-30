#!/usr/bin/env python3
"""Offline: same scene, different IEEE bands → detection contrast by object type.

Fixes SHORAD peak power / gain / NF (only retunes carrier via hardware_at_frequency)
so differences come from band physics: λ², Rayleigh RCS, σ⁰ clutter, atm, rain,
foliage attenuation — matching Enforce SurfaceTable + ClutterModel.

  python eval_band_detect_parity.py
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass

from rdf_radar_channel import band_for_frequency, hardware_at_frequency, scale_rcs_for_wavelength
from rdf_radar_materials import (
    SURF_ASPHALT,
    SURF_VEGETATION,
    SURF_WATER,
    Sigma0Table,
    attenuation_scale_for_band,
    sigma0_ref_linear,
)
from rdf_radar_physics import (
    C_LIGHT_M_S,
    K_BOLTZMANN,
    T0_K,
    atmospheric_one_way_db_per_km,
    db_to_lin,
    get_preset,
    lin_to_db,
)
from rdf_radar_systems import rain_one_way_db_per_km

DETECT_SNR_DB = 8.0
RANGE_M = 2000.0
GRAZING_CELL_M2 = 800.0  # fixed resolution-cell area for relative SCR
RAIN_MM_H = 25.0
VEG_PATH_KM = 0.4  # one-way foliage path through canopy (authoring 0.5 dB/km @ X)


BAND_HZ = [
    ("VHF", 160e6),
    ("UHF", 0.6e9),
    ("L", 1.5e9),
    ("S", 3.0e9),
    ("C", 5.5e9),
    ("X", 9.0e9),
    ("Ku", 16.0e9),
    ("K", 24.0e9),
    ("Ka", 35.0e9),
]


@dataclass(frozen=True)
class Target:
    name: str
    optical_rcs_m2: float
    length_m: float


TARGETS = [
    Target("truck", 12.0, 5.0),
    Target("heli", 5.0, 8.0),
    Target("rocket", 0.05, 0.50),
    Target("shell", 0.01, 0.15),
]

SURFACES = [
    ("water", SURF_WATER),
    ("vegetation", SURF_VEGETATION),
    ("asphalt", SURF_ASPHALT),
]


def thermal_noise_w(hw) -> float:
    bw = hw.effective_bandwidth_hz
    if bw < 1.0:
        bw = 1.0
    return K_BOLTZMANN * T0_K * bw * db_to_lin(hw.noise_figure_db)


def radar_pr_w(hw, sigma_m2: float, range_m: float, path_loss_lin: float) -> float:
    if range_m < 1.0:
        range_m = 1.0
    if sigma_m2 <= 0.0:
        return 0.0
    lam = C_LIGHT_M_S / hw.frequency_hz
    g = db_to_lin(hw.antenna_gain_dbi)
    four_pi = 4.0 * math.pi
    denom = (four_pi**3) * db_to_lin(hw.system_loss_db)
    const = hw.peak_power_w * g * g * (lam**2) / denom
    return const * sigma_m2 / (range_m**4) / path_loss_lin


def snr_db(pr_w: float, noise_w: float) -> float:
    if noise_w < 1.0e-30:
        noise_w = 1.0e-30
    if pr_w <= 0.0:
        return -300.0
    return lin_to_db(pr_w / noise_w)


def path_loss_linear(frequency_hz: float, range_m: float, rain_mm_h: float, band: str) -> float:
    atm = atmospheric_one_way_db_per_km(frequency_hz)
    rain = rain_one_way_db_per_km(rain_mm_h, frequency_hz)
    loss_db = 2.0 * (range_m / 1000.0) * (atm + rain)
    veg_db = 0.5 * attenuation_scale_for_band(band) * VEG_PATH_KM * 2.0
    return db_to_lin(loss_db + veg_db)


def clutter_rcs_m2(band: str, surface_id: int) -> float:
    table = Sigma0Table.builtin(band, 3)
    s0 = sigma0_ref_linear(surface_id, table)
    return s0 * GRAZING_CELL_M2


def evaluate_band(label: str, frequency_hz: float, base_hw) -> dict:
    band = band_for_frequency(frequency_hz)
    hw = hardware_at_frequency(base_hw, frequency_hz)
    lam = C_LIGHT_M_S / frequency_hz
    noise = thermal_noise_w(hw)
    loss_clear = path_loss_linear(frequency_hz, RANGE_M, 0.0, band)
    loss_rain = path_loss_linear(frequency_hz, RANGE_M, RAIN_MM_H, band)

    row: dict = {
        "band": band,
        "label": label,
        "f_ghz": round(frequency_hz / 1e9, 3),
        "lambda_cm": round(lam * 100.0, 2),
        "atm_db_km": atmospheric_one_way_db_per_km(frequency_hz),
        "rain25_db_km": round(rain_one_way_db_per_km(RAIN_MM_H, frequency_hz), 4),
        "foliage_scale": attenuation_scale_for_band(band),
        "targets": {},
        "clutter_sigma0_db": {},
        "scr_db_vs_truck": {},
    }

    for surf_name, surf_id in SURFACES:
        table = Sigma0Table.builtin(band, 3)
        s0 = sigma0_ref_linear(surf_id, table)
        row["clutter_sigma0_db"][surf_name] = round(lin_to_db(s0), 1)

    for tgt in TARGETS:
        rcs = scale_rcs_for_wavelength(tgt.optical_rcs_m2, tgt.length_m, lam)
        pr_clear = radar_pr_w(hw, rcs, RANGE_M, loss_clear)
        pr_rain = radar_pr_w(hw, rcs, RANGE_M, loss_rain)
        snr_c = snr_db(pr_clear, noise)
        snr_r = snr_db(pr_rain, noise)

        scr = {}
        for surf_name, surf_id in SURFACES:
            c_rcs = clutter_rcs_m2(band, surf_id)
            # Same path loss cancels in SCR; use clear-air powers.
            pr_c = radar_pr_w(hw, c_rcs, RANGE_M, loss_clear)
            if pr_c <= 1.0e-40:
                scr[surf_name] = 300.0
            else:
                scr[surf_name] = round(lin_to_db(pr_clear / pr_c), 1)

        row["targets"][tgt.name] = {
            "rcs_eff_m2": float(f"{rcs:.6e}"),
            "rcs_vs_optical_db": round(lin_to_db(max(rcs, 1.0e-30) / tgt.optical_rcs_m2), 1),
            "snr_clear_db": round(snr_c, 1),
            "snr_rain25_db": round(snr_r, 1),
            "detect_clear": snr_c >= DETECT_SNR_DB,
            "detect_rain25": snr_r >= DETECT_SNR_DB,
            "scr_db": scr,
        }

    truck_scr = row["targets"]["truck"]["scr_db"]
    row["scr_db_vs_truck"] = truck_scr
    return row


def print_table(rows: list[dict]) -> None:
    print(f"\n=== Band detect parity @ {RANGE_M:.0f} m (fixed SHORAD P/G/NF, retune f only) ===\n")
    hdr = (
        f"{'Band':<5} {'fGHz':>6} {'λcm':>5} "
        f"{'shellRCS':>9} {'shellSNR':>8} {'truckSNR':>8} "
        f"{'SCR_w':>6} {'SCR_v':>6} {'SNRrain':>8} "
        f"{'atm':>5} {'rain':>6}"
    )
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        shell = r["targets"]["shell"]
        truck = r["targets"]["truck"]
        print(
            f"{r['band']:<5} {r['f_ghz']:>6.2f} {r['lambda_cm']:>5.1f} "
            f"{shell['rcs_eff_m2']:>9.2e} {shell['snr_clear_db']:>8.1f} {truck['snr_clear_db']:>8.1f} "
            f"{truck['scr_db']['water']:>6.1f} {truck['scr_db']['vegetation']:>6.1f} "
            f"{shell['snr_rain25_db']:>8.1f} "
            f"{r['atm_db_km']:>5.3f} {r['rain25_db_km']:>6.3f}"
        )

    print("\n--- Verdict (same scene) ---")
    vhf = next(r for r in rows if r["band"] == "VHF")
    ku = next(r for r in rows if r["band"] == "Ku")
    x = next(r for r in rows if r["band"] == "X")
    print(
        f"Shell RCS VHF/Ku: "
        f"{vhf['targets']['shell']['rcs_eff_m2']:.2e} / "
        f"{ku['targets']['shell']['rcs_eff_m2']:.2e} m2 "
        f"(Rayleigh at VHF)."
    )
    print(
        f"Truck SNR clear VHF→Ku: "
        f"{vhf['targets']['truck']['snr_clear_db']:.1f} → "
        f"{ku['targets']['truck']['snr_clear_db']:.1f} dB "
        f"(λ² + path loss; aperture fixed)."
    )
    print(
        f"Truck SCR water X/Ku: "
        f"{x['targets']['truck']['scr_db']['water']:.1f} / "
        f"{ku['targets']['truck']['scr_db']['water']:.1f} dB "
        f"(Ku water σ⁰ higher → worse SCR)."
    )
    print(
        f"Shell detect clear: "
        + ", ".join(
            f"{r['band']}={'Y' if r['targets']['shell']['detect_clear'] else 'n'}"
            for r in rows
        )
    )
    print(
        f"Shell detect rain25: "
        + ", ".join(
            f"{r['band']}={'Y' if r['targets']['shell']['detect_rain25'] else 'n'}"
            for r in rows
        )
    )


def main() -> None:
    base = get_preset("shorad")
    rows = [evaluate_band(label, hz, base) for label, hz in BAND_HZ]
    print_table(rows)
    out = {
        "range_m": RANGE_M,
        "detect_snr_db": DETECT_SNR_DB,
        "rain_mm_h": RAIN_MM_H,
        "cell_m2": GRAZING_CELL_M2,
        "note": "Fixed SHORAD P/G/NF; carrier retuned only.",
        "bands": rows,
    }
    print("\nJSON_SUMMARY_BEGIN")
    print(json.dumps(out, indent=2))
    print("JSON_SUMMARY_END")


if __name__ == "__main__":
    main()
