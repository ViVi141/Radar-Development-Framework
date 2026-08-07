#!/usr/bin/env python3
"""CLI: measure XZ quadtree power-voxel memory vs dense grids.

Writes tools/dem/out/voxel_quadtree_measure.json
"""

from __future__ import annotations

import argparse
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_voxel_quadtree import measure_quadtree_vs_dense  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Measure quadtree vs dense voxel memory for power fields."
    )
    parser.add_argument(
        "--out",
        default=os.path.join(_HERE, "out", "voxel_quadtree_measure.json"),
        help="JSON report path",
    )
    parser.add_argument("--extent-x", type=float, default=2000.0)
    parser.add_argument("--extent-y", type=float, default=400.0)
    parser.add_argument("--extent-z", type=float, default=2000.0)
    parser.add_argument("--min-leaf", type=float, default=10.0)
    parser.add_argument("--max-leaf", type=float, default=160.0)
    parser.add_argument("--roi-radius", type=float, default=250.0)
    args = parser.parse_args(argv)

    report = measure_quadtree_vs_dense(
        extent_m=(args.extent_x, args.extent_y, args.extent_z),
        min_leaf_m=args.min_leaf,
        max_leaf_m=args.max_leaf,
        roi_radius_m=args.roi_radius,
    )
    parent = os.path.dirname(os.path.abspath(args.out))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")

    qt = report["quadtree"]
    dense = report["dense_at_min_leaf"]
    print("quadtree measure ->", args.out)
    print(
        " leaves=",
        qt["leaf_count"],
        " air/obs/roi=",
        report["class_hist"],
        sep="",
    )
    print(
        " quad MiB=",
        round(qt["mib_est"], 3),
        " dense@",
        args.min_leaf,
        "m MiB=",
        round(dense["mib"], 3),
        " ratio=",
        round(report["quad_over_dense_min_leaf"], 4),
        " savings=",
        round(100.0 * report["savings_frac_vs_dense_min"], 1),
        "%",
        sep="",
    )
    print(" fine_area_frac=", round(qt["fine_area_frac"], 4))
    print(" build_s=", round(qt["elapsed_s"], 4))
    return 0


if __name__ == "__main__":
    sys.exit(main())
