#!/usr/bin/env python3
"""Pack RDF DEM V3 CSV tiles into RDF_DEM_BIN_V1 (.dem.data) for Workshop.

Layout (little-endian):
  magic "RDFDEM1\\0" (8)
  version i32=1, flags i32=0
  bounds_min_x/z, bounds_max_x/z, cell_m f32
  tile_cells, tile_count_x, tile_count_z i32
  y_scale f32=0.1
  table_count i32
  world_key[64] ASCII null-padded
  table_count * (ix, iz, offset, length) i32x4
  payloads: tile_cells^2 * 6 bytes/cell
    int16 y (y/scale), u8 slope, u8 water_depth,
    u8 flags (water_type:2 | occ:1 | density_q:5), u8 surface_class

No column spans (game clutter path does not use them).

Examples:
  python tools/dem/rdf_dem_pack_bin.py --world GM_Arland
  python tools/dem/rdf_dem_pack_bin.py --world GM_Eden --out-dir DemData
"""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

TILE_RE = re.compile(r"tile_(\d+)_(\d+)\.csv$", re.IGNORECASE)
MAGIC = b"RDFDEM1\0"
VERSION = 1
HEADER_BYTES = 120
WORLD_KEY_BYTES = 64
CELL_BYTES = 6
Y_SCALE = 0.1


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


def parse_manifest(path: Path) -> dict:
    kv: dict[str, str] = {}
    with path.open("r", encoding="utf-8", errors="replace") as f:
        magic = f.readline().strip()
        if magic != "RDF_DEM_MANIFEST_V3":
            raise ValueError(f"bad manifest magic in {path}: {magic}")
        for line in f:
            line = line.strip()
            if not line or " " not in line:
                continue
            key, _, val = line.partition(" ")
            kv[key] = val
    required = [
        "world",
        "bounds_min_x",
        "bounds_min_z",
        "bounds_max_x",
        "bounds_max_z",
        "cell_m",
        "tile_cells",
        "tile_count_x",
        "tile_count_z",
    ]
    for key in required:
        if key not in kv:
            raise ValueError(f"manifest missing {key}")
    return kv


def parse_tile_cells(path: Path, size: int) -> dict[tuple[int, int], tuple]:
    cells: dict[tuple[int, int], tuple] = {}
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or not line[0].isdigit():
                continue
            if line.startswith("-"):
                continue
            parts = line.split()
            if len(parts) < 11:
                continue
            try:
                local_ix = int(float(parts[0]))
                local_iz = int(float(parts[1]))
            except ValueError:
                continue
            if local_ix < 0 or local_iz < 0 or local_ix >= size or local_iz >= size:
                continue
            terrain_y = float(parts[2])
            slope = float(parts[3])
            water_depth = float(parts[4])
            water_type = int(float(parts[5]))
            occ = int(float(parts[6]))
            density = float(parts[8])
            surface = int(float(parts[9]))
            cells[(local_ix, local_iz)] = (
                terrain_y,
                slope,
                water_depth,
                water_type,
                occ,
                density,
                surface,
            )
    return cells


def pack_cell(row: tuple) -> bytes:
    terrain_y, slope, water_depth, water_type, occ, density, surface = row
    y_q = int(round(terrain_y / Y_SCALE))
    y_q = max(-32768, min(32767, y_q))
    slope_q = int(round(max(0.0, min(90.0, slope))))
    water_q = int(round(max(0.0, min(255.0, water_depth))))
    water_type = max(0, min(3, water_type))
    occ_bit = 1 if occ else 0
    dens_q = int(round(max(0.0, min(3.1, density)) * 10.0))
    dens_q = max(0, min(31, dens_q))
    flags = (water_type & 3) | ((occ_bit & 1) << 2) | ((dens_q & 31) << 3)
    surf_q = max(0, min(255, surface))
    return struct.pack("<hBBBB", y_q, slope_q, water_q, flags, surf_q)


def pack_world(src_dir: Path, out_path: Path) -> None:
    manifest = parse_manifest(src_dir / "manifest.csv")
    world = manifest["world"]
    size = int(float(manifest["tile_cells"]))
    tx = int(float(manifest["tile_count_x"]))
    tz = int(float(manifest["tile_count_z"]))
    cell_m = float(manifest["cell_m"])
    bmin_x = float(manifest["bounds_min_x"])
    bmin_z = float(manifest["bounds_min_z"])
    bmax_x = float(manifest["bounds_max_x"])
    bmax_z = float(manifest["bounds_max_z"])

    tiles_dir = src_dir / "tiles"
    tile_files = sorted(tiles_dir.glob("tile_*.csv"))
    parsed: list[tuple[int, int, bytes]] = []
    for path in tile_files:
        m = TILE_RE.search(path.name)
        if not m:
            continue
        ix = int(m.group(1))
        iz = int(m.group(2))
        cells = parse_tile_cells(path, size)
        payload = bytearray(size * size * CELL_BYTES)
        for local_iz in range(size):
            for local_ix in range(size):
                key = (local_ix, local_iz)
                if key in cells:
                    blob = pack_cell(cells[key])
                else:
                    blob = pack_cell((0.0, 0.0, 0.0, 0, 0, 0.5, 0))
                base = (local_iz * size + local_ix) * CELL_BYTES
                payload[base : base + CELL_BYTES] = blob
        parsed.append((ix, iz, bytes(payload)))

    table_count = len(parsed)
    data_start = HEADER_BYTES + table_count * 16

    world_key = world.encode("ascii", errors="ignore")[: WORLD_KEY_BYTES - 1]
    world_pad = world_key + b"\0" * (WORLD_KEY_BYTES - len(world_key))

    header = bytearray()
    header += MAGIC
    header += struct.pack("<ii", VERSION, 0)
    header += struct.pack("<fffff", bmin_x, bmin_z, bmax_x, bmax_z, cell_m)
    header += struct.pack("<iii", size, tx, tz)
    header += struct.pack("<f", Y_SCALE)
    header += struct.pack("<i", table_count)
    header += world_pad
    if len(header) != HEADER_BYTES:
        raise RuntimeError(f"header size {len(header)} != {HEADER_BYTES}")

    table = bytearray()
    payloads = bytearray()
    cursor = data_start
    for ix, iz, payload in parsed:
        table += struct.pack("<iiii", ix, iz, cursor, len(payload))
        payloads += payload
        cursor += len(payload)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(header)
        f.write(table)
        f.write(payloads)

    mb = out_path.stat().st_size / (1024 * 1024)
    print(f"wrote {out_path} tiles={table_count} size={mb:.1f} MiB")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", required=True, help="World key, e.g. GM_Arland")
    ap.add_argument(
        "--src",
        type=Path,
        default=None,
        help="Source DemData/<world> (default: Workbench profile)",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory DemData (default: addon DemData/)",
    )
    args = ap.parse_args()

    src = args.src
    if src is None:
        src = profile_dem_dir(args.world)
    if not (src / "manifest.csv").is_file():
        raise SystemExit(f"manifest missing: {src / 'manifest.csv'}")

    if args.out_dir is None:
        root = Path(__file__).resolve().parents[2]
        out_dir = root / "DemData" / args.world
    else:
        out_dir = args.out_dir / args.world

    out_path = out_dir / f"{args.world}.dem.data"
    pack_world(src, out_path)


if __name__ == "__main__":
    main()
