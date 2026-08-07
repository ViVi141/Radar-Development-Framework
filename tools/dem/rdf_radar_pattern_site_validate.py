#!/usr/bin/env python3
"""CLI: bake segmented pattern LUT + fixed-site path LUT for Enforce.

Writes:
  tools/dem/out/pattern_site_report.json
  tools/dem/calib/PatternLut.json          → $profile:RDF/RadarData/PatternLut.json
  tools/dem/calib/SitePathLut.json         → $profile:RDF/RadarData/SitePathLut.json
"""

from __future__ import annotations

import argparse
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_radar_pattern_lut import run_pattern_lut_validation  # noqa: E402
from rdf_radar_site_path_lut import (  # noqa: E402
    build_synthetic_site_lut,
    lut_to_enforce_json,
    run_site_path_validation,
    try_build_from_dem_dir,
)


def _write_json(path: str, doc: dict) -> None:
    out_dir = os.path.dirname(os.path.abspath(path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print("wrote", path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Bake pattern + site-path LUTs for Enforce bake-back."
    )
    parser.add_argument(
        "--out",
        default=os.path.join(_HERE, "out", "pattern_site_report.json"),
    )
    parser.add_argument(
        "--pattern-out",
        default=os.path.join(_HERE, "calib", "PatternLut.json"),
    )
    parser.add_argument(
        "--site-out",
        default=os.path.join(_HERE, "calib", "SitePathLut.json"),
    )
    parser.add_argument("--hpbw-deg", type=float, default=2.5)
    parser.add_argument("--sll-db", type=float, default=-25.0)
    parser.add_argument("--origin-x", type=float, default=0.0)
    parser.add_argument("--origin-y", type=float, default=25.0)
    parser.add_argument("--origin-z", type=float, default=0.0)
    parser.add_argument("--max-range-m", type=float, default=4000.0)
    parser.add_argument("--az-count", type=int, default=72)
    parser.add_argument("--range-count", type=int, default=40)
    parser.add_argument(
        "--dem-dir",
        default="",
        help="Optional DemData/<world> with HEIGHT pack; else synthetic ridge",
    )
    args = parser.parse_args(argv)

    pattern_report = run_pattern_lut_validation(
        hpbw_deg=args.hpbw_deg,
        sidelobe_level_db=args.sll_db,
    )
    pattern_table = pattern_report.pop("table", None)
    pattern_report.pop("full", None)

    origin = (args.origin_x, args.origin_y, args.origin_z)
    from rdf_radar_site_path_lut import validate_site_path_lut

    if args.dem_dir:
        site_lut = try_build_from_dem_dir(
            args.dem_dir,
            origin,
            max_range_m=args.max_range_m,
            az_count=args.az_count,
            range_count=args.range_count,
        )
        site_check = validate_site_path_lut(site_lut, expect_ridge=False)
        site_report = {
            "all_ok": bool(site_check["ok"]),
            "validation": site_check,
            "az_count": site_lut["az_count"],
            "range_count": site_lut["range_count"],
            "bytes": site_lut["bytes"],
            "source": "dem",
            "dem_dir": args.dem_dir,
        }
        site_table = lut_to_enforce_json(site_lut)
    else:
        site_lut = build_synthetic_site_lut(
            origin,
            max_range_m=args.max_range_m,
            az_count=args.az_count,
            range_count=args.range_count,
        )
        site_check = validate_site_path_lut(site_lut, expect_ridge=True)
        site_report = {
            "all_ok": bool(site_check["ok"]),
            "validation": site_check,
            "az_count": site_lut["az_count"],
            "range_count": site_lut["range_count"],
            "bytes": site_lut["bytes"],
            "source": "synthetic",
        }
        site_table = lut_to_enforce_json(site_lut)

    report = {
        "all_ok": bool(pattern_report["all_ok"] and site_report["all_ok"]),
        "pattern": pattern_report,
        "site_path": site_report,
    }
    _write_json(args.out, report)
    if pattern_table is not None:
        _write_json(args.pattern_out, pattern_table)
        print("  copy to $profile:RDF/RadarData/PatternLut.json")
    if site_table is not None:
        _write_json(args.site_out, site_table)
        print("  copy to $profile:RDF/RadarData/SitePathLut.json")

    print("all_ok =", report["all_ok"])
    print(
        " pattern worst_abs=",
        round(pattern_report["interp_validation"]["worst_abs_err"], 6),
    )
    print(
        " site clear/ridge=",
        site_report["validation"].get("clear_az0_r500"),
        site_report["validation"].get("ridge_az90_r800"),
    )
    if report["all_ok"]:
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
