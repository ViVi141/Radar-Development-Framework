#!/usr/bin/env python3
"""Large-scale air-defense + artillery (WLR) radar battle simulation.

Uses RDF Python physics / Swerling / measurement synthesis / alpha-beta tracks.
Optionally loads the baked Enforce signature CSV for mean RCS priors.

Produces:
  tools/dem/out/mass_battle_summary.json
  tools/dem/out/mass_battle_detections.csv
  tools/dem/out/mass_battle_tracks.csv
  tools/dem/out/mass_battle_overview.png
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys
from dataclasses import dataclass, field

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_dem_io import choose_radar_site, default_tile_dir, load_dem, resolve_dem_source
from rdf_radar_channel import MultipathModel, SwerlingModel, aspect_factor_3d
from rdf_radar_physics import (
    RadarHardware,
    doppler_hz,
    elevation_beam_gains,
    get_preset,
    lin_to_db,
    mti_apply_target,
    processed_power_w,
    received_power_w,
    summarize_hardware,
)
from rdf_radar_targets import (
    AIR_DRAG_SHELL_82MM_HE,
    GRAVITY_M_S2,
    NO_WIND,
    RCS_ARTILLERY_SHELL_M2,
    RCS_FIGHTER_M2,
    RCS_HELICOPTER_M2,
    RCS_TRANSPORT_M2,
    GlobalWind,
    GroundYFn,
    RadarTarget,
    TargetTrajectory,
    dem_ground_y_fn,
    find_ground_intersection,
    flat_ground_y_fn,
    math_hypot3,
    radial_speed_toward_radar,
    random_global_wind,
    solve_launch_and_impact,
)
from rdf_radar_track import (
    TrackerConfig,
    associate_and_filter,
    fit_ballistic_vacuum,
    predict_ballistic,
)


# ---------------------------------------------------------------------------
# Signature table (Enforce bake CSV)
# ---------------------------------------------------------------------------


@dataclass
class SignaturePriors:
    fighter_rcs: float = RCS_FIGHTER_M2
    heli_rcs: float = RCS_HELICOPTER_M2
    transport_rcs: float = RCS_TRANSPORT_M2
    shell_rcs: float = RCS_ARTILLERY_SHELL_M2
    loaded_rows: int = 0
    source: str = "builtin"


def _default_sig_path() -> str:
    home = os.path.expanduser("~")
    return os.path.join(
        home,
        "Documents",
        "My Games",
        "ArmaReforgerWorkbench",
        "profile",
        "RDF",
        "Signatures",
        "rdf_radar_signatures.csv",
    )


def load_signature_priors(path: str) -> SignaturePriors:
    priors = SignaturePriors()
    if not path or not os.path.isfile(path):
        return priors

    vehicles: list[float] = []
    helios: list[float] = []
    shells: list[float] = []
    with open(path, "r", encoding="utf-8") as handle:
        lines = [ln.strip() for ln in handle if ln.strip()]
    if len(lines) < 3 or lines[0] != "RDF_RADAR_SIG_V2":
        return priors

    for row in lines[2:]:
        # "key",sx,sy,sz,cl,rcs,sw,th
        if not row.startswith('"'):
            continue
        end = row.find('"', 1)
        if end < 0:
            continue
        key = row[1:end]
        rest = row[end + 2 :].split(",")
        if len(rest) < 7:
            continue
        try:
            rcs = float(rest[4])
            type_hint = int(rest[6])
        except ValueError:
            continue
        if rcs <= 0.0:
            continue
        priors.loaded_rows += 1
        key_l = key.lower()
        if type_hint == 1 or "ammo" in key_l or "rocket" in key_l:
            if rcs >= 0.005:
                shells.append(rcs)
            continue
        if "heli" in key_l or "mi8" in key_l or "uh1" in key_l:
            helios.append(rcs)
            continue
        if "vehicle" in key_l:
            vehicles.append(rcs)

    if helios:
        priors.heli_rcs = float(np.median(helios))
    if shells:
        priors.shell_rcs = float(np.median(shells))
    if vehicles:
        med = float(np.median(vehicles))
        # Split wheeled median into fighter/transport order-of-magnitude slots.
        priors.fighter_rcs = max(1.0, med * 0.6)
        priors.transport_rcs = max(priors.fighter_rcs, med * 1.8)
    priors.source = path
    return priors


# ---------------------------------------------------------------------------
# Measurement synthesis (mirrors Enforce RDF_RadarMeasurement)
# ---------------------------------------------------------------------------


@dataclass
class SynthPlot:
    time_s: float
    scan_number: int
    target_name: str
    kind: str
    true_range_m: float
    measured_range_m: float
    azimuth_deg: float
    elevation_deg: float
    radial_speed_m_s: float
    snr_db: float
    detected: bool
    rcs_instant_m2: float
    radar_name: str


def _angle_diff_deg(a: float, b: float) -> float:
    return (a - b + 180.0) % 360.0 - 180.0


def synthesize_measurement(
    hardware: RadarHardware,
    true_range_m: float,
    true_az_deg: float,
    true_el_deg: float,
    true_radial_m_s: float,
    snr_db: float,
    rng: np.random.Generator,
) -> tuple[float, float, float, float]:
    snr_lin = max(10.0 ** (snr_db / 10.0), 1.0)
    denom = 1.6 * math.sqrt(2.0 * snr_lin)
    if denom < 0.001:
        denom = 0.001

    range_bin = hardware.range_bin_m()
    range_sigma = range_bin / denom
    qbin = math.floor(true_range_m / range_bin)
    if qbin < 0:
        qbin = 0
    quantized = (qbin + 0.5) * range_bin
    meas_range = quantized + float(rng.normal(0.0, range_sigma))
    if meas_range < 50.0:
        meas_range = 50.0
    if meas_range > hardware.instrumented_range_m:
        meas_range = hardware.instrumented_range_m

    az_sigma = hardware.az_beamwidth_deg / denom
    el_bw = hardware.el_beamwidth_deg
    if hardware.elevation_beams:
        # Use strongest beam width at this elevation.
        best_g = -1.0
        for beam in hardware.elevation_beams:
            off = true_el_deg - beam.boresight_deg
            g = math.exp(-2.772588722239781 * (off / max(beam.beamwidth_deg, 0.1)) ** 2)
            if g > best_g:
                best_g = g
                el_bw = beam.beamwidth_deg
    el_sigma = el_bw / denom
    meas_az = true_az_deg + float(rng.normal(0.0, az_sigma))
    meas_el = true_el_deg + float(rng.normal(0.0, el_sigma))

    wavelength = hardware.wavelength_m
    true_doppler = doppler_hz(true_radial_m_s, wavelength)
    obs_s = max(hardware.pulses_integrated, 1) / max(hardware.prf_hz, 1.0)
    doppler_sigma = 1.0 / (obs_s * denom) if obs_s > 1e-6 else 0.0
    meas_doppler = true_doppler + float(rng.normal(0.0, doppler_sigma))
    meas_radial = meas_doppler * wavelength * 0.5
    return meas_range, meas_az, meas_el, meas_radial


# ---------------------------------------------------------------------------
# Scenario
# ---------------------------------------------------------------------------


def build_mass_scenario(
    priors: SignaturePriors,
    cell_m: float,
    radar_ix: float,
    radar_iz: float,
    radar_y: float,
    seed: int,
    wind: GlobalWind = NO_WIND,
    ground_y_fn: GroundYFn | None = None,
) -> list[TargetTrajectory]:
    rng = np.random.default_rng(seed)
    trajectories: list[TargetTrajectory] = []
    if ground_y_fn is None:
        ground_y_fn = flat_ground_y_fn(radar_y)

    # --- Air defense raid: fighters inbound from multiple sectors ---
    for i in range(12):
        bearing = math.radians(15.0 + i * 28.0 + float(rng.uniform(-5, 5)))
        dist_m = float(rng.uniform(8000.0, 18000.0))
        speed = float(rng.uniform(180.0, 260.0))
        agl = float(rng.uniform(800.0, 4500.0))
        ix = radar_ix + (dist_m * math.cos(bearing)) / cell_m
        iz = radar_iz + (dist_m * math.sin(bearing)) / cell_m
        # Velocity toward radar.
        ux = (radar_ix - ix) * cell_m
        uz = (radar_iz - iz) * cell_m
        horiz = math.hypot(ux, uz)
        vx = speed * ux / horiz if horiz > 1.0 else 0.0
        vz = speed * uz / horiz if horiz > 1.0 else 0.0
        trajectories.append(
            TargetTrajectory(
                initial=RadarTarget(
                    name=f"fighter_{i:02d}",
                    ix=ix,
                    iz=iz,
                    y_m=radar_y + agl,
                    rcs_m2=priors.fighter_rcs * float(rng.uniform(0.7, 1.3)),
                    kind="aircraft",
                    vx_m_s=vx,
                    vy_m_s=float(rng.uniform(-5.0, 5.0)),
                    vz_m_s=vz,
                ),
                start_time_s=float(rng.uniform(0.0, 8.0)),
            )
        )

    # --- Helicopters NOE ---
    for i in range(6):
        bearing = math.radians(40.0 + i * 50.0)
        dist_m = float(rng.uniform(2500.0, 6000.0))
        speed = float(rng.uniform(35.0, 55.0))
        ix = radar_ix + (dist_m * math.cos(bearing)) / cell_m
        iz = radar_iz + (dist_m * math.sin(bearing)) / cell_m
        ux = (radar_ix - ix) * cell_m
        uz = (radar_iz - iz) * cell_m
        horiz = math.hypot(ux, uz)
        vx = speed * ux / horiz if horiz > 1.0 else 0.0
        vz = speed * uz / horiz if horiz > 1.0 else 0.0
        trajectories.append(
            TargetTrajectory(
                initial=RadarTarget(
                    name=f"heli_{i:02d}",
                    ix=ix,
                    iz=iz,
                    y_m=radar_y + float(rng.uniform(40.0, 120.0)),
                    rcs_m2=priors.heli_rcs * float(rng.uniform(0.8, 1.2)),
                    kind="heli",
                    vx_m_s=vx,
                    vy_m_s=0.0,
                    vz_m_s=vz,
                ),
                start_time_s=float(rng.uniform(0.0, 5.0)),
            )
        )

    # --- Transports ---
    for i in range(4):
        bearing = math.radians(90.0 + i * 40.0)
        dist_m = float(rng.uniform(10000.0, 22000.0))
        speed = float(rng.uniform(120.0, 160.0))
        ix = radar_ix + (dist_m * math.cos(bearing)) / cell_m
        iz = radar_iz + (dist_m * math.sin(bearing)) / cell_m
        ux = (radar_ix - ix) * cell_m
        uz = (radar_iz - iz) * cell_m
        horiz = math.hypot(ux, uz)
        vx = speed * ux / horiz if horiz > 1.0 else 0.0
        vz = speed * uz / horiz if horiz > 1.0 else 0.0
        trajectories.append(
            TargetTrajectory(
                initial=RadarTarget(
                    name=f"transport_{i:02d}",
                    ix=ix,
                    iz=iz,
                    y_m=radar_y + float(rng.uniform(2000.0, 6000.0)),
                    rcs_m2=priors.transport_rcs * float(rng.uniform(0.8, 1.2)),
                    kind="aircraft",
                    vx_m_s=vx,
                    vy_m_s=-2.0,
                    vz_m_s=vz,
                ),
                start_time_s=float(rng.uniform(0.0, 10.0)),
            )
        )

    # --- Artillery barrage: ballistic shells toward impact near radar ---
    # Truth uses Reforger-style AirDrag (82mm HE O832DU prior) + gravity.
    shell_drag = AIR_DRAG_SHELL_82MM_HE
    for i in range(40):
        launch_bearing = math.radians(200.0 + float(rng.uniform(-35, 35)))
        launch_dist = float(rng.uniform(12000.0, 22000.0))
        impact_offset = float(rng.uniform(400.0, 2500.0))
        impact_bearing = math.radians(float(rng.uniform(0, 360)))
        launch_ix = radar_ix + (launch_dist * math.cos(launch_bearing)) / cell_m
        launch_iz = radar_iz + (launch_dist * math.sin(launch_bearing)) / cell_m
        impact_ix = radar_ix + (impact_offset * math.cos(impact_bearing)) / cell_m
        impact_iz = radar_iz + (impact_offset * math.sin(impact_bearing)) / cell_m
        dx = (impact_ix - launch_ix) * cell_m
        dz = (impact_iz - launch_iz) * cell_m
        horiz = math.hypot(dx, dz)
        flight_s = float(rng.uniform(28.0, 48.0))
        elev = math.radians(float(rng.uniform(35.0, 55.0)))
        # Vacuum launch estimate; drag shortens range so slightly overspeed.
        v0 = horiz / (flight_s * max(math.cos(elev), 0.2)) * 1.12
        vx = v0 * math.cos(elev) * dx / max(horiz, 1.0)
        vz = v0 * math.cos(elev) * dz / max(horiz, 1.0)
        vy = v0 * math.sin(elev)
        launch_x_m = (launch_ix - radar_ix) * cell_m
        launch_z_m = (launch_iz - radar_iz) * cell_m
        launch_ground = ground_y_fn(launch_x_m, launch_z_m)
        trajectories.append(
            TargetTrajectory(
                initial=RadarTarget(
                    name=f"shell_{i:02d}",
                    ix=launch_ix,
                    iz=launch_iz,
                    y_m=launch_ground + 2.0,
                    rcs_m2=priors.shell_rcs * float(rng.uniform(0.7, 1.4)),
                    kind="shell",
                    vx_m_s=vx,
                    vy_m_s=vy,
                    vz_m_s=vz,
                ),
                start_time_s=float(rng.uniform(2.0, 25.0)),
                ay_m_s2=GRAVITY_M_S2,
                air_drag=shell_drag,
                wind=wind,
            )
        )

    return trajectories


# ---------------------------------------------------------------------------
# Plot-level radar (fast large-N; clutter as noise floor)
# ---------------------------------------------------------------------------


@dataclass
class RadarRunResult:
    radar_name: str
    hardware_name: str
    plots: list[SynthPlot] = field(default_factory=list)
    track_count: int = 0
    confirmed_tracks: int = 0
    detections: int = 0
    intercepts: int = 0
    by_kind_detected: dict[str, int] = field(default_factory=dict)
    by_kind_truth: dict[str, int] = field(default_factory=dict)
    track_rows: list[dict] = field(default_factory=list)
    tracker_tracks: list = field(default_factory=list)


def run_plot_level_radar(
    name: str,
    hardware: RadarHardware,
    trajectories: list[TargetTrajectory],
    cell_m: float,
    radar_ix: float,
    radar_iz: float,
    radar_y: float,
    duration_s: float,
    clutter_cnr_db: float,
    seed: int,
    kind_filter: set[str] | None = None,
    az_sector_center_deg: float | None = None,
    az_sector_half_deg: float = 180.0,
    detect_snr_db: float = 13.0,
    tracker_config: TrackerConfig | None = None,
) -> RadarRunResult:
    rng = np.random.default_rng(seed)
    result = RadarRunResult(radar_name=name, hardware_name=hardware.display_name)

    # Dwell cadence: mechanical RPM or fixed electronic revisit.
    if hardware.scan_rpm > 0.0:
        dwell_s = hardware.scan_period_s / max(360.0 / hardware.az_beamwidth_deg, 1.0)
        if dwell_s < 0.05:
            dwell_s = 0.05
    else:
        dwell_s = 0.25

    times = np.arange(0.0, duration_s + dwell_s, dwell_s)
    multipath = MultipathModel(enabled=True)
    swerling_air = SwerlingModel(model=1, seed=seed + 11)
    swerling_shell = SwerlingModel(model=0, seed=seed + 22)

    noise_w = hardware.noise_power_w()
    clutter_floor_w = noise_w * (10.0 ** (clutter_cnr_db / 10.0))
    # CFAR-ish: require processed SNR above threshold relative to noise+clutter.
    for time_s in times:
        if hardware.scan_rpm > 0.0:
            boresight_deg = (
                (hardware.scan_rpm / 60.0) * 360.0 * float(time_s)
            ) % 360.0
            scan_number = int(time_s / max(hardware.scan_period_s, 1e-6))
        else:
            boresight_deg = (
                az_sector_center_deg if az_sector_center_deg is not None else 0.0
            )
            # Staring radar: every dwell is a full revisit, so the tracker
            # must see one scan per dwell (one plot per target per scan).
            scan_number = int(round(time_s / dwell_s))

        for traj in trajectories:
            tgt = traj.sample(float(time_s), cell_m)
            if kind_filter is not None and tgt.kind not in kind_filter:
                continue
            if tgt.y_m < radar_y + 1.0:
                continue

            dx = (tgt.ix - radar_ix) * cell_m
            dy = tgt.y_m - radar_y
            dz = (tgt.iz - radar_iz) * cell_m
            range_m = math_hypot3(dx, dy, dz)
            if range_m < 80.0 or range_m > hardware.instrumented_range_m:
                continue

            az_deg = math.degrees(math.atan2(dz, dx)) % 360.0
            el_deg = math.degrees(math.atan2(dy, math.hypot(dx, dz)))

            if az_sector_center_deg is not None:
                if abs(_angle_diff_deg(az_deg, az_sector_center_deg)) > az_sector_half_deg:
                    continue

            # Beam gate (mechanical scan) or always-on sector stare.
            if hardware.scan_rpm > 0.0:
                if abs(_angle_diff_deg(az_deg, boresight_deg)) > 0.6 * hardware.az_beamwidth_deg:
                    continue

            beam_gains = elevation_beam_gains(hardware, el_deg)
            if not beam_gains:
                continue
            best_beam, el_gain = max(beam_gains.items(), key=lambda kv: kv[1])
            if el_gain < 0.05:
                continue

            result.by_kind_truth[tgt.kind] = result.by_kind_truth.get(tgt.kind, 0) + 1
            result.intercepts += 1

            sw = swerling_shell if tgt.kind == "shell" else swerling_air
            yaw_deg = math.degrees(math.atan2(tgt.vz_m_s, tgt.vx_m_s))
            speed_h = math.hypot(tgt.vx_m_s, tgt.vz_m_s)
            pitch_deg = math.degrees(math.atan2(tgt.vy_m_s, max(speed_h, 0.001)))
            aspect = aspect_factor_3d(yaw_deg, pitch_deg, az_deg, el_deg)
            rcs_mean = tgt.rcs_m2 * aspect
            rcs = sw.sample(rcs_mean, tgt.name, scan_number, 0)
            radial = radial_speed_toward_radar(
                tgt, radar_ix, radar_iz, radar_y, cell_m
            )
            mp = multipath.power_factor(
                hardware.wavelength_m,
                range_m,
                25.0,
                max(tgt.y_m - radar_y, 1.0),
            )
            pr = received_power_w(hardware, rcs, range_m, el_gain * mp)
            fd = doppler_hz(radial, hardware.wavelength_m)
            proc = processed_power_w(hardware, pr)
            proc = mti_apply_target(hardware, proc, fd)
            denom = noise_w + clutter_floor_w
            snr_db = lin_to_db(proc / denom) if denom > 0.0 else -300.0
            detected = snr_db >= detect_snr_db

            meas_r, meas_az, meas_el, meas_rad = synthesize_measurement(
                hardware,
                range_m,
                az_deg,
                el_deg,
                radial,
                snr_db,
                rng,
            )

            plot = SynthPlot(
                time_s=float(time_s),
                scan_number=scan_number,
                target_name=tgt.name,
                kind=tgt.kind,
                true_range_m=range_m,
                measured_range_m=meas_r,
                azimuth_deg=meas_az,
                elevation_deg=meas_el,
                radial_speed_m_s=meas_rad,
                snr_db=snr_db,
                detected=detected,
                rcs_instant_m2=rcs,
                radar_name=name,
            )
            result.plots.append(plot)
            if detected:
                result.detections += 1
                result.by_kind_detected[tgt.kind] = (
                    result.by_kind_detected.get(tgt.kind, 0) + 1
                )

    # Tracker (gate on measured plots).
    if hardware.scan_rpm > 0.0:
        tcfg = TrackerConfig(
            gate_range_m=500.0,
            gate_azimuth_deg=max(hardware.az_beamwidth_deg * 1.5, 3.0),
            alpha=0.45,
            beta=0.18,
            max_misses=4,
            confirm_hits=2,
            keep_confirmed_dead=True,
        )
    else:
        tcfg = TrackerConfig(
            gate_range_m=350.0,
            gate_azimuth_deg=max(hardware.az_beamwidth_deg * 1.5, 2.5),
            alpha=0.55,
            beta=0.25,
            max_misses=10,
            confirm_hits=3,
            keep_confirmed_dead=True,
        )
    if tracker_config is not None:
        tcfg = tracker_config
    tracker = associate_and_filter(result.plots, tcfg)
    result.tracker_tracks = tracker.tracks
    result.track_count = len(tracker.tracks)
    result.confirmed_tracks = sum(
        1 for t in tracker.tracks if t.hit_count >= tcfg.confirm_hits
    )
    for track in tracker.tracks:
        result.track_rows.append(
            {
                "radar": name,
                "track_id": track.track_id,
                "time_s": track.time_s,
                "range_m": track.range_m,
                "azimuth_deg": track.azimuth_deg,
                "range_rate_m_s": track.range_rate_m_s,
                "hits": track.hit_count,
                "misses": track.miss_count,
                "last_target_name": track.last_target_name,
                "snr_db": track.snr_db,
            }
        )
    return result


# ---------------------------------------------------------------------------
# Trajectory prediction evaluation
# ---------------------------------------------------------------------------


@dataclass
class PredictionSample:
    radar: str
    track_id: int
    target: str
    kind: str
    method: str
    anchor_time_s: float
    horizon_s: float
    pred_range_m: float
    true_range_m: float
    range_err_m: float
    az_err_deg: float
    pos_err_m: float
    hits: int


def _append_pred_sample(
    samples: list[PredictionSample],
    *,
    radar: str,
    track_id: int,
    target: str,
    kind: str,
    method: str,
    anchor_time_s: float,
    horizon_s: float,
    pred_range_m: float,
    pred_az_deg: float,
    true_range_m: float,
    true_az_deg: float,
    hits: int,
) -> None:
    range_err = pred_range_m - true_range_m
    az_err = _angle_diff_deg(pred_az_deg, true_az_deg)
    cross_m = true_range_m * math.radians(abs(az_err))
    pos_err = math.hypot(range_err, cross_m)
    samples.append(
        PredictionSample(
            radar=radar,
            track_id=track_id,
            target=target,
            kind=kind,
            method=method,
            anchor_time_s=anchor_time_s,
            horizon_s=horizon_s,
            pred_range_m=pred_range_m,
            true_range_m=true_range_m,
            range_err_m=range_err,
            az_err_deg=az_err,
            pos_err_m=pos_err,
            hits=hits,
        )
    )


def evaluate_prediction(
    run: RadarRunResult,
    trajectories: list[TargetTrajectory],
    cell_m: float,
    radar_ix: float,
    radar_iz: float,
    radar_y: float,
    horizons_s: tuple[float, ...] = (2.0, 5.0, 10.0),
    min_hits: int = 3,
    wind: GlobalWind | None = None,
) -> list[PredictionSample]:
    """Compare CV / vacuum / AirDrag / AirDrag+wind extrapolation vs truth."""
    truth_by_name = {t.initial.name: t for t in trajectories}
    samples: list[PredictionSample] = []

    for track in run.tracker_tracks:
        if track.hit_count < min_hits:
            continue
        if len(track.history) < min_hits:
            continue
        traj = truth_by_name.get(track.last_target_name)
        if traj is None:
            continue
        kind = traj.initial.kind

        hist = track.history
        anchor_step = max(len(hist) // 6, 1)
        for i in range(min_hits - 1, len(hist), anchor_step):
            t0, r0, a0, _el0 = hist[i]
            j = max(i - 3, 0)
            t_ref, r_ref, a_ref, _el_ref = hist[j]
            dt_hist = max(t0 - t_ref, 1e-3)
            az_rate = _angle_diff_deg(a0, a_ref) / dt_hist
            range_rate = (r0 - r_ref) / dt_hist

            ballistic = None
            if kind == "shell":
                ballistic = fit_ballistic_vacuum(
                    hist, i, gravity_m_s2=GRAVITY_M_S2
                )

            for horizon in horizons_s:
                t_pred = t0 + horizon
                tgt = traj.sample(t_pred, cell_m)
                if tgt.y_m < radar_y - 5.0:
                    continue

                dx = (tgt.ix - radar_ix) * cell_m
                dy = tgt.y_m - radar_y
                dz = (tgt.iz - radar_iz) * cell_m
                true_range = math_hypot3(dx, dy, dz)
                true_az = math.degrees(math.atan2(dz, dx)) % 360.0

                pred_range_cv = r0 + range_rate * horizon
                pred_az_cv = (a0 + az_rate * horizon) % 360.0
                _append_pred_sample(
                    samples,
                    radar=run.radar_name,
                    track_id=track.track_id,
                    target=track.last_target_name,
                    kind=kind,
                    method="cv",
                    anchor_time_s=t0,
                    horizon_s=horizon,
                    pred_range_m=pred_range_cv,
                    pred_az_deg=pred_az_cv,
                    true_range_m=true_range,
                    true_az_deg=true_az,
                    hits=track.hit_count,
                )

                if ballistic is not None:
                    pred_r_vac, pred_az_vac, _el_vac = predict_ballistic(
                        ballistic, horizon, air_drag=0.0
                    )
                    _append_pred_sample(
                        samples,
                        radar=run.radar_name,
                        track_id=track.track_id,
                        target=track.last_target_name,
                        kind=kind,
                        method="ballistic_vacuum",
                        anchor_time_s=t0,
                        horizon_s=horizon,
                        pred_range_m=pred_r_vac,
                        pred_az_deg=pred_az_vac,
                        true_range_m=true_range,
                        true_az_deg=true_az,
                        hits=track.hit_count,
                    )

                    drag = traj.air_drag
                    if drag <= 0.0:
                        drag = AIR_DRAG_SHELL_82MM_HE
                    pred_r_d, pred_az_d, _el_d = predict_ballistic(
                        ballistic, horizon, air_drag=drag
                    )
                    _append_pred_sample(
                        samples,
                        radar=run.radar_name,
                        track_id=track.track_id,
                        target=track.last_target_name,
                        kind=kind,
                        method="ballistic_drag",
                        anchor_time_s=t0,
                        horizon_s=horizon,
                        pred_range_m=pred_r_d,
                        pred_az_deg=pred_az_d,
                        true_range_m=true_range,
                        true_az_deg=true_az,
                        hits=track.hit_count,
                    )

                    # Wind-aware: radar site knows the global wind (weather
                    # manager in game), so drag is applied to airspeed.
                    if wind is not None and wind.speed_m_s > 0.0:
                        pred_r_w, pred_az_w, _el_w = predict_ballistic(
                            ballistic, horizon, air_drag=drag, wind=wind
                        )
                        _append_pred_sample(
                            samples,
                            radar=run.radar_name,
                            track_id=track.track_id,
                            target=track.last_target_name,
                            kind=kind,
                            method="ballistic_drag_wind",
                            anchor_time_s=t0,
                            horizon_s=horizon,
                            pred_range_m=pred_r_w,
                            pred_az_deg=pred_az_w,
                            true_range_m=true_range,
                            true_az_deg=true_az,
                            hits=track.hit_count,
                        )
    return samples


def summarize_prediction(samples: list[PredictionSample]) -> dict:
    summary: dict = {}
    methods = sorted({s.method for s in samples})
    kinds = sorted({s.kind for s in samples})
    horizons = sorted({s.horizon_s for s in samples})
    for method in methods:
        summary[method] = {}
        for kind in kinds:
            summary[method][kind] = {}
            for horizon in horizons:
                subset = [
                    s
                    for s in samples
                    if s.method == method
                    and s.kind == kind
                    and s.horizon_s == horizon
                ]
                if not subset:
                    continue
                pos = np.array([s.pos_err_m for s in subset])
                rng_e = np.array([abs(s.range_err_m) for s in subset])
                az_e = np.array([abs(s.az_err_deg) for s in subset])
                summary[method][kind][f"{horizon:.0f}s"] = {
                    "n": len(subset),
                    "pos_err_med_m": float(np.median(pos)),
                    "pos_err_p90_m": float(np.percentile(pos, 90)),
                    "range_err_med_m": float(np.median(rng_e)),
                    "az_err_med_deg": float(np.median(az_e)),
                }
    return summary


def _render_prediction(path: str, samples: list[PredictionSample]) -> None:
    if not samples:
        return
    fig, axes = plt.subplots(1, 2, figsize=(13, 5), constrained_layout=True)
    horizons = sorted({s.horizon_s for s in samples})

    ax = axes[0]
    styles = {
        ("shell", "cv"): ("#ff4466", "-", "shell CV"),
        ("shell", "ballistic_vacuum"): ("#ffaa00", "--", "shell vacuum"),
        ("shell", "ballistic_drag"): ("#44aaff", "-", "shell +AirDrag"),
        ("shell", "ballistic_drag_wind"): ("#22dd88", "-", "shell +AirDrag+wind"),
        ("heli", "cv"): ("#88cc88", ":", "heli CV"),
    }
    for (kind, method), (color, ls, label) in styles.items():
        med = []
        hs = []
        for horizon in horizons:
            subset = [
                s
                for s in samples
                if s.kind == kind and s.method == method and s.horizon_s == horizon
            ]
            if not subset:
                continue
            pos = np.array([s.pos_err_m for s in subset])
            hs.append(horizon)
            med.append(float(np.median(pos)))
        if not hs:
            continue
        ax.plot(hs, med, marker="o", color=color, linestyle=ls, label=label)
    ax.set_title("Median position error vs horizon")
    ax.set_xlabel("Prediction horizon (s)")
    ax.set_ylabel("Position error (m)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)

    ax = axes[1]
    for method, color, marker in (
        ("cv", "#ff4466", "o"),
        ("ballistic_vacuum", "#ffaa00", "s"),
        ("ballistic_drag", "#44aaff", "^"),
        ("ballistic_drag_wind", "#22dd88", "v"),
    ):
        subset = [
            s
            for s in samples
            if s.kind == "shell" and s.method == method and s.horizon_s == 10.0
        ]
        if not subset:
            continue
        ax.scatter(
            [s.true_range_m / 1000.0 for s in subset],
            [s.pos_err_m for s in subset],
            s=18,
            c=color,
            marker=marker,
            alpha=0.65,
            label=f"shell {method} @10s (n={len(subset)})",
        )
    ax.set_title("Shell @10s: CV / vacuum / AirDrag / +wind")
    ax.set_xlabel("True range (km)")
    ax.set_ylabel("Position error (m)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)

    fig.suptitle(
        "Track prediction: CV vs vacuum vs Reforger AirDrag (+global wind)",
        fontsize=13,
    )
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print(f"wrote: {path}")


# ---------------------------------------------------------------------------
# Weapon-locating: launch / impact back-projection
# ---------------------------------------------------------------------------


@dataclass
class WlrFixSample:
    radar: str
    track_id: int
    target: str
    hits: int
    anchor_time_s: float
    true_launch_x_m: float
    true_launch_z_m: float
    true_impact_x_m: float
    true_impact_z_m: float
    est_launch_x_m: float
    est_launch_z_m: float
    est_impact_x_m: float
    est_impact_z_m: float
    launch_err_m: float
    impact_err_m: float
    launch_time_err_s: float
    impact_time_err_s: float
    fit_rms_m: float = 0.0
    fit_span_s: float = 0.0
    fit_points: int = 0


def _true_shell_endpoints(
    traj: TargetTrajectory,
    cell_m: float,
    radar_ix: float,
    radar_iz: float,
    ground_y_m: float,
    ground_y_fn: GroundYFn | None = None,
) -> tuple[tuple[float, float, float], tuple[float, float, float], float] | None:
    """Return (launch_xyz_rel, impact_xyz_rel, flight_time_s) in radar frame."""
    init = traj.initial
    launch_x = (init.ix - radar_ix) * cell_m
    launch_z = (init.iz - radar_iz) * cell_m
    if ground_y_fn is None:
        ground_y_fn = flat_ground_y_fn(ground_y_m)
    launch_ground = ground_y_fn(launch_x, launch_z)
    launch_y = init.y_m - launch_ground
    hit = find_ground_intersection(
        launch_x,
        init.y_m,
        launch_z,
        init.vx_m_s,
        init.vy_m_s,
        init.vz_m_s,
        ground_y_m,
        air_drag=traj.air_drag,
        gravity_m_s2=traj.ay_m_s2 if traj.ay_m_s2 != 0.0 else GRAVITY_M_S2,
        wind=traj.wind,
        backward=False,
        max_time_s=120.0,
        ground_y_fn=ground_y_fn,
    )
    if hit is None:
        return None
    impact_ground = ground_y_fn(hit.x_m, hit.z_m)
    return (
        (launch_x, launch_y, launch_z),
        (hit.x_m, hit.y_m - impact_ground, hit.z_m),
        hit.time_offset_s,
    )


def evaluate_wlr_fixes(
    run: RadarRunResult,
    trajectories: list[TargetTrajectory],
    cell_m: float,
    radar_ix: float,
    radar_iz: float,
    radar_y: float,
    wind: GlobalWind = NO_WIND,
    min_hits: int = 5,
    min_span_s: float = 1.0,
    max_fit_rms_m: float = 80.0,
    window_points: int = 20,
    smooth_alpha: float = 0.35,
    ground_y_fn: GroundYFn | None = None,
    select_mode: str = "last",
) -> list[WlrFixSample]:
    """Match Enforce WLR: vacuum LS fit on recent history → AirDrag ground hits.

    Fit window ends at the latest plot (not apex-only). Soft quality gates mirror
    RDF_RadarSettings WeaponLocate* defaults; optional EMA across mid-track
    anchors approximates in-game smooth alpha.

    select_mode:
      "last" — final EMA'd fix (map marker / RefreshWeaponLocates)
      "best_launch" — lowest launch_err during the walk (ShellFire AutoTest metric)
    """
    if ground_y_fn is None:
        ground_y_fn = flat_ground_y_fn(radar_y)
    truth_by_name = {t.initial.name: t for t in trajectories}
    samples: list[WlrFixSample] = []
    for track in run.tracker_tracks:
        if track.hit_count < min_hits:
            continue
        if len(track.history) < min_hits:
            continue
        traj = truth_by_name.get(track.last_target_name)
        if traj is None or traj.initial.kind != "shell":
            continue
        endpoints = _true_shell_endpoints(
            traj, cell_m, radar_ix, radar_iz, radar_y, ground_y_fn=ground_y_fn
        )
        if endpoints is None:
            continue
        true_launch, true_impact, true_flight_s = endpoints

        drag = traj.air_drag
        if drag <= 0.0:
            drag = AIR_DRAG_SHELL_82MM_HE

        # Walk anchors like successive scans; always end on the latest plot.
        hist = track.history
        last_sample: WlrFixSample | None = None
        best_sample: WlrFixSample | None = None
        step = max(1, len(hist) // 12)
        anchors: list[int] = list(range(min_hits - 1, len(hist), step))
        final_i = len(hist) - 1
        if final_i not in anchors:
            anchors.append(final_i)

        for anchor_i in anchors:
            ballistic = fit_ballistic_vacuum(
                hist,
                anchor_i,
                gravity_m_s2=GRAVITY_M_S2,
                min_points=min_hits,
                min_span_s=min_span_s,
                max_fit_rms_m=max_fit_rms_m,
                window_points=window_points,
            )
            if ballistic is None:
                continue
            launch, impact = solve_launch_and_impact(
                ballistic.x_m,
                ballistic.y_m + radar_y,
                ballistic.z_m,
                ballistic.vx_m_s,
                ballistic.vy_m_s,
                ballistic.vz_m_s,
                radar_y,
                air_drag=drag,
                gravity_m_s2=GRAVITY_M_S2,
                wind=wind,
                ground_y_fn=ground_y_fn,
            )
            if launch is None or impact is None:
                continue

            est_lx = launch.x_m
            est_lz = launch.z_m
            est_ix = impact.x_m
            est_iz = impact.z_m
            if last_sample is not None and 0.0 < smooth_alpha < 1.0:
                a = smooth_alpha
                b = 1.0 - a
                est_lx = last_sample.est_launch_x_m * b + est_lx * a
                est_lz = last_sample.est_launch_z_m * b + est_lz * a
                est_ix = last_sample.est_impact_x_m * b + est_ix * a
                est_iz = last_sample.est_impact_z_m * b + est_iz * a

            launch_err = math.hypot(
                est_lx - true_launch[0], est_lz - true_launch[2]
            )
            impact_err = math.hypot(
                est_ix - true_impact[0], est_iz - true_impact[2]
            )
            last_sample = WlrFixSample(
                radar=run.radar_name,
                track_id=track.track_id,
                target=track.last_target_name,
                hits=track.hit_count,
                anchor_time_s=ballistic.t_anchor_s,
                true_launch_x_m=true_launch[0],
                true_launch_z_m=true_launch[2],
                true_impact_x_m=true_impact[0],
                true_impact_z_m=true_impact[2],
                est_launch_x_m=est_lx,
                est_launch_z_m=est_lz,
                est_impact_x_m=est_ix,
                est_impact_z_m=est_iz,
                launch_err_m=launch_err,
                impact_err_m=impact_err,
                launch_time_err_s=(
                    ballistic.t_anchor_s + launch.time_offset_s - traj.start_time_s
                ),
                impact_time_err_s=(
                    ballistic.t_anchor_s
                    + impact.time_offset_s
                    - (traj.start_time_s + true_flight_s)
                ),
                fit_rms_m=ballistic.fit_rms_m,
                fit_span_s=ballistic.fit_span_s,
                fit_points=ballistic.fit_points,
            )
            if best_sample is None or last_sample.launch_err_m < best_sample.launch_err_m:
                best_sample = last_sample

        chosen = last_sample
        if select_mode == "best_launch":
            chosen = best_sample
        if chosen is not None:
            samples.append(chosen)
    return samples


def summarize_wlr_fixes(samples: list[WlrFixSample]) -> dict:
    if not samples:
        return {"n": 0}
    launch = np.array([s.launch_err_m for s in samples])
    impact = np.array([s.impact_err_m for s in samples])
    return {
        "n": len(samples),
        "launch_err_med_m": float(np.median(launch)),
        "launch_err_p90_m": float(np.percentile(launch, 90)),
        "impact_err_med_m": float(np.median(impact)),
        "impact_err_p90_m": float(np.percentile(impact, 90)),
        "launch_time_err_med_s": float(
            np.median([abs(s.launch_time_err_s) for s in samples])
        ),
        "impact_time_err_med_s": float(
            np.median([abs(s.impact_time_err_s) for s in samples])
        ),
    }


def _render_wlr_fixes(path: str, samples: list[WlrFixSample]) -> None:
    if not samples:
        return
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), constrained_layout=True)

    ax = axes[0]
    ax.scatter(
        [s.true_launch_x_m / 1000.0 for s in samples],
        [s.true_launch_z_m / 1000.0 for s in samples],
        s=40,
        c="#ff4466",
        marker="*",
        label="true launch",
        zorder=3,
    )
    ax.scatter(
        [s.est_launch_x_m / 1000.0 for s in samples],
        [s.est_launch_z_m / 1000.0 for s in samples],
        s=28,
        c="#44aaff",
        marker="o",
        alpha=0.8,
        label="est launch",
        zorder=4,
    )
    for s in samples:
        ax.plot(
            [s.true_launch_x_m / 1000.0, s.est_launch_x_m / 1000.0],
            [s.true_launch_z_m / 1000.0, s.est_launch_z_m / 1000.0],
            color="#888888",
            alpha=0.35,
            linewidth=0.8,
        )
    ax.scatter([0.0], [0.0], s=80, c="#222222", marker="^", label="radar", zorder=5)
    ax.set_title("Launch localization (radar-relative XZ)")
    ax.set_xlabel("X (km)")
    ax.set_ylabel("Z (km)")
    ax.set_aspect("equal", adjustable="datalim")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)

    ax = axes[1]
    ax.scatter(
        [s.true_impact_x_m / 1000.0 for s in samples],
        [s.true_impact_z_m / 1000.0 for s in samples],
        s=40,
        c="#ff4466",
        marker="x",
        label="true impact",
        zorder=3,
    )
    ax.scatter(
        [s.est_impact_x_m / 1000.0 for s in samples],
        [s.est_impact_z_m / 1000.0 for s in samples],
        s=28,
        c="#22dd88",
        marker="o",
        alpha=0.8,
        label="est impact",
        zorder=4,
    )
    for s in samples:
        ax.plot(
            [s.true_impact_x_m / 1000.0, s.est_impact_x_m / 1000.0],
            [s.true_impact_z_m / 1000.0, s.est_impact_z_m / 1000.0],
            color="#888888",
            alpha=0.35,
            linewidth=0.8,
        )
    ax.scatter([0.0], [0.0], s=80, c="#222222", marker="^", label="radar", zorder=5)
    ax.set_title("Impact localization (radar-relative XZ)")
    ax.set_xlabel("X (km)")
    ax.set_ylabel("Z (km)")
    ax.set_aspect("equal", adjustable="datalim")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)

    med_l = float(np.median([s.launch_err_m for s in samples]))
    med_i = float(np.median([s.impact_err_m for s in samples]))
    fig.suptitle(
        f"WLR ballistic fix  n={len(samples)}  "
        f"launch med={med_l:.0f} m  impact med={med_i:.0f} m",
        fontsize=13,
    )
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print(f"wrote: {path}")


# ---------------------------------------------------------------------------
# Render / IO
# ---------------------------------------------------------------------------


def _render(
    path: str,
    ad: RadarRunResult,
    wlr: RadarRunResult,
    priors: SignaturePriors,
    duration_s: float,
) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(14, 10), constrained_layout=True)

    def _scatter_ppi(ax, run: RadarRunResult, title: str) -> None:
        truth = [p for p in run.plots if not p.detected]
        hits = [p for p in run.plots if p.detected]
        if truth:
            ax.scatter(
                [p.true_range_m / 1000.0 for p in truth],
                [p.azimuth_deg % 360.0 for p in truth],
                s=6,
                c="#666666",
                alpha=0.25,
                label="beam intercept",
            )
        colors = {"aircraft": "#4ea1ff", "heli": "#ffaa00", "shell": "#ff4466"}
        for kind, color in colors.items():
            pts = [p for p in hits if p.kind == kind]
            if not pts:
                continue
            ax.scatter(
                [p.measured_range_m / 1000.0 for p in pts],
                [p.azimuth_deg % 360.0 for p in pts],
                s=18,
                c=color,
                label=f"CFAR {kind} ({len(pts)})",
            )
        ax.set_title(title)
        ax.set_xlabel("Range (km)")
        ax.set_ylabel("Azimuth (deg)")
        ax.set_xlim(0, max(ad.plots and max(p.true_range_m for p in ad.plots) or 20000, 1) / 1000.0 + 1)
        ax.set_ylim(0, 360)
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=7, loc="upper right")

    _scatter_ppi(axes[0, 0], ad, f"AD PPI — {ad.hardware_name}")
    _scatter_ppi(axes[0, 1], wlr, f"WLR PPI — {wlr.hardware_name}")

    # Range-time for AD aircraft/heli and WLR shells.
    ax = axes[1, 0]
    for kind, color in (("aircraft", "#4ea1ff"), ("heli", "#ffaa00")):
        pts = [p for p in ad.plots if p.kind == kind and p.detected]
        if pts:
            ax.scatter(
                [p.time_s for p in pts],
                [p.measured_range_m / 1000.0 for p in pts],
                s=12,
                c=color,
                label=f"AD {kind}",
            )
    shell_hits = [p for p in wlr.plots if p.kind == "shell" and p.detected]
    if shell_hits:
        ax.scatter(
            [p.time_s for p in shell_hits],
            [p.measured_range_m / 1000.0 for p in shell_hits],
            s=10,
            c="#ff4466",
            alpha=0.7,
            label="WLR shell",
        )
    ax.set_title("Detections vs time")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Measured range (km)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)

    axes[1, 1].axis("off")
    lines = [
        "RDF mass AD + artillery battle sim",
        f"duration={duration_s:.0f}s",
        f"signatures={priors.loaded_rows} from {os.path.basename(priors.source)}",
        f"RCS priors fighter={priors.fighter_rcs:.2f} heli={priors.heli_rcs:.2f} "
        f"transport={priors.transport_rcs:.2f} shell={priors.shell_rcs:.4f}",
        "",
        f"[AD] {ad.hardware_name}",
        f"  intercepts={ad.intercepts} CFAR={ad.detections}",
        f"  tracks={ad.track_count} confirmed={ad.confirmed_tracks}",
        f"  detected by kind={ad.by_kind_detected}",
        "",
        f"[WLR] {wlr.hardware_name}",
        f"  intercepts={wlr.intercepts} CFAR={wlr.detections}",
        f"  tracks={wlr.track_count} confirmed={wlr.confirmed_tracks}",
        f"  detected by kind={wlr.by_kind_detected}",
        "",
        "Model chain:",
        "  signature mean RCS -> Swerling -> Pr(R^4)",
        "  multipath * el beam -> PC/MTI -> SNR gate",
        "  measurement synthesis -> alpha-beta tracks",
    ]
    axes[1, 1].text(0.0, 1.0, "\n".join(lines), va="top", family="monospace", fontsize=8)
    fig.suptitle("Large-scale air defense + artillery radar simulation", fontsize=13)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print(f"wrote: {path}")


def make_synthetic_dem(
    size: int = 512,
    cell_m: float = 20.0,
    base_y_m: float = 100.0,
    seed: int = 0,
) -> np.ndarray:
    """Hilly DEM for offline WLR tests when baked tiles are unavailable."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size]
    terrain = np.full((size, size), base_y_m, dtype=np.float32)
    terrain = terrain + 45.0 * np.sin(xx * 0.035 + 0.4) * np.cos(yy * 0.028)
    terrain = terrain + 28.0 * np.sin(xx * 0.012 - yy * 0.018)
    terrain = terrain + rng.normal(0.0, 2.5, size=(size, size)).astype(np.float32)
    _ = cell_m
    return terrain


