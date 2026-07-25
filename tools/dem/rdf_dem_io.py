#!/usr/bin/env python3
"""Load and stitch RDF DEM NPZ tiles (V2/V3) for offline radar prototypes."""

from __future__ import annotations

import glob
import os
import re

import numpy as np

TILE_RE = re.compile(r"dataset_(\d+)_(\d+)\.npz$")
MAX_SPANS = 8


def load_dem(tile_dir: str, load_spans: bool = True) -> dict[str, np.ndarray | float | bool]:
    """Stitch all dataset_*.npz tiles under tile_dir into full-map arrays.

    Indexing is [iz, ix]. World X/Z cell centers are:
      x = (ix + 0.5) * cell_m + world_origin_x  (origin is tile-relative; use indices)
    """
    files = glob.glob(os.path.join(tile_dir, "dataset_*.npz"))
    if not files:
        raise SystemExit(f"No dataset tiles found in {tile_dir}")

    metadata: list[tuple[str, int, int, int, int]] = []
    width = 0
    height = 0
    cell_m = 4.0
    max_spans = MAX_SPANS

    for path in files:
        match = TILE_RE.search(os.path.basename(path))
        if not match:
            continue

        with np.load(path) as tile:
            tile_height, tile_width = tile["terrain_y"].shape
            origin_ix = int(tile["origin_ix"])
            origin_iz = int(tile["origin_iz"])
            if "cell_m" in tile.files:
                cell_m = float(tile["cell_m"])
            if "n_spans" in tile.files and "span_lo" in tile.files:
                max_spans = int(tile["span_lo"].shape[2])

        metadata.append((path, origin_ix, origin_iz, tile_width, tile_height))
        width = max(width, origin_ix + tile_width)
        height = max(height, origin_iz + tile_height)

    terrain = np.full((height, width), np.nan, dtype=np.float32)
    slope = np.zeros((height, width), dtype=np.float32)
    water_type = np.zeros((height, width), dtype=np.uint8)
    occupancy = np.zeros((height, width), dtype=np.float32)
    density = np.full((height, width), 0.5, dtype=np.float32)
    surface = np.zeros((height, width), dtype=np.uint8)
    entity_hits = np.zeros((height, width), dtype=np.float32)
    n_spans = np.zeros((height, width), dtype=np.uint8)
    span_lo = None
    span_hi = None
    if load_spans:
        span_lo = np.zeros((height, width, max_spans), dtype=np.float32)
        span_hi = np.zeros((height, width, max_spans), dtype=np.float32)
    has_material = False
    has_spans = False

    for path, origin_ix, origin_iz, tile_width, tile_height in metadata:
        with np.load(path) as tile:
            rows = slice(origin_iz, origin_iz + tile_height)
            cols = slice(origin_ix, origin_ix + tile_width)
            terrain[rows, cols] = tile["terrain_y"]
            slope[rows, cols] = tile["slope_deg"]
            water_type[rows, cols] = tile["water_type"]
            occupancy[rows, cols] = tile["occ_ground"]
            if "density_gcm3" in tile.files:
                density[rows, cols] = tile["density_gcm3"]
                has_material = True
            if "surface_class" in tile.files:
                surface[rows, cols] = tile["surface_class"]
                has_material = True
            if "entity_hits" in tile.files:
                entity_hits[rows, cols] = tile["entity_hits"]
            if load_spans and "n_spans" in tile.files:
                n_spans[rows, cols] = tile["n_spans"]
                span_count = int(tile["span_lo"].shape[2])
                use = min(span_count, max_spans)
                span_lo[rows, cols, :use] = tile["span_lo"][:, :, :use]
                span_hi[rows, cols, :use] = tile["span_hi"][:, :, :use]
                has_spans = True

    terrain[terrain < -200.0] = np.nan
    result: dict[str, np.ndarray | float | bool] = {
        "terrain": terrain,
        "slope": slope,
        "water_type": water_type,
        "occupancy": occupancy,
        "density": density,
        "surface": surface,
        "entity_hits": entity_hits,
        "n_spans": n_spans,
        "has_material": has_material,
        "has_spans": has_spans,
        "cell_m": cell_m,
        "max_spans": float(max_spans),
    }
    if load_spans:
        result["span_lo"] = span_lo
        result["span_hi"] = span_hi
    return result


def choose_radar_site(terrain: np.ndarray, margin: int = 128) -> tuple[int, int]:
    """Pick a high interior land cell as default radar site (iz, ix)."""
    valid = np.isfinite(terrain)
    candidate = np.where(valid, terrain, -1e9)
    candidate[:margin, :] = -1e9
    candidate[-margin:, :] = -1e9
    candidate[:, :margin] = -1e9
    candidate[:, -margin:] = -1e9
    iz, ix = np.unravel_index(np.argmax(candidate), candidate.shape)
    return int(iz), int(ix)


def default_tile_dir(world: str = "GM_Eden") -> str:
    root = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(root, "TrainData", world, "tiles")
