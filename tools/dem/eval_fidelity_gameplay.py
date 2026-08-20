"""Offline gameplay-impact check for the 2026-08-19 fidelity knobs.

Uses in-game SHORAD defaults (RDF_RadarHardware + DetectionSnrDb=8,
atmospheric loss OFF). Compares each opt-in against the default-off baseline.
Does not mutate the game; print JSON to stdout.
"""

from __future__ import annotations

import json
import math

from rdf_radar_channel import radio_horizon_range_m
from rdf_radar_nctr import (
    NCTR_FAN,
    NCTR_FIXED,
    NCTR_ROTOR,
    NCTR_UNKNOWN,
    SURF_VEGETATION,
    SURF_WATER,
    class_to_short,
    classify,
    glint_elevation_bias_deg,
    rain_water_clutter_scale,
    track_quality,
)
from rdf_radar_physics import C_LIGHT_M_S, K_BOLTZMANN, T0_K, db_to_lin, get_preset, lin_to_db

DETECT_SNR_DB = 8.0
RWR_SNR_DB = 6.0
RWR_NOISE_MULT = 4.0
RWR_GAIN_DBI = 0.0
LPI_PEAK_SCALE = 0.15
LPI_RPM_SCALE = 0.5
WEAPON_MIN_CONF = 0.55
CONFIRM_HITS = 2
GATE_M = 400.0


def thermal_noise_w(hw):
    bw = hw.effective_bandwidth_hz
    if bw < 1.0:
        bw = 1.0
    return K_BOLTZMANN * T0_K * bw * db_to_lin(hw.noise_figure_db)


def radar_pr_w(hw, peak_w, sigma_m2, range_m):
    if range_m < 1.0:
        range_m = 1.0
    if sigma_m2 <= 0.0:
        return 0.0
    lam = C_LIGHT_M_S / hw.frequency_hz
    g = db_to_lin(hw.antenna_gain_dbi)
    four_pi = 4.0 * math.pi
    denom = (four_pi ** 3) * db_to_lin(hw.system_loss_db)
    const = peak_w * g * g * (lam ** 2) / denom
    return const * sigma_m2 / (range_m ** 4)


def snr_db_skin(hw, peak_w, sigma_m2, range_m):
    pr = radar_pr_w(hw, peak_w, sigma_m2, range_m)
    n = thermal_noise_w(hw)
    if n < 1.0e-30:
        n = 1.0e-30
    return lin_to_db(pr / n)


def range_for_snr(hw, peak_w, sigma_m2, snr_db):
    lo = 50.0
    hi = 80000.0
    for _ in range(48):
        mid = 0.5 * (lo + hi)
        if snr_db_skin(hw, peak_w, sigma_m2, mid) >= snr_db:
            lo = mid
        else:
            hi = mid
    return lo


def friis_pr_w(hw, peak_w, range_m):
    if range_m < 1.0:
        range_m = 1.0
    lam = C_LIGHT_M_S / hw.frequency_hz
    gt = db_to_lin(hw.antenna_gain_dbi)
    gr = db_to_lin(RWR_GAIN_DBI)
    loss = db_to_lin(hw.system_loss_db)
    four_pi = 4.0 * math.pi
    denom = (four_pi ** 2) * loss
    return peak_w * gt * gr * (lam ** 2) / denom / (range_m * range_m)


def rwr_snr_db(hw, peak_w, range_m):
    pr = friis_pr_w(hw, peak_w, range_m)
    n = thermal_noise_w(hw) * RWR_NOISE_MULT
    if n < 1.0e-30:
        n = 1.0e-30
    if pr <= 0.0:
        return -300.0
    return lin_to_db(pr / n)


def range_for_rwr(hw, peak_w):
    lo = 50.0
    hi = 200000.0
    for _ in range(48):
        mid = 0.5 * (lo + hi)
        if rwr_snr_db(hw, peak_w, mid) >= RWR_SNR_DB:
            lo = mid
        else:
            hi = mid
    return lo


