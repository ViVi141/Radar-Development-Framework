#!/usr/bin/env python3
"""Cold-War-style sector radar preview: absolute dBm, MTI, CFAR (no display gain).

Examples:
  python tools/dem/rdf_radar_sector_preview.py --world GM_Eden --preset shorad
  python tools/dem/rdf_radar_sector_preview.py --world GM_Eden --preset p18
  python tools/dem/rdf_radar_sector_preview.py --world GM_Eden --preset tps43
"""

from __future__ import annotations

import argparse
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

from rdf_dem_io import choose_radar_site, default_tile_dir, load_dem
from rdf_radar_materials import Sigma0Table, set_default_table
from rdf_radar_physics import (
    dbm_array,
    doppler_hz,
    get_preset,
    summarize_hardware,
    watts_to_dbm,
)
from rdf_radar_sector_sim import SectorConfig, simulate_sector
from rdf_radar_targets import default_scenario_targets, radial_speed_toward_radar


def _dem_max_range_m(terrain: np.ndarray, cell_m: float, radar_ix: int, radar_iz: int) -> float:
    height, width = terrain.shape
    dx = max(radar_ix, width - 1 - radar_ix) * cell_m
    dz = max(radar_iz, height - 1 - radar_iz) * cell_m
    return float(math.hypot(dx, dz))


