#!/usr/bin/env python3
"""Single knife-edge diffraction helpers aligned with RDF_RadarPhysicalDetect."""

from __future__ import annotations

import math


def knife_edge_linear_factor(nu: float) -> float:
    """ITU-R P.526-ish approximate diffraction loss → linear power factor.

    nu <= -0.78 → ~1; larger nu (deeper obstacle above LOS) attenuates.
    Matches Enforce ``RDF_RadarPhysicalDetect.KnifeEdgeLinearFactor``.
    """
    if nu <= -0.78:
        return 1.0
    t = nu - 0.1
    inner = math.sqrt(t * t + 1.0) + t
    if inner < 0.001:
        inner = 0.001
    loss_db = 6.9 + 20.0 * math.log10(inner)
    if loss_db < 0.0:
        loss_db = 0.0
    factor = 10.0 ** (-loss_db / 10.0)
    if factor > 1.0:
        factor = 1.0
    if factor < 0.0:
        factor = 0.0
    return factor


def fresnel_nu(
    h_obs_m: float,
    d1_m: float,
    d2_m: float,
    wavelength_m: float,
) -> float:
    """Fresnel diffraction parameter ν for a single knife edge."""
    d1 = max(float(d1_m), 1.0)
    d2 = max(float(d2_m), 1.0)
    lam = max(float(wavelength_m), 0.001)
    distance = d1 + d2
    return float(h_obs_m) * math.sqrt(2.0 * distance / (lam * d1 * d2))


def obstacle_height_above_los(
    origin_xyz: tuple[float, float, float],
    target_xyz: tuple[float, float, float],
    u: float,
    terrain_y: float,
    clearance_slack_m: float = 0.0,
) -> float:
    """Height of terrain above the geometric LOS at fraction u in [0, 1]."""
    y_los = origin_xyz[1] + (target_xyz[1] - origin_xyz[1]) * u
    return float(terrain_y) - float(clearance_slack_m) - y_los


def knife_edge_factor_from_geometry(
    h_obs_m: float,
    range_m: float,
    u_edge: float,
    wavelength_m: float,
    min_factor: float = 0.008,
) -> float:
    """End-to-end factor for one edge; 0 when below min_factor or clear of terrain."""
    if h_obs_m <= 0.0:
        return 0.0
    u = float(u_edge)
    if u < 0.05:
        u = 0.05
    if u > 0.95:
        u = 0.95
    d1 = u * float(range_m)
    d2 = float(range_m) - d1
    nu = fresnel_nu(h_obs_m, d1, d2, wavelength_m)
    factor = knife_edge_linear_factor(nu)
    if factor < min_factor:
        return 0.0
    return factor
