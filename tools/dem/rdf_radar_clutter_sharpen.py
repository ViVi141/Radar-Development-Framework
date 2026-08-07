#!/usr/bin/env python3
"""Clutter-boundary sharpening helpers (mirror Enforce PulseDoppler path).

  1) Asymmetric range–az clutter-map EMA (fast down / slow up)
  2) Footprint σ⁰ mix across a land/water strip
  3) Sea-state water scale sanity vs SurfaceTable authorship at ss=3

Not a full RD / STAP model — just the spatial sharpening knobs.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from rdf_radar_materials import (
    SURF_SOIL,
    SURF_VEGETATION,
    SURF_WATER,
    Sigma0Table,
    sigma0_linear,
    water_offset_db,
)


def asymmetric_ema_step(
    prev: float,
    sample: float,
    alpha_up: float,
    alpha_down: float,
) -> float:
    """One EMA update; uses alpha_down when sample < prev."""
    if sample < prev:
        alpha = float(alpha_down)
    else:
        alpha = float(alpha_up)
    if alpha < 0.01:
        alpha = 0.01
    if alpha > 1.0:
        alpha = 1.0
    return prev * (1.0 - alpha) + sample * alpha


def asymmetric_ema_series(
    samples: list[float],
    alpha_up: float,
    alpha_down: float,
) -> list[float]:
    if not samples:
        return []
    out = [float(samples[0])]
    prev = float(samples[0])
    for s in samples[1:]:
        prev = asymmetric_ema_step(prev, float(s), alpha_up, alpha_down)
        out.append(prev)
    return out


def footprint_mix_sigma0(
    surface_classes: list[int],
    grazing_rad: float,
    table: Sigma0Table | None = None,
) -> float:
    """Arithmetic mean of per-cell σ⁰ (Enforce ResolveFootprintSigma0)."""
    if not surface_classes:
        return 0.0
    total = 0.0
    for sc in surface_classes:
        total = total + sigma0_linear(int(sc), grazing_rad, table)
    return total / float(len(surface_classes))


@dataclass
class ClutterSharpenReport:
    ok: bool
    metrics: dict


def case_asymmetric_ema_drops_faster() -> ClutterSharpenReport:
    """After vegetation seed, water samples should fall faster with α_down>α_up."""
    veg = 1.0
    water = 0.05
    samples = [veg, water, water, water, water]
    sym = asymmetric_ema_series(samples, 0.15, 0.15)
    asym = asymmetric_ema_series(samples, 0.15, 0.45)
    ok = asym[-1] < sym[-1]
    return ClutterSharpenReport(
        ok=ok,
        metrics={
            "samples": samples,
            "symmetric_final": sym[-1],
            "asymmetric_final": asym[-1],
            "symmetric_series": sym,
            "asymmetric_series": asym,
            "alpha_up": 0.15,
            "alpha_down": 0.45,
        },
    )


def case_footprint_mix_land_water() -> ClutterSharpenReport:
    """Point sample on vegetation overestimates vs mix that includes water."""
    table = Sigma0Table.builtin("X", 3)
    grazing = math.radians(10.0)
    point = sigma0_linear(SURF_VEGETATION, grazing, table)
    # 5-cell footprint straddling a shore: 3 veg + 2 water.
    mixed_classes = [
        SURF_VEGETATION,
        SURF_VEGETATION,
        SURF_VEGETATION,
        SURF_WATER,
        SURF_WATER,
    ]
    mixed = footprint_mix_sigma0(mixed_classes, grazing, table)
    water = sigma0_linear(SURF_WATER, grazing, table)
    ok = mixed < point and mixed > water
    contrast_db = 10.0 * math.log10(max(point, 1e-30) / max(mixed, 1e-30))
    return ClutterSharpenReport(
        ok=ok,
        metrics={
            "grazing_deg": 10.0,
            "point_veg_sigma0": point,
            "mixed_sigma0": mixed,
            "water_sigma0": water,
            "point_over_mixed_db": contrast_db,
            "classes": mixed_classes,
        },
    )


def case_sea_state_water_scale() -> ClutterSharpenReport:
    """ss5 water σ⁰ > ss3; ss0 water σ⁰ < ss3 (authored row at ss3)."""
    grazing = math.radians(30.0)
    ss0 = Sigma0Table.builtin("X", 0)
    ss3 = Sigma0Table.builtin("X", 3)
    ss5 = Sigma0Table.builtin("X", 5)
    w0 = sigma0_linear(SURF_WATER, grazing, ss0)
    w3 = sigma0_linear(SURF_WATER, grazing, ss3)
    w5 = sigma0_linear(SURF_WATER, grazing, ss5)
    soil3 = sigma0_linear(SURF_SOIL, grazing, ss3)
    soil5 = sigma0_linear(SURF_SOIL, grazing, ss5)
    # Soil must be unchanged by sea_state; water must track offsets.
    expected_ratio_5 = 10.0 ** (water_offset_db(5) / 10.0)
    expected_ratio_0 = 10.0 ** (water_offset_db(0) / 10.0)
    ratio_5 = w5 / max(w3, 1e-30)
    ratio_0 = w0 / max(w3, 1e-30)
    ok = (
        abs(ratio_5 - expected_ratio_5) < 1e-9
        and abs(ratio_0 - expected_ratio_0) < 1e-9
        and abs(soil3 - soil5) < 1e-12
        and w5 > w3 > w0
    )
    return ClutterSharpenReport(
        ok=ok,
        metrics={
            "water_ss0": w0,
            "water_ss3": w3,
            "water_ss5": w5,
            "ratio_ss5_over_ss3": ratio_5,
            "ratio_ss0_over_ss3": ratio_0,
            "expected_ratio_ss5": expected_ratio_5,
            "expected_ratio_ss0": expected_ratio_0,
            "soil_unchanged": abs(soil3 - soil5) < 1e-12,
        },
    )


def run_clutter_sharpen_validation() -> dict:
    cases = [
        ("asymmetric_ema", case_asymmetric_ema_drops_faster()),
        ("footprint_mix", case_footprint_mix_land_water()),
        ("sea_state_water", case_sea_state_water_scale()),
    ]
    results = []
    all_ok = True
    for name, report in cases:
        if not report.ok:
            all_ok = False
        results.append({"name": name, "ok": report.ok, "metrics": report.metrics})
    return {
        "all_ok": all_ok,
        "model": "clutter_sharpen_asymmetric_ema+footprint_mix+sea_state",
        "results": results,
    }


def main() -> int:
    import json
    import os

    report = run_clutter_sharpen_validation()
    out_dir = os.path.join(os.path.dirname(__file__), "out")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "clutter_sharpen_report.json")
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")
    print("clutter_sharpen report ->", out_path)
    print("all_ok =", report["all_ok"])
    for row in report["results"]:
        status = "PASS"
        if not row["ok"]:
            status = "FAIL"
        print(" ", status, row["name"])
    if report["all_ok"]:
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
