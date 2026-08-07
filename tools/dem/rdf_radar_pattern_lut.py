#!/usr/bin/env python3
"""Segmented antenna pattern LUT (mainlobe + sidelobe peaks + floor).

Engineering one-way power pattern for bake-back into Enforce
``RDF_RadarPatternLut``. Not a measured aperture transform.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from rdf_radar_physics import db_to_lin, gaussian_beam_gain

SCHEMA = "RDF_PATTERN_LUT_V1"

# Peak centers as multiples of HPBW; peak levels in dB (one-way).
DEFAULT_SIDELOBE_PEAKS: tuple[tuple[float, float], ...] = (
    (2.2, -18.0),
    (3.8, -22.0),
    (5.5, -25.0),
)


def segmented_one_way_gain(
    offset_deg: float,
    hpbw_deg: float,
    sidelobe_level_db: float = -25.0,
    peaks: tuple[tuple[float, float], ...] = DEFAULT_SIDELOBE_PEAKS,
) -> float:
    """One-way linear gain in [floor, 1]. Symmetric in |offset|."""
    if hpbw_deg <= 1e-6:
        return 1.0
    abs_off = abs(float(offset_deg))
    gain = gaussian_beam_gain(abs_off, hpbw_deg)
    floor = db_to_lin(sidelobe_level_db)
    if floor < 0.0:
        floor = 0.0
    # Wide enough vs 0.5°–1° LUT grids for stable linear interpolation.
    lobe_width = max(1.25 * hpbw_deg, 1.0)
    for mult, peak_db in peaks:
        center = float(mult) * hpbw_deg
        peak_lin = db_to_lin(peak_db)
        if peak_lin < floor:
            peak_lin = floor
        # Soft lobe around ±center (use abs_off).
        x = (abs_off - center) / lobe_width
        lobe = peak_lin * math.exp(-0.693147 * (x * x))
        if lobe > gain:
            gain = lobe
    if gain < floor:
        gain = floor
    if gain > 1.0:
        gain = 1.0
    return float(gain)


def build_pattern_lut(
    hpbw_deg: float = 2.5,
    sidelobe_level_db: float = -25.0,
    offset_min_deg: float = -180.0,
    offset_max_deg: float = 180.0,
    n_samples: int = 721,
    peaks: tuple[tuple[float, float], ...] = DEFAULT_SIDELOBE_PEAKS,
) -> dict:
    if n_samples < 5:
        raise ValueError("n_samples must be >= 5")
    if offset_max_deg <= offset_min_deg:
        raise ValueError("offset_max must be > offset_min")
    offsets = np.linspace(offset_min_deg, offset_max_deg, n_samples, dtype=np.float64)
    factors = np.array(
        [
            segmented_one_way_gain(float(o), hpbw_deg, sidelobe_level_db, peaks)
            for o in offsets
        ],
        dtype=np.float64,
    )
    return {
        "schema": SCHEMA,
        "hpbw_deg": float(hpbw_deg),
        "sidelobe_level_db": float(sidelobe_level_db),
        "offset_min_deg": float(offset_min_deg),
        "offset_max_deg": float(offset_max_deg),
        "n": int(n_samples),
        "offset_deg": offsets,
        "one_way": factors,
        "peaks": [{"hpbw_mult": float(a), "level_db": float(b)} for a, b in peaks],
    }


def interp_pattern_lut(lut: dict, offset_deg: float) -> float:
    offs = lut["offset_deg"]
    vals = lut["one_way"]
    if offset_deg <= float(offs[0]):
        return float(vals[0])
    if offset_deg >= float(offs[-1]):
        return float(vals[-1])
    return float(np.interp(offset_deg, offs, vals))


def two_way_from_one_way(one_way: float) -> float:
    if one_way < 0.0:
        one_way = 0.0
    return one_way * one_way


def lut_to_enforce_json(lut: dict) -> dict:
    return {
        "schema": SCHEMA,
        "offset_min_deg": float(lut["offset_min_deg"]),
        "offset_max_deg": float(lut["offset_max_deg"]),
        "factors": [float(x) for x in lut["one_way"]],
        "hpbw_deg": float(lut["hpbw_deg"]),
        "sidelobe_level_db": float(lut["sidelobe_level_db"]),
    }


def validate_pattern_lut(
    lut: dict,
    n_probe: int = 300,
    seed: int = 11,
    max_abs_err: float = 0.03,
) -> dict:
    rng = np.random.default_rng(seed)
    lo = float(lut["offset_min_deg"])
    hi = float(lut["offset_max_deg"])
    probes = rng.uniform(lo, hi, size=n_probe)
    abs_errs = []
    for off in probes:
        truth = segmented_one_way_gain(
            float(off),
            float(lut["hpbw_deg"]),
            float(lut["sidelobe_level_db"]),
        )
        approx = interp_pattern_lut(lut, float(off))
        abs_errs.append(abs(approx - truth))
    worst = float(max(abs_errs))
    mean = float(sum(abs_errs) / len(abs_errs))
    return {
        "ok": worst <= max_abs_err,
        "n_probe": n_probe,
        "worst_abs_err": worst,
        "mean_abs_err": mean,
        "max_abs_err": max_abs_err,
    }


@dataclass
class PatternLutCaseResult:
    name: str
    ok: bool
    metrics: dict


def run_pattern_lut_validation(
    hpbw_deg: float = 2.5,
    sidelobe_level_db: float = -25.0,
) -> dict:
    lut = build_pattern_lut(hpbw_deg=hpbw_deg, sidelobe_level_db=sidelobe_level_db)
    check = validate_pattern_lut(lut)
    # Boresight must stay ~1; far axis must sit on floor.
    bore = interp_pattern_lut(lut, 0.0)
    far = interp_pattern_lut(lut, 90.0)
    floor = db_to_lin(sidelobe_level_db)
    shape_ok = bore > 0.98 and abs(far - floor) < 1e-3
    return {
        "all_ok": bool(check["ok"] and shape_ok),
        "interp_validation": check,
        "boresight_one_way": bore,
        "far_one_way": far,
        "floor_one_way": floor,
        "shape_ok": shape_ok,
        "table": lut_to_enforce_json(lut),
        "full": lut,
    }
