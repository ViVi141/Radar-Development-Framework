#!/usr/bin/env python3
"""Pack RDF radar signature CSV (V2) into RDF_SIG_BIN_V1 (.sig.data).

Layout (little-endian):
  magic "RDFSIG1\\0" (8)
  version i32=1
  count i32
  records:
    key_len u16
    key bytes (UTF-8, no NUL required)
    size_x, size_y, size_z, char_length, mean_rcs f32
    swerling i32, type_hint i32

Example:
  python tools/dem/rdf_sig_pack_bin.py
"""

from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path

MAGIC = b"RDFSIG1\0"
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
    rows: list[tuple] = []
    with src.open("r", encoding="utf-8", errors="replace", newline="") as f:
        magic = f.readline().strip()
        if magic != "RDF_RADAR_SIG_V2":
            raise ValueError(f"bad magic: {magic}")
        header = f.readline().strip()
        reader = csv.reader(f)
        for parts in reader:
            if len(parts) < 8:
                continue
            key = parts[0]
            rows.append(
                (
                    key,
                    float(parts[1]),
                    float(parts[2]),
                    float(parts[3]),
                    float(parts[4]),
                    float(parts[5]),
                    int(float(parts[6])),
                    int(float(parts[7])),
                )
            )

    body = bytearray()
    for key, sx, sy, sz, cl, rcs, sw, th in rows:
        key_b = key.encode("utf-8")
        if len(key_b) > 65535:
            raise ValueError(f"key too long: {key[:64]}...")
        body += struct.pack("<H", len(key_b))
        body += key_b
        body += struct.pack("<fffffii", sx, sy, sz, cl, rcs, sw, th)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<ii", VERSION, len(rows)))
        f.write(body)

    print(f"wrote {out_path} count={len(rows)} bytes={out_path.stat().st_size}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, default=None)
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output .sig.data (default: addon Signatures/rdf_radar_signatures.sig.data)",
    )
    args = ap.parse_args()

    src = args.src or profile_sig_csv()
    if not src.is_file():
        raise SystemExit(f"signature CSV missing: {src}")

    if args.out is None:
        root = Path(__file__).resolve().parents[2]
        out = root / "Signatures" / "rdf_radar_signatures.sig.data"
    else:
        out = args.out

    pack_csv(src, out)


if __name__ == "__main__":
    main()
