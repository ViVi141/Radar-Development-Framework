#!/usr/bin/env python3
"""Load and stitch RDF DEM NPZ tiles (V2/V3) for offline radar prototypes."""

from __future__ import annotations

import glob
import json
import os
import re

import numpy as np

TILE_RE = re.compile(r"dataset_(\d+)_(\d+)\.npz$")
MAX_SPANS = 8
SURF_JSON_MAGIC = "RDF_SURF_JSON_V1"


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
    # Prefer dry land when ocean/bathymetry is negative.
    land = candidate > 0.5
    if np.any(land[margin:-margin, margin:-margin]):
        candidate = np.where(land, candidate, -1e9)
    candidate[:margin, :] = -1e9
    candidate[-margin:, :] = -1e9
    candidate[:, :margin] = -1e9
    candidate[:, -margin:] = -1e9
    iz, ix = np.unravel_index(np.argmax(candidate), candidate.shape)
    return int(iz), int(ix)


def default_tile_dir(world: str = "GM_Eden") -> str:
    root = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(root, "TrainData", world, "tiles")


def world_to_ttile_stem(world: str) -> str:
    """Map Enforce world keys (GM_Eden) to unpacked ttile basenames (Eden)."""
    key = world.strip()
    if key.startswith("GM_"):
        return key[3:]
    return key


def world_to_gm_key(world: str) -> str:
    key = world.strip()
    if key.startswith("GM_"):
        return key
    return f"GM_{key}"


def default_ttile_height_npz(world: str = "GM_Eden") -> str:
    root = os.path.dirname(os.path.abspath(__file__))
    stem = world_to_ttile_stem(world)
    return os.path.join(root, "out", f"{stem}_ttile_height.npz")


def default_surf_dir(world: str = "GM_Eden") -> str:
    """Packaged SURF JSON under addon DemData/<GM_World>/."""
    root = os.path.dirname(os.path.abspath(__file__))
    addon = os.path.dirname(os.path.dirname(root))
    return os.path.join(addon, "DemData", world_to_gm_key(world))


def load_surf_json(surf_dir: str) -> dict[str, np.ndarray | float | str]:
    """Load RDF_SURF_JSON_V1 (surface_class only) into a full [iz, ix] grid."""
    manifest_path = os.path.join(surf_dir, "surf_manifest.json")
    if not os.path.isfile(manifest_path):
        raise SystemExit(f"surf_manifest.json not found under {surf_dir}")

    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("magic") != SURF_JSON_MAGIC:
        raise SystemExit(f"bad SURF magic in {manifest_path}")

    cell_m = float(manifest["cell_m"])
    tile_cells = int(manifest["tile_cells"])
    tile_count_x = int(manifest["tile_count_x"])
    tile_count_z = int(manifest["tile_count_z"])
    bounds_min_x = float(manifest["bounds_min_x"])
    bounds_min_z = float(manifest["bounds_min_z"])
    chunks_rel = str(manifest.get("chunks_dir", "surf_chunks/"))
    chunks_dir = os.path.join(surf_dir, chunks_rel.replace("/", os.sep))

    width = tile_count_x * tile_cells
    height = tile_count_z * tile_cells
    surface = np.zeros((height, width), dtype=np.uint8)
    loaded = 0

    for tz in range(tile_count_z):
        row_path = os.path.join(chunks_dir, f"row_{tz}.json")
        if not os.path.isfile(row_path):
            continue
        with open(row_path, "r", encoding="utf-8") as handle:
            doc = json.load(handle)
        for entry in doc.get("tiles", []):
            tx = int(entry["ix"])
            payload = bytes.fromhex(entry["hex"])
            expect = tile_cells * tile_cells
            if len(payload) < expect:
                continue
            tile = np.frombuffer(payload[:expect], dtype=np.uint8).reshape(
                tile_cells, tile_cells
            )
            z0 = tz * tile_cells
            x0 = tx * tile_cells
            surface[z0 : z0 + tile_cells, x0 : x0 + tile_cells] = tile
            loaded += 1

    return {
        "surface": surface,
        "cell_m": cell_m,
        "bounds_min_x": bounds_min_x,
        "bounds_min_z": bounds_min_z,
        "tile_cells": float(tile_cells),
        "source": f"surf:{surf_dir}",
        "tiles_loaded": float(loaded),
    }


def resample_surface_to_height_grid(
    surf: dict[str, np.ndarray | float | str],
    height_shape: tuple[int, int],
    height_cell_m: float,
    height_origin_x: float = 0.0,
    height_origin_z: float = 0.0,
) -> np.ndarray:
    """Nearest-neighbor map SURF cells onto a denser height grid (world metres)."""
    surf_grid = np.asarray(surf["surface"], dtype=np.uint8)
    surf_cell = float(surf["cell_m"])
    bmin_x = float(surf["bounds_min_x"])
    bmin_z = float(surf["bounds_min_z"])
    sh, sw = surf_grid.shape
    hh, hw = height_shape

    ix = (np.arange(hw, dtype=np.float64) + 0.5) * height_cell_m + height_origin_x
    iz = (np.arange(hh, dtype=np.float64) + 0.5) * height_cell_m + height_origin_z
    sx = np.floor((ix - bmin_x) / surf_cell).astype(np.int64)
    sz = np.floor((iz - bmin_z) / surf_cell).astype(np.int64)
    sx = np.clip(sx, 0, sw - 1)
    sz = np.clip(sz, 0, sh - 1)
    return surf_grid[sz[:, None], sx[None, :]]


