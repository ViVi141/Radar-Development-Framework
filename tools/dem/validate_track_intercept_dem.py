#!/usr/bin/env python3
"""CIWS TRACK precision with real DEM + RDF rain/clutter/MTI.

Loads GM_Eden ttile height (+ SURF if present), places radar on a high site,
samples DEM ground clutter along the beam LOS (σ⁰ · projected cell), then:

  SINR = Pr_tgt / (N_thermal + P_clutter · mti_leak)
  σ  from RDF CRLB (same as Enforce measurement)

Compares CLEAR vs RAIN25 + DEM clutter for heli/shell at 1–3.5 km.

  python validate_track_intercept_dem.py
  python validate_track_intercept_dem.py --world GM_Eden --rain 25 --trials 80
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from dataclasses import dataclass

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_dem_io import choose_radar_site, resolve_dem_source
from rdf_radar_materials import Sigma0Table, attenuation_scale_for_band, sigma0_linear
from rdf_radar_systems import rain_one_way_db_per_km

# ATR_CiwsRadarConstants TRACK
FREQ_HZ = 16.0e9
PEAK_W = 20000.0
GAIN_DBI = 39.0
AZ_BW_DEG = 1.5
EL_BW_DEG = 1.8
LOSS_DB = 6.0
NF_DB = 4.0
BW_HZ = 8.0e6
PRF_HZ = 6000.0
PULSES = 32
DETECT_SNR_DB = 8.0
CRLB_K = 1.6
C_LIGHT = 299792458.0
K_B = 1.380649e-23
T0 = 290.0
MTI_FLOOR = 1.0e-4
MTD_LEAK = 1.0e-6
RADAR_AGL_M = 2.5


@dataclass
class Case:
    name: str
    optical_rcs: float
    length_m: float
    range_m: float
    agl_m: float
    speed_ms: float


def db_to_lin(db: float) -> float:
    return 10.0 ** (db / 10.0)


def lin_to_db(x: float) -> float:
    if x <= 1e-30:
        return -300.0
    return 10.0 * math.log10(x)


def atm_db_km(f_hz: float) -> float:
    f = f_hz / 1e9
    if f < 12.0:
        return 0.020
    if f < 18.0:
        return 0.045
    return 0.090


def scale_rcs(optical: float, length_m: float, lam: float) -> float:
    a = max(length_m * 0.5, 0.01)
    ka = 2.0 * math.pi * a / lam
    if ka >= 10.0:
        return optical
    r = optical * (ka / 10.0) ** 4
    return max(r, 1e-8)


def range_bin_m() -> float:
    return C_LIGHT / (2.0 * BW_HZ)


def thermal_noise_w() -> float:
    return K_B * T0 * BW_HZ * db_to_lin(NF_DB)


def path_loss_lin(range_m: float, rain_mm_h: float, foliage_db: float) -> float:
    atm = atm_db_km(FREQ_HZ)
    rain = rain_one_way_db_per_km(rain_mm_h, FREQ_HZ)
    loss_db = 2.0 * (range_m / 1000.0) * (atm + rain) + foliage_db
    return db_to_lin(loss_db)


def received_power_w(sigma_m2: float, range_m: float, path_lin: float) -> float:
    if range_m < 1.0:
        range_m = 1.0
    lam = C_LIGHT / FREQ_HZ
    g = db_to_lin(GAIN_DBI)
    four_pi = 4.0 * math.pi
    denom = (four_pi**3) * db_to_lin(LOSS_DB)
    const = PEAK_W * g * g * (lam**2) / denom
    pr = const * sigma_m2 / (range_m**4) / path_lin
    return pr * float(PULSES)


def mti_leak_for_speed(speed_ms: float) -> float:
    # Fast RAM / heli body: MTD leakage; slow / hover near floor.
    if speed_ms >= 40.0:
        return MTD_LEAK
    if speed_ms >= 10.0:
        return math.sqrt(MTD_LEAK * MTI_FLOOR)
    return MTI_FLOOR


def sample_dem_clutter_sigma(
    dem: dict,
    radar_ix: int,
    radar_iz: int,
    radar_y: float,
    az_rad: float,
    target_range_m: float,
    table: Sigma0Table,
) -> dict:
    """Accumulate ground clutter RCS in the range gate around the target (RDF-like)."""
    terrain = dem["terrain"]
    slope = dem["slope"]
    surface = dem["surface"]
    cell_m = float(dem["cell_m"])
    height, width = terrain.shape
    bin_m = range_bin_m()
    gate_lo = max(target_range_m - 0.5 * bin_m, cell_m)
    gate_hi = target_range_m + 0.5 * bin_m

    dx = math.cos(az_rad)
    dz = math.sin(az_rad)
    max_steps = int(gate_hi / cell_m) + 2
    clutter_sigma = 0.0
    cells = 0
    classes: dict[int, int] = {}
    max_elev = -1e9
    blocked = False

    for step in range(1, max_steps + 1):
        range_m = step * cell_m
        if range_m > gate_hi + cell_m:
            break
        ix = int(round(radar_ix + dx * step))
        iz = int(round(radar_iz + dz * step))
        if ix < 0 or ix >= width or iz < 0 or iz >= height:
            break
        ty = float(terrain[iz, ix])
        if not np.isfinite(ty):
            continue
        elev = (ty - radar_y) / range_m
        visible = elev >= max_elev - 1e-4
        if elev > max_elev:
            max_elev = elev
        if not visible:
            if range_m < target_range_m - 5.0:
                blocked = True
            continue
        if range_m < gate_lo or range_m > gate_hi:
            continue

        # Grazing from geometry (sector_sim style).
        slope_deg = float(slope[iz, ix])
        look_down = math.asin(max(min((radar_y - ty) / range_m, 1.0), -1.0))
        grazing = look_down - math.radians(slope_deg) * 0.25
        if grazing < math.radians(0.5):
            grazing = math.radians(0.5)
        surf = int(surface[iz, ix])
        s0 = sigma0_linear(surf, grazing, table)
        projected = (cell_m * cell_m) * max(math.sin(grazing), 0.02)
        clutter_sigma += s0 * projected
        cells += 1
        classes[surf] = classes.get(surf, 0) + 1

    return {
        "clutter_sigma_m2": clutter_sigma,
        "cells": cells,
        "classes": classes,
        "los_blocked_early": blocked,
    }


def foliage_two_way_db(surface_hist: dict[int, int], band: str = "Ku") -> float:
    # SURF_VEGETATION = 2. Authoring 0.5 dB/km one-way * scale * path through veg cells.
    veg = int(surface_hist.get(2, 0))
    if veg <= 0:
        return 0.0
    # Rough: each veg cell ~ cell contributes small path; cap.
    path_km = min(veg * 0.004, 0.6)
    return 0.5 * attenuation_scale_for_band(band) * path_km * 2.0


def meas_from_sinr(sinr_db: float, range_m: float) -> dict:
    snr_lin = max(db_to_lin(sinr_db), 1.0)
    denom = CRLB_K * math.sqrt(2.0 * snr_lin)
    if denom < 0.001:
        denom = 0.001
    sig_r = range_bin_m() / denom
    sig_az = AZ_BW_DEG / denom
    sig_el = EL_BW_DEG / denom
    cross = range_m * math.radians(sig_az)
    elev = range_m * math.radians(sig_el)
    pos1s = math.sqrt(sig_r * sig_r + cross * cross + elev * elev)
    return {
        "sig_range_m": round(sig_r, 3),
        "sig_az_deg": round(sig_az, 4),
        "sig_pos_1s_m": round(pos1s, 3),
        "detect": sinr_db >= DETECT_SNR_DB,
    }


def evaluate_case(
    case: Case,
    dem: dict,
    radar_ix: int,
    radar_iz: int,
    radar_y: float,
    az_rad: float,
    table: Sigma0Table,
    rain_mm_h: float,
    use_dem_clutter: bool,
) -> dict:
    lam = C_LIGHT / FREQ_HZ
    rcs = scale_rcs(case.optical_rcs, case.length_m, lam)
    clutter = {
        "clutter_sigma_m2": 0.0,
        "cells": 0,
        "classes": {},
        "los_blocked_early": False,
    }
    if use_dem_clutter:
        clutter = sample_dem_clutter_sigma(
            dem, radar_ix, radar_iz, radar_y, az_rad, case.range_m, table
        )
    foliage_db = foliage_two_way_db(clutter["classes"])
    path_lin = path_loss_lin(case.range_m, rain_mm_h, foliage_db)
    pr_t = received_power_w(rcs, case.range_m, path_lin)
    pr_c = 0.0
    if use_dem_clutter and clutter["clutter_sigma_m2"] > 0.0:
        pr_c = received_power_w(clutter["clutter_sigma_m2"], case.range_m, path_lin)
    leak = mti_leak_for_speed(case.speed_ms)
    noise = thermal_noise_w() + pr_c * leak
    sinr = lin_to_db(pr_t / max(noise, 1e-30))
    snr_thermal = lin_to_db(pr_t / max(thermal_noise_w(), 1e-30))
    meas = meas_from_sinr(sinr, case.range_m)
    return {
        "case": case.name,
        "range_m": case.range_m,
        "agl_m": case.agl_m,
        "rain_mm_h": rain_mm_h,
        "dem_clutter": use_dem_clutter,
        "rcs_m2": float(f"{rcs:.4e}"),
        "clutter_sigma_m2": float(f"{clutter['clutter_sigma_m2']:.4e}"),
        "clutter_cells": clutter["cells"],
        "surf_classes": clutter["classes"],
        "foliage_db": round(foliage_db, 3),
        "mti_leak": leak,
        "snr_thermal_db": round(snr_thermal, 1),
        "sinr_db": round(sinr, 1),
        "los_blocked_early": clutter["los_blocked_early"],
        **meas,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--world", default="GM_Eden")
    ap.add_argument("--rain", type=float, default=25.0, help="Rain mm/h for harsh case")
    ap.add_argument("--az-deg", type=float, default=45.0, help="Look azimuth (deg)")
    args = ap.parse_args()

    print("Loading DEM…")
    dem = resolve_dem_source(world=args.world, prefer_ttile=True, merge_surf=True)
    terrain = dem["terrain"]
    cell_m = float(dem["cell_m"])
    margin = max(64, int(4000.0 / cell_m))
    radar_iz, radar_ix = choose_radar_site(terrain, margin=margin)
    radar_y = float(terrain[radar_iz, radar_ix]) + RADAR_AGL_M
    table = Sigma0Table.builtin("Ku", 3)
    az_rad = math.radians(args.az_deg)

    print(
        f"DEM source={dem.get('source')} cell={cell_m}m "
        f"site=({radar_ix},{radar_iz}) y={radar_y:.1f} az={args.az_deg:.0f}deg"
    )
    print(
        f"TRACK Ku P={PEAK_W/1000:.0f}kW G={GAIN_DBI:.0f}dBi azBW={AZ_BW_DEG}deg "
        f"rain_case={args.rain}mm/h"
    )

    cases = [
        Case("heli_1k", 5.0, 8.0, 1000.0, 80.0, 60.0),
        Case("heli_2k", 5.0, 8.0, 2000.0, 120.0, 70.0),
        Case("shell_1k", 0.01, 0.15, 1000.0, 200.0, 280.0),
        Case("shell_2k", 0.01, 0.15, 2000.0, 350.0, 300.0),
        Case("shell_3k5", 0.01, 0.15, 3500.0, 400.0, 320.0),
        # C-RAM terminal: low AGL → stronger ground clutter coupling.
        Case("shell_low_1k", 0.01, 0.15, 1000.0, 40.0, 250.0),
        Case("shell_low_2k", 0.01, 0.15, 2000.0, 60.0, 280.0),
    ]

    print(
        f"\n{'case':<10} {'mode':<14} {'SINR':>6} {'pos1s':>7} "
        f"{'clutσ':>9} {'det':>4}"
    )
    rows = []
    for case in cases:
        clear = evaluate_case(
            case, dem, radar_ix, radar_iz, radar_y, az_rad, table, 0.0, False
        )
        harsh = evaluate_case(
            case,
            dem,
            radar_ix,
            radar_iz,
            radar_y,
            az_rad,
            table,
            args.rain,
            True,
        )
        rows.append({"clear": clear, "harsh": harsh})
        print(
            f"{case.name:<10} {'clear':<14} {clear['sinr_db']:6.1f} "
            f"{clear['sig_pos_1s_m']:7.3f} {clear['clutter_sigma_m2']:>9} "
            f"{'Y' if clear['detect'] else 'n':>4}"
        )
        print(
            f"{'':<10} {'rain+DEM':<14} {harsh['sinr_db']:6.1f} "
            f"{harsh['sig_pos_1s_m']:7.3f} {harsh['clutter_sigma_m2']:>9} "
            f"{'Y' if harsh['detect'] else 'n':>4}"
        )

    print("\n--- Verdict ---")
    for pair in rows:
        c = pair["clear"]
        h = pair["harsh"]
        ratio = h["sig_pos_1s_m"] / max(c["sig_pos_1s_m"], 1e-9)
        print(
            f"{c['case']}: clear pos1s={c['sig_pos_1s_m']:.3f}m → "
            f"rain+DEM {h['sig_pos_1s_m']:.3f}m ({ratio:.1f}x), "
            f"SINR {c['sinr_db']:.0f}→{h['sinr_db']:.0f} dB, "
            f"clutter_cells={h['clutter_cells']} classes={h['surf_classes']}"
        )

    out = {
        "dem_source": dem.get("source"),
        "radar_ix": radar_ix,
        "radar_iz": radar_iz,
        "radar_y": radar_y,
        "cell_m": cell_m,
        "az_deg": args.az_deg,
        "rain_mm_h": args.rain,
        "rows": rows,
    }
    print("\nJSON_BEGIN")
    print(json.dumps(out, indent=2))
    print("JSON_END")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # noqa: BLE001
        print("ERROR:", exc, file=sys.stderr)
        raise
