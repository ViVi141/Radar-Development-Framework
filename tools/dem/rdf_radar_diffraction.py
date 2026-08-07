#!/usr/bin/env python3
"""Knife-edge diffraction helpers aligned with RDF_RadarPhysicalDetect.

Supports legacy single-edge and dual dominant-edge Epstein–Peterson cascade
(``m_EnableDualKnifeEdge`` default path in Enforce).
"""

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


def dual_knife_edge_factor_from_geometry(
    h1_m: float,
    u1: float,
    h2_m: float,
    u2: float,
    range_m: float,
    wavelength_m: float,
    min_factor: float = 0.008,
    min_u_sep: float = 0.10,
) -> float:
    """Dual-dominant cascade matching Enforce ``KnifeEdgeFactorTwoEdges``.

    Stronger full-path edge × runner-up local segment factor (min extra 0.25).
    Heights are above radar→target LOS.
    """
    if h1_m <= 0.0 and h2_m <= 0.0:
        return 0.0
    if h2_m <= 0.0:
        return knife_edge_factor_from_geometry(
            h1_m, range_m, u1, wavelength_m, min_factor
        )
    if h1_m <= 0.0:
        return knife_edge_factor_from_geometry(
            h2_m, range_m, u2, wavelength_m, min_factor
        )

    du = abs(float(u1) - float(u2))
    if du < float(min_u_sep):
        if h1_m >= h2_m:
            return knife_edge_factor_from_geometry(
                h1_m, range_m, u1, wavelength_m, min_factor
            )
        return knife_edge_factor_from_geometry(
            h2_m, range_m, u2, wavelength_m, min_factor
        )

    # Sort along path: edge1 then edge2.
    if float(u1) <= float(u2):
        ua, ha = float(u1), float(h1_m)
        ub, hb = float(u2), float(h2_m)
        main_is_first = True
        f_a = knife_edge_factor_from_geometry(
            h1_m, range_m, u1, wavelength_m, min_factor=0.0
        )
        f_b = knife_edge_factor_from_geometry(
            h2_m, range_m, u2, wavelength_m, min_factor=0.0
        )
    else:
        ua, ha = float(u2), float(h2_m)
        ub, hb = float(u1), float(h1_m)
        f_a = knife_edge_factor_from_geometry(
            h2_m, range_m, u2, wavelength_m, min_factor=0.0
        )
        f_b = knife_edge_factor_from_geometry(
            h1_m, range_m, u1, wavelength_m, min_factor=0.0
        )
        main_is_first = f_a >= f_b

    if ua < 0.05:
        ua = 0.05
    if ub > 0.95:
        ub = 0.95
    if ub <= ua + 0.02:
        return knife_edge_factor_from_geometry(
            h1_m, range_m, u1, wavelength_m, min_factor
        )

    if f_a >= f_b:
        f_main = f_a
        main_is_first = True
    else:
        f_main = f_b
        main_is_first = False

    distance = float(range_m)
    lam = max(float(wavelength_m), 0.001)
    d_tx_e1 = max(ua * distance, 1.0)
    d_e1_e2 = max((ub - ua) * distance, 1.0)
    d_e2_rx = max((1.0 - ub) * distance, 1.0)
    if main_is_first:
        span = d_e1_e2 + d_e2_rx
        nu_sec = hb * math.sqrt(2.0 * span / (lam * d_e1_e2 * d_e2_rx))
    else:
        span = d_tx_e1 + d_e1_e2
        nu_sec = ha * math.sqrt(2.0 * span / (lam * d_tx_e1 * d_e1_e2))

    f_sec = knife_edge_linear_factor(nu_sec)
    if f_sec < 0.25:
        f_sec = 0.25
    factor = f_main * f_sec
    if factor < min_factor:
        return 0.0
    return factor


def select_dual_dominant_edges(
    edges: list[tuple[float, float]],
    min_u_sep: float = 0.10,
) -> list[tuple[float, float]]:
    """From (u, h_obs) samples, return 0–2 dominant edges (best + spaced #2)."""
    positive = [(float(u), float(h)) for u, h in edges if h > 0.0]
    if not positive:
        return []
    positive.sort(key=lambda e: e[1], reverse=True)
    best_u, best_h = positive[0]
    second = None
    for u, h in positive[1:]:
        if abs(u - best_u) < float(min_u_sep):
            continue
        second = (u, h)
        break
    if second is None:
        return [(best_u, best_h)]
    return [(best_u, best_h), second]
