#!/usr/bin/env python3
"""Offline ShellFire / WLR mirror of RDF_RadarShellFireAutoTest.

Uses the same multi-point vacuum fit + AirDrag ground intersect path as
in-game SolveWeaponLocate (min hits/span/RMS + EMA smooth).

Examples:
  python tools/dem/rdf_radar_shellfire_offline.py --use-ttile --world GM_Eden
  python tools/dem/rdf_radar_shellfire_offline.py --duration-s 45 --shells 6
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_dem_io import resolve_dem_source, choose_radar_site
import rdf_radar_mass_battle_sim as mbs
from rdf_radar_physics import ElevationBeam, RadarHardware, get_preset, summarize_hardware
from rdf_radar_targets import (
    AIR_DRAG_SHELL_82MM_HE,
    GRAVITY_M_S2,
    GlobalWind,
    RadarTarget,
    TargetTrajectory,
    dem_ground_y_fn,
    flat_ground_y_fn,
    random_global_wind,
)


def build_shellfire_hardware() -> RadarHardware:
    """Match RDF_RadarShellFireAutoTest.ApplyTestRadarConfig (Shorad + mortar beams)."""
    hw = get_preset("shorad")
    hw.name = "shellfire_wlr"
    hw.display_name = "ShellFire WLR (Shorad + mortar beams)"
    hw.az_beamwidth_deg = 45.0
    hw.scan_rpm = 0.0
    hw.instrumented_range_m = 8000.0
    hw.elevation_beams = [
        ElevationBeam("mortar_low", 15.0, 28.0, 0.0),
        ElevationBeam("mortar_mid", 35.0, 30.0, 0.0),
        ElevationBeam("mortar_high", 55.0, 28.0, -0.5),
    ]
    return hw


def build_shellfire_scenario(
    shell_rcs: float,
    cell_m: float,
    radar_ix: float,
    radar_iz: float,
    radar_y: float,
    n_shells: int,
    seed: int,
    wind: GlobalWind,
    ground_y_fn,
    elev_deg: float = 55.0,
    speed_coef: float = 1.736,
) -> list[TargetTrajectory]:
    """Mirror RDF_RadarShellFireAutoTest: 82 mm HE, elev 55°, charge-2 coef."""
    del radar_y  # ground sampled via ground_y_fn at launch xz
    rng = np.random.default_rng(seed)
    trajectories: list[TargetTrajectory] = []
    elev = math.radians(elev_deg)
    # AutoTest: launch ~45 m ahead of subject along fire azimuth (+Z here).
    base_bearing = math.radians(90.0)
    # O832DU muzzle ~118 m/s * charge-2 coef ≈ 205 m/s at 55°.
    v0 = 118.0 * speed_coef
    for i in range(n_shells):
        launch_dist = 45.0 + float(rng.uniform(-5.0, 5.0))
        lateral = float(rng.uniform(-8.0, 8.0))
        launch_x = launch_dist * math.cos(base_bearing) + lateral * math.sin(
            base_bearing
        )
        launch_z = launch_dist * math.sin(base_bearing) - lateral * math.cos(
            base_bearing
        )
        launch_ix = radar_ix + launch_x / cell_m
        launch_iz = radar_iz + launch_z / cell_m
        launch_ground = ground_y_fn(launch_x, launch_z)

        aim_jitter = math.radians(float(rng.uniform(-3.0, 3.0)))
        aim = base_bearing + aim_jitter
        vx = v0 * math.cos(elev) * math.cos(aim)
        vz = v0 * math.cos(elev) * math.sin(aim)
        vy = v0 * math.sin(elev)

        trajectories.append(
            TargetTrajectory(
                initial=RadarTarget(
                    name=f"shell_{i + 1:02d}",
                    ix=launch_ix,
                    iz=launch_iz,
                    y_m=launch_ground + 8.0,
                    rcs_m2=shell_rcs * float(rng.uniform(0.9, 1.1)),
                    kind="shell",
                    vx_m_s=vx,
                    vy_m_s=vy,
                    vz_m_s=vz,
                ),
                start_time_s=2.0 + i * 6.0,
                ay_m_s2=GRAVITY_M_S2,
                air_drag=AIR_DRAG_SHELL_82MM_HE,
                wind=wind,
            )
        )
    return trajectories


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration-s", type=float, default=42.0)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--shells", type=int, default=6)
    parser.add_argument("--world", default="GM_Eden")
    parser.add_argument("--dem-npz", default="")
    parser.add_argument("--use-ttile", action="store_true")
    parser.add_argument("--surf-dir", default="")
    parser.add_argument("--no-surf", action="store_true")
    parser.add_argument("--no-dem", action="store_true")
    parser.add_argument("--output-dir", default="")
    args = parser.parse_args()

    out_dir = args.output_dir
    if not out_dir:
        out_dir = os.path.join(_HERE, "out")
    os.makedirs(out_dir, exist_ok=True)

    priors = mbs.load_signature_priors(mbs._default_sig_path())
    wind_rng = np.random.default_rng(args.seed + 777)
    wind = random_global_wind(wind_rng)

    if args.no_dem:
        cell_m = 20.0
        radar_ix = 256.0
        radar_iz = 256.0
        radar_y = 100.0
        terrain = np.full((512, 512), radar_y, dtype=np.float32)
        ground_y_fn = flat_ground_y_fn(radar_y)
        dem_source = "flat"
    else:
        dem = resolve_dem_source(
            world=args.world,
            dem_npz=args.dem_npz,
            prefer_ttile=args.use_ttile,
            load_spans=False,
            surf_dir=args.surf_dir,
            merge_surf=not args.no_surf,
        )
        terrain = np.asarray(dem["terrain"], dtype=np.float32)
        cell_m = float(dem["cell_m"])
        margin = 128
        if terrain.shape[0] < margin * 2 + 8:
            margin = max(8, terrain.shape[0] // 8)
        radar_iz_i, radar_ix_i = choose_radar_site(terrain, margin=margin)
        radar_ix = float(radar_ix_i)
        radar_iz = float(radar_iz_i)
        radar_y = float(terrain[radar_iz_i, radar_ix_i])
        if not np.isfinite(radar_y) or radar_y < 1.0:
            radar_y = 100.0
        ground_y_fn = dem_ground_y_fn(terrain, cell_m, radar_ix, radar_iz, radar_y)
        dem_source = str(dem.get("source", "dem"))

    print(
        f"ShellFire offline DEM={dem_source} cell={cell_m} "
        f"radar=({radar_ix:.0f},{radar_iz:.0f}) y={radar_y:.1f}"
    )
    print(
        f"signatures shell_rcs={priors.shell_rcs:.4f} "
        f"wind={wind.speed_m_s:.1f} m/s"
    )

    trajectories = build_shellfire_scenario(
        priors.shell_rcs,
        cell_m,
        radar_ix,
        radar_iz,
        radar_y,
        max(1, args.shells),
        args.seed,
        wind,
        ground_y_fn,
    )
    print(f"shells={len(trajectories)} duration={args.duration_s:.0f}s")

    wlr_hw = build_shellfire_hardware()
    # Ideal-channel-ish: low CNR + soft SNR gate (AutoTest DetectionSnrDb=-30).
    wlr = mbs.run_plot_level_radar(
        name="WLR",
        hardware=wlr_hw,
        trajectories=trajectories,
        cell_m=cell_m,
        radar_ix=radar_ix,
        radar_iz=radar_iz,
        radar_y=radar_y,
        duration_s=args.duration_s,
        clutter_cnr_db=0.0,
        seed=args.seed + 3,
        kind_filter={"shell"},
        detect_snr_db=-30.0,
        az_sector_center_deg=90.0,
        az_sector_half_deg=180.0,
    )

    # ShellFire AutoTest scores best launch err during the flight, not final marker.
    fixes = mbs.evaluate_wlr_fixes(
        wlr,
        trajectories,
        cell_m,
        radar_ix,
        radar_iz,
        radar_y,
        wind=wind,
        min_hits=5,
        min_span_s=1.0,
        max_fit_rms_m=80.0,
        window_points=20,
        smooth_alpha=0.35,
        ground_y_fn=ground_y_fn,
        select_mode="best_launch",
    )
    summary_wlr = mbs.summarize_wlr_fixes(fixes)
    launch_pass_m = 250.0
    n_pass = sum(1 for s in fixes if s.launch_err_m <= launch_pass_m)

    prefix = os.path.join(out_dir, "shellfire_offline")
    csv_path = prefix + "_wlr_fixes.csv"
    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "track_id",
                "target",
                "hits",
                "launch_err_m",
                "impact_err_m",
                "fit_rms_m",
                "fit_span_s",
                "fit_points",
            ]
        )
        for s in fixes:
            writer.writerow(
                [
                    s.track_id,
                    s.target,
                    s.hits,
                    f"{s.launch_err_m:.2f}",
                    f"{s.impact_err_m:.2f}",
                    f"{s.fit_rms_m:.2f}",
                    f"{s.fit_span_s:.2f}",
                    s.fit_points,
                ]
            )

    png_path = prefix + "_wlr_fixes.png"
    mbs._render_wlr_fixes(png_path, fixes)

    fig, ax = plt.subplots(figsize=(7, 6))
    ax.set_title("ShellFire offline — launch/impact (radar frame XZ)")
    ax.set_aspect("equal")
    ax.grid(True, alpha=0.3)
    ax.plot(0.0, 0.0, "k^", markersize=10, label="radar")
    for s in fixes:
        ax.plot(
            [s.true_launch_x_m, s.true_impact_x_m],
            [s.true_launch_z_m, s.true_impact_z_m],
            color="0.7",
            linewidth=1.0,
        )
        ax.plot(s.true_launch_x_m, s.true_launch_z_m, "go", markersize=5)
        ax.plot(s.true_impact_x_m, s.true_impact_z_m, "g*", markersize=8)
        ax.plot(s.est_launch_x_m, s.est_launch_z_m, "o", color="C1", markersize=5)
        ax.plot(s.est_impact_x_m, s.est_impact_z_m, "*", color="C0", markersize=8)
    ax.legend(loc="best", fontsize=8)
    overview = prefix + "_overview.png"
    fig.tight_layout()
    fig.savefig(overview, dpi=130)
    plt.close(fig)

    summary = {
        "mode": "shellfire_offline",
        "wlr_algorithm": "vacuum_ls_fit+airdrag_ground+ema",
        "select_mode": "best_launch",
        "launch_pass_m": launch_pass_m,
        "launch_pass_count": n_pass,
        "gates": {
            "min_hits": 5,
            "min_span_s": 1.0,
            "max_fit_rms_m": 80.0,
            "window_points": 20,
            "smooth_alpha": 0.35,
        },
        "dem": dem_source,
        "shells": len(trajectories),
        "wlr": {
            "detections": wlr.detections,
            "intercepts": wlr.intercepts,
            "tracks": wlr.track_count,
            "confirmed": wlr.confirmed_tracks,
        },
        "wlr_fix": summary_wlr,
        "outputs": {
            "csv": csv_path,
            "wlr_png": png_path,
            "overview_png": overview,
        },
    }
    json_path = prefix + "_summary.json"
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)

    print(f"wrote: {csv_path}")
    print(f"wrote: {png_path}")
    print(f"wrote: {overview}")
    print(f"wrote: {json_path}")
    for line in summarize_hardware(wlr_hw):
        print(" ", line)
    print(
        f"WLR CFAR={wlr.detections}/{wlr.intercepts} "
        f"tracks={wlr.track_count} confirmed={wlr.confirmed_tracks}"
    )
    if summary_wlr.get("n", 0) == 0:
        print("WLR fix: no qualifying shell tracks")
    else:
        print(
            f"WLR fix n={summary_wlr['n']} (best_launch)  "
            f"launch med={summary_wlr['launch_err_med_m']:.1f} m "
            f"p90={summary_wlr['launch_err_p90_m']:.1f} m  "
            f"impact med={summary_wlr['impact_err_med_m']:.1f} m "
            f"p90={summary_wlr['impact_err_p90_m']:.1f} m"
        )
        print(
            f"launch_pass <= {launch_pass_m:.0f} m: {n_pass}/{summary_wlr['n']}"
        )


if __name__ == "__main__":
    main()
