#!/usr/bin/env python3
"""Offline range–azimuth ground-clutter intensity (movie-style PPI preview).

Uses GM_Eden DEM σ⁰ · projected cell / R^4 with simple LOS horizon cut.
Writes Cartesian range–az + polar PPI PNGs under tools/dem/out/.

  python render_range_az_clutter.py
  python render_range_az_clutter.py --world GM_Eden --max-range 6000 --sector-deg 60
"""

from __future__ import annotations

import argparse
import math
import os
import sys

import matplotlib.pyplot as plt
import numpy as np

# Prefer a CJK-capable font on Windows so howto labels render.
plt.rcParams["font.sans-serif"] = [
    "Microsoft YaHei",
    "SimHei",
    "Noto Sans CJK SC",
    "DejaVu Sans",
]
plt.rcParams["axes.unicode_minus"] = False

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from rdf_dem_io import choose_radar_site, resolve_dem_source
from rdf_radar_materials import Sigma0Table, sigma0_linear

C_LIGHT = 299792458.0
FREQ_HZ = 16.0e9
BW_HZ = 8.0e6
RADAR_AGL_M = 8.0
NOISE_FLOOR = 1.0e-22


def range_bin_m() -> float:
    return C_LIGHT / (2.0 * BW_HZ)


def build_intensity(
    dem: dict,
    radar_ix: int,
    radar_iz: int,
    radar_y: float,
    az0_rad: float,
    sector_rad: float,
    max_range_m: float,
    n_az: int,
    n_rng: int,
    band: str,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return (power[n_az, n_rng], az_centers_rad, range_centers_m)."""
    terrain = dem["terrain"]
    slope = dem["slope"]
    surface = dem["surface"]
    cell_m = float(dem["cell_m"])
    height, width = terrain.shape
    table = Sigma0Table.builtin(band)

    az_edges = np.linspace(az0_rad - 0.5 * sector_rad, az0_rad + 0.5 * sector_rad, n_az + 1)
    rng_edges = np.linspace(cell_m, max_range_m, n_rng + 1)
    az_c = 0.5 * (az_edges[:-1] + az_edges[1:])
    rng_c = 0.5 * (rng_edges[:-1] + rng_edges[1:])
    power = np.full((n_az, n_rng), NOISE_FLOOR, dtype=np.float64)

    # Sample along each ray at DEM cell spacing; deposit into range bins.
    step_m = max(cell_m, 0.5 * range_bin_m())
    n_steps = int(max_range_m / step_m) + 2

    for ia, az in enumerate(az_c):
        dx = math.cos(az)
        dz = math.sin(az)
        max_elev = -1e9
        blocked = False
        for step in range(1, n_steps + 1):
            r = step * step_m
            if r > max_range_m:
                break
            ix = int(round(radar_ix + dx * (r / cell_m)))
            iz = int(round(radar_iz + dz * (r / cell_m)))
            if ix < 1 or iz < 1 or ix >= width - 1 or iz >= height - 1:
                break
            ty = float(terrain[iz, ix])
            if math.isnan(ty):
                continue

            # Geometric elevation to this ground cell (radar look-down).
            elev = math.atan2(ty - radar_y, r)
            if elev > max_elev + 1e-4:
                max_elev = elev
                blocked = False
            elif elev < max_elev - 0.002:
                # Behind local horizon — no return into the display.
                blocked = True
            if blocked:
                continue

            # Local grazing ≈ look angle vs slope (simple RDF-like).
            slope_deg = float(slope[iz, ix])
            grazing = max(0.02, abs(elev) + math.radians(max(slope_deg, 0.0)) * 0.15)
            surf = int(surface[iz, ix])
            s0 = sigma0_linear(surf, grazing, table)

            # Azimuth cell width grows with range; range cell ≈ step.
            az_width_m = max(step_m, r * (sector_rad / max(n_az, 1)))
            area = step_m * az_width_m
            sigma = s0 * area
            # Relative received power ∝ σ / R^4 (drop common radar constant).
            pr = sigma / max(r, 50.0) ** 4

            ir = int((r - rng_edges[0]) / (rng_edges[-1] - rng_edges[0]) * n_rng)
            if ir < 0:
                ir = 0
            if ir >= n_rng:
                ir = n_rng - 1
            power[ia, ir] += pr

    return power, az_c, rng_c


def inject_demo_targets(power: np.ndarray, az_c: np.ndarray, rng_c: np.ndarray) -> list[dict]:
    """Bright synthetic aircraft/shell blips for movie readability."""
    targets = [
        {"name": "heli", "az_off_deg": -8.0, "range_m": 2200.0, "gain": 400.0},
        {"name": "shell", "az_off_deg": 12.0, "range_m": 3100.0, "gain": 180.0},
        {"name": "truck", "az_off_deg": 3.0, "range_m": 900.0, "gain": 250.0},
    ]
    az0 = float(np.mean(az_c))
    placed = []
    live = power > (NOISE_FLOOR * 10.0)
    if np.count_nonzero(live) > 0:
        base = float(np.median(power[live]))
    else:
        base = NOISE_FLOOR * 1.0e6
    for t in targets:
        az = az0 + math.radians(t["az_off_deg"])
        ia = int(np.argmin(np.abs(az_c - az)))
        ir = int(np.argmin(np.abs(rng_c - t["range_m"])))
        for dia in range(-2, 3):
            for dir_ in range(-2, 3):
                ja = ia + dia
                jr = ir + dir_
                if ja < 0 or jr < 0 or ja >= power.shape[0] or jr >= power.shape[1]:
                    continue
                dist2 = dia * dia + dir_ * dir_
                if dist2 == 0:
                    w = 1.0
                elif dist2 <= 2:
                    w = 0.55
                else:
                    w = 0.22
                power[ja, jr] += t["gain"] * w * base
        placed.append({"name": t["name"], "ia": ia, "ir": ir, "az": az, "r": float(rng_c[ir])})
    return placed


def display_db(
    power: np.ndarray,
    rng_c: np.ndarray | None = None,
    compensate_r4: bool = True,
) -> tuple[np.ndarray, float, float]:
    """Display stretch. Default: ×R^4 so terrain σ structure is not crushed by near field."""
    work = np.maximum(power, NOISE_FLOOR).astype(np.float64)
    if compensate_r4 and rng_c is not None:
        r = np.maximum(rng_c.astype(np.float64), 80.0)
        work = work * (r[np.newaxis, :] ** 4)

    db = 10.0 * np.log10(work)
    live = power > (NOISE_FLOOR * 1.0e3)
    if rng_c is not None:
        # Ignore ultra-near bins when picking stretch limits.
        near = rng_c < 250.0
        live = live & (~near[np.newaxis, :])
    if np.count_nonzero(live) < 32:
        live = power > (NOISE_FLOOR * 1.0e3)
    vals = db[live]
    vmin = float(np.percentile(vals, 8))
    # Clip highs so mid clutter uses cyan→yellow, strong peaks go white.
    vmax = float(np.percentile(vals, 88))
    span = vmax - vmin
    if span < 10.0:
        vmax = vmin + 10.0
    if span > 28.0:
        vmax = vmin + 28.0
    # Mild gamma in dB space: expand midtones.
    out = db.copy()
    out[~np.isfinite(out)] = vmin - 10.0
    out[power <= (NOISE_FLOOR * 1.0e3)] = vmin - 10.0
    return out, vmin, vmax


def phosphor_cmap():
    """Black (weak) → green → yellow → white (strong); more mid contrast."""
    from matplotlib.colors import LinearSegmentedColormap

    return LinearSegmentedColormap.from_list(
        "radar_phosphor",
        [
            (0.00, "#000000"),
            (0.08, "#001400"),
            (0.20, "#064a06"),
            (0.35, "#18a018"),
            (0.50, "#7dff2a"),
            (0.65, "#d4ff00"),
            (0.80, "#ffcc33"),
            (0.92, "#fff0a8"),
            (1.00, "#ffffff"),
        ],
    )


def heat_cmap():
    """Dark (weak) → blue → cyan → yellow → orange → white (strong)."""
    from matplotlib.colors import LinearSegmentedColormap

    return LinearSegmentedColormap.from_list(
        "radar_heat",
        [
            (0.00, "#050510"),
            (0.10, "#1a0a40"),
            (0.25, "#2040c0"),
            (0.40, "#00b8d4"),
            (0.55, "#40e070"),
            (0.70, "#f0e020"),
            (0.85, "#ff7000"),
            (1.00, "#ffffff"),
        ],
    )


def save_cartesian(path: str, power: np.ndarray, az_c: np.ndarray, rng_c: np.ndarray, title: str):
    db, vmin, vmax = display_db(power, rng_c=rng_c, compensate_r4=True)
    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=140, facecolor="#101018")
    ax.set_facecolor("#101018")
    extent = [
        float(rng_c[0]),
        float(rng_c[-1]),
        math.degrees(float(az_c[0])),
        math.degrees(float(az_c[-1])),
    ]
    im = ax.imshow(
        db,
        origin="lower",
        aspect="auto",
        extent=extent,
        cmap=heat_cmap(),
        vmin=vmin,
        vmax=vmax,
        interpolation="bilinear",
    )
    ax.set_xlabel("Range (m)", color="#ddd")
    ax.set_ylabel("Azimuth (deg, local)", color="#ddd")
    ax.set_title(title + "  (×R⁴ display)", color="#eee")
    ax.tick_params(colors="#ccc")
    for spine in ax.spines.values():
        spine.set_color("#555")
    cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("Range-compensated power (dB)  ·  bright = strong", color="#ddd")
    cbar.ax.yaxis.set_tick_params(color="#ccc")
    plt.setp(plt.getp(cbar.ax.axes, "yticklabels"), color="#ccc")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def save_ppi(
    path: str,
    power: np.ndarray,
    az_c: np.ndarray,
    rng_c: np.ndarray,
    sector_deg: float,
    max_range_m: float,
    title: str,
    targets: list[dict],
):
    # Build compensated grid once for consistent stretch + PPI sample.
    work = np.maximum(power, NOISE_FLOOR).astype(np.float64)
    r = np.maximum(rng_c.astype(np.float64), 80.0)
    compensated = work * (r[np.newaxis, :] ** 4)
    db_grid, vmin, vmax = display_db(power, rng_c=rng_c, compensate_r4=True)

    size = 720
    canvas = np.full((size, size), np.nan, dtype=np.float64)
    cx = cy = size * 0.5
    scale = (size * 0.48) / max_range_m
    az0 = float(np.mean(az_c))
    half = math.radians(0.5 * sector_deg)

    yy, xx = np.mgrid[0:size, 0:size]
    dx = (xx - cx) / scale
    dz = (cy - yy) / scale
    rr = np.hypot(dx, dz)
    az = np.arctan2(dz, dx)
    d_az = (az - az0 + math.pi) % (2.0 * math.pi) - math.pi
    mask = (rr <= max_range_m) & (np.abs(d_az) <= half + 1e-6)

    az_lo = float(az_c[0])
    az_hi = float(az_c[-1])
    r_lo = float(rng_c[0])
    r_hi = float(rng_c[-1])
    ia = (az[mask] - az_lo) / max(az_hi - az_lo, 1e-9) * (power.shape[0] - 1)
    ir = (rr[mask] - r_lo) / max(r_hi - r_lo, 1e-9) * (power.shape[1] - 1)
    ia = np.clip(ia, 0, power.shape[0] - 1.001)
    ir = np.clip(ir, 0, power.shape[1] - 1.001)
    ia0 = np.floor(ia).astype(np.int32)
    ir0 = np.floor(ir).astype(np.int32)
    ia1 = np.minimum(ia0 + 1, power.shape[0] - 1)
    ir1 = np.minimum(ir0 + 1, power.shape[1] - 1)
    wa = ia - ia0
    wr = ir - ir0
    samp_db = (
        db_grid[ia0, ir0] * (1 - wa) * (1 - wr)
        + db_grid[ia1, ir0] * wa * (1 - wr)
        + db_grid[ia0, ir1] * (1 - wa) * wr
        + db_grid[ia1, ir1] * wa * wr
    )
    samp_p = (
        power[ia0, ir0] * (1 - wa) * (1 - wr)
        + power[ia1, ir0] * wa * (1 - wr)
        + power[ia0, ir1] * (1 - wa) * wr
        + power[ia1, ir1] * wa * wr
    )
    live = samp_p > (NOISE_FLOOR * 1.0e3)
    samp_db = np.where(live, samp_db, vmin - 10.0)
    canvas[mask] = samp_db
    _ = compensated  # kept for clarity / future debug dumps

    # Same heat ramp as Cartesian — phosphor green hides mid-tone strength.
    fig, ax = plt.subplots(figsize=(7.2, 7.2), dpi=140, facecolor="#050510")
    ax.set_facecolor("#050510")
    cmap = heat_cmap()
    cmap.set_bad("#050510")
    im = ax.imshow(
        canvas,
        origin="upper",
        cmap=cmap,
        vmin=vmin,
        vmax=vmax,
        interpolation="bilinear",
    )
    for rm in (0.25, 0.5, 0.75, 1.0):
        rad_px = rm * max_range_m * scale
        circ = plt.Circle((cx, cy), rad_px, fill=False, ec="#5a6a8a", lw=0.7, alpha=0.75)
        ax.add_patch(circ)
        ax.text(
            cx + 4,
            cy - rad_px + 10,
            f"{int(rm * max_range_m)}m",
            color="#a8c0e0",
            fontsize=7,
            alpha=0.9,
        )

    for edge_az in (az0 - half, az0 + half):
        ex = cx + math.cos(edge_az) * max_range_m * scale
        ey = cy - math.sin(edge_az) * max_range_m * scale
        ax.plot([cx, ex], [cy, ey], color="#c0d8ff", lw=1.1, alpha=0.9)

    for t in targets:
        px = cx + math.cos(t["az"]) * t["r"] * scale
        py = cy - math.sin(t["az"]) * t["r"] * scale
        ax.plot(px, py, marker="+", color="#ff4d6d", markersize=12, markeredgewidth=1.8)
        ax.text(px + 8, py - 8, t["name"], color="#ff8fa3", fontsize=8, fontweight="bold")

    ax.set_xlim(0, size)
    ax.set_ylim(size, 0)
    ax.set_xticks([])
    ax.set_yticks([])
    ax.set_title(title + "  ·  ×R⁴  ·  bright = strong", color="#d8e8ff", fontsize=11, pad=10)
    cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.03)
    cbar.ax.yaxis.set_tick_params(color="#d8e8ff")
    plt.setp(plt.getp(cbar.ax.axes, "yticklabels"), color="#d8e8ff")
    cbar.set_label("compensated dB  dark→cyan→yellow→white", color="#d8e8ff")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def hillshade(z: np.ndarray, cell_m: float, az_deg: float = 315.0, alt_deg: float = 45.0) -> np.ndarray:
    """Simple Lambertian hillshade; NaN-safe."""
    dy, dx = np.gradient(z, cell_m, cell_m)
    slope = np.pi / 2.0 - np.arctan(np.hypot(dx, dy))
    aspect = np.arctan2(-dx, dy)
    az = math.radians(az_deg)
    alt = math.radians(alt_deg)
    shaded = (
        math.sin(alt) * np.sin(slope)
        + math.cos(alt) * np.cos(slope) * np.cos(az - aspect)
    )
    shaded = np.clip(shaded, 0.0, 1.0)
    shaded[~np.isfinite(z)] = np.nan
    return shaded


def sample_dem_range_az(
    dem: dict,
    radar_ix: int,
    radar_iz: int,
    az_c: np.ndarray,
    rng_c: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """Height and LOS-visible height on the same range–az grid as clutter."""
    terrain = dem["terrain"]
    cell_m = float(dem["cell_m"])
    height, width = terrain.shape
    n_az = len(az_c)
    n_rng = len(rng_c)
    elev = np.full((n_az, n_rng), np.nan, dtype=np.float64)
    elev_vis = np.full((n_az, n_rng), np.nan, dtype=np.float64)
    radar_y = float(terrain[radar_iz, radar_ix]) + RADAR_AGL_M

    for ia, az in enumerate(az_c):
        dx = math.cos(float(az))
        dz = math.sin(float(az))
        max_elev = -1e9
        for ir, r in enumerate(rng_c):
            ix = int(round(radar_ix + dx * (float(r) / cell_m)))
            iz = int(round(radar_iz + dz * (float(r) / cell_m)))
            if ix < 0 or iz < 0 or ix >= width or iz >= height:
                continue
            ty = float(terrain[iz, ix])
            if math.isnan(ty):
                continue
            elev[ia, ir] = ty
            look = math.atan2(ty - radar_y, float(r))
            if look >= max_elev - 0.002:
                max_elev = max(max_elev, look)
                elev_vis[ia, ir] = ty
    return elev, elev_vis


def save_dem_range_az(
    path: str,
    elev: np.ndarray,
    elev_vis: np.ndarray,
    az_c: np.ndarray,
    rng_c: np.ndarray,
    title: str,
):
    fig, axes = plt.subplots(1, 2, figsize=(12.5, 5.2), dpi=140, facecolor="#101018")
    extent = [
        float(rng_c[0]),
        float(rng_c[-1]),
        math.degrees(float(az_c[0])),
        math.degrees(float(az_c[-1])),
    ]
    valid = elev[np.isfinite(elev)]
    if valid.size > 0:
        z0 = float(np.percentile(valid, 2))
        z1 = float(np.percentile(valid, 98))
    else:
        z0, z1 = 0.0, 1.0

    for ax, data, sub in (
        (axes[0], elev, "DEM height (all cells)"),
        (axes[1], elev_vis, "DEM height (radar-visible only)"),
    ):
        ax.set_facecolor("#101018")
        im = ax.imshow(
            data,
            origin="lower",
            aspect="auto",
            extent=extent,
            cmap="terrain",
            vmin=z0,
            vmax=z1,
            interpolation="bilinear",
        )
        ax.set_xlabel("Range (m)", color="#ddd")
        ax.set_ylabel("Azimuth (deg)", color="#ddd")
        ax.set_title(sub, color="#eee", fontsize=10)
        ax.tick_params(colors="#ccc")
        for spine in ax.spines.values():
            spine.set_color("#555")
        cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        cbar.set_label("Height (m)", color="#ddd")
        cbar.ax.yaxis.set_tick_params(color="#ccc")
        plt.setp(plt.getp(cbar.ax.axes, "yticklabels"), color="#ccc")

    fig.suptitle(title, color="#eee", fontsize=12)
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def save_dem_plan(
    path: str,
    dem: dict,
    radar_ix: int,
    radar_iz: int,
    az0: float,
    sector_rad: float,
    max_range_m: float,
    targets: list[dict],
    title: str,
):
    """Top-down DEM crop with the same search sector overlay as the PPI."""
    terrain = dem["terrain"]
    cell_m = float(dem["cell_m"])
    height, width = terrain.shape
    pad = int(max_range_m / cell_m) + 4
    x0 = max(0, radar_ix - pad)
    x1 = min(width, radar_ix + pad + 1)
    z0 = max(0, radar_iz - pad)
    z1 = min(height, radar_iz + pad + 1)
    crop = terrain[z0:z1, x0:x1].astype(np.float64)
    shade = hillshade(crop, cell_m)

    # World metres relative to radar (X right, Z up in plot).
    xs = (np.arange(x0, x1) - radar_ix) * cell_m
    zs = (np.arange(z0, z1) - radar_iz) * cell_m
    extent = [xs[0], xs[-1], zs[0], zs[-1]]

    valid = crop[np.isfinite(crop)]
    h0 = float(np.percentile(valid, 2))
    h1 = float(np.percentile(valid, 98))

    fig, axes = plt.subplots(1, 2, figsize=(12.0, 5.8), dpi=140, facecolor="#101018")

    # Height + sector
    ax = axes[0]
    ax.set_facecolor("#101018")
    im0 = ax.imshow(
        crop,
        origin="lower",
        extent=extent,
        cmap="terrain",
        vmin=h0,
        vmax=h1,
        interpolation="bilinear",
        aspect="equal",
    )
    half = 0.5 * sector_rad
    for edge in (az0 - half, az0 + half):
        ax.plot(
            [0.0, math.cos(edge) * max_range_m],
            [0.0, math.sin(edge) * max_range_m],
            color="#ff3355",
            lw=1.4,
        )
    wedge_az = np.linspace(az0 - half, az0 + half, 64)
    ax.plot(
        np.cos(wedge_az) * max_range_m,
        np.sin(wedge_az) * max_range_m,
        color="#ff3355",
        lw=1.0,
        alpha=0.85,
    )
    for rm in (0.25, 0.5, 0.75, 1.0):
        circ = plt.Circle(
            (0.0, 0.0),
            rm * max_range_m,
            fill=False,
            ec="#334455",
            lw=0.6,
            alpha=0.8,
        )
        ax.add_patch(circ)
    ax.plot(0.0, 0.0, marker="^", color="#ff3355", markersize=10)
    for t in targets:
        ax.plot(
            math.cos(t["az"]) * t["r"],
            math.sin(t["az"]) * t["r"],
            marker="+",
            color="#ffe082",
            markersize=10,
            markeredgewidth=1.5,
        )
        ax.text(
            math.cos(t["az"]) * t["r"],
            math.sin(t["az"]) * t["r"],
            " " + t["name"],
            color="#ffe082",
            fontsize=8,
        )
    ax.set_xlim(extent[0], extent[1])
    ax.set_ylim(extent[2], extent[3])
    ax.set_xlabel("X rel. radar (m)", color="#ddd")
    ax.set_ylabel("Z rel. radar (m)", color="#ddd")
    ax.set_title("DEM height + same sector", color="#eee")
    ax.tick_params(colors="#ccc")
    cbar = fig.colorbar(im0, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("Height (m)", color="#ddd")
    cbar.ax.yaxis.set_tick_params(color="#ccc")
    plt.setp(plt.getp(cbar.ax.axes, "yticklabels"), color="#ccc")

    # Hillshade
    ax = axes[1]
    ax.set_facecolor("#101018")
    im1 = ax.imshow(
        shade,
        origin="lower",
        extent=extent,
        cmap="gray",
        vmin=0.0,
        vmax=1.0,
        interpolation="bilinear",
        aspect="equal",
    )
    for edge in (az0 - half, az0 + half):
        ax.plot(
            [0.0, math.cos(edge) * max_range_m],
            [0.0, math.sin(edge) * max_range_m],
            color="#00e5ff",
            lw=1.4,
        )
    ax.plot(
        np.cos(wedge_az) * max_range_m,
        np.sin(wedge_az) * max_range_m,
        color="#00e5ff",
        lw=1.0,
        alpha=0.85,
    )
    ax.plot(0.0, 0.0, marker="^", color="#00e5ff", markersize=10)
    ax.set_xlim(extent[0], extent[1])
    ax.set_ylim(extent[2], extent[3])
    ax.set_xlabel("X rel. radar (m)", color="#ddd")
    ax.set_ylabel("Z rel. radar (m)", color="#ddd")
    ax.set_title("Hillshade (terrain relief)", color="#eee")
    ax.tick_params(colors="#ccc")
    cbar = fig.colorbar(im1, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("Shade", color="#ddd")
    cbar.ax.yaxis.set_tick_params(color="#ccc")
    plt.setp(plt.getp(cbar.ax.axes, "yticklabels"), color="#ccc")

    fig.suptitle(title, color="#eee", fontsize=12)
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def save_compare_panel(
    path: str,
    power: np.ndarray,
    elev_vis: np.ndarray,
    az_c: np.ndarray,
    rng_c: np.ndarray,
    title: str,
):
    """Side-by-side: clutter intensity vs radar-visible DEM height (same grid)."""
    db, vmin, vmax = display_db(power, rng_c=rng_c, compensate_r4=True)
    extent = [
        float(rng_c[0]),
        float(rng_c[-1]),
        math.degrees(float(az_c[0])),
        math.degrees(float(az_c[-1])),
    ]
    valid = elev_vis[np.isfinite(elev_vis)]
    if valid.size > 0:
        z0 = float(np.percentile(valid, 2))
        z1 = float(np.percentile(valid, 98))
    else:
        z0, z1 = 0.0, 1.0

    fig, axes = plt.subplots(1, 2, figsize=(12.5, 5.2), dpi=140, facecolor="#101018")
    ax = axes[0]
    ax.set_facecolor("#101018")
    im0 = ax.imshow(
        db,
        origin="lower",
        aspect="auto",
        extent=extent,
        cmap=heat_cmap(),
        vmin=vmin,
        vmax=vmax,
        interpolation="bilinear",
    )
    ax.set_title("Clutter (×R⁴) — bright = strong", color="#eee")
    ax.set_xlabel("Range (m)", color="#ddd")
    ax.set_ylabel("Azimuth (deg)", color="#ddd")
    ax.tick_params(colors="#ccc")
    cbar = fig.colorbar(im0, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("dB", color="#ddd")
    cbar.ax.yaxis.set_tick_params(color="#ccc")
    plt.setp(plt.getp(cbar.ax.axes, "yticklabels"), color="#ccc")

    ax = axes[1]
    ax.set_facecolor("#101018")
    im1 = ax.imshow(
        elev_vis,
        origin="lower",
        aspect="auto",
        extent=extent,
        cmap="terrain",
        vmin=z0,
        vmax=z1,
        interpolation="bilinear",
    )
    ax.set_title("DEM visible height — same range–az", color="#eee")
    ax.set_xlabel("Range (m)", color="#ddd")
    ax.set_ylabel("Azimuth (deg)", color="#ddd")
    ax.tick_params(colors="#ccc")
    cbar = fig.colorbar(im1, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("Height (m)", color="#ddd")
    cbar.ax.yaxis.set_tick_params(color="#ccc")
    plt.setp(plt.getp(cbar.ax.axes, "yticklabels"), color="#ccc")

    fig.suptitle(title, color="#eee", fontsize=12)
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def save_howto_panel(
    path: str,
    dem: dict,
    radar_ix: int,
    radar_iz: int,
    az0: float,
    sector_rad: float,
    max_range_m: float,
    power: np.ndarray,
    az_c: np.ndarray,
    rng_c: np.ndarray,
    power360: np.ndarray,
    az360: np.ndarray,
    rng360: np.ndarray,
    lang: str = "zh",
):
    """Easy read: plan pie-slice ↔ sector PPI ↔ full 360 PPI."""
    if lang == "en":
        t_sup = "Narrow sector: plan view → sector PPI → full rotation"
        t0 = "1) Plan map: radar only sees the red sector"
        t0_note = "Red triangle = radar\nRed wedge = ground lit by the beam now"
        t1 = "2) Sector PPI: only paints the red wedge from (1)"
        t1_note = "Map is not missing — the beam only covers this slice"
        t2 = "3) After one sweep: full-circle clutter (red = panel 2)"
        t2_note = "Movies often show a full disc; narrow sector = antenna mid-scan"
    else:
        t_sup = "窄扇形怎么读：俯视扇区 → 扇形屏 → 转完一圈的完整 PPI"
        t0 = "① 俯视地图：雷达只看红色扇区"
        t0_note = "红三角=雷达\n红扇形=当前波束扫过的地面"
        t1 = "② 扇形 PPI：只画①里红色那一块"
        t1_note = "不是缺了一半地图，是波束此刻只照这一角"
        t2 = "③ 若转一圈：整圆杂波（红区=②）"
        t2_note = "电影里常画整圆；窄扇形=天线尚未转完"

    terrain = dem["terrain"]
    cell_m = float(dem["cell_m"])
    height, width = terrain.shape
    pad = int(max_range_m / cell_m) + 4
    x0 = max(0, radar_ix - pad)
    x1 = min(width, radar_ix + pad + 1)
    z0i = max(0, radar_iz - pad)
    z1i = min(height, radar_iz + pad + 1)
    crop = terrain[z0i:z1i, x0:x1].astype(np.float64)
    xs = (np.arange(x0, x1) - radar_ix) * cell_m
    zs = (np.arange(z0i, z1i) - radar_iz) * cell_m
    extent_plan = [xs[0], xs[-1], zs[0], zs[-1]]
    valid = crop[np.isfinite(crop)]
    h0 = float(np.percentile(valid, 2))
    h1 = float(np.percentile(valid, 98))
    half = 0.5 * sector_rad
    wedge_az = np.linspace(az0 - half, az0 + half, 80)

    def paint_ppi(ax, pwr, az_arr, rng_arr, sector_only: bool):
        db, vmin, vmax = display_db(pwr, rng_c=rng_arr, compensate_r4=True)
        size = 520
        canvas = np.full((size, size), np.nan, dtype=np.float64)
        cx = cy = size * 0.5
        scale = (size * 0.46) / max_range_m
        yy, xx = np.mgrid[0:size, 0:size]
        dx = (xx - cx) / scale
        dz = (cy - yy) / scale
        rr = np.hypot(dx, dz)
        az = np.arctan2(dz, dx)
        if sector_only:
            d_az = (az - az0 + math.pi) % (2.0 * math.pi) - math.pi
            mask = (rr <= max_range_m) & (np.abs(d_az) <= half + 1e-6)
        else:
            mask = rr <= max_range_m
        az_lo = float(az_arr[0])
        r_lo = float(rng_arr[0])
        r_hi = float(rng_arr[-1])
        if not sector_only:
            az_norm = (az[mask] - az_lo) % (2.0 * math.pi)
            span = (float(az_arr[-1]) - az_lo) % (2.0 * math.pi)
            if span < 1e-6:
                span = 2.0 * math.pi
            ia = az_norm / span * (pwr.shape[0] - 1)
        else:
            ia = (az[mask] - az_lo) / max(float(az_arr[-1]) - az_lo, 1e-9) * (pwr.shape[0] - 1)
        ir = (rr[mask] - r_lo) / max(r_hi - r_lo, 1e-9) * (pwr.shape[1] - 1)
        ia = np.clip(ia, 0, pwr.shape[0] - 1.001)
        ir = np.clip(ir, 0, pwr.shape[1] - 1.001)
        ia0 = np.floor(ia).astype(np.int32)
        ir0 = np.floor(ir).astype(np.int32)
        ia1 = np.minimum(ia0 + 1, pwr.shape[0] - 1)
        ir1 = np.minimum(ir0 + 1, pwr.shape[1] - 1)
        wa = ia - ia0
        wr = ir - ir0
        samp = (
            db[ia0, ir0] * (1 - wa) * (1 - wr)
            + db[ia1, ir0] * wa * (1 - wr)
            + db[ia0, ir1] * (1 - wa) * wr
            + db[ia1, ir1] * wa * wr
        )
        live = (
            pwr[ia0, ir0] * (1 - wa) * (1 - wr)
            + pwr[ia1, ir0] * wa * (1 - wr)
            + pwr[ia0, ir1] * (1 - wa) * wr
            + pwr[ia1, ir1] * wa * wr
        ) > (NOISE_FLOOR * 1.0e3)
        canvas[mask] = np.where(live, samp, vmin - 10.0)
        cmap = heat_cmap()
        cmap.set_bad("#0a0a12")
        ax.imshow(canvas, origin="upper", cmap=cmap, vmin=vmin, vmax=vmax, interpolation="bilinear")
        for rm in (0.5, 1.0):
            ax.add_patch(
                plt.Circle((cx, cy), rm * max_range_m * scale, fill=False, ec="#667788", lw=0.7)
            )
        ax.plot(cx, cy, marker="^", color="white", markersize=8)
        if sector_only:
            for edge in (az0 - half, az0 + half):
                ax.plot(
                    [cx, cx + math.cos(edge) * max_range_m * scale],
                    [cy, cy - math.sin(edge) * max_range_m * scale],
                    color="white",
                    lw=1.2,
                )
        ax.set_xlim(0, size)
        ax.set_ylim(size, 0)
        ax.set_xticks([])
        ax.set_yticks([])
        return vmin, vmax

    fig = plt.figure(figsize=(14.5, 5.6), dpi=140, facecolor="#0e1018")
    gs = fig.add_gridspec(1, 3, wspace=0.18)

    ax0 = fig.add_subplot(gs[0, 0])
    ax0.set_facecolor("#0e1018")
    ax0.imshow(
        crop,
        origin="lower",
        extent=extent_plan,
        cmap="terrain",
        vmin=h0,
        vmax=h1,
        aspect="equal",
        interpolation="bilinear",
    )
    ax0.fill(
        np.concatenate([[0.0], np.cos(wedge_az) * max_range_m]),
        np.concatenate([[0.0], np.sin(wedge_az) * max_range_m]),
        color="#ff3355",
        alpha=0.22,
        zorder=2,
    )
    ax0.plot(
        np.concatenate([[0.0], np.cos(wedge_az) * max_range_m, [0.0]]),
        np.concatenate([[0.0], np.sin(wedge_az) * max_range_m, [0.0]]),
        color="#ff3355",
        lw=2.0,
        zorder=3,
    )
    ax0.plot(0.0, 0.0, marker="^", color="#ff3355", markersize=12, zorder=4)
    ax0.set_xlim(extent_plan[0], extent_plan[1])
    ax0.set_ylim(extent_plan[2], extent_plan[3])
    ax0.set_title(t0, color="#fff", fontsize=11)
    ax0.set_xlabel("X (m)", color="#ccc")
    ax0.set_ylabel("Z (m)", color="#ccc")
    ax0.tick_params(colors="#aaa")
    ax0.text(
        0.02,
        0.02,
        t0_note,
        transform=ax0.transAxes,
        color="#ffc0c8",
        fontsize=9,
        va="bottom",
    )

    ax1 = fig.add_subplot(gs[0, 1])
    ax1.set_facecolor("#0a0a12")
    paint_ppi(ax1, power, az_c, rng_c, sector_only=True)
    ax1.set_title(t1, color="#fff", fontsize=11)
    ax1.text(
        0.5,
        0.04,
        t1_note,
        transform=ax1.transAxes,
        color="#cde",
        fontsize=9,
        ha="center",
    )

    ax2 = fig.add_subplot(gs[0, 2])
    ax2.set_facecolor("#0a0a12")
    paint_ppi(ax2, power360, az360, rng360, sector_only=False)
    size = 520
    cx = cy = size * 0.5
    scale = (size * 0.46) / max_range_m
    ax2.fill(
        np.concatenate([[cx], cx + np.cos(wedge_az) * max_range_m * scale]),
        np.concatenate([[cy], cy - np.sin(wedge_az) * max_range_m * scale]),
        color="#ff3355",
        alpha=0.18,
        zorder=5,
    )
    ax2.set_title(t2, color="#fff", fontsize=11)
    ax2.text(
        0.5,
        0.04,
        t2_note,
        transform=ax2.transAxes,
        color="#cde",
        fontsize=9,
        ha="center",
    )

    fig.suptitle(t_sup, color="#eee", fontsize=13, y=1.02)
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--world", default="GM_Eden")
    ap.add_argument("--max-range", type=float, default=5000.0)
    ap.add_argument("--sector-deg", type=float, default=70.0)
    ap.add_argument("--n-az", type=int, default=180)
    ap.add_argument("--n-rng", type=int, default=220)
    ap.add_argument("--band", default="Ku")
    ap.add_argument("--heading-deg", type=float, default=None, help="Sector center; default auto")
    args = ap.parse_args()

    print("Loading DEM…")
    dem = resolve_dem_source(world=args.world, prefer_ttile=True)
    terrain = dem["terrain"]
    cell_m = float(dem["cell_m"])
    margin = int(args.max_range / cell_m) + 8
    radar_iz, radar_ix = choose_radar_site(terrain, margin=margin)
    radar_y = float(terrain[radar_iz, radar_ix]) + RADAR_AGL_M
    print(
        f"site ix={radar_ix} iz={radar_iz} y={radar_y:.1f}m "
        f"cell={cell_m}m source={dem.get('source')}"
    )

    if args.heading_deg is None:
        h, w = terrain.shape
        aim_x = w * 0.5 - radar_ix
        aim_z = h * 0.5 - radar_iz
        az0 = math.atan2(aim_z, aim_x)
    else:
        az0 = math.radians(args.heading_deg)

    sector = math.radians(args.sector_deg)
    print(
        f"Rendering range–az {args.n_az}×{args.n_rng}, "
        f"sector={args.sector_deg:.0f}°, Rmax={args.max_range:.0f}m, band={args.band}…"
    )
    power, az_c, rng_c = build_intensity(
        dem,
        radar_ix,
        radar_iz,
        radar_y,
        az0,
        sector,
        args.max_range,
        args.n_az,
        args.n_rng,
        args.band,
    )
    # Keep a copy before demo blips for clean DEM compare; blips only on sector display.
    power_for_blips = power.copy()
    targets = inject_demo_targets(power_for_blips, az_c, rng_c)
    elev, elev_vis = sample_dem_range_az(dem, radar_ix, radar_iz, az_c, rng_c)

    print("Rendering full 360° clutter for howto panel…")
    # Coarser 360 for speed.
    n_az360 = 240
    n_rng360 = min(args.n_rng, 140)
    power360, az360, rng360 = build_intensity(
        dem,
        radar_ix,
        radar_iz,
        radar_y,
        0.0,
        2.0 * math.pi,
        args.max_range,
        n_az360,
        n_rng360,
        args.band,
    )

    out_dir = os.path.join(_HERE, "out")
    os.makedirs(out_dir, exist_ok=True)
    cart = os.path.join(out_dir, "range_az_clutter_cartesian.png")
    ppi = os.path.join(out_dir, "range_az_clutter_ppi.png")
    dem_raz = os.path.join(out_dir, "range_az_dem_height.png")
    dem_plan = os.path.join(out_dir, "sector_dem_plan.png")
    compare = os.path.join(out_dir, "clutter_vs_dem_compare.png")
    howto = os.path.join(out_dir, "sector_howto.png")
    howto_en = os.path.join(out_dir, "sector_howto_en.png")
    ppi360 = os.path.join(out_dir, "range_az_clutter_ppi_360.png")

    save_cartesian(
        cart,
        power_for_blips,
        az_c,
        rng_c,
        f"GM_Eden ground clutter · {args.band} · range–az intensity",
    )
    save_ppi(
        ppi,
        power_for_blips,
        az_c,
        rng_c,
        args.sector_deg,
        args.max_range,
        f"Narrow-sector PPI · DEM σ⁰ clutter (+ demo blips)",
        targets,
    )
    save_dem_range_az(
        dem_raz,
        elev,
        elev_vis,
        az_c,
        rng_c,
        f"Same sector DEM · site ix={radar_ix} iz={radar_iz}",
    )
    save_dem_plan(
        dem_plan,
        dem,
        radar_ix,
        radar_iz,
        az0,
        sector,
        args.max_range,
        targets,
        f"Plan DEM crop · Rmax={args.max_range:.0f}m · sector={args.sector_deg:.0f}°",
    )
    save_compare_panel(
        compare,
        power_for_blips,
        elev_vis,
        az_c,
        rng_c,
        "Clutter vs radar-visible DEM (identical range–az grid)",
    )
    save_howto_panel(
        howto,
        dem,
        radar_ix,
        radar_iz,
        az0,
        sector,
        args.max_range,
        power_for_blips,
        az_c,
        rng_c,
        power360,
        az360,
        rng360,
        lang="zh",
    )
    save_howto_panel(
        howto_en,
        dem,
        radar_ix,
        radar_iz,
        az0,
        sector,
        args.max_range,
        power_for_blips,
        az_c,
        rng_c,
        power360,
        az360,
        rng360,
        lang="en",
    )
    # Standalone 360 PPI (no blips).
    save_ppi(
        ppi360,
        power360,
        az360,
        rng360,
        360.0,
        args.max_range,
        "Full-rotation PPI · DEM σ⁰ clutter",
        [],
    )
    print(
        f"Wrote:\n  {cart}\n  {ppi}\n  {ppi360}\n  {dem_raz}\n  {dem_plan}\n"
        f"  {compare}\n  {howto}\n  {howto_en}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
