#!/usr/bin/env python3
"""CLI validation for offline 3D voxel EM power fields + toy FDTD.

Writes JSON (and optional PNG slices) under tools/dem/out/.
Does not drive in-game detection — calibration / feasibility only.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_voxel_em import (  # noqa: E402
    EmSource,
    VoxelGrid,
    fill_power_field_los,
    horizontal_slice_db,
    run_validation_suite,
)
from rdf_voxel_fdtd import run_fdtd_demo_slice  # noqa: E402


def _ensure_out_dir(path: str) -> None:
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)


def _write_report(path: str, report: dict) -> None:
    _ensure_out_dir(path)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")


def _maybe_write_power_slice(out_dir: str) -> list[str]:
    """Build a small occluded scene and dump horizontal power slices."""
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return []

    cell_m = 4.0
    half = 60.0
    n = int((2.0 * half) / cell_m)
    grid = VoxelGrid(origin_m=(-half, -half, -half), cell_m=cell_m, shape=(n, n, n))
    grid.fill_box_obstacle(15.0, -40.0, -40.0, 20.0, 40.0, 40.0, atten_per_m=20.0)
    source = EmSource(-30.0, 0.0, 0.0, 1000.0)
    fill_power_field_los(grid, source)
    img, extent = horizontal_slice_db(grid, 0.0)

    os.makedirs(out_dir, exist_ok=True)
    png_path = os.path.join(out_dir, "voxel_em_power_slice_y0.png")
    fig, ax = plt.subplots(figsize=(7.0, 6.0))
    mesh = ax.imshow(
        img,
        origin="lower",
        extent=extent,
        cmap="viridis",
        aspect="equal",
    )
    ax.set_xlabel("X (m)")
    ax.set_ylabel("Z (m)")
    ax.set_title("Voxel EM power density (dB, Y=0)")
    fig.colorbar(mesh, ax=ax, fraction=0.046, label="dB (W/m^2)")
    fig.tight_layout()
    fig.savefig(png_path, dpi=120)
    plt.close(fig)
    return [png_path]


def _maybe_write_fdtd_slice(out_dir: str) -> list[str]:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return []

    img, meta = run_fdtd_demo_slice()
    os.makedirs(out_dir, exist_ok=True)
    png_path = os.path.join(out_dir, "voxel_em_fdtd_ez_slice.png")
    fig, ax = plt.subplots(figsize=(7.0, 6.0))
    mesh = ax.imshow(
        img,
        origin="lower",
        extent=meta["extent"],
        cmap="magma",
        aspect="equal",
    )
    ax.set_xlabel("X (m)")
    ax.set_ylabel("Z (m)")
    ax.set_title("Toy 3D FDTD |Ez| mid-slice (PEC wall)")
    fig.colorbar(mesh, ax=ax, fraction=0.046, label="|Ez| (a.u.)")
    fig.tight_layout()
    fig.savefig(png_path, dpi=120)
    plt.close(fig)
    return [png_path]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate 3D voxel EM power-field + toy FDTD (offline)."
    )
    parser.add_argument(
        "--out",
        default=os.path.join(_HERE, "out", "voxel_em_report.json"),
        help="JSON report path (default: out/voxel_em_report.json)",
    )
    parser.add_argument(
        "--plot",
        action="store_true",
        help="Write optional matplotlib PNG slices under out/",
    )
    parser.add_argument(
        "--no-fdtd",
        action="store_true",
        help="Skip toy Yee FDTD cases (power-field only).",
    )
    args = parser.parse_args(argv)

    include_fdtd = True
    if args.no_fdtd:
        include_fdtd = False
    report = run_validation_suite(include_fdtd=include_fdtd)
    artifacts = []
    if args.plot:
        out_dir = os.path.dirname(os.path.abspath(args.out))
        artifacts.extend(_maybe_write_power_slice(out_dir))
        if include_fdtd:
            artifacts.extend(_maybe_write_fdtd_slice(out_dir))
    report["artifacts"] = artifacts
    _write_report(args.out, report)

    print("voxel_em report ->", args.out)
    print("all_ok =", report["all_ok"])
    for row in report["results"]:
        status = "PASS"
        if not row["ok"]:
            status = "FAIL"
        print(" ", status, row["name"])
        if row["name"] == "fdtd_vs_game_accuracy":
            fs = row["metrics"]["free_space"]
            occ = row["metrics"]["occlusion"]
            print(
                "    free-space max_rel_err=",
                round(fs["max_rel_err"], 4),
                "mean=",
                round(fs["mean_rel_err"], 4),
            )
            print(
                "    occlusion FDTD ratio=",
                round(occ["fdtd_shadow_over_lit"], 6),
                "hard_los=",
                round(occ["game_hard_los_shadow_over_lit"], 6),
                "knife=",
                round(occ["game_knife_edge_shadow_over_lit"], 6),
            )
            print(
                "    err_db hard_los=",
                round(occ["hard_los_err_db"], 2),
                "knife_edge=",
                round(occ["knife_edge_err_db"], 2),
            )
            two = row["metrics"].get("occlusion_two_edge")
            if two:
                print(
                    "    two-edge FDTD ratio=",
                    round(two["fdtd_shadow_over_lit"], 6),
                    "single=",
                    round(two["game_single_knife_shadow_over_lit"], 6),
                    "dual=",
                    round(two["game_dual_knife_shadow_over_lit"], 6),
                )
                print(
                    "    err_db single=",
                    round(two["single_knife_err_db"], 2),
                    "dual=",
                    round(two["dual_knife_err_db"], 2),
                )
        if row["name"] == "quadtree_memory":
            qt = row["metrics"]["quadtree"]
            print(
                "    quad MiB=",
                round(qt["mib_est"], 3),
                " dense@min MiB=",
                round(row["metrics"]["dense_at_min_leaf"]["mib"], 3),
                " ratio=",
                round(row["metrics"]["quad_over_dense_min_leaf"], 4),
                " savings%=",
                round(100.0 * row["metrics"]["savings_frac_vs_dense_min"], 1),
            )
    if artifacts:
        for path in artifacts:
            print(" artifact", path)

    if report["all_ok"]:
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
