#!/usr/bin/env python3
"""Demonstrate RDF scan(t), moving tracks, multi-beam, and optional EW plugins."""

from __future__ import annotations

import argparse
import csv
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
from rdf_radar_ew import (
    DeceptionJammer,
    EWStack,
    FalseTarget,
    FrequencyHopSchedule,
    NoiseJammer,
)
from rdf_radar_materials import Sigma0Table
from rdf_radar_physics import dbm_array, get_preset, summarize_hardware
from rdf_radar_scan import ScanConfig, simulate_scan
from rdf_radar_sector_sim import SectorConfig, simulate_sector
from rdf_radar_targets import default_scenario_targets, trajectories_from_targets


def _max_dem_range(
    terrain: np.ndarray,
    cell_m: float,
    radar_ix: int,
    radar_iz: int,
) -> float:
    height, width = terrain.shape
    dx = max(radar_ix, width - 1 - radar_ix) * cell_m
    dz = max(radar_iz, height - 1 - radar_iz) * cell_m
    return math.hypot(dx, dz)


def _write_detections(path: str, history) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "time_s",
                "scan",
                "target",
                "range_m",
                "azimuth_deg",
                "elevation_deg",
                "radial_speed_m_s",
                "doppler_hz",
                "beam",
                "snr_db",
                "cfar_detected",
                "frequency_hz",
                "rcs_instant_m2",
                "multipath_factor",
            ]
        )
        for detection in history.detections:
            writer.writerow(
                [
                    f"{detection.time_s:.3f}",
                    detection.scan_number,
                    detection.target_name,
                    f"{detection.measured_range_m:.3f}",
                    f"{detection.azimuth_deg:.3f}",
                    f"{detection.elevation_deg:.3f}",
                    f"{detection.radial_speed_m_s:.3f}",
                    f"{detection.doppler_hz:.3f}",
                    detection.beam_name,
                    f"{detection.snr_db:.3f}",
                    int(detection.detected),
                    f"{detection.frequency_hz:.0f}",
                    f"{detection.rcs_instant_m2:.4f}",
                    f"{detection.multipath_factor:.4f}",
                ]
            )


def _write_tracks(path: str, history) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "track_id",
                "time_s",
                "range_m",
                "azimuth_deg",
                "range_rate_m_s",
                "hits",
                "misses",
                "last_target_name",
                "snr_db",
            ]
        )
        if history.tracker is None:
            return
        for track in history.tracker.tracks:
            writer.writerow(
                [
                    track.track_id,
                    f"{track.time_s:.3f}",
                    f"{track.range_m:.3f}",
                    f"{track.azimuth_deg:.3f}",
                    f"{track.range_rate_m_s:.3f}",
                    track.hit_count,
                    track.miss_count,
                    track.last_target_name,
                    f"{track.snr_db:.3f}",
                ]
            )


