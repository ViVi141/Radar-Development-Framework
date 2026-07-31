#!/usr/bin/env python3
"""Pack RDF radar signature CSV (V2) into RDF_SIG_JSON_V1 JSON for Workshop.

Example:
  python tools/dem/rdf_sig_pack_json.py
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

MAGIC = "RDF_SIG_JSON_V1"
VERSION = 1


def profile_sig_csv() -> Path:
    home = Path.home()
    return (
        home
        / "Documents"
        / "My Games"
        / "ArmaReforgerWorkbench"
        / "profile"
        / "RDF"
        / "Signatures"
        / "rdf_radar_signatures.csv"
    )


def pack_csv(src: Path, out_path: Path) -> None:
    entries: list[dict] = []
    with src.open("r", encoding="utf-8", errors="replace", newline="") as f:
        magic = f.readline().strip()
        if magic != "RDF_RADAR_SIG_V2":
            raise ValueError(f"bad magic: {magic}")
        _header = f.readline().strip()
        reader = csv.reader(f)
        for parts in reader:
            if len(parts) < 8:
                continue
            entry = {
                "key": parts[0],
                "size_x_m": float(parts[1]),
                "size_y_m": float(parts[2]),
                "size_z_m": float(parts[3]),
                "char_length_m": float(parts[4]),
                "mean_rcs_m2": float(parts[5]),
                "swerling": int(float(parts[6])),
                "type_hint": int(float(parts[7])),
            }
            if len(parts) >= 12:
                entry["rotor_tip_ms"] = float(parts[8] or 0.0)
                entry["blade_count"] = int(float(parts[9] or 0.0))
                entry["rotor_rcs_frac"] = float(parts[10] or 0.0)
                entry["hub_width_ms"] = float(parts[11] or 0.0)
            entries.append(entry)

    doc = {"magic": MAGIC, "version": VERSION, "entries": entries}
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(
        json.dumps(doc, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    legacy = out_path.with_suffix(".sig.data")
    # out is *.json; also remove sibling .sig.data if packing default name
    sibling = out_path.parent / "rdf_radar_signatures.sig.data"
    if sibling.is_file():
        sibling.unlink()
        print(f"removed legacy {sibling}")

    print(f"wrote {out_path} count={len(entries)} bytes={out_path.stat().st_size}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, default=None)
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output JSON (default: addon Signatures/rdf_radar_signatures.json)",
    )
    args = ap.parse_args()

    src = args.src or profile_sig_csv()
    if not src.is_file():
        raise SystemExit(f"signature CSV missing: {src}")

    if args.out is None:
        root = Path(__file__).resolve().parents[2]
        out = root / "Signatures" / "rdf_radar_signatures.json"
    else:
        out = args.out

    pack_csv(src, out)


if __name__ == "__main__":
    main()
