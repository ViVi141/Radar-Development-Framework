#!/usr/bin/env python3
"""Pack RDF radar signature CSV (V2) into Enfusion .conf for Workshop.

Example:
  python tools/dem/rdf_sig_pack_conf.py
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

CONF_GUID = "C8A3F15E902B47D1"


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


def escape_conf_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def make_entry_guid(index: int) -> str:
    # Deterministic-ish unique 16-hex GUIDs under the pack namespace.
    return f"{CONF_GUID[:8]}{index:08X}"


def pack_csv(src: Path, out_conf: Path) -> None:
    rows: list[tuple] = []
    with src.open("r", encoding="utf-8", errors="replace", newline="") as f:
        magic = f.readline().strip()
        if magic != "RDF_RADAR_SIG_V2":
            raise ValueError(f"bad magic: {magic}")
        _header = f.readline().strip()
        reader = csv.reader(f)
        for parts in reader:
            if len(parts) < 8:
                continue
            rows.append(
                (
                    parts[0],
                    float(parts[1]),
                    float(parts[2]),
                    float(parts[3]),
                    float(parts[4]),
                    float(parts[5]),
                    int(float(parts[6])),
                    int(float(parts[7])),
                )
            )

    lines: list[str] = []
    lines.append("RDF_RadarSignatureTableConf {")
    lines.append(" m_iVersion 1")
    lines.append(" m_aEntries {")
    for i, (key, sx, sy, sz, cl, rcs, sw, th) in enumerate(rows):
        guid = make_entry_guid(i + 1)
        lines.append(f'  RDF_RadarSignatureEntryConf "{{{guid}}}" {{')
        lines.append(f'   m_sKey "{escape_conf_string(key)}"')
        lines.append(f"   m_fSizeX {sx}")
        lines.append(f"   m_fSizeY {sy}")
        lines.append(f"   m_fSizeZ {sz}")
        lines.append(f"   m_fCharLengthM {cl}")
        lines.append(f"   m_fMeanRcsM2 {rcs}")
        lines.append(f"   m_iSwerling {sw}")
        lines.append(f"   m_iTypeHint {th}")
        lines.append("  }")
    lines.append(" }")
    lines.append("}")
    lines.append("")

    out_conf.parent.mkdir(parents=True, exist_ok=True)
    out_conf.write_text("\n".join(lines), encoding="utf-8")

    meta = out_conf.with_suffix(".conf.meta")
    # File is *.conf → with_suffix('.conf.meta') becomes name.conf.meta only if stem ends oddly.
    meta = Path(str(out_conf) + ".meta")
    meta.write_text(
        "\n".join(
            [
                "MetaFileClass {",
                f' Name "{{{CONF_GUID}}}Signatures/rdf_radar_signatures.conf"',
                " Configurations {",
                "  CONFResourceClass PC {",
                "  }",
                "  CONFResourceClass XBOX_ONE : PC {",
                "  }",
                "  CONFResourceClass XBOX_SERIES : PC {",
                "  }",
                "  CONFResourceClass PS4 : PC {",
                "  }",
                "  CONFResourceClass PS5 : PC {",
                "  }",
                "  CONFResourceClass HEADLESS : PC {",
                "  }",
                " }",
                "}",
                "",
            ]
        ),
        encoding="utf-8",
    )

    for legacy_name in (
        "rdf_radar_signatures.sig.data",
        "rdf_radar_signatures.json",
    ):
        legacy = out_conf.parent / legacy_name
        if legacy.is_file():
            legacy.unlink()
            print(f"removed legacy {legacy}")

    print(
        f"wrote {out_conf} count={len(rows)} bytes={out_conf.stat().st_size} meta={meta.name}"
    )


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, default=None)
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output .conf (default: addon Signatures/rdf_radar_signatures.conf)",
    )
    args = ap.parse_args()

    src = args.src or profile_sig_csv()
    if not src.is_file():
        raise SystemExit(f"signature CSV missing: {src}")

    if args.out is None:
        root = Path(__file__).resolve().parents[2]
        out = root / "Signatures" / "rdf_radar_signatures.conf"
    else:
        out = args.out

    pack_csv(src, out)


if __name__ == "__main__":
    main()