def _resolve_battle_dem(
    dem_dir: str,
    no_dem: bool,
    seed: int,
    dem_npz: str = "",
    use_ttile: bool = False,
    world: str = "GM_Eden",
    surf_dir: str = "",
    merge_surf: bool = True,
) -> tuple[np.ndarray, float, float, float, float, GroundYFn, str]:
    """Return terrain, cell_m, radar_ix/iz/y, ground_y_fn, source label."""
    if no_dem:
        cell_m = 20.0
        radar_ix = 256.0
        radar_iz = 256.0
        radar_y = 100.0
        terrain = np.full((512, 512), radar_y, dtype=np.float32)
        ground_fn = flat_ground_y_fn(radar_y)
        return terrain, cell_m, radar_ix, radar_iz, radar_y, ground_fn, "flat"

    try:
        dem = resolve_dem_source(
            world=world,
            dem_dir=dem_dir,
            dem_npz=dem_npz,
            prefer_ttile=use_ttile,
            load_spans=False,
            surf_dir=surf_dir,
            merge_surf=merge_surf,
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
        ground_fn = dem_ground_y_fn(
            terrain, cell_m, radar_ix, radar_iz, radar_y
        )
        source = str(dem.get("source", "dem"))
        return (
            terrain,
            cell_m,
            radar_ix,
            radar_iz,
            radar_y,
            ground_fn,
            source,
        )
    except SystemExit:
        pass
    except OSError:
        pass

    cell_m = 20.0
    terrain = make_synthetic_dem(512, cell_m, 100.0, seed=seed + 19)
    radar_ix = 256.0
    radar_iz = 256.0
    radar_y = float(terrain[256, 256])
    ground_fn = dem_ground_y_fn(terrain, cell_m, radar_ix, radar_iz, radar_y)
    return terrain, cell_m, radar_ix, radar_iz, radar_y, ground_fn, "synthetic"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration-s", type=float, default=60.0)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--sig-csv", default="")
    parser.add_argument("--ad-preset", default="shorad", choices=["p18", "tps43", "shorad"])
    parser.add_argument("--output-dir", default="")
    parser.add_argument(
        "--wind-speed-m-s",
        type=float,
        default=-1.0,
        help="Global wind speed; <0 draws a random 2-14 m/s wind from the seed.",
    )
    parser.add_argument(
        "--wind-dir-deg",
        type=float,
        default=-1.0,
        help="Global wind bearing (deg, +X=0); <0 draws random.",
    )
    parser.add_argument(
        "--dem-dir",
        default="",
        help="DEM tile dir (dataset_*.npz). Empty tries default GM_Eden tiles, "
        "then falls back to a synthetic hilly DEM.",
    )
    parser.add_argument(
        "--dem-npz",
        default="",
        help="Heightfield npz from rdf_ttile_unpack.py (overrides --dem-dir).",
    )
    parser.add_argument(
        "--use-ttile",
        action="store_true",
        help="Prefer tools/dem/out/<World>_ttile_height.npz over TrainData tiles.",
    )
    parser.add_argument(
        "--surf-dir",
        default="",
        help="SURF JSON dir (surf_manifest.json). Default: addon DemData/<GM_World>/.",
    )
    parser.add_argument(
        "--no-surf",
        action="store_true",
        help="Do not overlay SURF surface_class onto ttile height.",
    )
    parser.add_argument(
        "--world",
        default="GM_Eden",
        help="World key for default tile/ttile paths (GM_Eden / GM_Arland / GM_Cain).",
    )
    parser.add_argument(
        "--no-dem",
        action="store_true",
        help="Force flat ground (legacy WLR plane at radar altitude).",
    )
    args = parser.parse_args()

    out_dir = args.output_dir
    if not out_dir:
        out_dir = os.path.join(_HERE, "out")
    os.makedirs(out_dir, exist_ok=True)

    sig_path = args.sig_csv if args.sig_csv else _default_sig_path()
    priors = load_signature_priors(sig_path)
    print(
        f"signatures loaded={priors.loaded_rows} "
        f"fighter={priors.fighter_rcs:.2f} heli={priors.heli_rcs:.2f} "
        f"shell={priors.shell_rcs:.4f}"
    )

    wind_rng = np.random.default_rng(args.seed + 777)
    wind = random_global_wind(wind_rng)
    if args.wind_speed_m_s >= 0.0 or args.wind_dir_deg >= 0.0:
        speed = wind.speed_m_s
        if args.wind_speed_m_s >= 0.0:
            speed = args.wind_speed_m_s
        direction = wind.direction_deg
        if args.wind_dir_deg >= 0.0:
            direction = args.wind_dir_deg
        wind = GlobalWind(speed_m_s=speed, direction_deg=direction)
    print(
        f"global wind {wind.speed_m_s:.1f} m/s bearing {wind.direction_deg:.0f} deg"
    )

    terrain, cell_m, radar_ix, radar_iz, radar_y, ground_y_fn, dem_source = (
        _resolve_battle_dem(
            args.dem_dir,
            args.no_dem,
            args.seed,
            dem_npz=args.dem_npz,
            use_ttile=args.use_ttile,
            world=args.world,
            surf_dir=args.surf_dir,
            merge_surf=not args.no_surf,
        )
    )
    print(
        f"DEM source={dem_source} cell={cell_m:.1f} m "
        f"radar=({radar_ix:.0f},{radar_iz:.0f}) y={radar_y:.1f} m "
        f"shape={terrain.shape}"
    )

    trajectories = build_mass_scenario(
        priors,
        cell_m,
        radar_ix,
        radar_iz,
        radar_y,
        args.seed,
        wind=wind,
        ground_y_fn=ground_y_fn,
    )
    n_air = sum(1 for t in trajectories if t.initial.kind in ("aircraft", "heli"))
    n_shell = sum(1 for t in trajectories if t.initial.kind == "shell")
    print(f"scenario targets air={n_air} shells={n_shell} total={len(trajectories)}")

    ad_hw = get_preset(args.ad_preset)
    wlr_hw = get_preset("artillery")

    ad = run_plot_level_radar(
        name="AD",
        hardware=ad_hw,
        trajectories=trajectories,
        cell_m=cell_m,
        radar_ix=radar_ix,
        radar_iz=radar_iz,
        radar_y=radar_y,
        duration_s=args.duration_s,
        clutter_cnr_db=8.0,
        seed=args.seed + 1,
        kind_filter={"aircraft", "heli"},
    )
    wlr = run_plot_level_radar(
        name="WLR",
        hardware=wlr_hw,
        trajectories=trajectories,
        cell_m=cell_m,
        radar_ix=radar_ix,
        radar_iz=radar_iz,
        radar_y=radar_y,
        duration_s=args.duration_s,
        clutter_cnr_db=12.0,
        seed=args.seed + 2,
        kind_filter={"shell"},
        az_sector_center_deg=200.0,
        az_sector_half_deg=50.0,
    )

    png = os.path.join(out_dir, "mass_battle_overview.png")
    _render(png, ad, wlr, priors, args.duration_s)

    det_csv = os.path.join(out_dir, "mass_battle_detections.csv")
    with open(det_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "radar",
                "time_s",
                "scan",
                "target",
                "kind",
                "true_range_m",
                "meas_range_m",
                "az_deg",
                "el_deg",
                "radial_m_s",
                "snr_db",
                "detected",
                "rcs_m2",
            ]
        )
        for run in (ad, wlr):
            for p in run.plots:
                writer.writerow(
                    [
                        p.radar_name,
                        f"{p.time_s:.3f}",
                        p.scan_number,
                        p.target_name,
                        p.kind,
                        f"{p.true_range_m:.2f}",
                        f"{p.measured_range_m:.2f}",
                        f"{p.azimuth_deg:.3f}",
                        f"{p.elevation_deg:.3f}",
                        f"{p.radial_speed_m_s:.3f}",
                        f"{p.snr_db:.2f}",
                        int(p.detected),
                        f"{p.rcs_instant_m2:.4f}",
                    ]
                )
    print(f"wrote: {det_csv}")

    trk_csv = os.path.join(out_dir, "mass_battle_tracks.csv")
    with open(trk_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "radar",
                "track_id",
                "time_s",
                "range_m",
                "azimuth_deg",
                "range_rate_m_s",
                "hits",
                "misses",
                "last_target_name",
                "snr_db",
            ],
        )
        writer.writeheader()
        for run in (ad, wlr):
            for row in run.track_rows:
                writer.writerow(row)
    print(f"wrote: {trk_csv}")

    pred_samples: list[PredictionSample] = []
    for run in (ad, wlr):
        pred_samples.extend(
            evaluate_prediction(
                run, trajectories, cell_m, radar_ix, radar_iz, radar_y, wind=wind
            )
        )
    pred_summary = summarize_prediction(pred_samples)

    pred_csv = os.path.join(out_dir, "mass_battle_prediction.csv")
    with open(pred_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "radar",
                "track_id",
                "target",
                "kind",
                "method",
                "anchor_time_s",
                "horizon_s",
                "pred_range_m",
                "true_range_m",
                "range_err_m",
                "az_err_deg",
                "pos_err_m",
                "hits",
            ]
        )
        for s in pred_samples:
            writer.writerow(
                [
                    s.radar,
                    s.track_id,
                    s.target,
                    s.kind,
                    s.method,
                    f"{s.anchor_time_s:.3f}",
                    f"{s.horizon_s:.1f}",
                    f"{s.pred_range_m:.2f}",
                    f"{s.true_range_m:.2f}",
                    f"{s.range_err_m:.2f}",
                    f"{s.az_err_deg:.4f}",
                    f"{s.pos_err_m:.2f}",
                    s.hits,
                ]
            )
    print(f"wrote: {pred_csv}")

    pred_png = os.path.join(out_dir, "mass_battle_prediction.png")
    _render_prediction(pred_png, pred_samples)

    wlr_fixes = evaluate_wlr_fixes(
        wlr,
        trajectories,
        cell_m,
        radar_ix,
        radar_iz,
        radar_y,
        wind=wind,
        ground_y_fn=ground_y_fn,
    )
    wlr_fix_summary = summarize_wlr_fixes(wlr_fixes)
    wlr_fix_csv = os.path.join(out_dir, "mass_battle_wlr_fixes.csv")
    with open(wlr_fix_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "radar",
                "track_id",
                "target",
                "hits",
                "anchor_time_s",
                "true_launch_x_m",
                "true_launch_z_m",
                "est_launch_x_m",
                "est_launch_z_m",
                "launch_err_m",
                "true_impact_x_m",
                "true_impact_z_m",
                "est_impact_x_m",
                "est_impact_z_m",
                "impact_err_m",
                "launch_time_err_s",
                "impact_time_err_s",
                "fit_rms_m",
                "fit_span_s",
                "fit_points",
            ]
        )
        for s in wlr_fixes:
            writer.writerow(
                [
                    s.radar,
                    s.track_id,
                    s.target,
                    s.hits,
                    f"{s.anchor_time_s:.3f}",
                    f"{s.true_launch_x_m:.1f}",
                    f"{s.true_launch_z_m:.1f}",
                    f"{s.est_launch_x_m:.1f}",
                    f"{s.est_launch_z_m:.1f}",
                    f"{s.launch_err_m:.1f}",
                    f"{s.true_impact_x_m:.1f}",
                    f"{s.true_impact_z_m:.1f}",
                    f"{s.est_impact_x_m:.1f}",
                    f"{s.est_impact_z_m:.1f}",
                    f"{s.impact_err_m:.1f}",
                    f"{s.launch_time_err_s:.2f}",
                    f"{s.impact_time_err_s:.2f}",
                    f"{s.fit_rms_m:.2f}",
                    f"{s.fit_span_s:.2f}",
                    s.fit_points,
                ]
            )
    print(f"wrote: {wlr_fix_csv}")
    wlr_fix_png = os.path.join(out_dir, "mass_battle_wlr_fixes.png")
    _render_wlr_fixes(wlr_fix_png, wlr_fixes)

    summary = {
        "duration_s": args.duration_s,
        "seed": args.seed,
        "signatures": {
            "rows": priors.loaded_rows,
            "source": priors.source,
            "fighter_rcs_m2": priors.fighter_rcs,
            "heli_rcs_m2": priors.heli_rcs,
            "transport_rcs_m2": priors.transport_rcs,
            "shell_rcs_m2": priors.shell_rcs,
        },
        "scenario": {"air": n_air, "shells": n_shell, "total": len(trajectories)},
        "dem": {
            "source": dem_source,
            "cell_m": cell_m,
            "radar_ix": radar_ix,
            "radar_iz": radar_iz,
            "radar_y_m": radar_y,
            "shape": [int(terrain.shape[0]), int(terrain.shape[1])],
        },
        "wind": {
            "speed_m_s": wind.speed_m_s,
            "direction_deg": wind.direction_deg,
            "vx_m_s": wind.vx_m_s,
            "vz_m_s": wind.vz_m_s,
        },
        "ad": {
            "preset": args.ad_preset,
            "hardware": ad_hw.display_name,
            "hardware_notes": ad_hw.notes,
            "intercepts": ad.intercepts,
            "detections": ad.detections,
            "tracks": ad.track_count,
            "confirmed": ad.confirmed_tracks,
            "by_kind_detected": ad.by_kind_detected,
            "pd_air": (
                ad.detections / max(ad.intercepts, 1)
            ),
        },
        "wlr": {
            "hardware": wlr_hw.display_name,
            "hardware_notes": wlr_hw.notes,
            "intercepts": wlr.intercepts,
            "detections": wlr.detections,
            "tracks": wlr.track_count,
            "confirmed": wlr.confirmed_tracks,
            "by_kind_detected": wlr.by_kind_detected,
            "pd_shell": (
                wlr.detections / max(wlr.intercepts, 1)
            ),
        },
        "prediction": pred_summary,
        "wlr_fix": wlr_fix_summary,
        "outputs": {
            "png": png,
            "detections_csv": det_csv,
            "tracks_csv": trk_csv,
            "prediction_csv": pred_csv,
            "prediction_png": pred_png,
            "wlr_fixes_csv": wlr_fix_csv,
            "wlr_fixes_png": wlr_fix_png,
        },
    }
    summary_path = os.path.join(out_dir, "mass_battle_summary.json")
    with open(summary_path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    print(f"wrote: {summary_path}")
    print("--- AD ---")
    for line in summarize_hardware(ad_hw):
        print(" ", line)
    print(
        f"  CFAR={ad.detections}/{ad.intercepts} "
        f"tracks={ad.track_count} confirmed={ad.confirmed_tracks}"
    )
    print("--- WLR ---")
    for line in summarize_hardware(wlr_hw):
        print(" ", line)
    print(
        f"  CFAR={wlr.detections}/{wlr.intercepts} "
        f"tracks={wlr.track_count} confirmed={wlr.confirmed_tracks}"
    )
    print("--- Prediction (CV / vacuum / AirDrag) ---")
    for method, by_kind in pred_summary.items():
        for kind, per_h in by_kind.items():
            for horizon, stats in per_h.items():
                print(
                    f"  {method:10s} {kind:9s} +{horizon}: n={stats['n']:4d} "
                    f"pos_med={stats['pos_err_med_m']:8.1f} m "
                    f"pos_p90={stats['pos_err_p90_m']:8.1f} m "
                    f"range_med={stats['range_err_med_m']:7.1f} m "
                    f"az_med={stats['az_err_med_deg']:.3f} deg"
                )
    print("--- WLR launch / impact fix ---")
    if wlr_fix_summary.get("n", 0) == 0:
        print("  (no confirmed shell tracks with valid ground intersections)")
    else:
        print(
            f"  n={wlr_fix_summary['n']}  "
            f"launch med={wlr_fix_summary['launch_err_med_m']:.0f} m "
            f"p90={wlr_fix_summary['launch_err_p90_m']:.0f} m  "
            f"impact med={wlr_fix_summary['impact_err_med_m']:.0f} m "
            f"p90={wlr_fix_summary['impact_err_p90_m']:.0f} m  "
            f"|dt_launch|med={wlr_fix_summary['launch_time_err_med_s']:.2f} s "
            f"|dt_impact|med={wlr_fix_summary['impact_time_err_med_s']:.2f} s"
        )


if __name__ == "__main__":
    main()