def main():
    hw = get_preset("shorad")
    peak = hw.peak_power_w
    peak_lpi = peak * LPI_PEAK_SCALE

    targets = [
        ("UAZ / truck", 12.0),
        ("Mi-8 class heli", 25.0),
        ("fighter / jet", 6.0),
        ("82 mm shell", 0.015),
    ]

    detect_rows = []
    snr_ranges_km = [0.5, 1.0, 2.0, 4.0, 6.0, 8.0, 12.0]
    snr_series_surveil = []
    snr_series_lpi = []
    heli_sigma = 25.0
    for r_km in snr_ranges_km:
        r_m = r_km * 1000.0
        snr_series_surveil.append(round(snr_db_skin(hw, peak, heli_sigma, r_m), 2))
        snr_series_lpi.append(round(snr_db_skin(hw, peak_lpi, heli_sigma, r_m), 2))

    rwr_surveil = range_for_rwr(hw, peak)
    rwr_lpi = range_for_rwr(hw, peak_lpi)
    play_ranges_km = [2.0, 6.0, 8.0, 13.0]
    rwr_at_play = []
    for r_km in play_ranges_km:
        r_m = r_km * 1000.0
        rwr_at_play.append(
            {
                "range_km": r_km,
                "snr_surveil_db": round(rwr_snr_db(hw, peak, r_m), 1),
                "snr_lpi_db": round(rwr_snr_db(hw, peak_lpi, r_m), 1),
            }
        )

    shell_at_8 = snr_db_skin(hw, peak, 0.015, 8000.0)
    shell_lpi_at_8 = snr_db_skin(hw, peak_lpi, 0.015, 8000.0)

    for name, sigma in targets:
        r0 = range_for_snr(hw, peak, sigma, DETECT_SNR_DB)
        r_lpi = range_for_snr(hw, peak_lpi, sigma, DETECT_SNR_DB)
        detect_rows.append(
            {
                "name": name,
                "sigma_m2": sigma,
                "detect_m": round(r0, 0),
                "detect_lpi_m": round(r_lpi, 0),
                "range_ratio": round(r_lpi / r0, 3),
                "rwr_still_hears_at_detect": rwr_surveil >= r0,
                "rwr_lpi_hears_at_detect": rwr_lpi >= r_lpi,
            }
        )

    nctr_cases = []
    for label, tip, frac, fan_tip, fan_frac, side, snr in [
        ("Mi-8 hovering, MTD sideband", 80.0, 0.25, 0.0, 0.0, True, 12.0),
        ("Mi-8 edge SNR, no sideband", 80.0, 0.25, 0.0, 0.0, False, 8.0),
        ("UAZ skin return", 0.0, 0.0, 0.0, 0.0, False, 10.0),
        ("jet fan + sideband", 0.0, 0.0, 320.0, 0.07, True, 11.0),
        ("shell / debris", 0.0, 0.0, 0.0, 0.0, False, 5.0),
        ("below detect floor", 80.0, 0.25, 0.0, 0.0, False, -1.0),
    ]:
        cls = classify(tip, frac, fan_tip, fan_frac, side, snr)
        nctr_cases.append(
            {
                "scene": label,
                "class": class_to_short(cls),
                "code": cls,
                "snr_db": snr,
            }
        )

    fire_rows = []
    pass_count = 0
    total = 0
    for hits in (1, 2, 4, 8):
        for snr in (4.0, 8.0, 13.0, 20.0):
            for residual in (0.0, 80.0, 200.0):
                for coast in (False, True):
                    q = track_quality(hits, CONFIRM_HITS, snr, residual, GATE_M, coast)
                    ok = q >= WEAPON_MIN_CONF
                    total = total + 1
                    if ok:
                        pass_count = pass_count + 1
                    if residual == 80.0:
                        if snr == 8.0 or snr == 13.0:
                            if hits == 1 or hits == 2 or hits == 4:
                                fire_rows.append(
                                    {
                                        "hits": hits,
                                        "snr_db": snr,
                                        "residual_m": residual,
                                        "coasting": coast,
                                        "confidence": round(q, 3),
                                        "authorize": ok,
                                    }
                                )

    # Typical lock: confirmed, SNR 10, residual 40, not coasting.
    typical_conf = track_quality(4, CONFIRM_HITS, 10.0, 40.0, GATE_M, False)
    typical_coast = track_quality(4, CONFIRM_HITS, 10.0, 40.0, GATE_M, True)
    first_hit = track_quality(1, CONFIRM_HITS, 8.0, 80.0, GATE_M, False)

    glint_water = glint_elevation_bias_deg(20.0, SURF_WATER, 1.0, 4000.0)
    glint_veg = glint_elevation_bias_deg(20.0, SURF_VEGETATION, 1.0, 4000.0)
    height_err_water_m = 4000.0 * math.tan(abs(glint_water) * math.pi / 180.0)
    glint_high = glint_elevation_bias_deg(400.0, SURF_WATER, 1.0, 4000.0)
    height_err_high_m = 4000.0 * math.tan(abs(glint_high) * math.pi / 180.0)

    rain0 = rain_water_clutter_scale(0.0)
    rain1 = rain_water_clutter_scale(1.0)

    hz_shorad = radio_horizon_range_m(8.0, 25.0, 4.0 / 3.0)
    hz_duct = hz_shorad * 2.0
    hz_p18 = radio_horizon_range_m(20.0, 80.0, 4.0 / 3.0)

    report = {
        "baseline": "all m_Enable* off; SHORAD CreateShorad(); atm loss off; detect 8 dB",
        "verdict": {
            "default_off_impact": "none",
            "largest_if_enabled": "LPI search (range x0.62, scan period x2)",
            "weapon_grade_typical_lock": typical_conf >= WEAPON_MIN_CONF,
            "nctr_changes_pd": False,
        },
        "lpi": {
            "peak_scale": LPI_PEAK_SCALE,
            "rpm_scale": LPI_RPM_SCALE,
            "scan_period_s_surveil": round(60.0 / hw.scan_rpm, 2),
            "scan_period_s_lpi": round(60.0 / (hw.scan_rpm * LPI_RPM_SCALE), 2),
            "r4_range_ratio": round(LPI_PEAK_SCALE ** 0.25, 3),
            "detect": detect_rows,
            "heli_snr_db": {
                "range_km": snr_ranges_km,
                "surveil": snr_series_surveil,
                "lpi": snr_series_lpi,
            },
        },
        "rwr_friis": {
            "intercept_m_surveil": round(rwr_surveil, 0),
            "intercept_m_lpi": round(rwr_lpi, 0),
            "ratio": round(rwr_lpi / rwr_surveil, 3),
            "at_play_ranges": rwr_at_play,
            "note": "geometric RWR (default) reports whenever this radar already detected; Friis is extra range gate",
        },
        "play_envelope": {
            "search_range_m": 2000.0,
            "wlr_range_m": 8000.0,
            "airborne_demo_m": 13000.0,
            "shell_snr_db_at_8km_surveil": round(shell_at_8, 2),
            "shell_snr_db_at_8km_lpi": round(shell_lpi_at_8, 2),
            "shell_detects_at_8km_surveil": shell_at_8 >= DETECT_SNR_DB,
            "shell_detects_at_8km_lpi": shell_lpi_at_8 >= DETECT_SNR_DB,
        },
        "nctr": {
            "changes_detection": False,
            "cases": nctr_cases,
            "unknown_code": NCTR_UNKNOWN,
            "fixed_code": NCTR_FIXED,
            "rotor_code": NCTR_ROTOR,
            "fan_code": NCTR_FAN,
        },
        "weapon_grade": {
            "min_confidence": WEAPON_MIN_CONF,
            "grid_pass_frac": round(pass_count / float(total), 3),
            "grid_n": total,
            "typical_confirmed": round(typical_conf, 3),
            "typical_coast": round(typical_coast, 3),
            "first_hit_edge": round(first_hit, 3),
            "rows": fire_rows,
        },
        "environment": {
            "glint_el_deg_water_20m_agl_4km": round(glint_water, 2),
            "glint_height_err_m_water": round(height_err_water_m, 1),
            "glint_el_deg_veg_20m_agl_4km": round(glint_veg, 2),
            "glint_el_deg_water_400m_agl": round(glint_high, 2),
            "glint_height_err_m_high": round(height_err_high_m, 1),
            "rain_water_sigma0_scale_dry": rain0,
            "rain_water_sigma0_scale_full": rain1,
            "horizon_m_shorad_8m_25m": round(hz_shorad, 0),
            "horizon_m_duct_x2": round(hz_duct, 0),
            "horizon_m_p18_20m_80m": round(hz_p18, 0),
            "everon_span_m": 15000.0,
        },
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
