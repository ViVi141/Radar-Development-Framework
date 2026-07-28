#!/usr/bin/env python3
"""Pack RDF DEM into Workshop-friendly JSON (RDF_DEM_JSON_V1).

Layout:
  DemData/<world>/manifest.json
  DemData/<world>/jchunks/row_<iz>.json   # one file per tile row

Each tile cell is 6 bytes (same as RDF_DEM_BIN_V1), stored as lowercase hex.

Sources (first available):
  1) existing <world>.dem.data (fastest, exact payload copy)
  2) V3 CSV under --src / profile DemData/<world>

Examples:
  python tools/dem/rdf_dem_pack_json.py --world GM_Arland
  python tools/dem/rdf_dem_pack_json.py --world GM_Eden --from-bin
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

TILE_RE = re.compile(r"tile_(\d+)_(\d+)\.csv$", re.IGNORECASE)
BIN_MAGIC = b"RDFDEM1\0"
JSON_MAGIC = "RDF_DEM_JSON_V1"
CELL_BYTES = 6
Y_SCALE = 0.1
CHUNKS_DIR = "jchunks/"


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


def parse_manifest_csv(path: Path) -> dict:
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


def write_json_pack(
    out_dir: Path,
    world: str,
    bmin_x: float,
    bmin_z: float,
    bmax_x: float,
    bmax_z: float,
    cell_m: float,
    size: int,
    tx: int,
    tz: int,
    y_scale: float,
    tiles_by_iz: dict[int, list[tuple[int, bytes]]],
) -> None:
    chunks = out_dir / "jchunks"
    if chunks.exists():
        for old in chunks.glob("row_*.json"):
            old.unlink()
    chunks.mkdir(parents=True, exist_ok=True)

    manifest = {
        "magic": JSON_MAGIC,
        "world": world,
        "bounds_min_x": bmin_x,
        "bounds_min_z": bmin_z,
        "bounds_max_x": bmax_x,
        "bounds_max_z": bmax_z,
        "cell_m": cell_m,
        "tile_cells": size,
        "tile_count_x": tx,
        "tile_count_z": tz,
        "y_scale": y_scale,
        "chunks_dir": CHUNKS_DIR,
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=True, separators=(",", ":")),
        encoding="utf-8",
    )

    row_count = 0
    tile_count = 0
    for iz in sorted(tiles_by_iz.keys()):
        entries = []
        for ix, payload in sorted(tiles_by_iz[iz], key=lambda t: t[0]):
            entries.append({"ix": ix, "hex": payload.hex()})
            tile_count += 1
        doc = {"iz": iz, "tiles": entries}
        (chunks / f"row_{iz}.json").write_text(
            json.dumps(doc, ensure_ascii=True, separators=(",", ":")),
            encoding="utf-8",
        )
        row_count += 1

    # Remove legacy binary if present next to JSON pack.
    legacy = out_dir / f"{world}.dem.data"
    if legacy.is_file():
        legacy.unlink()
        print(f"removed legacy {legacy}")

    mb = sum(p.stat().st_size for p in out_dir.rglob("*.json")) / (1024 * 1024)
    print(
        f"wrote {out_dir} rows={row_count} tiles={tile_count} json={mb:.1f} MiB"
    )


def pack_from_bin(bin_path: Path, out_dir: Path) -> None:
    data = bin_path.read_bytes()
    if data[:8] != BIN_MAGIC:
        raise ValueError(f"bad bin magic: {bin_path}")
    ver, _flags = struct.unpack_from("<ii", data, 8)
    if ver != 1:
        raise ValueError(f"unsupported bin version {ver}")
    bmin_x, bmin_z, bmax_x, bmax_z, cell_m = struct.unpack_from("<fffff", data, 16)
    size, tx, tz = struct.unpack_from("<iii", data, 36)
    y_scale = struct.unpack_from("<f", data, 48)[0]
    table_count = struct.unpack_from("<i", data, 52)[0]
    world = (
        data[56 : 56 + 64]
        .split(b"\0", 1)[0]
        .decode("ascii", errors="ignore")
    )
    table_off = 120
    tiles_by_iz: dict[int, list[tuple[int, bytes]]] = {}
    for i in range(table_count):
        ix, iz, offset, length = struct.unpack_from(
            "<iiii", data, table_off + i * 16
        )
        payload = data[offset : offset + length]
        expected = size * size * CELL_BYTES
        if length != expected:
            raise ValueError(f"tile {ix},{iz} length {length} != {expected}")
        tiles_by_iz.setdefault(iz, []).append((ix, payload))

    write_json_pack(
        out_dir,
        world,
        bmin_x,
        bmin_z,
        bmax_x,
        bmax_z,
        cell_m,
        size,
        tx,
        tz,
        y_scale,
        tiles_by_iz,
    )


def pack_from_csv(src_dir: Path, out_dir: Path) -> None:
    manifest = parse_manifest_csv(src_dir / "manifest.csv")
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
    tiles_by_iz: dict[int, list[tuple[int, bytes]]] = {}
    for path in sorted(tiles_dir.glob("tile_*.csv")):
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
        tiles_by_iz.setdefault(iz, []).append((ix, bytes(payload)))

    write_json_pack(
        out_dir,
        world,
        bmin_x,
        bmin_z,
        bmax_x,
        bmax_z,
        cell_m,
        size,
        tx,
        tz,
        Y_SCALE,
        tiles_by_iz,
    )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", required=True, help="World key, e.g. GM_Arland")
    ap.add_argument(
        "--src",
        type=Path,
        default=None,
        help="Source DemData/<world> CSV root (default: Workbench profile)",
    )
    ap.add_argument(
        "--from-bin",
        type=Path,
        nargs="?",
        const=Path("__auto__"),
        default=None,
        help="Read existing .dem.data (default path: addon DemData/<world>/...)",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output DemData root (default: addon DemData/)",
    )
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[2]
    if args.out_dir is None:
        out_dir = root / "DemData" / args.world
    else:
        out_dir = args.out_dir / args.world
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.from_bin is not None:
        if args.from_bin == Path("__auto__"):
            bin_path = root / "DemData" / args.world / f"{args.world}.dem.data"
        else:
            bin_path = args.from_bin
        if not bin_path.is_file():
            raise SystemExit(f"bin missing: {bin_path}")
        pack_from_bin(bin_path, out_dir)
        return

    src = args.src
    if src is None:
        src = profile_dem_dir(args.world)
    # Prefer local bin conversion if CSV missing but bin exists.
    local_bin = out_dir / f"{args.world}.dem.data"
    if not (src / "manifest.csv").is_file() and local_bin.is_file():
        print(f"CSV missing; converting from {local_bin}")
        pack_from_bin(local_bin, out_dir)
        return
    if not (src / "manifest.csv").is_file():
        raise SystemExit(f"manifest missing: {src / 'manifest.csv'}")
    pack_from_csv(src, out_dir)


if __name__ == "__main__":
    main()
