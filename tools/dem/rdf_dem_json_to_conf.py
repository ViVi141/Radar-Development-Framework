#!/usr/bin/env python3
"""Convert DemData SURF/HEIGHT JSON packs to workshop-safe .conf (CONFResourceClass).

Game / dedicated-server runtimes do not register a Resource loader for .json
(JSONResourceClass is Workbench-oriented). SurfaceTable/Signatures already ship
as .conf for this reason.

Usage:
  python tools/dem/rdf_dem_json_to_conf.py
  python tools/dem/rdf_dem_json_to_conf.py --keep-json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEM = ROOT / "DemData"

META_NAME_RE = re.compile(r'Name\s+"(\{[0-9A-Fa-f]+\})([^"]+)"')


def child_guid(seed: str) -> str:
    digest = hashlib.md5(seed.encode("utf-8")).hexdigest()[:16].upper()
    return "{" + digest + "}"


def read_meta_guid(meta_path: Path) -> str | None:
    if not meta_path.is_file():
        return None
    text = meta_path.read_text(encoding="utf-8", errors="replace")
    match = META_NAME_RE.search(text)
    if not match:
        return None
    return match.group(1)


def write_meta(conf_rel: str, guid: str, out_meta: Path) -> None:
    body = (
        "MetaFileClass {\n"
        f' Name "{guid}{conf_rel}"\n'
        " Configurations {\n"
        "  CONFResourceClass PC {\n"
        "  }\n"
        "  CONFResourceClass XBOX_ONE : PC {\n"
        "  }\n"
        "  CONFResourceClass XBOX_SERIES : PC {\n"
        "  }\n"
        "  CONFResourceClass PS4 : PC {\n"
        "  }\n"
        "  CONFResourceClass PS5 : PC {\n"
        "  }\n"
        "  CONFResourceClass HEADLESS : PC {\n"
        "  }\n"
        " }\n"
        "}\n"
    )
    out_meta.write_text(body, encoding="utf-8", newline="\n")


def fmt_float(value: float) -> str:
    text = f"{float(value):.9g}"
    return text


def write_surf_manifest_conf(data: dict, out_path: Path) -> None:
    chunks = data.get("chunks_dir") or "surf_chunks/"
    lines = [
        "RDF_DemSurfManifestConf {",
        f' m_sMagic "{data.get("magic", "RDF_SURF_JSON_V1")}"',
        f' m_sWorld "{data["world"]}"',
        f" m_fBoundsMinX {fmt_float(data['bounds_min_x'])}",
        f" m_fBoundsMinZ {fmt_float(data['bounds_min_z'])}",
        f" m_fBoundsMaxX {fmt_float(data['bounds_max_x'])}",
        f" m_fBoundsMaxZ {fmt_float(data['bounds_max_z'])}",
        f" m_fCellM {fmt_float(data['cell_m'])}",
        f" m_iTileCells {int(data['tile_cells'])}",
        f" m_iTileCountX {int(data['tile_count_x'])}",
        f" m_iTileCountZ {int(data['tile_count_z'])}",
        f' m_sChunksDir "{chunks}"',
        "}",
        "",
    ]
    out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_height_manifest_conf(data: dict, out_path: Path) -> None:
    chunks = data.get("chunks_dir") or "height_chunks/"
    y_scale = data.get("y_scale", 0.1)
    lines = [
        "RDF_DemHeightManifestConf {",
        f' m_sMagic "{data.get("magic", "RDF_HEIGHT_JSON_V1")}"',
        f' m_sWorld "{data["world"]}"',
        f" m_fBoundsMinX {fmt_float(data['bounds_min_x'])}",
        f" m_fBoundsMinZ {fmt_float(data['bounds_min_z'])}",
        f" m_fBoundsMaxX {fmt_float(data['bounds_max_x'])}",
        f" m_fBoundsMaxZ {fmt_float(data['bounds_max_z'])}",
        f" m_fCellM {fmt_float(data['cell_m'])}",
        f" m_iTileCells {int(data['tile_cells'])}",
        f" m_iTileCountX {int(data['tile_count_x'])}",
        f" m_iTileCountZ {int(data['tile_count_z'])}",
        f" m_fYScale {fmt_float(y_scale)}",
        f' m_sChunksDir "{chunks}"',
        "}",
        "",
    ]
    out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_row_conf(data: dict, out_path: Path, seed_prefix: str) -> None:
    iz = int(data["iz"])
    tiles = data.get("tiles") or []
    lines = [
        "RDF_DemChunkRowConf {",
        f" m_iIz {iz}",
        " m_aTiles {",
    ]
    for tile in tiles:
        ix = int(tile["ix"])
        hex_payload = tile.get("hex") or ""
        guid = child_guid(f"{seed_prefix}:{iz}:{ix}")
        lines.append(f'  RDF_DemChunkTileConf "{guid}" {{')
        lines.append(f"   m_iIx {ix}")
        lines.append(f'   m_sHex "{hex_payload}"')
        lines.append("  }")
    lines.append(" }")
    lines.append("}")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def convert_one(json_path: Path, keep_json: bool) -> None:
    rel = json_path.relative_to(ROOT).as_posix()
    data = json.loads(json_path.read_text(encoding="utf-8"))
    conf_path = json_path.with_suffix(".conf")
    conf_rel = conf_path.relative_to(ROOT).as_posix()
    old_meta = Path(str(json_path) + ".meta")
    guid = read_meta_guid(old_meta)
    if guid is None:
        guid = child_guid(conf_rel)

    name = json_path.name
    if name == "surf_manifest.json":
        write_surf_manifest_conf(data, conf_path)
    elif name == "height_manifest.json":
        write_height_manifest_conf(data, conf_path)
    elif name.startswith("row_") and json_path.parent.name in (
        "surf_chunks",
        "height_chunks",
    ):
        write_row_conf(data, conf_path, conf_rel)
    else:
        print(f"skip unknown json: {rel}", file=sys.stderr)
        return

    write_meta(conf_rel, guid, Path(str(conf_path) + ".meta"))

    if not keep_json:
        json_path.unlink(missing_ok=True)
        old_meta.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--keep-json",
        action="store_true",
        help="Keep original .json/.meta (default: delete after convert)",
    )
    args = parser.parse_args()

    json_files = sorted(DEM.rglob("*.json"))
    # Skip accidental non-pack json under DemData if any
    pack_files = [
        p
        for p in json_files
        if p.name in ("surf_manifest.json", "height_manifest.json")
        or (
            p.name.startswith("row_")
            and p.parent.name in ("surf_chunks", "height_chunks")
        )
    ]
    print(f"converting {len(pack_files)} DemData JSON -> CONF")
    for index, path in enumerate(pack_files, start=1):
        convert_one(path, keep_json=args.keep_json)
        if index % 50 == 0 or index == len(pack_files):
            print(f"  {index}/{len(pack_files)}")
    print("done")


if __name__ == "__main__":
    main()
