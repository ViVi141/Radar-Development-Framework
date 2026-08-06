#!/usr/bin/env python3
"""Unpack Reforger FORM/TERR .ttile HGHT into a heightfield preview."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

# Default: Arland from official full data dump.
DEFAULT_TERRAIN = Path(
    r"C:\Users\74738\Documents\arma_reforger_code\worlds\Arland\Terrain"
)


def read_form_chunks(data: bytes) -> tuple[str, list[tuple[str, bytes]]]:
    if data[:4] != b"FORM":
        raise ValueError("not a FORM file")
    form_size = struct.unpack(">I", data[4:8])[0]
    form_type = data[8:12].decode("ascii", errors="replace")
    chunks: list[tuple[str, bytes]] = []
    off = 12
    end = min(len(data), 8 + form_size)
    while off + 8 <= end:
        tag = data[off : off + 4].decode("ascii", errors="replace")
        size = struct.unpack(">I", data[off + 4 : off + 8])[0]
        payload = data[off + 8 : off + 8 + size]
        chunks.append((tag, payload))
        off = off + 8 + size
        if size % 2 == 1:
            off += 1
    return form_type, chunks


def parse_terr_head(payload: bytes) -> dict:
    # Observed Arland HEAD (32 bytes, little-endian fields):
    # resX, resY, block_verts, unk, cell_m, height_scale, ...
    # Tile vertex count is NOT in HEAD — it comes from HGHT payload size
    # (wiki: block 33x33, tile 129x129).
    if len(payload) < 24:
        raise ValueError("HEAD too short")
    res_x, res_y, block_verts, unk = struct.unpack_from("<IIII", payload, 0)
    cell_m, height_scale, height_offset = struct.unpack_from("<fff", payload, 16)
    return {
        "res_x": res_x,
        "res_y": res_y,
        "block_verts": block_verts,
        "unk": unk,
        "cell_m": cell_m,
        "height_scale": height_scale,
        "height_offset": height_offset,
    }


def decode_hght(payload: bytes) -> np.ndarray:
    """Decode HGHT payload as little-endian uint16 height codes."""
    nbytes = len(payload)
    if nbytes % 2 != 0:
        nbytes -= 1
    n2 = nbytes // 2
    tile_verts = int(round(n2**0.5))
    if tile_verts * tile_verts != n2:
        for candidate in (129, 257, 65, 33):
            need = candidate * candidate * 2
            if len(payload) >= need:
                tile_verts = candidate
                n2 = tile_verts * tile_verts
                break
        else:
            raise ValueError(f"cannot square HGHT len={len(payload)}")
    # Pixel samples are LE; FORM chunk sizes remain BE.
    return np.frombuffer(payload[: n2 * 2], dtype="<u2").reshape(
        tile_verts, tile_verts
    )


def unpack_terrain(terrain_dir: Path, out_dir: Path) -> Path:
    terr_path = terrain_dir / "Terrain.terr"
    if not terr_path.exists():
        matches = sorted(terrain_dir.glob("*.terr"))
        if matches:
            terr_path = matches[0]
    data_dir = terrain_dir / ".Data"
    if not terr_path.exists():
        raise SystemExit(f"missing *.terr under {terrain_dir}")
    if not data_dir.exists():
        raise SystemExit(f"missing {data_dir}")

    _, terr_chunks = read_form_chunks(terr_path.read_bytes())
    head = None
    for tag, payload in terr_chunks:
        if tag == "HEAD":
            head = parse_terr_head(payload)
    if not head:
        raise SystemExit("Terrain.terr has no HEAD")

    res_x = head["res_x"]
    res_y = head["res_y"]
    block_verts = head["block_verts"]
    cell_m = head["cell_m"]
    height_scale = head["height_scale"]
    height_offset = head["height_offset"]

    files = sorted(data_dir.glob("*.ttile"))
    if not files:
        raise SystemExit(f"no .ttile under {data_dir}")

    # Infer tile vertex count from the first HGHT chunk.
    _, first_chunks = read_form_chunks(files[0].read_bytes())
    tile_verts = None
    for tag, payload in first_chunks:
        if tag == "HGHT":
            tile_verts = decode_hght(payload).shape[0]
            break
    if tile_verts is None:
        raise SystemExit(f"no HGHT in {files[0].name}")

    step = tile_verts - 1
    tiles_x = (res_x - 1) // step
    tiles_z = (res_y - 1) // step

    print(
        f"HEAD res={res_x}x{res_y} block_verts={block_verts} "
        f"tile_verts={tile_verts} cell_m={cell_m} "
        f"scale={height_scale} offset={height_offset} "
        f"tiles={tiles_x}x{tiles_z}"
    )

    world = np.zeros((res_y, res_x), dtype=np.uint16)
    loaded = 0
    for path in files:
        stem = path.stem
        idx_s = stem.split("_")[-1]
        if not idx_s.isdigit():
            continue
        idx = int(idx_s)
        tx = idx % tiles_x
        tz = idx // tiles_x
        _, chunks = read_form_chunks(path.read_bytes())
        hght = None
        for tag, payload in chunks:
            if tag == "HGHT":
                hght = decode_hght(payload)
                break
        if hght is None:
            print(f"skip {path.name}: no HGHT")
            continue
        if hght.shape[0] != tile_verts:
            print(f"skip {path.name}: tile size {hght.shape}")
            continue
        x0 = tx * step
        z0 = tz * step
        world[z0 : z0 + tile_verts, x0 : x0 + tile_verts] = hght
        loaded += 1

    height_m = world.astype(np.float32) * height_scale + height_offset
    print(
        f"loaded {loaded}/{len(files)} tiles  "
        f"height_m min={height_m.min():.3f} max={height_m.max():.3f} "
        f"mean={height_m.mean():.3f}"
    )
    print(
        f"map size ~ {(res_x - 1) * cell_m:.0f} m x {(res_y - 1) * cell_m:.0f} m "
        f"(RDF bake often uses cell=4 m → this is denser)"
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    world_name = terrain_dir.parent.name
    npz = out_dir / f"{world_name}_ttile_height.npz"
    np.savez_compressed(
        npz,
        height_u16=world,
        height_m=height_m,
        cell_m=np.float32(cell_m),
        height_scale=np.float32(height_scale),
        height_offset=np.float32(height_offset),
        res_x=np.int32(res_x),
        res_y=np.int32(res_y),
        tile_verts=np.int32(tile_verts),
        block_verts=np.int32(block_verts),
    )
    print(f"wrote {npz}")

    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        png = out_dir / f"{world_name}_ttile_height.png"
        fig, ax = plt.subplots(figsize=(8, 8))
        im = ax.imshow(height_m, origin="lower", cmap="terrain")
        fig.colorbar(im, ax=ax, label="height_m")
        ax.set_title(
            f"{world_name} .ttile HGHT  {res_x}x{res_y} @ {cell_m:g} m"
        )
        fig.tight_layout()
        fig.savefig(png, dpi=140)
        plt.close(fig)
        print(f"wrote {png}")
    except Exception as exc:
        print("png failed:", exc)

    return npz


def main() -> None:
    terrain = DEFAULT_TERRAIN
    if len(sys.argv) > 1:
        terrain = Path(sys.argv[1])
    out = Path(__file__).resolve().parent / "out"
    unpack_terrain(terrain, out)


if __name__ == "__main__":
    main()
