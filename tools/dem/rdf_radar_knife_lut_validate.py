#!/usr/bin/env python3
"""CLI: bake knife-edge LUTs and validate interpolation accuracy.

Writes:
  tools/dem/out/knife_lut_report.json   (validation metrics)
  tools/dem/calib/knife_edge_lut.json   (full table bake-back candidate)
  tools/dem/calib/KnifeEdgeLut.json     (slim ν table for Enforce profile)
"""

from __future__ import annotations

import argparse
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_radar_knife_lut import SCHEMA, run_knife_lut_validation  # noqa: E402


def _write_enforce_nu_profile(table: dict, path: str) -> None:
    """Slim JSON matching RDF_RadarKnifeEdgeLutDoc (profile bake-back)."""
    nu = table.get("nu_lut")
    if not isinstance(nu, dict):
        return
    factors = nu.get("factor")
    if not isinstance(factors, list):
        return
    if len(factors) < 5:
        return
    doc = {
        "schema": SCHEMA,
        "nu_min": float(nu["nu_min"]),
        "nu_max": float(nu["nu_max"]),
        "factors": [float(x) for x in factors],
    }
    out_dir = os.path.dirname(os.path.abspath(path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print("Enforce ν LUT profile ->", path)
    print("  copy to $profile:RDF/RadarData/KnifeEdgeLut.json")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate knife-edge ν / band×geometry LUT accuracy."
    )
    parser.add_argument(
        "--out",
        default=os.path.join(_HERE, "out", "knife_lut_report.json"),
        help="Validation report JSON",
    )
    parser.add_argument(
        "--table-out",
        default=os.path.join(_HERE, "calib", "knife_edge_lut.json"),
        help="Full LUT table JSON (bake-back candidate)",
    )
    parser.add_argument(
        "--enforce-out",
        default=os.path.join(_HERE, "calib", "KnifeEdgeLut.json"),
        help="Slim ν LUT for Enforce RDF_RadarKnifeEdgeLut",
    )
    parser.add_argument(
        "--no-table",
        action="store_true",
        help="Skip writing the bulky geometry table file",
    )
    args = parser.parse_args(argv)

    report = run_knife_lut_validation()
    table = report.pop("table", None)

    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")

    if (not args.no_table) and table is not None:
        table_dir = os.path.dirname(os.path.abspath(args.table_out))
        if table_dir:
            os.makedirs(table_dir, exist_ok=True)
        with open(args.table_out, "w", encoding="utf-8") as handle:
            json.dump(table, handle, indent=2, sort_keys=True)
            handle.write("\n")
        print("knife LUT table ->", args.table_out)
        _write_enforce_nu_profile(table, args.enforce_out)

    print("knife LUT report ->", args.out)
    print("all_ok =", report["all_ok"])
    nu = report["nu_lut_validation"]
    geo = report["geometry_interp_validation"]
    print(
        " ν LUT worst_abs=",
        round(nu["worst_abs_err"], 6),
        " worst_rel=",
        round(nu["worst_rel_err"], 6),
    )
    print(
        " geometry LUT worst_abs=",
        round(geo["worst_abs_err"], 6),
        " worst_rel=",
        round(geo["worst_rel_err"], 6),
        " bytes=",
        report["geometry_lut_bytes"],
    )
    print(" dual_band_ok =", report["dual_band_validation"]["ok"])

    if report["all_ok"]:
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
