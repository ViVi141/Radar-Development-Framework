#!/usr/bin/env python3
"""Fixed-site DEM polar path-factor LUT (az × range).

Bakes terrain-only LOS / single-knife factors for a stationary radar origin.
Enforce loads the slim table via ``RDF_RadarSitePathLut``.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import numpy as np

from rdf_radar_diffraction import knife_edge_factor_from_geometry

SCHEMA = "RDF_SITE_PATH_LUT_V1"


def synthetic_ridge_height(
    x_m: float,
    z_m: float,
    ridge_z_m: float = 800.0,
    ridge_half_m: float = 80.0,
    ridge_h_m: float = 45.0,
    base_y_m: float = 0.0,
) -> float:
    """Single Gaussian ridge parallel to X — unit-test / demo DEM."""
    dz = z_m - ridge_z_m
    bump = ridge_h_m * math.exp(-0.5 * (dz / ridge_half_m) ** 2)
    return base_y_m + bump


def make_grid_sampler(
    grid: np.ndarray,
    cell_m: float,
    bmin_x: float,
    bmin_z: float,
) -> Callable[[float, float], float]:
    gh, gw = grid.shape

    def sample(x_m: float, z_m: float) -> float:
        ix = int(math.floor((x_m - bmin_x) / cell_m))
        iz = int(math.floor((z_m - bmin_z) / cell_m))
        if ix < 0:
            ix = 0
        if iz < 0:
            iz = 0
        if ix >= gw:
            ix = gw - 1
        if iz >= gh:
            iz = gh - 1
        return float(grid[iz, ix])

    return sample


def path_factor_along_ray(
    origin_xyz: tuple[float, float, float],
    az_rad: float,
    range_m: float,
    sample_y: Callable[[float, float], float],
    wavelength_m: float,
    n_samples: int = 12,
    clearance_slack_m: float = 2.0,
    target_agl_m: float = 5.0,
) -> float:
    """Terrain-only factor to a point at (az, range) with target AGL."""
    if range_m < 1.0:
        return 1.0
    ox, oy, oz = origin_xyz
    tx = ox + range_m * math.cos(az_rad)
    tz = oz + range_m * math.sin(az_rad)
    ty = sample_y(tx, tz) + target_agl_m
    best_h = -1.0e9
    best_u = 0.5
    for i in range(1, n_samples + 1):
        u = i / (n_samples + 1.0)
        x = ox + (tx - ox) * u
        z = oz + (tz - oz) * u
        y_los = oy + (ty - oy) * u
        terrain = sample_y(x, z)
        h_obs = terrain - clearance_slack_m - y_los
        if h_obs > best_h:
            best_h = h_obs
            best_u = u
    if best_h <= 0.0:
        return 1.0
    return knife_edge_factor_from_geometry(
        best_h,
        range_m,
        best_u,
        wavelength_m,
        min_factor=0.0,
    )


def build_site_path_lut(
    sample_y: Callable[[float, float], float],
    origin_xyz: tuple[float, float, float],
    *,
    wavelength_m: float = 0.032,
    max_range_m: float = 4000.0,
    az_count: int = 72,
    range_count: int = 40,
    n_samples: int = 12,
    clearance_slack_m: float = 2.0,
    target_agl_m: float = 5.0,
    world_name: str = "synthetic",
) -> dict:
    if az_count < 4 or range_count < 4:
        raise ValueError("az_count/range_count must be >= 4")
    az_deg = np.linspace(0.0, 360.0, az_count, endpoint=False, dtype=np.float64)
    # Range bins: centers from max_range/range_count … max_range
    dr = max_range_m / range_count
    ranges = (np.arange(range_count, dtype=np.float64) + 0.5) * dr
    factors = np.zeros((az_count, range_count), dtype=np.float64)
    for ia, az in enumerate(az_deg):
        az_rad = math.radians(float(az))
        for ir, r in enumerate(ranges):
            factors[ia, ir] = path_factor_along_ray(
                origin_xyz,
                az_rad,
                float(r),
                sample_y,
                wavelength_m,
                n_samples=n_samples,
                clearance_slack_m=clearance_slack_m,
                target_agl_m=target_agl_m,
            )
    return {
        "schema": SCHEMA,
        "world": world_name,
        "origin_x": float(origin_xyz[0]),
        "origin_y": float(origin_xyz[1]),
        "origin_z": float(origin_xyz[2]),
        "wavelength_m": float(wavelength_m),
        "max_range_m": float(max_range_m),
        "az_count": int(az_count),
        "range_count": int(range_count),
        "az_deg": az_deg,
        "range_m": ranges,
        "factor": factors,
        "target_agl_m": float(target_agl_m),
        "clearance_slack_m": float(clearance_slack_m),
        "bytes": int(factors.nbytes),
    }


def interp_site_path_lut(lut: dict, az_deg: float, range_m: float) -> float:
    """Nearest-az + linear range interp; clamp outside max range."""
    azs = lut["az_deg"]
    rs = lut["range_m"]
    table = lut["factor"]
    az_count = int(lut["az_count"])
    # Wrap az to [0, 360).
    az = float(az_deg) % 360.0
    if az < 0.0:
        az += 360.0
    # Nearest azimuth bin (uniform).
    step = 360.0 / az_count
    ia = int(round(az / step)) % az_count
    if range_m <= float(rs[0]):
        return float(table[ia, 0])
    if range_m >= float(rs[-1]):
        return float(table[ia, -1])
    # Linear in range.
    ir = int(np.searchsorted(rs, range_m) - 1)
    if ir < 0:
        ir = 0
    if ir > len(rs) - 2:
        ir = len(rs) - 2
    r0 = float(rs[ir])
    r1 = float(rs[ir + 1])
    t = (range_m - r0) / max(r1 - r0, 1e-6)
    a = float(table[ia, ir])
    b = float(table[ia, ir + 1])
    return a + (b - a) * t


def lut_to_enforce_json(lut: dict) -> dict:
    flat = [float(x) for x in np.asarray(lut["factor"]).reshape(-1)]
    return {
        "schema": SCHEMA,
        "world": str(lut.get("world", "")),
        "origin_x": float(lut["origin_x"]),
        "origin_y": float(lut["origin_y"]),
        "origin_z": float(lut["origin_z"]),
        "max_range_m": float(lut["max_range_m"]),
        "az_count": int(lut["az_count"]),
        "range_count": int(lut["range_count"]),
        "wavelength_m": float(lut["wavelength_m"]),
        "factors": flat,
    }


def build_synthetic_site_lut(
    origin_xyz: tuple[float, float, float] = (0.0, 25.0, 0.0),
    **kwargs,
) -> dict:
    return build_site_path_lut(synthetic_ridge_height, origin_xyz, world_name="synthetic", **kwargs)


def try_build_from_dem_dir(
    dem_dir: Path,
    origin_xyz: tuple[float, float, float],
    **kwargs,
) -> dict:
    from rdf_dem_runtime_bench import load_height_grid

    height = load_height_grid(Path(dem_dir))
    sample = make_grid_sampler(
        height["grid"],
        height["cell_m"],
        height["bmin_x"],
        height["bmin_z"],
    )
    world = Path(dem_dir).name
    return build_site_path_lut(sample, origin_xyz, world_name=world, **kwargs)


def validate_site_path_lut(lut: dict, expect_ridge: bool = True) -> dict:
    """Sanity: values in [0,1]; synthetic ridge scene has a dip on +Z."""
    table = np.asarray(lut["factor"], dtype=np.float64)
    tmin = float(table.min())
    tmax = float(table.max())
    range_ok = tmin >= -1e-6 and tmax <= 1.0 + 1e-6
    clear = interp_site_path_lut(lut, 0.0, 500.0)
    ridge = interp_site_path_lut(lut, 90.0, 800.0)
    ridge_ok = True
    if expect_ridge:
        ridge_ok = clear > 0.9 and ridge < clear
    ok = range_ok and ridge_ok
    return {
        "ok": bool(ok),
        "range_ok": bool(range_ok),
        "ridge_ok": bool(ridge_ok),
        "factor_min": tmin,
        "factor_max": tmax,
        "clear_az0_r500": float(clear),
        "ridge_az90_r800": float(ridge),
    }


@dataclass
class SitePathCaseResult:
    name: str
    ok: bool
    metrics: dict


def run_site_path_validation() -> dict:
    lut = build_synthetic_site_lut()
    check = validate_site_path_lut(lut)
    return {
        "all_ok": bool(check["ok"]),
        "validation": check,
        "az_count": lut["az_count"],
        "range_count": lut["range_count"],
        "bytes": lut["bytes"],
        "table": lut_to_enforce_json(lut),
    }
