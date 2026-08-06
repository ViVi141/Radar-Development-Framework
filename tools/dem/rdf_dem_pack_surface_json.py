#!/usr/bin/env python3
"""Pack surface-class-only JSON (RDF_SURF_JSON_V1) from RDF_DEM_BIN_V1 or V3 CSV.

Height is NOT stored here — use rdf_dem_pack_height_json.py (official .ttile).
Each cell is 1 byte (surface_class), hex-encoded in row chunk JSON.

To match 2 m HEIGHT packs from game terrain:
  python tools/dem/rdf_dem_pack_surface_json.py --world GM_Arland --match-height
  python tools/dem/rdf_dem_pack_surface_json.py --world GM_Eden --match-height --from-bin

Examples:
  python tools/dem/rdf_dem_pack_surface_json.py --world GM_Arland --from-bin
  python tools/dem/rdf_dem_pack_surface_json.py --world GM_Eden --from-bin
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

TILE_RE = re.compile(r"tile_(\d+)_(\d+)\.csv$", re.IGNORECASE)
BIN_MAGIC = b"RDFDEM1\0"
JSON_MAGIC = "RDF_SURF_JSON_V1"
CELL_BYTES = 6
CHUNKS_DIR = "surf_chunks/"
# RDF_DEM_SURF_UNKNOWN
SURF_UNKNOWN = 0


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


def parse_tile_surface(path: Path, size: int) -> bytes:
    cells = bytearray(size * size)
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or not line[0].isdigit():
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
            surface = max(0, min(255, int(float(parts[9]))))
            cells[local_iz * size + local_ix] = surface
    return bytes(cells)


def write_surface_pack(
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
    tiles_by_iz: dict[int, list[tuple[int, bytes]]],
) -> None:
    chunks = out_dir / "surf_chunks"
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
        "chunks_dir": CHUNKS_DIR,
    }
    (out_dir / "surf_manifest.json").write_text(
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

    mb = (out_dir / "surf_manifest.json").stat().st_size / (1024 * 1024)
    mb += sum(p.stat().st_size for p in chunks.glob("row_*.json")) / (1024 * 1024)
    print(
        f"wrote {out_dir} SURF rows={row_count} tiles={tile_count} "
        f"cell={cell_m} json≈{mb:.1f} MiB"
    )


def load_height_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        doc = json.load(handle)
    if doc.get("magic") != "RDF_HEIGHT_JSON_V1":
        raise SystemExit(f"bad HEIGHT magic: {path}")
    return doc


def tiles_from_full_grid(
    grid: bytes,
    width: int,
    height: int,
    size: int,
) -> dict[int, list[tuple[int, bytes]]]:
    if width % size != 0 or height % size != 0:
        raise SystemExit(f"grid {width}x{height} not divisible by tile_cells={size}")
    tx = width // size
    tz = height // size
    tiles_by_iz: dict[int, list[tuple[int, bytes]]] = {}
    for tile_iz in range(tz):
        row_tiles: list[tuple[int, bytes]] = []
        for tile_ix in range(tx):
            buf = bytearray(size * size)
            for local_iz in range(size):
                src_z = tile_iz * size + local_iz
                src_off = src_z * width + tile_ix * size
                dst_off = local_iz * size
                buf[dst_off : dst_off + size] = grid[src_off : src_off + size]
            row_tiles.append((tile_ix, bytes(buf)))
        tiles_by_iz[tile_iz] = row_tiles
    return tiles_by_iz


def load_source_surface_grid_dims(
    bin_path: Path | None,
    csv_dir: Path | None,
) -> tuple[bytes, float, float, float, float, float, int, int]:
    if bin_path is not None and bin_path.is_file():
        data = bin_path.read_bytes()
        if data[:8] != BIN_MAGIC:
            raise ValueError(f"bad bin magic: {bin_path}")
        bmin_x, bmin_z, bmax_x, bmax_z, cell_m = struct.unpack_from("<fffff", data, 16)
        size, tx, tz = struct.unpack_from("<iii", data, 36)
        table_count = struct.unpack_from("<i", data, 52)[0]
        table_off = 120
        width = tx * size
        height = tz * size
        grid = bytearray(width * height)
        expected = size * size * CELL_BYTES
        for i in range(table_count):
            ix, iz, offset, length = struct.unpack_from(
                "<iiii", data, table_off + i * 16
            )
            if length != expected:
                raise ValueError(f"tile {ix},{iz} length {length} != {expected}")
            payload = data[offset : offset + length]
            for local_iz in range(size):
                for local_ix in range(size):
                    cell = local_iz * size + local_ix
                    gx = ix * size + local_ix
                    gz = iz * size + local_iz
                    grid[gz * width + gx] = payload[cell * CELL_BYTES + 5]
        return (
            bytes(grid),
            cell_m,
            bmin_x,
            bmin_z,
            bmax_x,
            bmax_z,
            width,
            height,
        )

    if csv_dir is not None and (csv_dir / "manifest.csv").is_file():
        manifest = parse_manifest_csv(csv_dir / "manifest.csv")
        size = int(float(manifest["tile_cells"]))
        tx = int(float(manifest["tile_count_x"]))
        tz = int(float(manifest["tile_count_z"]))
        cell_m = float(manifest["cell_m"])
        bmin_x = float(manifest["bounds_min_x"])
        bmin_z = float(manifest["bounds_min_z"])
        bmax_x = float(manifest["bounds_max_x"])
        bmax_z = float(manifest["bounds_max_z"])
        width = tx * size
        height = tz * size
        grid = bytearray(width * height)
        tiles_dir = csv_dir / "tiles"
        for path in sorted(tiles_dir.glob("tile_*.csv")):
            m = TILE_RE.search(path.name)
            if not m:
                continue
            ix = int(m.group(1))
            iz = int(m.group(2))
            tile = parse_tile_surface(path, size)
            for local_iz in range(size):
                for local_ix in range(size):
                    gx = ix * size + local_ix
                    gz = iz * size + local_iz
                    grid[gz * width + gx] = tile[local_iz * size + local_ix]
        return (
            bytes(grid),
            cell_m,
            bmin_x,
            bmin_z,
            bmax_x,
            bmax_z,
            width,
            height,
        )

    raise SystemExit("no surface source (bin/csv) for resample")


def resample_surface_nearest(
    src: bytes,
    src_w: int,
    src_h: int,
    src_cell: float,
    src_bmin_x: float,
    src_bmin_z: float,
    dst_w: int,
    dst_h: int,
    dst_cell: float,
    dst_bmin_x: float,
    dst_bmin_z: float,
) -> bytes:
    out = bytearray(dst_w * dst_h)
    for gz in range(dst_h):
        wz = dst_bmin_z + (gz + 0.5) * dst_cell
        sz = int((wz - src_bmin_z) / src_cell)
        if sz < 0:
            sz = 0
        if sz >= src_h:
            sz = src_h - 1
        row_base = gz * dst_w
        src_row = sz * src_w
        for gx in range(dst_w):
            wx = dst_bmin_x + (gx + 0.5) * dst_cell
            sx = int((wx - src_bmin_x) / src_cell)
            if sx < 0:
                sx = 0
            if sx >= src_w:
                sx = src_w - 1
            out[row_base + gx] = src[src_row + sx]
    return bytes(out)


def pack_match_height(
    out_dir: Path,
    height_manifest: Path,
    bin_path: Path | None,
    csv_dir: Path | None,
) -> None:
    hm = load_height_manifest(height_manifest)
    world = str(hm["world"])
    cell_m = float(hm["cell_m"])
    size = int(hm["tile_cells"])
    tx = int(hm["tile_count_x"])
    tz = int(hm["tile_count_z"])
    bmin_x = float(hm["bounds_min_x"])
    bmin_z = float(hm["bounds_min_z"])
    bmax_x = float(hm["bounds_max_x"])
    bmax_z = float(hm["bounds_max_z"])
    width = tx * size
    height = tz * size

    use_bin = bin_path is not None and bin_path.is_file()
    use_csv = csv_dir is not None and (csv_dir / "manifest.csv").is_file()
    if use_bin or use_csv:
        (
            src_grid,
            src_cell,
            smin_x,
            smin_z,
            _smax_x,
            _smax_z,
            src_w,
            src_h,
        ) = load_source_surface_grid_dims(
            bin_path if use_bin else None,
            csv_dir if use_csv else None,
        )
        grid = resample_surface_nearest(
            src_grid,
            src_w,
            src_h,
            src_cell,
            smin_x,
            smin_z,
            width,
            height,
            cell_m,
            bmin_x,
            bmin_z,
        )
        print(
            f"SURF match-height: resampled bake {src_cell}m {src_w}x{src_h} "
            f"→ {cell_m}m {width}x{height}"
        )
        tiles = tiles_from_full_grid(grid, width, height, size)
    else:
        blank = bytes([SURF_UNKNOWN]) * (size * size)
        tiles = {iz: [(ix, blank) for ix in range(tx)] for iz in range(tz)}
        print(
            f"SURF match-height: UNKNOWN fill cell={cell_m} "
            f"grid={width}x{height} tiles={tx}x{tz} "
            f"(add --from-bin later for classes)"
        )

    write_surface_pack(
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
        tiles,
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
    table_count = struct.unpack_from("<i", data, 52)[0]
    world = (
        data[56 : 56 + 64]
        .split(b"\0", 1)[0]
        .decode("ascii", errors="ignore")
    )
    table_off = 120
    tiles_by_iz: dict[int, list[tuple[int, bytes]]] = {}
    expected = size * size * CELL_BYTES
    for i in range(table_count):
        ix, iz, offset, length = struct.unpack_from(
            "<iiii", data, table_off + i * 16
        )
        if length != expected:
            raise ValueError(f"tile {ix},{iz} length {length} != {expected}")
        payload = data[offset : offset + length]
        surf = bytearray(size * size)
        for cell in range(size * size):
            surf[cell] = payload[cell * CELL_BYTES + 5]
        tiles_by_iz.setdefault(iz, []).append((ix, bytes(surf)))

    write_surface_pack(
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
        tiles_by_iz.setdefault(iz, []).append((ix, parse_tile_surface(path, size)))

    write_surface_pack(
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
        tiles_by_iz,
    )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", required=True)
    ap.add_argument("--src", type=Path, default=None)
    ap.add_argument(
        "--from-bin",
        type=Path,
        nargs="?",
        const=Path("__auto__"),
        default=None,
    )
    ap.add_argument("--out-dir", type=Path, default=None)
    ap.add_argument(
        "--match-height",
        action="store_true",
        help="Write SURF on the same grid as height_manifest.json (e.g. 2 m .ttile)",
    )
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[2]
    world = args.world
    if not world.startswith("GM_"):
        world = f"GM_{world}"

    if args.out_dir is None:
        out_dir = root / "DemData" / world
    else:
        out_dir = args.out_dir / world
    out_dir.mkdir(parents=True, exist_ok=True)

    bin_path = None
    if args.from_bin is not None:
        if args.from_bin == Path("__auto__"):
            bin_path = root / "DemData" / world / f"{world}.dem.data"
            if not bin_path.is_file():
                bin_path = profile_dem_dir(world) / f"{world}.dem.data"
        else:
            bin_path = args.from_bin

    if args.match_height:
        hm = out_dir / "height_manifest.json"
        if not hm.is_file():
            raise SystemExit(f"height_manifest missing: {hm} (pack HEIGHT first)")
        csv_dir = args.src or profile_dem_dir(world)
        pack_match_height(out_dir, hm, bin_path, csv_dir)
        return

    if args.from_bin is not None:
        if bin_path is None or not bin_path.is_file():
            raise SystemExit(f"bin missing: {bin_path}")
        pack_from_bin(bin_path, out_dir)
        return

    src = args.src or profile_dem_dir(world)
    local_bin = out_dir / f"{world}.dem.data"
    if not (src / "manifest.csv").is_file() and local_bin.is_file():
        print(f"CSV missing; converting from {local_bin}")
        pack_from_bin(local_bin, out_dir)
        return
    if not (src / "manifest.csv").is_file():
        raise SystemExit(f"manifest missing: {src / 'manifest.csv'}")
    pack_from_csv(src, out_dir)


if __name__ == "__main__":
    main()
