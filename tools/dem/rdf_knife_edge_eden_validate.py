#!/usr/bin/env python3
"""Eden coverage: LOS vs bounce vs knife-edge diffraction recovery."""

from __future__ import annotations

import math
import os
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "dem"))

from rdf_dem_io import load_surf_json, resample_surface_to_height_grid
from rdf_radar_diffraction import knife_edge_factor_from_geometry
from rdf_radar_physics import preset_shorad_x


def main() -> None:
    hw = preset_shorad_x()
    lam = hw.wavelength_m
    print("lambda=%.4f m" % lam, flush=True)

    npz = os.path.join(ROOT, "tools", "dem", "out", "Eden_ttile_height.npz")
    surf_dir = os.path.join(ROOT, "DemData", "GM_Eden")
    with np.load(npz) as d:
        terrain = np.asarray(d["height_m"], np.float32)
        cell_m = float(d["cell_m"])
    surf = load_surf_json(surf_dir)
    mapped = resample_surface_to_height_grid(surf, terrain.shape, cell_m, 0, 0)
    surface = np.where(
        mapped > 0, mapped, np.where(terrain < 0.25, 1, 0)
    ).astype(np.uint8)
    land = (surface != 1) & np.isfinite(terrain) & (terrain > 0.5)

    step = 32
    t = terrain[::step, ::step]
    l = land[::step, ::step]
    sub = cell_m * step
    iz, ix = np.nonzero(l)
    xw = ix.astype(np.float64) * sub
    zw = iz.astype(np.float64) * sub
    elev = t[iz, ix]
    z_med = 5312.0
    mast = 12.0
    r0 = 4000.0
    clearance = 10.0
    agl = 150.0
    slack = 2.0
    tx = xw
    tz = zw
    ty = elev + agl
    sites = [
        (8320.0, 4608.0, 243.0),
        (7872.0, 4096.0, 249.0),
        (7808.0, 3392.0, 343.0),
        (6656.0, 6336.0, 149.0),
        (6144.0, 6336.0, 162.0),
        (5824.0, 7232.0, 233.0),
    ]
    min_f = 0.008
    gamma = 0.55

    def sample_t(x: float, z: float) -> float:
        jx = int(np.clip(x / sub, 0, t.shape[1] - 1))
        jz = int(np.clip(z / sub, 0, t.shape[0] - 1))
        return float(t[jz, jx])

    def los_probe(x0, y0, z0, x1, y1, z1):
        dx = x1 - x0
        dy = y1 - y0
        dz = z1 - z0
        dist = math.hypot(math.hypot(dx, dz), dy)
        if dist < 1:
            return True, 1.0, 0.0, 0.5
        n = max(2, int(dist / 128))
        first_u = None
        max_h = -1.0e9
        max_u = 0.5
        for k in range(1, n):
            u = k / n
            if u < 0.03 or u > 0.97:
                continue
            x = x0 + dx * u
            y = y0 + dy * u
            z = z0 + dz * u
            g = sample_t(x, z)
            hobs = g - slack - y
            if hobs > max_h:
                max_h = hobs
                max_u = u
            if np.isfinite(g) and g + clearance > y:
                if first_u is None:
                    first_u = u
        if first_u is None:
            return True, 1.0, max_h, max_u
        return False, first_u, max_h, max_u

    def bounce_factor(dist, hit_u, hr, ht):
        if ht > 800:
            return 0.0
        if hr < 0.5:
            hr = 0.5
        if ht < 0.5:
            ht = 0.5
        sum_h = hr + ht
        br = math.sqrt(dist * dist + sum_h * sum_h)
        if br < 1:
            return 0.0
        ratio = dist / br
        geom = ratio**4
        f = (gamma * gamma) * geom
        depth = max(0.2, min(1.0, hit_u))
        f *= depth
        if f < min_f:
            return 0.0
        return min(1.0, f)

    def knife_factor(dist, max_h, max_u):
        return knife_edge_factor_from_geometry(max_h, dist, max_u, lam, min_f)

    def cover(mode: str):
        cov = np.zeros(len(tx), dtype=bool)
        n_los = 0
        n_bounce = 0
        n_knife = 0
        n_only_knife = 0
        factors = []
        for sx, sz, se in sites:
            x0 = sx
            z0 = sz
            y0 = se + mast
            ot = sample_t(x0, z0)
            hr = y0 - ot
            dx = tx - x0
            dy = ty - y0
            dz = tz - z0
            d2 = dx * dx + dy * dy + dz * dz
            for i in np.nonzero(d2 <= r0 * r0)[0]:
                dist = math.sqrt(float(d2[i]))
                clear, hit_u, max_h, max_u = los_probe(
                    x0, y0, z0, tx[i], ty[i], tz[i]
                )
                if clear:
                    cov[i] = True
                    n_los += 1
                    continue
                if mode == "los":
                    continue
                ht = float(ty[i] - elev[i])
                b = 0.0
                k = 0.0
                if mode in ("bounce", "both"):
                    b = bounce_factor(dist, hit_u, hr, ht)
                if mode in ("knife", "both"):
                    k = knife_factor(dist, max_h, max_u)
                f = b
                src = "b"
                if k > f:
                    f = k
                    src = "k"
                if f <= 0.0:
                    continue
                reff = r0 * (f**0.25)
                if dist <= reff:
                    cov[i] = True
                    factors.append(f)
                    if src == "k":
                        n_knife += 1
                    else:
                        n_bounce += 1
                    if mode == "both" and k > b and b <= 0.0:
                        n_only_knife += 1
        stats = {
            "n_los_ok": n_los,
            "n_via_bounce": n_bounce,
            "n_via_knife": n_knife,
            "n_only_knife": n_only_knife,
            "f_med": float(np.median(factors)) if factors else 0.0,
            "f_p90": float(np.percentile(factors, 90)) if factors else 0.0,
        }
        return cov, stats

    south = zw < z_med
    north = zw >= z_med
    results = {}
    for mode in ("los", "bounce", "knife", "both"):
        cov, st = cover(mode)
        results[mode] = (cov, st)
        line = (
            "%s: land_cov=%.1f%% S=%.1f%% N=%.1f%% los_hits=%d bounce=%d knife=%d "
            "only_knife=%d f_med=%.4f f_p90=%.4f"
            % (
                mode,
                100.0 * cov.mean(),
                100.0 * cov[south].mean(),
                100.0 * cov[north].mean(),
                st["n_los_ok"],
                st["n_via_bounce"],
                st["n_via_knife"],
                st["n_only_knife"],
                st["f_med"],
                st["f_p90"],
            )
        )
        print(line, flush=True)

    cov_los = results["los"][0]
    cov_b = results["bounce"][0]
    cov_k = results["knife"][0]
    cov_both = results["both"][0]

    in_range = np.zeros(len(tx), dtype=bool)
    for sx, sz, se in sites:
        x0 = sx
        z0 = sz
        y0 = se + mast
        d2 = (tx - x0) ** 2 + (ty - y0) ** 2 + (tz - z0) ** 2
        in_range |= d2 <= r0 * r0
    shadow = in_range & ~cov_los
    n_sh = max(int(shadow.sum()), 1)
    print("", flush=True)
    print(
        "In-range land: %d  geometric shadow: %d (%.1f%% of land)"
        % (int(in_range.sum()), int(shadow.sum()), 100.0 * shadow.mean()),
        flush=True,
    )
    print(
        "  recovered by bounce: %d (%.1f%% of shadow)"
        % (int((shadow & cov_b).sum()), 100.0 * (shadow & cov_b).sum() / n_sh),
        flush=True,
    )
    print(
        "  recovered by knife:  %d (%.1f%% of shadow)"
        % (int((shadow & cov_k).sum()), 100.0 * (shadow & cov_k).sum() / n_sh),
        flush=True,
    )
    print(
        "  recovered by both:   %d (%.1f%% of shadow)"
        % (
            int((shadow & cov_both).sum()),
            100.0 * (shadow & cov_both).sum() / n_sh,
        ),
        flush=True,
    )
    print(
        "  knife-only vs bounce: %d cells"
        % int((shadow & cov_k & ~cov_b).sum()),
        flush=True,
    )
    print(
        "  delta land_cov both-bounce: %+.2f pp"
        % (100.0 * (cov_both.mean() - cov_b.mean())),
        flush=True,
    )


if __name__ == "__main__":
    main()
