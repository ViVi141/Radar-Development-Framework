#!/usr/bin/env python3
"""Offline RAM sample + DEM-LOS precheck benchmark for packaged DemData.

Mirrors Enforce hot paths (flat SURF/HEIGHT grids, 8-sample DEM precheck).
Does not measure TraceMove (engine-only); reports DEM-block rate as potential
Trace skips.

Examples:
  python tools/dem/rdf_dem_runtime_bench.py --world GM_Arland
  python tools/dem/rdf_dem_runtime_bench.py --all
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np

SURF_MAGIC = "RDF_SURF_JSON_V1"
HEIGHT_MAGIC = "RDF_HEIGHT_JSON_V1"
WORLDS = ("GM_Arland", "GM_Eden", "GM_Cain")


def addon_dem_dir(world: str) -> Path:
    root = Path(__file__).resolve().parents[2]
    return root / "DemData" / world


def load_row_tiles(chunks_dir: Path, tile_iz: int, tile_cells: int, bytes_per_cell: int):
    path = chunks_dir / f"row_{tile_iz}.json"
    with path.open("r", encoding="utf-8") as handle:
        doc = json.load(handle)
    return doc.get("tiles", []), tile_cells, bytes_per_cell


def load_surf_grid(dem_dir: Path) -> dict:
    manifest = json.loads((dem_dir / "surf_manifest.json").read_text(encoding="utf-8"))
    if manifest.get("magic") != SURF_MAGIC:
        raise SystemExit(f"bad SURF magic in {dem_dir}")
    size = int(manifest["tile_cells"])
    tx = int(manifest["tile_count_x"])
    tz = int(manifest["tile_count_z"])
    width = tx * size
    height = tz * size
    grid = np.zeros((height, width), dtype=np.uint8)
    chunks = dem_dir / str(manifest.get("chunks_dir", "surf_chunks/")).replace("/", "")
    # chunks_dir is "surf_chunks/"
    chunks = dem_dir / "surf_chunks"
    t0 = time.perf_counter()
    loaded = 0
    for tile_iz in range(tz):
        tiles, _, _ = load_row_tiles(chunks, tile_iz, size, 1)
        for entry in tiles:
            ix = int(entry["ix"])
            payload = bytes.fromhex(entry["hex"])
            expect = size * size
            if len(payload) < expect:
                continue
            tile = np.frombuffer(payload[:expect], dtype=np.uint8).reshape(size, size)
            z0 = tile_iz * size
            x0 = ix * size
            grid[z0 : z0 + size, x0 : x0 + size] = tile
            loaded += 1
    decode_ms = (time.perf_counter() - t0) * 1000.0
    return {
        "grid": grid,
        "cell_m": float(manifest["cell_m"]),
        "bmin_x": float(manifest["bounds_min_x"]),
        "bmin_z": float(manifest["bounds_min_z"]),
        "bmax_x": float(manifest["bounds_max_x"]),
        "bmax_z": float(manifest["bounds_max_z"]),
        "tiles": loaded,
        "decode_ms": decode_ms,
        "ram_mib": grid.nbytes / (1024 * 1024),
    }


def load_height_grid(dem_dir: Path) -> dict:
    manifest = json.loads((dem_dir / "height_manifest.json").read_text(encoding="utf-8"))
    if manifest.get("magic") != HEIGHT_MAGIC:
        raise SystemExit(f"bad HEIGHT magic in {dem_dir}")
    size = int(manifest["tile_cells"])
    tx = int(manifest["tile_count_x"])
    tz = int(manifest["tile_count_z"])
    y_scale = float(manifest.get("y_scale", 0.1))
    width = tx * size
    height = tz * size
    grid = np.zeros((height, width), dtype=np.float32)
    chunks = dem_dir / "height_chunks"
    t0 = time.perf_counter()
    loaded = 0
    for tile_iz in range(tz):
        path = chunks / f"row_{tile_iz}.json"
        with path.open("r", encoding="utf-8") as handle:
            doc = json.load(handle)
        for entry in doc.get("tiles", []):
            ix = int(entry["ix"])
            raw = bytes.fromhex(entry["hex"])
            expect = size * size * 2
            if len(raw) < expect:
                continue
            q = np.frombuffer(raw[:expect], dtype="<i2").reshape(size, size)
            z0 = tile_iz * size
            x0 = ix * size
            grid[z0 : z0 + size, x0 : x0 + size] = q.astype(np.float32) * y_scale
            loaded += 1
    decode_ms = (time.perf_counter() - t0) * 1000.0
    return {
        "grid": grid,
        "cell_m": float(manifest["cell_m"]),
        "bmin_x": float(manifest["bounds_min_x"]),
        "bmin_z": float(manifest["bounds_min_z"]),
        "bmax_x": float(manifest["bounds_max_x"]),
        "bmax_z": float(manifest["bounds_max_z"]),
        "y_scale": y_scale,
        "tiles": loaded,
        "decode_ms": decode_ms,
        "ram_mib": grid.nbytes / (1024 * 1024),
    }


def sample_y(height: dict, wx: np.ndarray, wz: np.ndarray) -> np.ndarray:
    grid = height["grid"]
    cell = height["cell_m"]
    ix = np.floor((wx - height["bmin_x"]) / cell).astype(np.int64)
    iz = np.floor((wz - height["bmin_z"]) / cell).astype(np.int64)
    ix = np.clip(ix, 0, grid.shape[1] - 1)
    iz = np.clip(iz, 0, grid.shape[0] - 1)
    return grid[iz, ix]


def dem_los_precheck(
    height: dict,
    origin: np.ndarray,
    end: np.ndarray,
    samples: int = 8,
    slack_m: float = 2.0,
) -> tuple[bool, float]:
    """Return (blocked, hit_u). Vectorized along one ray."""
    delta = end - origin
    length = float(np.linalg.norm(delta))
    if length < 1.0:
        return False, 1.0
    us = np.arange(1, samples + 1, dtype=np.float64) / (samples + 1.0)
    wx = origin[0] + delta[0] * us
    wy = origin[1] + delta[1] * us
    wz = origin[2] + delta[2] * us
    top = sample_y(height, wx, wz)
    blocked = (top - slack_m) > wy
    if not np.any(blocked):
        return False, 1.0
    idx = int(np.argmax(blocked))
    return True, float(us[idx])


def bench_world(
    world: str,
    dem_dir: Path,
    n_samples: int,
    n_rays: int,
    los_samples: int,
    seed: int,
) -> dict:
    print(f"== {world} ==")
    if not (dem_dir / "surf_manifest.json").is_file():
        raise SystemExit(f"missing SURF under {dem_dir}")
    if not (dem_dir / "height_manifest.json").is_file():
        raise SystemExit(f"missing HEIGHT under {dem_dir}")

    surf = load_surf_grid(dem_dir)
    height = load_height_grid(dem_dir)
    print(
        f"  decode SURF {surf['decode_ms']:.0f} ms ({surf['ram_mib']:.1f} MiB) "
        f"HEIGHT {height['decode_ms']:.0f} ms ({height['ram_mib']:.1f} MiB)"
    )

    rng = np.random.default_rng(seed)
    bmin_x, bmax_x = height["bmin_x"], height["bmax_x"]
    bmin_z, bmax_z = height["bmin_z"], height["bmax_z"]
    pad = 50.0
    xs = rng.uniform(bmin_x + pad, bmax_x - pad, size=n_samples)
    zs = rng.uniform(bmin_z + pad, bmax_z - pad, size=n_samples)

    # Warm
    _ = sample_y(height, xs[:1000], zs[:1000])
    t0 = time.perf_counter()
    ys = sample_y(height, xs, zs)
    sample_ms = (time.perf_counter() - t0) * 1000.0
    per_sample_ns = (sample_ms * 1e6) / max(1, n_samples)

    # Random radar→target rays (radar ~30 m AGL, target ~2–80 m AGL)
    ox = rng.uniform(bmin_x + pad, bmax_x - pad, size=n_rays)
    oz = rng.uniform(bmin_z + pad, bmax_z - pad, size=n_rays)
    oy_ground = sample_y(height, ox, oz)
    oy = oy_ground + 30.0
    tx = rng.uniform(bmin_x + pad, bmax_x - pad, size=n_rays)
    tz = rng.uniform(bmin_z + pad, bmax_z - pad, size=n_rays)
    ty_ground = sample_y(height, tx, tz)
    ty = ty_ground + rng.uniform(2.0, 80.0, size=n_rays)

    blocked = 0
    t0 = time.perf_counter()
    for i in range(n_rays):
        ok, _u = dem_los_precheck(
            height,
            np.array([ox[i], oy[i], oz[i]], dtype=np.float64),
            np.array([tx[i], ty[i], tz[i]], dtype=np.float64),
            samples=los_samples,
            slack_m=2.0,
        )
        if ok:
            blocked += 1
    los_ms = (time.perf_counter() - t0) * 1000.0
    per_ray_us = (los_ms * 1000.0) / max(1, n_rays)
    block_pct = 100.0 * blocked / max(1, n_rays)

    # Unique surface classes (non-zero share)
    surf_grid = surf["grid"]
    nonzero = float(np.count_nonzero(surf_grid)) / float(surf_grid.size) * 100.0
    n_classes = int(len(np.unique(surf_grid)))

    report = {
        "world": world,
        "grid": f"{height['grid'].shape[1]}x{height['grid'].shape[0]}",
        "cell_m": height["cell_m"],
        "surf_decode_ms": round(surf["decode_ms"], 1),
        "height_decode_ms": round(height["decode_ms"], 1),
        "surf_ram_mib": round(surf["ram_mib"], 2),
        "height_ram_mib": round(height["ram_mib"], 2),
        "sample_count": n_samples,
        "sample_batch_ms": round(sample_ms, 2),
        "sample_ns_each": round(per_sample_ns, 1),
        "sample_y_mean": round(float(np.mean(ys)), 2),
        "los_rays": n_rays,
        "los_samples_per_ray": los_samples,
        "los_batch_ms": round(los_ms, 2),
        "los_us_each": round(per_ray_us, 2),
        "dem_block_pct": round(block_pct, 1),
        "dem_blocked": blocked,
        "surf_nonzero_pct": round(nonzero, 1),
        "surf_unique_classes": n_classes,
    }
    print(
        f"  sample {n_samples}: {sample_ms:.1f} ms ({per_sample_ns:.0f} ns/ea)  "
        f"LOS {n_rays}: {los_ms:.1f} ms ({per_ray_us:.1f} us/ray)  "
        f"demBlk={block_pct:.1f}%"
    )
    return report


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default=None, help="GM_Arland / GM_Eden / GM_Cain")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--samples", type=int, default=200_000)
    ap.add_argument("--rays", type=int, default=5_000)
    ap.add_argument("--los-samples", type=int, default=8)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Write JSON report (default tools/dem/out/dem_runtime_bench.json)",
    )
    args = ap.parse_args()

    worlds = list(WORLDS) if args.all or args.world is None else [args.world]
    if args.world and not args.all:
        worlds = [args.world]

    reports = []
    for world in worlds:
        dem_dir = addon_dem_dir(world)
        reports.append(
            bench_world(
                world,
                dem_dir,
                args.samples,
                args.rays,
                args.los_samples,
                args.seed,
            )
        )

    out = args.out
    if out is None:
        out = Path(__file__).resolve().parent / "out" / "dem_runtime_bench.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "note": "Offline RAM paths only; TraceMove not measured. dem_block_pct ≈ Trace skips.",
        "worlds": reports,
    }
    out.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