def _render(path: str, hardware, sector, history, ew_names: list[str]) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(14, 10), constrained_layout=True)

    # Mechanical scan timing.
    axes[0, 0].plot(
        history.times_s,
        np.degrees(history.boresight_rad) % 360.0,
        color="#ff8800",
    )
    axes[0, 0].set_title("scan(t): mechanical/electronic boresight")
    axes[0, 0].set_xlabel("Time (s)")
    axes[0, 0].set_ylabel("Azimuth (deg)")
    axes[0, 0].grid(True, alpha=0.3)

    # Moving truth tracks in range vs time, with CFAR detections overlaid.
    for target_name in history.target_truth:
        samples = history.target_truth[target_name]
        times = [sample[0] for sample in samples]
        # Truth map stores X/Z; measurement range is plotted from beam intercepts.
        measured = [
            detection
            for detection in history.detections
            if detection.target_name == target_name
        ]
        if measured:
            axes[0, 1].plot(
                [d.time_s for d in measured],
                [d.measured_range_m / 1000.0 for d in measured],
                marker=".",
                linewidth=1.0,
                label=target_name,
            )
            detected = [d for d in measured if d.detected]
            if detected:
                axes[0, 1].scatter(
                    [d.time_s for d in detected],
                    [d.measured_range_m / 1000.0 for d in detected],
                    marker="o",
                    facecolors="none",
                    edgecolors="lime",
                    s=50,
                )
        elif times:
            axes[0, 1].plot([], [], label=target_name)
    axes[0, 1].set_title("Moving target intercepts (green ring = CFAR)")
    axes[0, 1].set_xlabel("Time (s)")
    axes[0, 1].set_ylabel("Slant range (km)")
    axes[0, 1].grid(True, alpha=0.3)
    axes[0, 1].legend(fontsize=8)

    # Persistent PPI / track memory.
    power_dbm = dbm_array(history.accumulated_power_w)
    extent = [
        0.0,
        sector.range_centers_m[-1] / 1000.0,
        0.0,
        360.0,
    ]
    image = axes[1, 0].imshow(
        power_dbm,
        origin="lower",
        aspect="auto",
        extent=extent,
        cmap="inferno",
    )
    det_az, det_range = np.where(history.accumulated_cfar)
    if det_az.size > 0:
        axes[1, 0].scatter(
            sector.range_centers_m[det_range] / 1000.0,
            np.degrees(sector.az_rad[det_az]) % 360.0,
            s=8,
            c="cyan",
            label="CFAR",
        )
    if history.tracker is not None:
        for track in history.tracker.tracks:
            if len(track.history) < 1:
                continue
            axes[1, 0].plot(
                [h[1] / 1000.0 for h in track.history],
                [h[2] % 360.0 for h in track.history],
                marker="x",
                linewidth=1.2,
                label=f"T{track.track_id}",
            )
    axes[1, 0].set_title("PPI memory + alpha-beta tracks")
    axes[1, 0].set_xlabel("Range (km)")
    axes[1, 0].set_ylabel("Azimuth (deg)")
    fig.colorbar(image, ax=axes[1, 0], label="Post-EW power (dBm)")

    # Framework / beam / EW / hop / track summary.
    axes[1, 1].axis("off")
    hit_count = sum(1 for detection in history.detections if detection.detected)
    beam_counts: dict[str, int] = {}
    for detection in history.detections:
        beam_counts[detection.beam_name] = beam_counts.get(detection.beam_name, 0) + 1
    track_count = 0
    confirmed = 0
    if history.tracker is not None:
        track_count = len(history.tracker.tracks)
        confirmed = sum(1 for t in history.tracker.tracks if t.hit_count >= 2)
    freq_mhz = sorted({round(f / 1e6, 1) for f in history.frequencies_hz.tolist()})
    mp_vals = [d.multipath_factor for d in history.detections]
    mp_min = min(mp_vals) if mp_vals else 1.0
    mp_max = max(mp_vals) if mp_vals else 1.0
    lines = ["RDF modular radar framework", ""]
    lines.extend(summarize_hardware(hardware))
    lines.extend(
        [
            "",
            f"duration={history.times_s[-1]:.1f}s scans={int(history.scan_number[-1]) + 1}",
            f"beam intercepts={len(history.detections)} CFAR target hits={hit_count}",
            f"tracks={track_count} confirmed(>=2 hits)={confirmed}",
            f"beam usage={beam_counts}",
            f"EW plugins={ew_names if ew_names else ['none']}",
            f"hop channels_MHz={freq_mhz}",
            f"multipath factor range=[{mp_min:.2f},{mp_max:.2f}]",
            "",
            "Contracts:",
            "  RadarHardware + ElevationBeam[]",
            "  TargetTrajectory.sample(t)",
            "  hardware_at_frequency(f) / hop",
            "  MultipathModel + SwerlingModel",
            "  EWEffect.apply(power, context)",
            "  associate_and_filter(detections)",
            "",
            "Presets are examples; core is era/technology agnostic.",
        ]
    )
    axes[1, 1].text(
        0.0,
        1.0,
        "\n".join(lines),
        va="top",
        family="monospace",
        fontsize=7.5,
    )

    fig.suptitle(
        f"RDF time-domain framework demo — {hardware.display_name}",
        fontsize=13,
    )
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print(f"wrote: {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--preset",
        default="shorad",
        choices=["p18", "tps43", "shorad", "artillery", "airborne"],
    )
    parser.add_argument("--world", default="GM_Eden")
    parser.add_argument("--duration-s", type=float, default=0.0)
    parser.add_argument("--azimuth-count", type=int, default=180)
    parser.add_argument("--ew", action="store_true")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    hardware = get_preset(args.preset)
    tile_dir = default_tile_dir(args.world)
    dem = load_dem(tile_dir, load_spans=True)
    terrain = np.asarray(dem["terrain"])
    cell_m = float(dem["cell_m"])
    radar_iz, radar_ix = choose_radar_site(terrain)
    dem["_radar_ix"] = radar_ix
    dem["_radar_iz"] = radar_iz

    max_range = min(
        _max_dem_range(terrain, cell_m, radar_ix, radar_iz),
        hardware.instrumented_range_m,
    )
    sigma0 = Sigma0Table.builtin(hardware.band, 3)
    sector_config = SectorConfig(
        radar_ix=radar_ix,
        radar_iz=radar_iz,
        radar_height_m=25.0,
        max_range_m=max_range,
        range_bin_m=hardware.range_bin_m(),
        azimuth_count=args.azimuth_count,
        hardware=hardware,
        sigma0_table=sigma0,
    )
    static_sector = simulate_sector(dem, sector_config, [])
    targets = default_scenario_targets(terrain, cell_m, radar_ix, radar_iz)
    trajectories = trajectories_from_targets(targets)

    ew_stack = EWStack()
    hop = FrequencyHopSchedule()
    if args.ew:
        jammer_x = (radar_ix + 1000) * cell_m
        jammer_z = (radar_iz + 600) * cell_m
        # Fixed jammer center: hopping can walk partially out of band.
        jam_center = hardware.frequency_hz
        ew_stack = EWStack(
            effects=[
                NoiseJammer(
                    x_m=jammer_x,
                    z_m=jammer_z,
                    erp_w=1.0e4,
                    bandwidth_hz=20.0e6,
                    center_frequency_hz=jam_center,
                ),
                DeceptionJammer(
                    false_targets=[
                        FalseTarget(
                            range_m=3500.0,
                            azimuth_deg=65.0,
                            power_w=1.0e-10,
                            range_rate_m_s=40.0,
                        )
                    ]
                ),
            ]
        )
        hop = FrequencyHopSchedule(
            channels_hz=[
                hardware.frequency_hz - 40.0e6,
                hardware.frequency_hz,
                hardware.frequency_hz + 40.0e6,
            ],
            dwell_s=0.2,
        )

    duration = args.duration_s
    if duration <= 0.0:
        if hardware.scan_period_s < 1000.0:
            duration = hardware.scan_period_s * 2.0
        else:
            duration = 10.0
    scan_config = ScanConfig(
        duration_s=duration,
        ew_stack=ew_stack,
        frequency_hop=hop,
    )
    history = simulate_scan(
        dem,
        hardware,
        static_sector,
        trajectories,
        scan_config,
    )

    output = args.output
    if not output:
        suffix = "ew" if args.ew else "clean"
        output = os.path.join(
            _HERE,
            "out",
            f"{args.world}_framework_{args.preset}_{suffix}.png",
        )
    csv_path = os.path.splitext(output)[0] + "_detections.csv"
    track_path = os.path.splitext(output)[0] + "_tracks.csv"
    _write_detections(csv_path, history)
    _write_tracks(track_path, history)
    _render(output, hardware, static_sector, history, ew_stack.names())
    print(f"wrote: {csv_path}")
    print(f"wrote: {track_path}")
    if history.tracker is not None:
        print(
            f"tracks={len(history.tracker.tracks)} "
            f"confirmed={sum(1 for t in history.tracker.tracks if t.hit_count >= 2)}"
        )


if __name__ == "__main__":
    main()
