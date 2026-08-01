#!/usr/bin/env python3
"""Offline clutter Doppler PSD + multi-PRF ambiguity calibration.

Writes JSON under tools/dem/calib/ for manual bake-back into RDF_RadarHardware
fields. Does NOT drive in-game detection at runtime.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_radar_physics import (  # noqa: E402
    C_LIGHT_M_S,
    RadarHardware,
    get_preset,
    mti_apply_target,
)


SCHEMA = "RDF_HW_CALIB_V1"


def gaussian_clutter_psd(
    fd_hz: np.ndarray,
    sigma_vr_m_s: float,
    wavelength_m: float,
) -> np.ndarray:
    """Unit-peak Gaussian clutter PSD vs Doppler (Hz)."""
    if wavelength_m <= 0.0:
        raise ValueError("wavelength_m must be > 0")
    sigma_fd = max(1e-6, 2.0 * sigma_vr_m_s / wavelength_m)
    psd = np.exp(-0.5 * (fd_hz / sigma_fd) ** 2)
    peak = float(np.max(psd))
    if peak > 0.0:
        psd = psd / peak
    return psd


def suggest_mtd_leakage(psd: np.ndarray, bin_centers_norm: np.ndarray) -> float:
    """Estimate non-zero-bin leakage from PSD mass away from dc."""
    if psd.size == 0:
        return 1.0e-6
    dc_mask = np.abs(bin_centers_norm) < 0.08
    if np.any(dc_mask):
        dc_power = float(np.sum(psd[dc_mask]))
    else:
        dc_power = float(psd[0])
    if np.any(~dc_mask):
        side_power = float(np.sum(psd[~dc_mask]))
    else:
        side_power = 0.0
    total = dc_power + side_power
    if total <= 1e-30:
        return 1.0e-6
    leakage = side_power / total
    return float(max(1.0e-9, min(1.0e-2, leakage)))


def prf_coverage_score(
    hardware: RadarHardware,
    speed_grid_m_s: np.ndarray,
) -> dict[str, Any]:
    """Fraction of radial speeds surviving MTI/MTD above a small floor."""
    scores = []
    blind = []
    prf_list = hardware.prf_set_hz
    if not prf_list:
        prf_list = [hardware.prf_hz]

    for prf in prf_list:
        hw = RadarHardware(**{**hardware.__dict__})
        hw.prf_hz = float(prf)
        hw.prf_set_hz = [float(prf)]
        ok = 0
        for vr in speed_grid_m_s:
            out_w = mti_apply_target(hw, 1.0, float(vr))
            if out_w >= 0.05:
                ok += 1
        frac = ok / max(1, speed_grid_m_s.size)
        scores.append({"prf_hz": float(prf), "detect_frac": frac})
        blind.append(0.5 * hardware.wavelength_m * float(prf))

    mean_frac = float(np.mean([s["detect_frac"] for s in scores]))
    return {
        "per_prf": scores,
        "blind_speeds_m_s": blind,
        "unamb_range_m": [0.5 * C_LIGHT_M_S / float(p) for p in prf_list],
        "coverage_score": mean_frac,
    }


def build_calib(
    hardware: RadarHardware,
    preset: str,
    sigma_vr_m_s: float,
    fd_samples: int,
) -> dict[str, Any]:
    wavelength = hardware.wavelength_m
    prf = float(hardware.prf_hz)
    fd_max = 0.5 * prf
    fd = np.linspace(-fd_max, fd_max, fd_samples)
    psd = gaussian_clutter_psd(fd, sigma_vr_m_s, wavelength)
    fd_norm = fd / max(1e-9, prf)
    leakage = suggest_mtd_leakage(psd, fd_norm)

    speed_grid = np.linspace(
        -hardware.unambiguous_velocity_m_s,
        hardware.unambiguous_velocity_m_s,
        81,
    )
    ambiguity = prf_coverage_score(hardware, speed_grid)

    prf_set = list(hardware.prf_set_hz) if hardware.prf_set_hz else [hardware.prf_hz]
    stagger = 1.0
    if len(prf_set) >= 2 and prf_set[0] > 1.0:
        stagger = float(prf_set[1] / prf_set[0])

    mode = "two_pulse"
    if hardware.mti_mode == "mtd_bank":
        mode = "mtd_bank"

    return {
        "schema": SCHEMA,
        "preset": preset,
        "frequency_hz": hardware.frequency_hz,
        "prf_hz": hardware.prf_hz,
        "prf_set_hz": prf_set,
        "prf_stagger_ratio": stagger,
        "mti_clutter_floor": hardware.mti_clutter_floor,
        "mtd_clutter_leakage": leakage,
        "doppler_bin_count": hardware.doppler_bin_count,
        "mti_mode": mode,
        "clutter_psd": {
            "fd_hz": [float(x) for x in fd.tolist()],
            "fd_norm": [float(x) for x in fd_norm.tolist()],
            "power_rel": [float(x) for x in psd.tolist()],
            "model": "gaussian_spread",
            "sigma_vr_m_s": sigma_vr_m_s,
        },
        "ambiguity": ambiguity,
        "notes": "Offline bake-back only; does not drive in-game detects",
    }


def apply_calib_to_hardware(calib: dict[str, Any], hardware: RadarHardware) -> RadarHardware:
    """Copy suggested fields onto a RadarHardware (offline / tools only)."""
    hardware.prf_hz = float(calib.get("prf_hz", hardware.prf_hz))
    prf_set = calib.get("prf_set_hz")
    if isinstance(prf_set, list) and prf_set:
        hardware.prf_set_hz = [float(x) for x in prf_set]
    hardware.mti_clutter_floor = float(
        calib.get("mti_clutter_floor", hardware.mti_clutter_floor)
    )
    hardware.mtd_clutter_leakage = float(
        calib.get("mtd_clutter_leakage", hardware.mtd_clutter_leakage)
    )
    hardware.doppler_bin_count = int(
        calib.get("doppler_bin_count", hardware.doppler_bin_count)
    )
    mode = str(calib.get("mti_mode", "mtd_bank"))
    if mode == "mtd_bank":
        hardware.mti_mode = "mtd_bank"
    else:
        hardware.mti_mode = "twopulse"
    return hardware


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="shorad", help="RadarHardware preset name")
    parser.add_argument("--sigma-vr", type=float, default=0.5, help="Clutter radial spread m/s")
    parser.add_argument("--fd-samples", type=int, default=129)
    parser.add_argument(
        "--out",
        default="",
        help="Output JSON path (default tools/dem/calib/prf_clutter_<preset>.json)",
    )
    args = parser.parse_args()

    hardware = get_preset(args.preset)
    hardware.mti_mode = "mtd_bank"
    if not hardware.prf_set_hz:
        hardware.prf_set_hz = [hardware.prf_hz, hardware.prf_hz * 1.2]

    calib = build_calib(hardware, args.preset, args.sigma_vr, args.fd_samples)
    apply_calib_to_hardware(calib, hardware)

    out = args.out
    if not out:
        calib_dir = os.path.join(_HERE, "calib")
        os.makedirs(calib_dir, exist_ok=True)
        out = os.path.join(calib_dir, f"prf_clutter_{args.preset}.json")
    else:
        parent = os.path.dirname(os.path.abspath(out))
        if parent:
            os.makedirs(parent, exist_ok=True)

    with open(out, "w", encoding="utf-8") as f:
        json.dump(calib, f, indent=2)
        f.write("\n")
    print(f"wrote {out}")
    print(
        "suggested mtd_clutter_leakage="
        f"{calib['mtd_clutter_leakage']:.3e} "
        f"coverage={calib['ambiguity']['coverage_score']:.3f}"
    )


if __name__ == "__main__":
    main()
