#!/usr/bin/env python3
"""Offline knife-edge LUT bake + interpolation validation.

Builds:
  1) 1D LUT: Fresnel ν → linear power factor (matches KnifeEdgeLinearFactor)
  2) Band×geometry LUT: (band λ, h_obs, range, u) → single-edge factor
  3) Dual-edge spot checks via the same λ grid

Purpose: bake ν / band×geometry LUTs for offline checks and Enforce bake-back.
Enforce loads the slim ν table via RDF_RadarKnifeEdgeLut
($profile:RDF/RadarData/KnifeEdgeLut.json).
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass

import numpy as np

from rdf_radar_diffraction import (
    dual_knife_edge_factor_from_geometry,
    knife_edge_factor_from_geometry,
    knife_edge_linear_factor,
)


@dataclass
class KnifeLutCaseResult:
    name: str
    ok: bool
    metrics: dict

# Common radar-band center wavelengths (engineering round numbers).
BAND_WAVELENGTH_M = {
    "VHF": 3.0,
    "L": 0.23,
    "S": 0.10,
    "C": 0.056,
    "X": 0.032,
    "Ku": 0.018,
}

SCHEMA = "RDF_KNIFE_LUT_V1"


def build_nu_lut(
    nu_min: float = -1.5,
    nu_max: float = 8.0,
    n_samples: int = 513,
) -> dict:
    """Uniform ν grid → analytic knife_edge_linear_factor."""
    if n_samples < 5:
        raise ValueError("n_samples must be >= 5")
    if nu_max <= nu_min:
        raise ValueError("nu_max must be > nu_min")
    nus = np.linspace(nu_min, nu_max, n_samples, dtype=np.float64)
    factors = np.array([knife_edge_linear_factor(float(v)) for v in nus], dtype=np.float64)
    return {
        "nu_min": float(nu_min),
        "nu_max": float(nu_max),
        "n": int(n_samples),
        "nu": nus,
        "factor": factors,
    }


def interp_nu_lut(lut: dict, nu: float) -> float:
    """Linear interpolate 1D ν LUT; clamp outside range."""
    nus = lut["nu"]
    factors = lut["factor"]
    if nu <= float(nus[0]):
        return float(factors[0])
    if nu >= float(nus[-1]):
        return float(factors[-1])
    # np.interp is fine for 1D.
    return float(np.interp(nu, nus, factors))


def validate_nu_lut(
    lut: dict,
    n_probe: int = 400,
    seed: int = 7,
    max_abs_err: float = 5.0e-3,
    max_rel_err: float = 0.03,
) -> dict:
    """Random ν probes: LUT vs analytic."""
    rng = np.random.default_rng(seed)
    lo = float(lut["nu_min"])
    hi = float(lut["nu_max"])
    probes = rng.uniform(lo, hi, size=n_probe)
    abs_errs = []
    rel_errs = []
    for nu in probes:
        truth = knife_edge_linear_factor(float(nu))
        approx = interp_nu_lut(lut, float(nu))
        abs_e = abs(approx - truth)
        if truth > 1e-6:
            rel_e = abs_e / truth
        else:
            rel_e = abs_e
        abs_errs.append(abs_e)
        rel_errs.append(rel_e)
    worst_abs = float(max(abs_errs))
    worst_rel = float(max(rel_errs))
    mean_abs = float(sum(abs_errs) / len(abs_errs))
    ok = worst_abs <= max_abs_err and worst_rel <= max_rel_err
    return {
        "ok": ok,
        "n_probe": n_probe,
        "worst_abs_err": worst_abs,
        "worst_rel_err": worst_rel,
        "mean_abs_err": mean_abs,
        "max_abs_err": max_abs_err,
        "max_rel_err": max_rel_err,
    }


@dataclass
class GeometryLutAxes:
    wavelengths_m: np.ndarray
    h_obs_m: np.ndarray
    ranges_m: np.ndarray
    u_edges: np.ndarray


def default_geometry_axes() -> GeometryLutAxes:
    # Axes must be ascending for searchsorted / multilinear interp.
    wavelengths = np.array(
        sorted(BAND_WAVELENGTH_M[b] for b in ("VHF", "L", "S", "C", "X", "Ku")),
        dtype=np.float64,
    )
    h_obs = np.array(
        [1.0, 2.5, 5.0, 10.0, 20.0, 40.0, 60.0, 80.0], dtype=np.float64
    )
    ranges = np.array(
        [500.0, 1000.0, 2000.0, 5000.0, 10000.0, 15000.0], dtype=np.float64
    )
    u_edges = np.array([0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8], dtype=np.float64)
    return GeometryLutAxes(wavelengths, h_obs, ranges, u_edges)


def build_geometry_lut(axes: GeometryLutAxes | None = None) -> dict:
    """Dense table factor[iw, ih, ir, iu] from analytic single-edge geometry."""
    if axes is None:
        axes = default_geometry_axes()
    nw = int(axes.wavelengths_m.size)
    nh = int(axes.h_obs_m.size)
    nr = int(axes.ranges_m.size)
    nu = int(axes.u_edges.size)
    table = np.zeros((nw, nh, nr, nu), dtype=np.float64)
    t0 = time.perf_counter()
    for iw in range(nw):
        lam = float(axes.wavelengths_m[iw])
        for ih in range(nh):
            h = float(axes.h_obs_m[ih])
            for ir in range(nr):
                r = float(axes.ranges_m[ir])
                for iu in range(nu):
                    u = float(axes.u_edges[iu])
                    table[iw, ih, ir, iu] = knife_edge_factor_from_geometry(
                        h, r, u, lam, min_factor=0.0
                    )
    elapsed = time.perf_counter() - t0
    return {
        "wavelengths_m": axes.wavelengths_m,
        "h_obs_m": axes.h_obs_m,
        "ranges_m": axes.ranges_m,
        "u_edges": axes.u_edges,
        "factor": table,
        "build_s": elapsed,
        "bytes": int(table.nbytes)
        + int(axes.wavelengths_m.nbytes)
        + int(axes.h_obs_m.nbytes)
        + int(axes.ranges_m.nbytes)
        + int(axes.u_edges.nbytes),
    }


def _axis_index_frac(axis: np.ndarray, value: float) -> tuple[int, int, float]:
    """Return (i0, i1, t) for linear interpolation along a sorted axis."""
    if value <= float(axis[0]):
        return 0, 0, 0.0
    if value >= float(axis[-1]):
        last = int(axis.size) - 1
        return last, last, 0.0
    # Find right index.
    i1 = int(np.searchsorted(axis, value, side="right"))
    if i1 <= 0:
        return 0, 0, 0.0
    i0 = i1 - 1
    if i1 >= int(axis.size):
        last = int(axis.size) - 1
        return last, last, 0.0
    a0 = float(axis[i0])
    a1 = float(axis[i1])
    if abs(a1 - a0) < 1e-30:
        return i0, i1, 0.0
    t = (value - a0) / (a1 - a0)
    return i0, i1, t


def nearest_wavelength_index(wavelengths_m: np.ndarray, wavelength_m: float) -> int:
    """Pick nearest band node (game path switches bands, not continuous λ)."""
    diffs = np.abs(wavelengths_m - float(wavelength_m))
    return int(np.argmin(diffs))


def interp_geometry_lut(
    lut: dict,
    wavelength_m: float,
    h_obs_m: float,
    range_m: float,
    u_edge: float,
) -> float:
    """Nearest-band + trilinear (h, range, u) in log10(factor) space."""
    wl = lut["wavelengths_m"]
    hs = lut["h_obs_m"]
    rs = lut["ranges_m"]
    us = lut["u_edges"]
    table = lut["factor"]

    iw = nearest_wavelength_index(wl, wavelength_m)
    ih0, ih1, th = _axis_index_frac(hs, h_obs_m)
    ir0, ir1, tr = _axis_index_frac(rs, range_m)
    iu0, iu1, tu = _axis_index_frac(us, u_edge)

    eps = 1e-12
    acc_log = 0.0
    for ah, ih in ((1.0 - th, ih0), (th, ih1)):
        for ar, ir in ((1.0 - tr, ir0), (tr, ir1)):
            for au, iu in ((1.0 - tu, iu0), (tu, iu1)):
                w = ah * ar * au
                if w <= 0.0:
                    continue
                val = max(float(table[iw, ih, ir, iu]), eps)
                acc_log += w * math.log10(val)
    return float(10.0 ** acc_log)


def validate_geometry_lut(
    lut: dict,
    n_probe: int = 300,
    seed: int = 11,
    max_abs_err: float = 0.04,
    max_rel_err: float = 0.15,
) -> dict:
    """Probes at exact band λ with interior (h,R,u): LUT vs analytic."""
    rng = np.random.default_rng(seed)
    wl = lut["wavelengths_m"]
    hs = lut["h_obs_m"]
    rs = lut["ranges_m"]
    us = lut["u_edges"]
    abs_errs = []
    rel_errs = []
    for _ in range(n_probe):
        lam = float(wl[int(rng.integers(0, int(wl.size)))])
        h = float(rng.uniform(float(hs[0]), float(hs[-1])))
        r = float(rng.uniform(float(rs[0]), float(rs[-1])))
        u = float(rng.uniform(float(us[0]), float(us[-1])))
        truth = knife_edge_factor_from_geometry(h, r, u, lam, min_factor=0.0)
        approx = interp_geometry_lut(lut, lam, h, r, u)
        abs_e = abs(approx - truth)
        if truth > 1e-4:
            rel_e = abs_e / truth
        else:
            rel_e = abs_e
        abs_errs.append(abs_e)
        rel_errs.append(rel_e)
    worst_abs = float(max(abs_errs))
    worst_rel = float(max(rel_errs))
    mean_abs = float(sum(abs_errs) / len(abs_errs))
    ok = worst_abs <= max_abs_err and worst_rel <= max_rel_err
    return {
        "ok": ok,
        "n_probe": n_probe,
        "worst_abs_err": worst_abs,
        "worst_rel_err": worst_rel,
        "mean_abs_err": mean_abs,
        "max_abs_err": max_abs_err,
        "max_rel_err": max_rel_err,
        "mode": "nearest_band_log_trilinear_hru",
    }


def validate_band_nodes(lut: dict) -> dict:
    """Exact band nodes must match analytic (no interp error on grid points)."""
    wl = lut["wavelengths_m"]
    hs = lut["h_obs_m"]
    rs = lut["ranges_m"]
    us = lut["u_edges"]
    table = lut["factor"]
    worst = 0.0
    for iw, lam in enumerate(wl):
        for ih, h in enumerate(hs):
            for ir, r in enumerate(rs):
                for iu, u in enumerate(us):
                    truth = knife_edge_factor_from_geometry(
                        float(h), float(r), float(u), float(lam), min_factor=0.0
                    )
                    err = abs(float(table[iw, ih, ir, iu]) - truth)
                    if err > worst:
                        worst = err
    return {"ok": worst < 1e-12, "worst_abs_err": worst}


def validate_dual_band_consistency(
    n_probe: int = 80,
    seed: int = 3,
) -> dict:
    """Dual-edge analytic at band λ stays finite and ≤ single main edge."""
    rng = np.random.default_rng(seed)
    bands = list(BAND_WAVELENGTH_M.items())
    rows = []
    ok = True
    for _ in range(n_probe):
        name, lam = bands[int(rng.integers(0, len(bands)))]
        h1 = float(rng.uniform(2.0, 40.0))
        h2 = float(rng.uniform(2.0, 40.0))
        u1 = float(rng.uniform(0.2, 0.45))
        u2 = float(rng.uniform(0.55, 0.85))
        r = float(rng.uniform(1000.0, 12000.0))
        single_a = knife_edge_factor_from_geometry(h1, r, u1, lam, min_factor=0.0)
        single_b = knife_edge_factor_from_geometry(h2, r, u2, lam, min_factor=0.0)
        if single_a >= single_b:
            single = single_a
        else:
            single = single_b
        dual = dual_knife_edge_factor_from_geometry(
            h1, u1, h2, u2, r, lam, min_factor=0.0
        )
        row_ok = dual <= single + 1e-9 and dual >= 0.0
        if not row_ok:
            ok = False
        rows.append(
            {
                "band": name,
                "lambda_m": lam,
                "single": single,
                "dual": dual,
                "ok": row_ok,
            }
        )
    return {"ok": ok, "n_probe": n_probe, "samples": rows[:8]}


def lut_to_jsonable(nu_lut: dict, geo_lut: dict) -> dict:
    """Strip numpy arrays for JSON export."""
    return {
        "schema": SCHEMA,
        "bands_wavelength_m": dict(BAND_WAVELENGTH_M),
        "nu_lut": {
            "nu_min": nu_lut["nu_min"],
            "nu_max": nu_lut["nu_max"],
            "n": nu_lut["n"],
            "nu": [float(x) for x in nu_lut["nu"]],
            "factor": [float(x) for x in nu_lut["factor"]],
        },
        "geometry_lut": {
            "wavelengths_m": [float(x) for x in geo_lut["wavelengths_m"]],
            "h_obs_m": [float(x) for x in geo_lut["h_obs_m"]],
            "ranges_m": [float(x) for x in geo_lut["ranges_m"]],
            "u_edges": [float(x) for x in geo_lut["u_edges"]],
            "factor": geo_lut["factor"].tolist(),
            "build_s": geo_lut["build_s"],
            "bytes": geo_lut["bytes"],
        },
    }


def run_knife_lut_validation() -> dict:
    """Build LUTs and run all validation cases."""
    t0 = time.perf_counter()
    nu_lut = build_nu_lut()
    geo_lut = build_geometry_lut()
    nu_check = validate_nu_lut(nu_lut)
    node_check = validate_band_nodes(geo_lut)
    geo_check = validate_geometry_lut(geo_lut)
    dual_check = validate_dual_band_consistency()
    elapsed = time.perf_counter() - t0
    all_ok = (
        nu_check["ok"]
        and node_check["ok"]
        and geo_check["ok"]
        and dual_check["ok"]
    )
    return {
        "all_ok": all_ok,
        "elapsed_s": elapsed,
        "nu_lut_validation": nu_check,
        "geometry_node_validation": node_check,
        "geometry_interp_validation": geo_check,
        "dual_band_validation": {
            "ok": dual_check["ok"],
            "n_probe": dual_check["n_probe"],
            "sample_head": dual_check["samples"],
        },
        "geometry_lut_bytes": geo_lut["bytes"],
        "geometry_lut_build_s": geo_lut["build_s"],
        "nu_lut_n": nu_lut["n"],
            "verdict": (
            "1D ν LUT is high-accuracy; band×geometry uses nearest-band + "
            "log-trilinear (h,R,u). Suitable bake-back for band switching."
        ),
        "table": lut_to_jsonable(nu_lut, geo_lut),
    }


def case_knife_lut_accuracy() -> KnifeLutCaseResult:
    report = run_knife_lut_validation()
    # Drop bulky table from suite metrics (CLI writes full JSON separately).
    metrics = dict(report)
    metrics.pop("table", None)
    return KnifeLutCaseResult(
        name="knife_lut_accuracy",
        ok=bool(report["all_ok"]),
        metrics=metrics,
    )
