#!/usr/bin/env python3
"""Pack terrain height JSON (RDF_HEIGHT_JSON_V1) from official game terrain.

Source of truth is Reforger FORM/TERR `.ttile` HGHT (same data behind
BaseWorld.GetSurfaceY), NOT RDF V3 CSV / `.dem.data` bake products.

Typical flow:
  1) Point at unpacked world Terrain folder (Terrain.terr + .Data/*.ttile), or
     reuse tools/dem/out/<World>_ttile_height.npz from rdf_ttile_unpack.py
  2) Align cells to DemData/<GM_*>/surf_manifest.json when present (workshop grid)
  3) Write height_manifest.json + height_chunks/ next to SURF

Examples:
  python tools/dem/rdf_dem_pack_height_json.py --world GM_Arland --terrain "D:/arma_reforger_full/worlds/Arland/Terrain"
  python tools/dem/rdf_dem_pack_height_json.py --world GM_Eden --from-ttile-npz
  python tools/dem/rdf_ttile_unpack.py "D:/arma_reforger_full/worlds/Eden/Terrain"
  python tools/dem/rdf_dem_pack_height_json.py --world GM_Eden --from-ttile-npz
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

_TOOLS_DEM = Path(__file__).resolve().parent
if str(_TOOLS_DEM) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DEM))

from rdf_ttile_unpack import unpack_terrain  # noqa: E402

JSON_MAGIC = "RDF_HEIGHT_JSON_V1"
Y_SCALE = 0.1
CHUNKS_DIR = "height_chunks/"
DEFAULT_TILE_CELLS = 32

# Common dump layouts (override with --terrain).
DEFAULT_FULL_ROOTS = [
    Path(r"C:\Users\74738\Documents\arma_reforger_code\worlds"),
    Path(r"C:\Users\74738\Desktop\arma_reforger_full\worlds"),
    Path(r"C:\Users\74738\Documents\arma_reforger_full\worlds"),
]


def world_to_ttile_stem(world: str) -> str:
    key = world.strip()
    if key.startswith("GM_"):
        return key[3:]
    return key


def world_to_gm_key(world: str) -> str:
    key = world.strip()
    if key.startswith("GM_"):
        return key
    return f"GM_{key}"


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
        / world_to_gm_key(world)
    )


def resolve_terrain_dir(world: str, terrain: Path | None) -> Path:
    if terrain is not None:
        return terrain
    stem = world_to_ttile_stem(world)
    candidates: list[Path] = []
    for root in DEFAULT_FULL_ROOTS:
        # Arland/Cain: worlds/<Name>/Terrain
        candidates.append(root / stem / "Terrain")
        # Eden dump layout: worlds/Eden/Eden (Eden.terr + .Data)
        candidates.append(root / stem / stem)
        candidates.append(root / stem)
    for candidate in candidates:
        if not candidate.is_dir():
            continue
        has_terr = (candidate / "Terrain.terr").is_file() or bool(
            list(candidate.glob("*.terr"))
        )
        has_data = (candidate / ".Data").is_dir()
        if has_terr and has_data:
            return candidate
    raise SystemExit(
        f"Terrain dir not found for {world}. Pass --terrain "
        f"<.../worlds/{stem}/Terrain> or --from-ttile-npz after rdf_ttile_unpack.py"
    )


def default_ttile_npz(world: str, tools_dem: Path) -> Path:
    stem = world_to_ttile_stem(world)
    return tools_dem / "out" / f"{stem}_ttile_height.npz"


def load_height_m_from_npz(path: Path) -> tuple[np.ndarray, float]:
    if not path.is_file():
        raise SystemExit(f"ttile npz missing: {path}")
    with np.load(path) as data:
        if "height_m" in data.files:
            height = np.asarray(data["height_m"], dtype=np.float32)
        elif "height_u16" in data.files:
            scale = float(data["height_scale"]) if "height_scale" in data.files else 0.03125
            offset = (
                float(data["height_offset"])
                if "height_offset" in data.files
                else -204.78125
            )
            height = data["height_u16"].astype(np.float32) * scale + offset
        else:
            raise SystemExit(f"npz missing height_m/height_u16: {path}")
        cell_m = float(data["cell_m"]) if "cell_m" in data.files else 2.0
    return height, cell_m


def load_surf_grid_meta(surf_dir: Path) -> dict | None:
    manifest_path = surf_dir / "surf_manifest.json"
    if not manifest_path.is_file():
        return None
    with manifest_path.open("r", encoding="utf-8") as handle:
        doc = json.load(handle)
    if doc.get("magic") != "RDF_SURF_JSON_V1":
        raise SystemExit(f"bad SURF magic: {manifest_path}")
    return {
        "world": str(doc["world"]),
        "bounds_min_x": float(doc["bounds_min_x"]),
        "bounds_min_z": float(doc["bounds_min_z"]),
        "bounds_max_x": float(doc["bounds_max_x"]),
        "bounds_max_z": float(doc["bounds_max_z"]),
        "cell_m": float(doc["cell_m"]),
        "tile_cells": int(doc["tile_cells"]),
        "tile_count_x": int(doc["tile_count_x"]),
        "tile_count_z": int(doc["tile_count_z"]),
    }


def sample_ttile_bilinear(
    height: np.ndarray,
    src_cell_m: float,
    world_x: np.ndarray,
    world_z: np.ndarray,
    origin_x: float = 0.0,
    origin_z: float = 0.0,
) -> np.ndarray:
    """Sample official heightfield at world XZ (metres). Vertices at i*cell_m."""
    rows, cols = height.shape
    fx = (world_x - origin_x) / float(src_cell_m)
    fz = (world_z - origin_z) / float(src_cell_m)
    x0 = np.floor(fx).astype(np.int64)
    z0 = np.floor(fz).astype(np.int64)
    x1 = x0 + 1
    z1 = z0 + 1
    x0 = np.clip(x0, 0, cols - 1)
    x1 = np.clip(x1, 0, cols - 1)
    z0 = np.clip(z0, 0, rows - 1)
    z1 = np.clip(z1, 0, rows - 1)
    tx = np.clip(fx - np.floor(fx), 0.0, 1.0).astype(np.float32)
    tz = np.clip(fz - np.floor(fz), 0.0, 1.0).astype(np.float32)
    h00 = height[z0, x0]
    h10 = height[z0, x1]
    h01 = height[z1, x0]
    h11 = height[z1, x1]
    top = h00 * (1.0 - tx) + h10 * tx
    bot = h01 * (1.0 - tx) + h11 * tx
    return top * (1.0 - tz) + bot * tz


def build_grid_from_ttile_native(
    height: np.ndarray,
    src_cell_m: float,
    world_key: str,
    tile_cells: int,
) -> tuple[dict, np.ndarray]:
    """Use native .ttile resolution tiled for HEIGHT JSON (no SURF align)."""
    rows, cols = height.shape
    # Vertex grid: map extent uses (n-1)*cell between corners.
    cells_x = max(1, cols - 1)
    cells_z = max(1, rows - 1)
    # Sample cell centers on a (cells_z, cells_x) grid matching SURF-style cells.
    bmin_x = 0.0
    bmin_z = 0.0
    bmax_x = cells_x * src_cell_m
    bmax_z = cells_z * src_cell_m
    # Pad to whole tiles.
    tile_count_x = (cells_x + tile_cells - 1) // tile_cells
    tile_count_z = (cells_z + tile_cells - 1) // tile_cells
    width = tile_count_x * tile_cells
    height_out = tile_count_z * tile_cells
    bmax_x = width * src_cell_m
    bmax_z = height_out * src_cell_m

    ix = (np.arange(width, dtype=np.float64) + 0.5) * src_cell_m + bmin_x
    iz = (np.arange(height_out, dtype=np.float64) + 0.5) * src_cell_m + bmin_z
    wx, wz = np.meshgrid(ix, iz)
    sampled = sample_ttile_bilinear(height, src_cell_m, wx, wz)

    meta = {
        "world": world_key,
        "bounds_min_x": bmin_x,
        "bounds_min_z": bmin_z,
        "bounds_max_x": bmax_x,
        "bounds_max_z": bmax_z,
        "cell_m": float(src_cell_m),
        "tile_cells": int(tile_cells),
        "tile_count_x": int(tile_count_x),
        "tile_count_z": int(tile_count_z),
    }
    return meta, sampled.astype(np.float32)


def build_grid_aligned_to_surf(
    height: np.ndarray,
    src_cell_m: float,
    surf: dict,
) -> tuple[dict, np.ndarray]:
    """Resample official height onto the SURF workshop grid (required for attach)."""
    cell_m = float(surf["cell_m"])
    size = int(surf["tile_cells"])
    tx = int(surf["tile_count_x"])
    tz = int(surf["tile_count_z"])
    bmin_x = float(surf["bounds_min_x"])
    bmin_z = float(surf["bounds_min_z"])
    width = tx * size
    height_out = tz * size

    ix = (np.arange(width, dtype=np.float64) + 0.5) * cell_m + bmin_x
    iz = (np.arange(height_out, dtype=np.float64) + 0.5) * cell_m + bmin_z
    wx, wz = np.meshgrid(ix, iz)
    sampled = sample_ttile_bilinear(height, src_cell_m, wx, wz)

    meta = {
        "world": str(surf["world"]),
        "bounds_min_x": bmin_x,
        "bounds_min_z": bmin_z,
        "bounds_max_x": float(surf["bounds_max_x"]),
        "bounds_max_z": float(surf["bounds_max_z"]),
        "cell_m": cell_m,
        "tile_cells": size,
        "tile_count_x": tx,
        "tile_count_z": tz,
    }
    return meta, sampled.astype(np.float32)


def quantize_y(terrain_y: float, y_scale: float) -> int:
    q = int(round(float(terrain_y) / float(y_scale)))
    if q < -32768:
        q = -32768
    if q > 32767:
        q = 32767
    return q


def write_height_pack_from_grid(
    out_dir: Path,
    meta: dict,
    height_grid: np.ndarray,
    y_scale: float,
) -> None:
    size = int(meta["tile_cells"])
    tx = int(meta["tile_count_x"])
    tz = int(meta["tile_count_z"])
    rows, cols = height_grid.shape
    if rows != tz * size or cols != tx * size:
        raise SystemExit(
            f"grid shape {height_grid.shape} != tiles {tz}x{tx} @ {size}"
        )

    chunks = out_dir / "height_chunks"
    if chunks.exists():
        for old in chunks.glob("row_*.json"):
            old.unlink()
    chunks.mkdir(parents=True, exist_ok=True)

    manifest = {
        "magic": JSON_MAGIC,
        "world": meta["world"],
        "bounds_min_x": meta["bounds_min_x"],
        "bounds_min_z": meta["bounds_min_z"],
        "bounds_max_x": meta["bounds_max_x"],
        "bounds_max_z": meta["bounds_max_z"],
        "cell_m": meta["cell_m"],
        "tile_cells": size,
        "tile_count_x": tx,
        "tile_count_z": tz,
        "y_scale": y_scale,
        "chunks_dir": CHUNKS_DIR,
        "source": "game_ttile",
    }
    (out_dir / "height_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=True, separators=(",", ":")),
        encoding="utf-8",
    )

    row_count = 0
    tile_count = 0
    for tile_iz in range(tz):
        entries = []
        for tile_ix in range(tx):
            z0 = tile_iz * size
            x0 = tile_ix * size
            tile = height_grid[z0 : z0 + size, x0 : x0 + size]
            payload = bytearray(size * size * 2)
            flat = tile.reshape(-1)
            for i, y in enumerate(flat):
                q = quantize_y(float(y), y_scale)
                payload[i * 2 : i * 2 + 2] = struct.pack("<h", q)
            entries.append({"ix": tile_ix, "hex": bytes(payload).hex()})
            tile_count += 1
        doc = {"iz": tile_iz, "tiles": entries}
        (chunks / f"row_{tile_iz}.json").write_text(
            json.dumps(doc, ensure_ascii=True, separators=(",", ":")),
            encoding="utf-8",
        )
        row_count += 1

    mb = (out_dir / "height_manifest.json").stat().st_size / (1024 * 1024)
    mb += sum(p.stat().st_size for p in chunks.glob("row_*.json")) / (1024 * 1024)
    print(
        f"wrote {out_dir} HEIGHT(from game .ttile) rows={row_count} tiles={tile_count} "
        f"grid={cols}x{rows} cell={meta['cell_m']} y_scale={y_scale} json≈{mb:.1f} MiB"
    )


def pack_from_game_height(
    world: str,
    height: np.ndarray,
    src_cell_m: float,
    out_dir: Path,
    surf_dir: Path,
    y_scale: float,
    tile_cells: int,
    align_surf: bool,
) -> None:
    gm = world_to_gm_key(world)
    surf = load_surf_grid_meta(surf_dir)
    if align_surf:
        if surf is None:
            raise SystemExit(
                f"SURF manifest missing under {surf_dir} "
                "(pack SURF first, or pass --native-grid)"
            )
        meta, grid = build_grid_aligned_to_surf(height, src_cell_m, surf)
        print(
            f"aligned to SURF grid cell={meta['cell_m']} "
            f"tiles={meta['tile_count_x']}x{meta['tile_count_z']} "
            f"from ttile cell={src_cell_m}"
        )
    else:
        meta, grid = build_grid_from_ttile_native(
            height, src_cell_m, gm, tile_cells
        )
        print(
            f"native ttile grid cell={meta['cell_m']} "
            f"tiles={meta['tile_count_x']}x{meta['tile_count_z']} "
            "(runtime attach needs matching SURF bounds/cell)"
        )
    if meta["world"] != gm and surf is not None:
        # Prefer SURF world key for Enforce attach.
        meta["world"] = gm
    write_height_pack_from_grid(out_dir, meta, grid, y_scale)


def main() -> None:
    tools_dem = _TOOLS_DEM
    addon_root = tools_dem.parents[1]

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", required=True, help="GM_Arland / GM_Eden / …")
    ap.add_argument(
        "--terrain",
        type=Path,
        default=None,
        help="Official worlds/<Name>/Terrain (Terrain.terr + .Data/*.ttile)",
    )
    ap.add_argument(
        "--from-ttile-npz",
        type=Path,
        nargs="?",
        const=Path("__auto__"),
        default=None,
        help="Use out/<World>_ttile_height.npz (omit path → auto)",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Parent out dir (writes <out>/<GM_world>/…). Default: addon DemData/",
    )
    ap.add_argument(
        "--surf-dir",
        type=Path,
        default=None,
        help="SURF pack dir for grid align (default: DemData/<world> or profile)",
    )
    ap.add_argument(
        "--native-grid",
        action="store_true",
        help="Do not align to SURF; tile at native .ttile cell size",
    )
    ap.add_argument("--tile-cells", type=int, default=DEFAULT_TILE_CELLS)
    ap.add_argument("--y-scale", type=float, default=Y_SCALE)
    args = ap.parse_args()

    gm = world_to_gm_key(args.world)
    if args.out_dir is None:
        out_dir = addon_root / "DemData" / gm
    else:
        out_dir = args.out_dir / gm
    out_dir.mkdir(parents=True, exist_ok=True)

    surf_dir = args.surf_dir
    if surf_dir is None:
        surf_dir = addon_root / "DemData" / gm
        if not (surf_dir / "surf_manifest.json").is_file():
            profile = profile_dem_dir(gm)
            if (profile / "surf_manifest.json").is_file():
                surf_dir = profile

    # Resolve official heightfield.
    if args.from_ttile_npz is not None:
        if args.from_ttile_npz == Path("__auto__"):
            npz_path = default_ttile_npz(gm, tools_dem)
        else:
            npz_path = args.from_ttile_npz
        height, src_cell_m = load_height_m_from_npz(npz_path)
        print(f"loaded game height from {npz_path} cell={src_cell_m}")
    else:
        terrain_dir = resolve_terrain_dir(gm, args.terrain)
        print(f"unpacking official terrain: {terrain_dir}")
        npz_path = unpack_terrain(terrain_dir, tools_dem / "out")
        height, src_cell_m = load_height_m_from_npz(npz_path)

    align_surf = not args.native_grid
    pack_from_game_height(
        gm,
        height,
        src_cell_m,
        out_dir,
        surf_dir,
        float(args.y_scale),
        int(args.tile_cells),
        align_surf,
    )


if __name__ == "__main__":
    main()