def load_ttile_height_npz(
    path: str,
    surf_dir: str = "",
    merge_surf: bool = True,
    world: str = "",
) -> dict[str, np.ndarray | float | bool]:
    """Load rdf_ttile_unpack.py output; optionally overlay SURF JSON surface_class."""
    if not os.path.isfile(path):
        raise SystemExit(f"ttile height npz not found: {path}")

    with np.load(path) as data:
        if "height_m" in data.files:
            terrain = np.asarray(data["height_m"], dtype=np.float32)
        elif "height_u16" in data.files:
            scale = float(data["height_scale"]) if "height_scale" in data.files else 0.03125
            offset = (
                float(data["height_offset"]) if "height_offset" in data.files else -204.78125
            )
            terrain = data["height_u16"].astype(np.float32) * scale + offset
        else:
            raise SystemExit(f"npz missing height_m/height_u16: {path}")
        cell_m = float(data["cell_m"]) if "cell_m" in data.files else 2.0

    dz, dx = np.gradient(terrain, cell_m, cell_m)
    slope = np.degrees(np.arctan(np.hypot(dx, dz))).astype(np.float32)

    surface = np.full(terrain.shape, 0, dtype=np.uint8)
    surface[terrain < 0.25] = 1
    surf_source = "heuristic"
    has_material = True

    resolved_surf = surf_dir
    if merge_surf and not resolved_surf and world:
        resolved_surf = default_surf_dir(world)
    if merge_surf and resolved_surf and os.path.isdir(resolved_surf):
        manifest = os.path.join(resolved_surf, "surf_manifest.json")
        if os.path.isfile(manifest):
            surf = load_surf_json(resolved_surf)
            mapped = resample_surface_to_height_grid(
                surf,
                terrain.shape,
                cell_m,
                height_origin_x=0.0,
                height_origin_z=0.0,
            )
            use_surf = mapped > 0
            surface = np.where(use_surf, mapped, surface).astype(np.uint8)
            surf_source = str(surf["source"])
            print(
                f"SURF overlay: {surf_source} "
                f"tiles={int(float(surf['tiles_loaded']))} "
                f"unique_classes={len(np.unique(surface))}"
            )

    water_type = (surface == 1).astype(np.uint8)

    return {
        "terrain": terrain,
        "slope": slope,
        "water_type": water_type,
        "occupancy": np.zeros(terrain.shape, dtype=np.float32),
        "density": np.full(terrain.shape, 0.5, dtype=np.float32),
        "surface": surface,
        "entity_hits": np.zeros(terrain.shape, dtype=np.float32),
        "n_spans": np.zeros(terrain.shape, dtype=np.uint8),
        "has_material": has_material,
        "has_spans": False,
        "cell_m": cell_m,
        "max_spans": 0.0,
        "source": f"ttile:{path}+{surf_source}",
    }


def resolve_dem_source(
    world: str = "GM_Eden",
    dem_dir: str = "",
    dem_npz: str = "",
    prefer_ttile: bool = False,
    load_spans: bool = True,
    surf_dir: str = "",
    merge_surf: bool = True,
) -> dict[str, np.ndarray | float | bool]:
    """Resolve DEM from explicit npz, ttile+SURF, or TrainData tiles."""
    if dem_npz:
        return load_ttile_height_npz(
            dem_npz,
            surf_dir=surf_dir,
            merge_surf=merge_surf,
            world=world,
        )

    if prefer_ttile:
        ttile_path = default_ttile_height_npz(world)
        if os.path.isfile(ttile_path):
            return load_ttile_height_npz(
                ttile_path,
                surf_dir=surf_dir,
                merge_surf=merge_surf,
                world=world,
            )

    tile_dir = dem_dir if dem_dir else default_tile_dir(world)
    if tile_dir and os.path.isdir(tile_dir):
        files = glob.glob(os.path.join(tile_dir, "dataset_*.npz"))
        if files:
            dem = load_dem(tile_dir, load_spans=load_spans)
            dem["source"] = f"tiles:{tile_dir}"
            return dem

    ttile_path = default_ttile_height_npz(world)
    if os.path.isfile(ttile_path):
        return load_ttile_height_npz(
            ttile_path,
            surf_dir=surf_dir,
            merge_surf=merge_surf,
            world=world,
        )

    raise SystemExit(
        f"No DEM for world={world}. Unpack ttile first or provide TrainData tiles.\n"
        f"  tried tiles: {tile_dir}\n"
        f"  tried ttile: {ttile_path}"
    )
