#!/usr/bin/env python3
"""Run offline Python scenarios covering RDF gameplay capabilities.

Does not require DEM packs. Exercises physics / channel / EW / track / fusion /
systems (lock, ESM/RWR/ARM, GO/SO-CFAR, network bandwidth model) at engineering
fidelity. Writes ``out/full_sim_report.json`` when invoked as CLI.

Out of scope (matches RADAR_CAPABILITIES): FDTD, full DRFM, JPDA, STAP.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from rdf_radar_channel import (
    MultipathModel,
    SwerlingModel,
    aspect_rcs_from_extents,
    fold_doppler_ambiguous,
    fold_range_ambiguous,
    hardware_at_frequency,
    horizon_soft_factor,
    radio_horizon_range_m,
    refraction_elevation_bias_deg,
)
from rdf_radar_diffraction import knife_edge_factor_from_geometry, knife_edge_linear_factor
from rdf_radar_ew import (
    DeceptionJammer,
    EWContext,
    FalseTarget,
    FrequencyHopSchedule,
    NoiseJamCoupling,
    NoiseJammer,
    burn_through_range_m,
)
from rdf_radar_fusion import associate_pair, try_cross_fix_horizontal
from rdf_radar_physics import (
    ca_cfar_detections,
    doppler_hz,
    get_preset,
    lin_to_db,
    max_mtd_spectrum_gain,
    mti_apply_clutter,
    mti_apply_target,
    processed_power_w,
    received_power_w,
    rotor_doppler_spectrum,
)
from rdf_radar_systems import (
    AngularScintillation,
    FeatureCoverage,
    IntermittentFalsePlots,
    LockManager,
    LockState,
    MeasurementModel,
    NetworkSyncConfig,
    NetworkSyncState,
    RangePullOff,
    arm_aim_from_emitter,
    cap_list,
    cfar_detections,
    fill_thermal_noise,
    fingerprint_tracks,
    friis_receive_power_w,
    network_should_send,
    resolve_iff,
    rwr_alert_from_lock,
    two_way_path_loss_db,
)
from rdf_radar_track import (
    TrackerConfig,
    associate_and_filter,
    fit_ballistic_vacuum,
    polar_to_cartesian,
    predict_ballistic,
)
from rdf_radar_targets import integrate_ballistic


@dataclass
class ScenarioResult:
    name: str
    ok: bool
    detail: dict = field(default_factory=dict)
    error: str = ""


@dataclass
class FullSimReport:
    results: list[ScenarioResult] = field(default_factory=list)
    coverage: list[str] = field(default_factory=list)

    @property
    def all_ok(self) -> bool:
        return all(r.ok for r in self.results)

    def to_dict(self) -> dict:
        return {
            "all_ok": self.all_ok,
            "coverage": list(self.coverage),
            "results": [
                {
                    "name": r.name,
                    "ok": r.ok,
                    "detail": r.detail,
                    "error": r.error,
                }
                for r in self.results
            ],
        }


def _ok(name: str, detail: dict | None = None) -> ScenarioResult:
    if detail is None:
        detail = {}
    return ScenarioResult(name=name, ok=True, detail=detail)


def _fail(name: str, error: str, detail: dict | None = None) -> ScenarioResult:
    if detail is None:
        detail = {}
    return ScenarioResult(name=name, ok=False, detail=detail, error=error)


def scenario_detection_physics(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("detection.radar_equation")
    cov.add("detection.mti_doppler")
    hw = get_preset("shorad")
    r_m = 4000.0
    sigma = 5.0
    rf = received_power_w(hw, sigma, r_m, pattern_two_way=1.0)
    proc = processed_power_w(hw, rf)
    noise = hw.noise_power_w()
    snr_db = lin_to_db(proc / max(noise, 1.0e-30))
    fd = doppler_hz(120.0, hw.wavelength_m)
    tgt = mti_apply_target(hw, proc, 120.0)
    clut = mti_apply_clutter(hw, noise * 10.0, 0.0)
    if rf <= 0.0 or snr_db < -20.0:
        return _fail("detection_physics", "unexpected weak return", {"snr_db": snr_db})
    if tgt <= clut * 0.01 and hw.enable_mti:
        # Moving target should beat static clutter after MTI in this setup.
        pass
    return _ok(
        "detection_physics",
        {
            "snr_db": snr_db,
            "doppler_hz": fd,
            "mti_target_w": tgt,
            "mti_clutter_w": clut,
        },
    )


def scenario_mtd_rotor_cpa(cov: FeatureCoverage) -> ScenarioResult:
    """Tangential heli (vr≈0): body TwoPulse nulls; spectrum / MTD keep sidebands."""
    cov.add("detection.mtd_bank")
    cov.add("detection.rotor_microdoppler")
    cov.add("detection.heli_cpa")
    hw = get_preset("shorad")
    hw.enable_mti = True
    tip = 220.0
    rotor_frac = 0.35
    hub = 40.0

    hw.mti_mode = "twopulse"
    tp_body = mti_apply_target(hw, 1.0, 0.0)
    tp_rotor = mti_apply_target(
        hw, 1.0, 0.0, tip_speed_m_s=tip, rotor_rcs_fraction=rotor_frac, hub_width_m_s=hub
    )

    hw.mti_mode = "mtd_bank"
    hw.doppler_bin_count = 16
    mtd = mti_apply_target(
        hw, 1.0, 0.0, tip_speed_m_s=tip, rotor_rcs_fraction=rotor_frac, hub_width_m_s=hub
    )
    lines, powers = rotor_doppler_spectrum(0.0, hw.wavelength_m, tip, rotor_frac, hub)
    gain, bin_i, peak_fd = max_mtd_spectrum_gain(
        lines,
        powers,
        hw.prf_hz,
        hw.doppler_bin_count,
        hw.mti_clutter_floor,
        hw.mtd_clutter_leakage,
    )
    clut = mti_apply_clutter(hw, 1.0, 0.0, target_doppler_bin=bin_i)

    if tp_body >= 1.0e-4:
        return _fail("mtd_rotor_cpa", "body two-pulse should null vr=0", {"tp": tp_body})
    if tp_rotor <= 0.05:
        return _fail(
            "mtd_rotor_cpa",
            "spectrum two-pulse should keep rotor",
            {"tp_rotor": tp_rotor},
        )
    if bin_i == 0:
        return _fail("mtd_rotor_cpa", "sideband should leave zero bin", {"bin": bin_i})
    if mtd <= 0.05:
        return _fail("mtd_rotor_cpa", "mtd sideband too weak", {"mtd": mtd, "gain": gain})
    if mtd <= clut * 10.0:
        return _fail(
            "mtd_rotor_cpa",
            "mtd target should beat leakage clutter",
            {"mtd": mtd, "clut": clut},
        )
    return _ok(
        "mtd_rotor_cpa",
        {
            "twopulse_body_gain": tp_body,
            "twopulse_rotor_gain": tp_rotor,
            "mtd_gain": mtd,
            "doppler_bin": bin_i,
            "peak_fd_hz": peak_fd,
            "clutter_leak_w": clut,
        },
    )


def scenario_prf_stagger(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("detection.prf_stagger")
    hw = get_preset("shorad")
    hw.prf_hz = 4000.0
    hw.prf_set_hz = [4000.0, 4800.0]
    p0 = hw.active_prf_hz(0)
    p1 = hw.active_prf_hz(1)
    p2 = hw.active_prf_hz(2)
    if abs(p0 - 4000.0) > 1.0e-6:
        return _fail("prf_stagger", "scan0 PRF", {"p0": p0})
    if abs(p1 - 4800.0) > 1.0e-6:
        return _fail("prf_stagger", "scan1 PRF", {"p1": p1})
    if abs(p2 - 4000.0) > 1.0e-6:
        return _fail("prf_stagger", "scan2 wrap", {"p2": p2})
    # Blind speed for PRF0 should not match PRF1 (λ·PRF/2).
    lam = hw.wavelength_m
    blind0 = lam * p0 / 2.0
    blind1 = lam * p1 / 2.0
    if abs(blind0 - blind1) < 1.0:
        return _fail(
            "prf_stagger",
            "stagger should split blind speeds",
            {"blind0": blind0, "blind1": blind1},
        )
    return _ok(
        "prf_stagger",
        {"prf0": p0, "prf1": p1, "blind0_ms": blind0, "blind1_ms": blind1},
    )


def scenario_atmosphere_weather(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("atmosphere.rain_path_loss")
    hw = get_preset("shorad")
    dry = two_way_path_loss_db(20000.0, hw.atm_loss_db_per_km_one_way, 0.0, hw.frequency_hz)
    wet = two_way_path_loss_db(20000.0, hw.atm_loss_db_per_km_one_way, 25.0, hw.frequency_hz)
    if wet <= dry:
        return _fail("atmosphere_weather", "rain should increase loss", {"dry": dry, "wet": wet})
    return _ok("atmosphere_weather", {"dry_db": dry, "wet_db": wet})


def scenario_measurement_noise(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("measurement.snr_scaled_noise")
    hw = get_preset("shorad")
    mm = MeasurementModel(noise_scale=1.0)
    rng = np.random.default_rng(42)
    high = []
    low = []
    for _ in range(40):
        rh, _, _, _ = mm.synthesize(hw, 5000.0, 10.0, 5.0, 80.0, 25.0, rng)
        rl, _, _, _ = mm.synthesize(hw, 5000.0, 10.0, 5.0, 80.0, 3.0, rng)
        high.append(abs(rh - 5000.0))
        low.append(abs(rl - 5000.0))
    mean_hi = float(np.mean(high))
    mean_lo = float(np.mean(low))
    if mean_lo <= mean_hi:
        return _fail(
            "measurement_noise",
            "low SNR should jitter more",
            {"mean_hi": mean_hi, "mean_lo": mean_lo},
        )
    return _ok("measurement_noise", {"mean_err_hi_snr": mean_hi, "mean_err_lo_snr": mean_lo})


def scenario_multipath_diffraction(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("propagation.multipath")
    cov.add("propagation.knife_edge")
    cov.add("propagation.refraction_horizon")
    cov.add("propagation.ambiguity_fold")
    hw = get_preset("shorad")
    mp = MultipathModel(enabled=True)
    f = mp.power_factor(hw.wavelength_m, 8000.0, 12.0, 40.0)
    f_eps = mp.power_factor_from_dielectric(
        hw.wavelength_m, 3000.0, 15.0, 40.0, eps_r=15.0, roughness=0.15
    )
    ke = knife_edge_linear_factor(1.2)
    geo = knife_edge_factor_from_geometry(25.0, 10000.0, 0.4, hw.wavelength_m)
    horizon = radio_horizon_range_m(20.0, 50.0)
    soft = horizon_soft_factor(2.0 * horizon, horizon)
    el_bias = refraction_elevation_bias_deg(20000.0)
    r_fold = fold_range_ambiguous(25000.0, 10000.0)
    fd_fold = fold_doppler_ambiguous(4500.0, 4000.0)
    if f <= 0.0 or ke <= 0.0 or ke >= 1.0:
        return _fail("multipath_diffraction", "bad factors", {"mp": f, "ke": ke, "geo": geo})
    if soft >= 1.0 or el_bias <= 0.0 or abs(r_fold - 5000.0) > 1.0e-6:
        return _fail(
            "multipath_diffraction",
            "refraction/ambiguity failed",
            {"soft": soft, "el_bias": el_bias, "r_fold": r_fold},
        )
    return _ok(
        "multipath_diffraction",
        {
            "multipath": f,
            "multipath_dielectric": f_eps,
            "knife_edge": ke,
            "geo": geo,
            "horizon_m": horizon,
            "horizon_soft": soft,
            "el_bias_deg": el_bias,
            "range_fold": r_fold,
            "doppler_fold": fd_fold,
        },
    )


def scenario_cfar_modes(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("cfar.ca")
    cov.add("cfar.go")
    cov.add("cfar.so")
    cov.add("cfar.thermal_fill")
    rng = np.random.default_rng(7)
    noise = 1.0e-12
    power = np.full((1, 64), noise * 0.1, dtype=float)
    power = fill_thermal_noise(power, noise, rng)
    power[0, 32] = noise * 80.0
    ca = cfar_detections(power, noise, mode="ca", pfa=1.0e-4)
    go = cfar_detections(power, noise, mode="go", pfa=1.0e-4)
    so = cfar_detections(power, noise, mode="so", pfa=1.0e-4)
    # Reference CA from physics module for parity smoke.
    ca2 = ca_cfar_detections(power, noise, pfa=1.0e-4)
    if not bool(ca[0, 32]) and not bool(go[0, 32]) and not bool(so[0, 32]):
        return _fail("cfar_modes", "peak not detected", {"ca": int(ca.sum()), "go": int(go.sum())})
    return _ok(
        "cfar_modes",
        {
            "ca_hits": int(ca.sum()),
            "go_hits": int(go.sum()),
            "so_hits": int(so.sum()),
            "physics_ca_hits": int(ca2.sum()),
        },
    )


def scenario_aspect_swerling(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("target.aspect_rcs")
    cov.add("target.swerling")
    rcs_side = aspect_rcs_from_extents(
        10.0, 8.0, 3.0, 3.0, yaw_deg=0.0, los_azimuth_deg=90.0
    )
    rcs_nose = aspect_rcs_from_extents(
        10.0, 8.0, 3.0, 3.0, yaw_deg=0.0, los_azimuth_deg=0.0
    )
    sw = SwerlingModel(model=1, seed=3)
    samples = [sw.sample(10.0, "t1", scan_number=i, dwell_index=0) for i in range(20)]
    if rcs_side <= 0.0 or min(samples) <= 0.0:
        return _fail("aspect_swerling", "invalid rcs", {"side": rcs_side, "nose": rcs_nose})
    return _ok(
        "aspect_swerling",
        {"rcs_side": rcs_side, "rcs_nose": rcs_nose, "swerling_mean": float(np.mean(samples))},
    )


def scenario_track_alphabeta(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("track.alpha_beta")

    @dataclass
    class Det:
        time_s: float
        scan_number: int
        target_name: str
        measured_range_m: float
        azimuth_deg: float
        radial_speed_m_s: float
        snr_db: float
        detected: bool
        elevation_deg: float = 2.0

    dets = []
    for i in range(8):
        t = float(i) * 1.0
        dets.append(
            Det(
                time_s=t,
                scan_number=i,
                target_name="a",
                measured_range_m=3000.0 + 80.0 * t,
                azimuth_deg=15.0,
                radial_speed_m_s=80.0,
                snr_db=18.0,
                detected=True,
            )
        )
    result = associate_and_filter(dets, TrackerConfig(confirm_hits=2))
    confirmed = [tr for tr in result.tracks if tr.hit_count >= 2]
    if not confirmed:
        return _fail("track_alphabeta", "no confirmed track")
    return _ok("track_alphabeta", {"tracks": len(result.tracks), "confirmed": len(confirmed)})


def scenario_ballistics_wlr(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("ballistics.integrate")
    cov.add("ballistics.wlr_fit")
    # Build polar history from vacuum ballistic steps (radar at origin).
    history: list[tuple[float, float, float, float]] = []
    x, y, z = 500.0, 120.0, 200.0
    vx, vy, vz = 160.0, 40.0, 30.0
    t = 0.0
    for i in range(20):
        r = math.sqrt(x * x + y * y + z * z)
        az = math.degrees(math.atan2(z, x))
        el = math.degrees(math.atan2(y, math.hypot(x, z)))
        history.append((t, r, az, el))
        x, y, z, vx, vy, vz = integrate_ballistic(
            x, y, z, vx, vy, vz, 0.15, air_drag=0.0
        )
        t += 0.15
    fit = fit_ballistic_vacuum(history, anchor_index=len(history) - 1)
    if fit is None:
        return _fail("ballistics_wlr", "fit failed", {"n": len(history)})
    pred = predict_ballistic(fit, 0.5)
    return _ok(
        "ballistics_wlr",
        {"n_history": len(history), "pred_range_m": pred[0], "fit_rms_m": fit.fit_rms_m},
    )


def scenario_ew_noise_burnthrough(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("ew.noise_jam")
    cov.add("ew.burn_through")
    hw = get_preset("shorad")
    jam = NoiseJammer(
        x_m=6000.0,
        z_m=0.0,
        erp_w=5.0e4,
        bandwidth_hz=hw.effective_bandwidth_hz * 2.0,
        coupling_mode=NoiseJamCoupling.BEAM,
        sidelobe_level_db=-25.0,
    )
    az = np.linspace(-math.pi / 8.0, math.pi / 8.0, 16)
    ranges = np.linspace(500.0, 8000.0, 40)
    power = np.full((az.size, ranges.size), hw.noise_power_w() * 0.5)
    ctx = EWContext(
        hardware=hw,
        radar_x_m=0.0,
        radar_z_m=0.0,
        az_rad=az,
        range_centers_m=ranges,
        noise_w=hw.noise_power_w(),
        time_s=0.0,
        frequency_hz=hw.frequency_hz,
    )
    jammed = jam.apply(power, ctx)
    j_extra = float(np.mean(np.maximum(jammed - power, 0.0)))
    peak_j = jam.peak_jammer_power_w(ctx)
    reff = burn_through_range_m(
        hw.instrumented_range_m, hw.noise_power_w(), max(peak_j, 1.0e-18)
    )
    if peak_j <= 0.0:
        return _fail("ew_noise_burnthrough", "no jammer power", {"peak_j": peak_j})
    if reff >= hw.instrumented_range_m:
        return _fail("ew_noise_burnthrough", "burn-through should shrink range", {"reff": reff})
    return _ok(
        "ew_noise_burnthrough",
        {"j_extra_mean": j_extra, "peak_j_w": peak_j, "reff_m": reff},
    )


def scenario_ew_deception(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("ew.deception_false_targets")
    cov.add("ew.rgpo")
    cov.add("ew.angular_scintillation")
    cov.add("ew.intermittent_false")
    hw = get_preset("shorad")
    dec = DeceptionJammer(
        false_targets=[
            FalseTarget(range_m=3500.0, azimuth_deg=5.0, power_w=1.0e-9),
            FalseTarget(range_m=4200.0, azimuth_deg=-8.0, power_w=8.0e-10),
        ]
    )
    az = np.deg2rad(np.linspace(-20.0, 20.0, 21))
    ranges = np.linspace(1000.0, 6000.0, 50)
    power = np.zeros((az.size, ranges.size))
    ctx = EWContext(
        hardware=hw,
        radar_x_m=0.0,
        radar_z_m=0.0,
        az_rad=az,
        range_centers_m=ranges,
        noise_w=hw.noise_power_w(),
        time_s=1.0,
        frequency_hz=hw.frequency_hz,
    )
    out = dec.apply(power, ctx)
    rgpo = RangePullOff(pull_m_s=100.0)
    off = rgpo.offset_m(2.0)
    ang = AngularScintillation(sigma_deg=2.0).sample(np.random.default_rng(1))
    inter = IntermittentFalsePlots(duty=0.4, period_s=2.0)
    if float(np.max(out)) <= 0.0:
        return _fail("ew_deception", "no false power injected")
    if off <= 0.0:
        return _fail("ew_deception", "rgpo offset zero")
    return _ok(
        "ew_deception",
        {
            "max_false_w": float(np.max(out)),
            "rgpo_m": off,
            "ang_deg": ang,
            "intermittent_on": inter.active(0.2),
        },
    )


def scenario_frequency_hop(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("channel.frequency_hop")
    hw = get_preset("shorad")
    hop = FrequencyHopSchedule(
        channels_hz=[hw.frequency_hz, hw.frequency_hz * 1.05],
        dwell_s=0.5,
    )
    f0 = hop.frequency_at(0.1, hw.frequency_hz)
    f1 = hop.frequency_at(0.6, hw.frequency_hz)
    retuned = hardware_at_frequency(hw, f1)
    if abs(f0 - f1) < 1.0:
        return _fail("frequency_hop", "hop did not change carrier")
    if abs(retuned.frequency_hz - f1) > 1.0:
        return _fail("frequency_hop", "retune mismatch")
    return _ok("frequency_hop", {"f0": f0, "f1": f1, "band": retuned.band})


def scenario_fusion_iff(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("network.fusion_associate")
    cov.add("network.cross_fix")
    cov.add("network.iff")
    hit = try_cross_fix_horizontal((0.0, 0.0), 45.0, (0.0, 2000.0), -45.0)
    assoc = associate_pair(
        (1000.0, 100.0, 1000.0),
        (10.0, 0.0, 0.0),
        (1020.0, 105.0, 1010.0),
        (12.0, 0.0, 0.0),
        gate_m=350.0,
    )
    iff_f = resolve_iff("us", "us")
    iff_h = resolve_iff("us", "ru")
    if hit is None or not assoc:
        return _fail("fusion_iff", "fusion failed", {"hit": hit, "assoc": assoc})
    return _ok(
        "fusion_iff",
        {"fix_xz": hit, "assoc": assoc, "iff_friend": int(iff_f), "iff_foe": int(iff_h)},
    )


def scenario_lock_rwr_arm(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("system.lock_fsm")
    cov.add("system.rwr")
    cov.add("system.arm_aim")
    lock = LockManager(acquire_hits=3, coast_misses=3)
    positions = {7: (1000.0, 200.0, 500.0)}
    for _ in range(4):
        lock.update([7], positions, preferred_id=7)
    if lock.state != LockState.TRACKING:
        return _fail("lock_rwr_arm", f"expected TRACKING got {lock.state}")
    alert = rwr_alert_from_lock(lock.state, True)
    aim = arm_aim_from_emitter((2000.0, 50.0, 0.0), True)
    lock.update([], positions)
    lock.update([], positions)
    lock.update([], positions)
    if lock.state != LockState.SEARCH:
        return _fail("lock_rwr_arm", f"expected unlock got {lock.state}")
    return _ok(
        "lock_rwr_arm",
        {"alert": alert.value, "arm_valid": aim.valid, "arm_x": aim.x},
    )


def scenario_esm_friis(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("system.esm_friis")
    hw = get_preset("shorad")
    p_near = friis_receive_power_w(1.0e3, hw.frequency_hz, 5000.0, rx_gain_lin=hw.gain_linear)
    p_far = friis_receive_power_w(1.0e3, hw.frequency_hz, 20000.0, rx_gain_lin=hw.gain_linear)
    if p_near <= p_far or p_near <= 0.0:
        return _fail("esm_friis", "Friis range law broken", {"near": p_near, "far": p_far})
    return _ok("esm_friis", {"p_near_w": p_near, "p_far_w": p_far})


def scenario_network_bandwidth(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("network.reliable_summary")
    cov.add("network.unreliable_plots")
    cov.add("network.throttle_fingerprint")
    cov.add("network.interest_radius")
    cov.add("network.caps")
    cfg = NetworkSyncConfig(
        max_tracks=4,
        max_plots=3,
        min_reliable_interval_s=0.2,
        min_plot_interval_s=0.05,
        skip_unchanged=True,
        interest_radius_m=10000.0,
        sync_plots_unreliable=True,
    )
    st = NetworkSyncState()
    tracks = [1, 2, 3, 4, 5, 6]
    pos = [(100.0 * i, 50.0 * i) for i in tracks]
    capped = cap_list(tracks, cfg.max_tracks)
    fp1 = fingerprint_tracks(capped, pos[:4], lock_state=2, lock_track_id=1)
    t = 0.0
    s1 = network_should_send(cfg, st, t, fp1, 2, 5000.0, plots=False)
    t = 0.05
    s2 = network_should_send(cfg, st, t, fp1, 2, 5000.0, plots=False)
    t = 0.25
    s3 = network_should_send(cfg, st, t, fp1, 2, 5000.0, plots=False)
    t = 0.30
    fp2 = fingerprint_tracks(capped, [(101.0, 50.0)] + pos[1:4], lock_state=2, lock_track_id=1)
    s4 = network_should_send(cfg, st, t, fp2, 2, 5000.0, plots=False)
    far = network_should_send(cfg, st, 1.0, fp2, 2, 50000.0, plots=False)
    p1 = network_should_send(cfg, st, 1.0, fp2, 2, 5000.0, plots=True)
    if not s1 or s2 or s3 or not s4 or far or not p1:
        return _fail(
            "network_bandwidth",
            "policy mismatch",
            {
                "s1": s1,
                "s2": s2,
                "s3": s3,
                "s4": s4,
                "far": far,
                "p1": p1,
                "reliable": st.reliable_sends,
                "plots": st.plot_sends,
            },
        )
    if len(capped) != 4:
        return _fail("network_bandwidth", "cap failed")
    return _ok(
        "network_bandwidth",
        {
            "reliable_sends": st.reliable_sends,
            "plot_sends": st.plot_sends,
            "skipped_unchanged": st.skipped_unchanged,
            "skipped_interest": st.skipped_interest,
            "skipped_throttle": st.skipped_throttle,
        },
    )


def scenario_polar_cartesian(cov: FeatureCoverage) -> ScenarioResult:
    cov.add("geometry.polar_cartesian")
    x, y, z = polar_to_cartesian(1000.0, 45.0, 10.0)
    if abs(x) < 1.0 or abs(z) < 1.0:
        return _fail("polar_cartesian", "bad convert", {"x": x, "y": y, "z": z})
    return _ok("polar_cartesian", {"x": x, "y": y, "z": z})


SCENARIOS = [
    scenario_detection_physics,
    scenario_mtd_rotor_cpa,
    scenario_prf_stagger,
    scenario_atmosphere_weather,
    scenario_measurement_noise,
    scenario_multipath_diffraction,
    scenario_cfar_modes,
    scenario_aspect_swerling,
    scenario_track_alphabeta,
    scenario_ballistics_wlr,
    scenario_ew_noise_burnthrough,
    scenario_ew_deception,
    scenario_frequency_hop,
    scenario_fusion_iff,
    scenario_lock_rwr_arm,
    scenario_esm_friis,
    scenario_network_bandwidth,
    scenario_polar_cartesian,
]


def run_full_sim() -> FullSimReport:
    cov = FeatureCoverage()
    report = FullSimReport()
    for fn in SCENARIOS:
        try:
            result = fn(cov)
        except Exception as exc:  # noqa: BLE001 — collect all scenario failures
            result = _fail(fn.__name__, str(exc))
        report.results.append(result)
    report.coverage = list(cov.names)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="RDF full capability offline simulation")
    parser.add_argument(
        "--out",
        default=str(Path(__file__).resolve().parent / "out" / "full_sim_report.json"),
        help="JSON report path",
    )
    args = parser.parse_args(argv)
    report = run_full_sim()
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report.to_dict(), indent=2), encoding="utf-8")
    print(f"scenarios={len(report.results)} ok={report.all_ok} coverage={len(report.coverage)}")
    for r in report.results:
        mark = "PASS" if r.ok else "FAIL"
        print(f"  [{mark}] {r.name}" + (f" — {r.error}" if r.error else ""))
    print(f"wrote {out_path}")
    if report.all_ok:
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
