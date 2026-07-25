#!/usr/bin/env python3
"""Pack RDF DEM CSV tiles from Workbench profile into npz tiles for waveform sim.

Supports:
  V1: height / water / occ
  V2: + density_gcm3, surface_class
  V3: + n_spans and up to 8 [lo,hi] height intervals per column
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import numpy as np

TILE_RE = re.compile(r"tile_(\d+)_(\d+)\.csv$", re.IGNORECASE)
MAX_SPANS = 8


def profile_dem_dir(world: str) -> Path:
    home = Path.home()
    return (
        home
        / "Documents"
        / "My Games"
        / "ArmaReforgerWorkbench"
        / "profile"
        / "RDF"
        / "DemData"
        / world
    )


def parse_tile(path: Path) -> dict:
    meta: dict = {"path": str(path)}
    rows: list[list[float]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("RDF_DEM_TILE"):
                meta["format"] = line
                continue
            if " " in line and not line[0].isdigit() and not line.startswith("-"):
                key, _, val = line.partition(" ")
                if key == "cols":
                    meta["cols"] = val.split()
                else:
                    meta[key] = val
                continue
            parts = line.split()
            if len(parts) < 8:
                continue
            rows.append([float(x) for x in parts])

    if "size" not in meta:
        raise ValueError(f"missing size in {path}")

    size = int(float(meta["size"]))
    max_spans = int(float(meta.get("max_spans", MAX_SPANS)))
    terrain = np.full((size, size), np.nan, dtype=np.float32)
    slope = np.zeros((size, size), dtype=np.float32)
    water = np.zeros((size, size), dtype=np.float32)
    water_type = np.zeros((size, size), dtype=np.uint8)
    occ = np.zeros((size, size), dtype=np.float32)
    hits = np.zeros((size, size), dtype=np.float32)
    density = np.full((size, size), 0.5, dtype=np.float32)
    surface = np.zeros((size, size), dtype=np.uint8)
    n_spans = np.zeros((size, size), dtype=np.uint8)
    span_lo = np.zeros((size, size, max_spans), dtype=np.float32)
    span_hi = np.zeros((size, size, max_spans), dtype=np.float32)

    for r in rows:
        local_ix = int(r[0])
        local_iz = int(r[1])
        if local_ix < 0 or local_ix >= size or local_iz < 0 or local_iz >= size:
            continue
        terrain[local_iz, local_ix] = r[2]
        slope[local_iz, local_ix] = r[3]
        water[local_iz, local_ix] = r[4]
        water_type[local_iz, local_ix] = int(r[5])
        occ[local_iz, local_ix] = r[6]
        hits[local_iz, local_ix] = r[7]
        if len(r) >= 10:
            density[local_iz, local_ix] = r[8]
            surface[local_iz, local_ix] = int(r[9])
        if len(r) >= 11 + max_spans * 2:
            count = int(r[10])
            if count < 0:
                count = 0
            if count > max_spans:
                count = max_spans
            n_spans[local_iz, local_ix] = count
            base = 11
            for s in range(max_spans):
                span_lo[local_iz, local_ix, s] = r[base + s * 2]
                span_hi[local_iz, local_ix, s] = r[base + s * 2 + 1]

    origin_ix = int(float(meta.get("origin_ix", int(meta.get("tile_ix", 0)) * size)))
    origin_iz = int(float(meta.get("origin_iz", int(meta.get("tile_iz", 0)) * size)))

    return {
        "terrain_y": terrain,
        "slope_deg": slope,
        "water_depth_m": water,
        "water_type": water_type,
        "occ_ground": occ,
        "entity_hits": hits,
        "density_gcm3": density,
        "surface_class": surface,
        "n_spans": n_spans,
        "span_lo": span_lo,
        "span_hi": span_hi,
        "origin_ix": np.int32(origin_ix),
        "origin_iz": np.int32(origin_iz),
        "cell_m": np.float32(float(meta.get("cell_m", 4.0))),
        "tile_ix": int(float(meta.get("tile_ix", 0))),
        "tile_iz": int(float(meta.get("tile_iz", 0))),
        "size": size,
        "format": meta.get("format", "RDF_DEM_TILE_V1"),
        "max_spans": max_spans,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--world", default="GM_Eden")
    ap.add_argument("--tiles-dir", default=None)
    ap.add_argument("--output", default=None)
    args = ap.parse_args()

    src = Path(args.tiles_dir) if args.tiles_dir else profile_dem_dir(args.world) / "tiles"
    if not src.is_dir():
        raise SystemExit(f"tiles dir not found: {src}")

    out_root = (
        Path(args.output)
        if args.output
        else Path(__file__).resolve().parent / "TrainData" / args.world
    )
    out_tiles = out_root / "tiles"
    out_tiles.mkdir(parents=True, exist_ok=True)

    files = sorted(src.glob("tile_*.csv"))
    if not files:
        raise SystemExit(f"no tile_*.csv under {src}")

    written = 0
    cell_m = None
    v2_count = 0
    v3_count = 0
    for path in files:
        if not TILE_RE.search(path.name):
            continue
        tile = parse_tile(path)
        if cell_m is None:
            cell_m = float(tile["cell_m"])
        fmt = str(tile.get("format", ""))
        if "V3" in fmt:
            v3_count += 1
        elif "V2" in fmt:
            v2_count += 1
        out_name = f"dataset_{tile['tile_ix']}_{tile['tile_iz']}.npz"
        np.savez_compressed(
            out_tiles / out_name,
            terrain_y=tile["terrain_y"],
            slope_deg=tile["slope_deg"],
            water_depth_m=tile["water_depth_m"],
            water_type=tile["water_type"],
            occ_ground=tile["occ_ground"],
            entity_hits=tile["entity_hits"],
            density_gcm3=tile["density_gcm3"],
            surface_class=tile["surface_class"],
            n_spans=tile["n_spans"],
            span_lo=tile["span_lo"],
            span_hi=tile["span_hi"],
            origin_ix=tile["origin_ix"],
            origin_iz=tile["origin_iz"],
            cell_m=tile["cell_m"],
        )
        written += 1

    manifest = {
        "world": args.world,
        "cell_m": cell_m,
        "tiles_written": written,
        "tiles_v2": v2_count,
        "tiles_v3": v3_count,
        "max_spans": MAX_SPANS,
        "channels": [
            "terrain_y",
            "slope_deg",
            "water_depth_m",
            "water_type",
            "occ_ground",
            "entity_hits",
            "density_gcm3",
            "surface_class",
            "n_spans",
            "span_lo",
            "span_hi",
        ],
        "source": str(src),
        "output": str(out_tiles),
    }
    with (out_root / "manifest.json").open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print(
        f"packed {written} tiles (V2={v2_count}, V3={v3_count}) -> {out_tiles}"
    )
    print(f"manifest -> {out_root / 'manifest.json'}")


if __name__ == "__main__":
    main()