def render_preview(
    dem: dict,
    result,
    targets,
    output_path: str,
    cell_m: float,
    hardware,
) -> None:
    terrain = np.asarray(dem["terrain"])
    radar_ix = int(dem["_radar_ix"])
    radar_iz = int(dem["_radar_iz"])

    total_dbm = dbm_array(result.total_mti_w)
    clutter_dbm = dbm_array(result.clutter_mti_w)
    target_dbm = dbm_array(result.target_mti_w)
    noise_dbm = watts_to_dbm(result.noise_proc_w)
    snr_db = 10.0 * np.log10(np.maximum(result.snr_mti, 1e-30))

    naz, nbin = total_dbm.shape
    if naz > 1:
        d_az = float(result.az_rad[1] - result.az_rad[0])
    else:
        d_az = math.radians(1.0)
    theta_edges = np.concatenate(
        [result.az_rad - 0.5 * d_az, [result.az_rad[-1] + 0.5 * d_az]]
    )
    if nbin > 1:
        dr = float(result.range_centers_m[1] - result.range_centers_m[0])
        mid = 0.5 * (result.range_centers_m[:-1] + result.range_centers_m[1:])
        r_edges = np.concatenate(
            [
                [result.range_centers_m[0] - 0.5 * dr],
                mid,
                [result.range_centers_m[-1] + 0.5 * dr],
            ]
        )
    else:
        dr = float(hardware.range_bin_m())
        r_edges = np.array(
            [
                result.range_centers_m[0] - 0.5 * dr,
                result.range_centers_m[0] + 0.5 * dr,
            ]
        )

    fig = plt.figure(figsize=(14, 11), constrained_layout=True)
    gs = fig.add_gridspec(2, 2)

    # PPI: post-MTI SNR; CFAR hits as cyan markers (no fake target gain).
    ax_ppi = fig.add_subplot(gs[0, 0], projection="polar")
    vmax = float(np.percentile(snr_db, 99.0))
    vmin = float(np.percentile(snr_db, 5.0))
    if vmax <= vmin + 1.0:
        vmax = vmin + 20.0
    mesh = ax_ppi.pcolormesh(
        theta_edges,
        r_edges / 1000.0,
        snr_db.T,
        cmap="inferno",
        shading="flat",
        vmin=vmin,
        vmax=vmax,
    )
    # CFAR detections
    det_az, det_bin = np.where(result.cfar_mask)
    if det_az.size > 0:
        ax_ppi.scatter(
            result.az_rad[det_az],
            result.range_centers_m[det_bin] / 1000.0,
            s=8,
            c="cyan",
            alpha=0.7,
            label="CFAR",
        )
    for tgt in targets:
        dx = (tgt.ix - radar_ix) * cell_m
        dz = (tgt.iz - radar_iz) * cell_m
        dy = tgt.y_m - result.radar_y
        rng = math.sqrt(dx * dx + dy * dy + dz * dz)
        az = math.atan2(dz, dx)
        ax_ppi.plot(az, rng / 1000.0, marker="o", markersize=7, markerfacecolor="none",
                    markeredgecolor="lime", markeredgewidth=1.5)
    ax_ppi.set_title(
        f"PPI SNR after MTI + CFAR hits\n"
        f"DEM Rmax={result.dem_range_limit_m/1000:.1f} km / "
        f"instr {result.instrumented_range_m/1000:.0f} km"
    )
    fig.colorbar(mesh, ax=ax_ppi, pad=0.1, label="SNR (dB)")

    ax_ascope = fig.add_subplot(gs[0, 1])
    ax_ascope.axhline(noise_dbm, color="#44aa44", linestyle=":", label=f"N0 {noise_dbm:.1f} dBm")
    ax_ascope.plot(
        result.range_centers_m / 1000.0,
        np.mean(clutter_dbm, axis=0),
        color="#888888",
        label="mean clutter MTI",
    )
    ax_ascope.plot(
        result.range_centers_m / 1000.0,
        np.max(total_dbm, axis=0),
        color="#ff6600",
        label="max-hold total MTI",
    )
    ax_ascope.plot(
        result.range_centers_m / 1000.0,
        np.max(target_dbm, axis=0),
        color="#00c2ff",
        linestyle="--",
        label="peak target MTI",
    )
    # CFAR rate vs range
    cfar_rate = 100.0 * np.mean(result.cfar_mask.astype(np.float64), axis=0)
    ax_ascope2 = ax_ascope.twinx()
    ax_ascope2.plot(
        result.range_centers_m / 1000.0,
        cfar_rate,
        color="#aa66ff",
        alpha=0.5,
        label="CFAR hit %",
    )
    ax_ascope2.set_ylabel("CFAR hit rate (%)")
    ax_ascope.set_xlabel("Range (km)")
    ax_ascope.set_ylabel("Power (dBm)")
    ax_ascope.set_title("A-scope (absolute, post PC/int/MTI)")
    ax_ascope.grid(True, alpha=0.3)
    lines1, labels1 = ax_ascope.get_legend_handles_labels()
    lines2, labels2 = ax_ascope2.get_legend_handles_labels()
    ax_ascope.legend(lines1 + lines2, labels1 + labels2, loc="upper right", fontsize=7)

    ax_map = fig.add_subplot(gs[1, 0])
    extent_km = [
        0,
        terrain.shape[1] * cell_m / 1000.0,
        0,
        terrain.shape[0] * cell_m / 1000.0,
    ]
    ax_map.imshow(terrain, origin="lower", extent=extent_km, cmap="terrain")
    vis = np.ma.masked_where(result.visible_map == 0, result.visible_map)
    ax_map.imshow(
        vis,
        origin="lower",
        extent=extent_km,
        cmap=matplotlib.colors.ListedColormap(["#ffea00"]),
        alpha=0.45,
    )
    ax_map.plot(
        radar_ix * cell_m / 1000.0,
        radar_iz * cell_m / 1000.0,
        "r^",
        markersize=10,
    )
    for tgt in targets:
        ax_map.plot(
            tgt.ix * cell_m / 1000.0,
            tgt.iz * cell_m / 1000.0,
            "wo",
            markersize=5,
            markeredgecolor="k",
        )
        ax_map.text(
            tgt.ix * cell_m / 1000.0,
            tgt.iz * cell_m / 1000.0,
            tgt.name,
            color="white",
            fontsize=7,
            ha="left",
            va="bottom",
        )
    ax_map.set_title("Terrain LOS / truth targets")
    ax_map.set_xlabel("X (km)")
    ax_map.set_ylabel("Z (km)")

    ax_txt = fig.add_subplot(gs[1, 1])
    ax_txt.axis("off")
    lines = summarize_hardware(hardware)
    lines.append(f"  sigma0={result.sigma0_source}")
    lines.append(
        f"  sim R={result.range_centers_m[-1]/1000:.1f} km  "
        f"(DEM cap {result.dem_range_limit_m/1000:.1f} km)"
    )
    lines.append(
        f"  noise RF={watts_to_dbm(result.noise_w):.1f} dBm  "
        f"proc={noise_dbm:.1f} dBm"
    )
    lines.append(f"  CFAR hits={int(result.cfar_mask.sum())} cells")
    lines.append("")
    lines.append("Targets (R, vr, fd, SNRcell, CFAR):")
    for tgt in targets:
        dx = (tgt.ix - radar_ix) * cell_m
        dz = (tgt.iz - radar_iz) * cell_m
        dy = tgt.y_m - result.radar_y
        rng = math.sqrt(dx * dx + dy * dy + dz * dz)
        v_rad = radial_speed_toward_radar(
            tgt, float(radar_ix), float(radar_iz), result.radar_y, cell_m
        )
        fd = doppler_hz(v_rad, hardware.wavelength_m)
        az = math.atan2(dz, dx)
        diffs = (result.az_rad - az + math.pi) % (2 * math.pi) - math.pi
        az_i = int(np.argmin(np.abs(diffs)))
        bin_i = int(rng / dr) if dr > 0 else 0
        if bin_i < 0:
            bin_i = 0
        if bin_i >= nbin:
            bin_i = nbin - 1
        snr_t = 10.0 * math.log10(
            max(float(result.target_mti_w[az_i, bin_i]) / max(result.noise_proc_w, 1e-30), 1e-30)
        )
        hit = bool(result.cfar_mask[az_i, bin_i])
        lines.append(
            f"  {tgt.name}: R={rng/1000:.2f}km vr={v_rad:.0f} "
            f"fd={fd:.0f}Hz SNR={snr_t:.1f}dB CFAR={hit}"
        )
    lines.append("")
    lines.append("Chain: Pr(RF) -> PC*int -> MTI -> CA-CFAR")
    lines.append("No artificial display gain.")
    ax_txt.text(0.0, 1.0, "\n".join(lines), va="top", family="monospace", fontsize=7.5)

    fig.suptitle(
        f"{hardware.display_name} — Cold-War waveform chain on RDF DEM",
        fontsize=13,
    )
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    fig.savefig(output_path, dpi=130)
    plt.close(fig)
    print(f"wrote: {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", default="GM_Eden")
    parser.add_argument("--tile-dir", default="")
    parser.add_argument("--output", default="")
    parser.add_argument("--max-range-m", type=float, default=0.0,
                        help="0 = min(DEM edge, instrumented)")
    parser.add_argument("--range-bin-m", type=float, default=0.0, help="0 = from waveform")
    parser.add_argument("--azimuth-count", type=int, default=180)
    parser.add_argument("--az-center", type=float, default=0.0)
    parser.add_argument("--az-width", type=float, default=360.0)
    parser.add_argument("--radar-height-m", type=float, default=25.0)
    parser.add_argument(
        "--preset",
        default="shorad",
        choices=["p18", "tps43", "shorad", "ad", "artillery", "airborne"],
    )
    parser.add_argument("--band", default="", help="override sigma0 band")
    parser.add_argument("--sea-state", type=int, default=3)
    parser.add_argument("--sigma0-calib", default="")
    parser.add_argument("--write-sigma0-calib", default="")
    parser.add_argument("--no-mti", action="store_true")
    parser.add_argument("--no-spans", action="store_true")
    parser.add_argument("--no-targets", action="store_true")
    args = parser.parse_args()

    if args.write_sigma0_calib:
        band = args.band if args.band else "X"
        table = Sigma0Table.builtin(band, args.sea_state)
        table.write_calibration_template(args.write_sigma0_calib)
        print(f"wrote sigma0 calibration template: {args.write_sigma0_calib}")
        return

    hardware = get_preset(args.preset)
    if args.no_mti:
        hardware.enable_mti = False

    if args.sigma0_calib:
        table = Sigma0Table.from_json(args.sigma0_calib)
    else:
        band = args.band if args.band else hardware.band
        if band == "VHF":
            pass
        table = Sigma0Table.builtin(band if band in ("X", "C", "S", "L", "VHF") else "X",
                                    args.sea_state)
    set_default_table(table)

    tile_dir = args.tile_dir if args.tile_dir else default_tile_dir(args.world)
    dem = load_dem(tile_dir, load_spans=not args.no_spans)
    terrain = np.asarray(dem["terrain"])
    cell_m = float(dem["cell_m"])
    radar_iz, radar_ix = choose_radar_site(terrain)
    dem["_radar_ix"] = radar_ix
    dem["_radar_iz"] = radar_iz

    dem_cap = _dem_max_range_m(terrain, cell_m, radar_ix, radar_iz)
    if args.max_range_m > 0.0:
        max_range = args.max_range_m
    else:
        max_range = min(dem_cap, hardware.instrumented_range_m)
    # Never exceed DEM geometry.
    if max_range > dem_cap:
        max_range = dem_cap

    if args.range_bin_m > 0.0:
        range_bin = args.range_bin_m
    else:
        range_bin = hardware.range_bin_m()

    config = SectorConfig(
        radar_ix=radar_ix,
        radar_iz=radar_iz,
        radar_height_m=args.radar_height_m,
        max_range_m=max_range,
        range_bin_m=range_bin,
        az_center_rad=math.radians(args.az_center),
        az_halfwidth_rad=math.radians(args.az_width) * 0.5,
        azimuth_count=args.azimuth_count,
        use_spans=not args.no_spans,
        hardware=hardware,
        sigma0_table=table,
    )

    if args.no_targets:
        targets = []
    else:
        targets = default_scenario_targets(terrain, cell_m, radar_ix, radar_iz)

    print(f"loading DEM from {tile_dir}")
    print(f"preset={hardware.name} ({hardware.display_name})")
    print(f"DEM cap={dem_cap/1000:.1f} km  sim R={max_range/1000:.1f} km  "
          f"instr={hardware.instrumented_range_m/1000:.0f} km")
    print(f"PRF={hardware.prf_hz:.0f} Hz  tau={hardware.pulse_width_s*1e6:.2f} us  "
          f"Gpc={10*math.log10(hardware.pulse_compression_gain):.1f} dB  "
          f"MTI={hardware.enable_mti}")

    result = simulate_sector(dem, config, targets)

    output = args.output
    if not output:
        output = os.path.join(_HERE, "out", f"{args.world}_radar_{args.preset}.png")
    # Also refresh legacy filename so open tabs aren't stuck on old display-gain art.
    if args.preset in ("shorad", "ad"):
        legacy = os.path.join(_HERE, "out", f"{args.world}_radar_sector_preview.png")
        render_preview(dem, result, targets, legacy, cell_m, hardware)

    render_preview(dem, result, targets, output, cell_m, hardware)

    print(f"noise proc: {watts_to_dbm(result.noise_proc_w):.1f} dBm")
    print(f"clutter MTI peak: {watts_to_dbm(float(np.max(result.clutter_mti_w))):.1f} dBm")
    print(f"target MTI peak: {watts_to_dbm(float(np.max(result.target_mti_w))):.1f} dBm")
    print(f"CFAR hits: {int(result.cfar_mask.sum())}")
    print(f"peak SNR: {10.0 * math.log10(max(float(np.max(result.snr_mti)), 1e-30)):.1f} dB")


if __name__ == "__main__":
    main()
