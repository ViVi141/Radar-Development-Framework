"""NCTR / track-quality / glint / rain-sea scale (offline mirror of RDF_RadarNctr.c).

Does not use entity type. Rotor vs fan vs fixed vs unknown from micro-Doppler
observables only. Import-light — same convention as rdf_radar_eccm.py.
"""

from __future__ import annotations

import math

NCTR_UNKNOWN = 0
NCTR_FIXED = 1
NCTR_ROTOR = 2
NCTR_FAN = 3

ROTOR_FRAC_MIN = 0.15
FAN_FRAC_MAX = 0.149
TIP_CUE_MS = 40.0

SURF_UNKNOWN = 0
SURF_WATER = 1
SURF_VEGETATION = 2


def classify(rotor_tip_ms, rotor_frac, fan_tip_ms, fan_frac, sideband_used, snr_db):
    if snr_db < 0.0:
        return NCTR_UNKNOWN

    rotor_cue = False
    if rotor_tip_ms > TIP_CUE_MS:
        if rotor_frac >= ROTOR_FRAC_MIN:
            rotor_cue = True
    fan_cue = False
    if fan_tip_ms > TIP_CUE_MS:
        if fan_frac > 0.0:
            if fan_frac < FAN_FRAC_MAX:
                fan_cue = True

    if rotor_cue:
        if sideband_used:
            return NCTR_ROTOR
        if snr_db >= 6.0:
            return NCTR_ROTOR
    if fan_cue:
        if sideband_used:
            return NCTR_FAN
        if snr_db >= 8.0:
            return NCTR_FAN
    if snr_db >= 4.0:
        return NCTR_FIXED
    return NCTR_UNKNOWN


def classify_confidence(nctr_class, sideband_used, snr_db):
    if nctr_class == NCTR_UNKNOWN:
        return 0.08

    snr_term = snr_db / 20.0
    if snr_term < 0.0:
        snr_term = 0.0
    if snr_term > 1.0:
        snr_term = 1.0

    conf = 0.35 + 0.45 * snr_term
    if sideband_used:
        conf = conf + 0.2
    if nctr_class == NCTR_FIXED:
        conf = conf * 0.85
    if conf < 0.05:
        conf = 0.05
    if conf > 1.0:
        conf = 1.0
    return conf


def track_quality(hit_count, confirm_hits, snr_db, residual_m, gate_m, coasting):
    hits = float(hit_count)
    need = float(confirm_hits)
    if need < 1.0:
        need = 1.0
    age = hits / need
    if age > 1.0:
        age = 1.0

    snr_term = snr_db / 20.0
    if snr_term < 0.0:
        snr_term = 0.0
    if snr_term > 1.0:
        snr_term = 1.0

    res_term = 1.0
    if gate_m > 1.0:
        if residual_m > 0.0:
            res_term = 1.0 - (residual_m / gate_m)
    if res_term < 0.0:
        res_term = 0.0
    if res_term > 1.0:
        res_term = 1.0

    conf = 0.45 * age + 0.35 * snr_term + 0.20 * res_term
    if coasting:
        conf = conf * 0.6
    if conf < 0.0:
        conf = 0.0
    if conf > 1.0:
        conf = 1.0
    return conf


def class_to_short(nctr_class):
    if nctr_class == NCTR_ROTOR:
        return "rotor"
    if nctr_class == NCTR_FAN:
        return "fan"
    if nctr_class == NCTR_FIXED:
        return "fixed"
    return "?"


def glint_elevation_bias_deg(agl_m, surface_class, world_time_s, range_m):
    if agl_m < 0.0:
        return 0.0
    if range_m < 50.0:
        return 0.0

    floor_agl = agl_m
    if floor_agl < 5.0:
        floor_agl = 5.0
    amp = 18.0 / floor_agl
    if amp > 2.5:
        amp = 2.5

    surf = 0.35
    if surface_class == SURF_WATER:
        surf = 1.0
    elif surface_class == SURF_VEGETATION:
        surf = 0.15

    phase = world_time_s * 1.7
    swing = 1.0 + 0.35 * math.sin(phase)
    return -amp * surf * swing


def rain_water_clutter_scale(rain_intensity):
    r = rain_intensity
    if r < 0.0:
        r = 0.0
    if r > 1.0:
        r = 1.0
    scale = 1.0 - 0.55 * r
    if scale < 0.35:
        scale = 0.35
    return scale
