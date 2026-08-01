#!/usr/bin/env python3
"""Insert explicit rotor columns into airframe heli rows in rdf_radar_signatures.conf.

Skips VehParts / rotor_ / cockpit entries. Family table:
  Mi-8 / SP01_Mi8 → tip 230, blades 5, frac 0.40, hub 45
  UH-1 / other Helicopters/ → tip 220, blades 2, frac 0.35, hub 40

Example:
  python tools/dem/rdf_sig_patch_heli_rotors.py
  python tools/dem/rdf_sig_patch_heli_rotors.py --dry-run
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

UH1 = {
    "tip": 220.0,
    "blades": 2,
    "frac": 0.35,
    "hub": 40.0,
}
MI8 = {
    "tip": 230.0,
    "blades": 5,
    "frac": 0.40,
    "hub": 45.0,
}

ENTRY_RE = re.compile(
    r'(RDF_RadarSignatureEntryConf\s+"\{[^}]+\}"\s*\{)(.*?)(\n\s*\})',
    re.DOTALL,
)
KEY_RE = re.compile(r'm_sKey\s+"([^"]+)"')


def is_airframe_heli(key: str) -> bool:
    if "VehParts/" in key:
        return False
    if "rotor_" in key:
        return False
    if "cockpit" in key.lower():
        return False
    if "Helicopters/" in key:
        return True
    if "UH1H" in key or "Mi8" in key or "SP01_Mi8" in key:
        return True
    if "Cinematic_Flying_Mi8" in key:
        return True
    return False


def family_for_key(key: str) -> dict[str, float | int]:
    if "Mi8" in key or "SP01_Mi8" in key:
        return MI8
    return UH1


def has_rotor_columns(body: str) -> bool:
    return "m_fRotorTipSpeedMs" in body


def rotor_lines(indent: str, fam: dict[str, float | int]) -> str:
    return (
        f"{indent}m_fRotorTipSpeedMs {fam['tip']}\n"
        f"{indent}m_iBladeCount {fam['blades']}\n"
        f"{indent}m_fRotorRcsFraction {fam['frac']}\n"
        f"{indent}m_fHubWidthMs {fam['hub']}"
    )


def patch_conf_text(text: str) -> tuple[str, int, int]:
    """Return (new_text, patched_count, skipped_already)."""
    patched = 0
    skipped = 0

    def repl(match: re.Match[str]) -> str:
        nonlocal patched, skipped
        head = match.group(1)
        body = match.group(2)
        tail = match.group(3)
        key_m = KEY_RE.search(body)
        if not key_m:
            return match.group(0)
        key = key_m.group(1)
        if not is_airframe_heli(key):
            return match.group(0)
        if has_rotor_columns(body):
            skipped += 1
            return match.group(0)
        fam = family_for_key(key)
        # Match trailing indent of last attribute line (usually 3 spaces).
        indent = "   "
        last_line = body.rstrip("\n").split("\n")[-1]
        lead = re.match(r"^(\s*)", last_line)
        if lead and lead.group(1):
            indent = lead.group(1)
        body_stripped = body.rstrip()
        if not body_stripped.endswith("\n"):
            body_stripped = body_stripped + "\n"
        new_body = body_stripped + rotor_lines(indent, fam) + "\n"
        patched += 1
        return head + new_body + tail

    new_text = ENTRY_RE.sub(repl, text)
    return new_text, patched, skipped


def default_conf_path() -> Path:
    root = Path(__file__).resolve().parents[2]
    return root / "Signatures" / "rdf_radar_signatures.conf"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--conf",
        type=Path,
        default=None,
        help="Path to rdf_radar_signatures.conf",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report counts without writing",
    )
    args = parser.parse_args()
    conf = args.conf
    if conf is None:
        conf = default_conf_path()
    text = conf.read_text(encoding="utf-8", errors="replace")
    new_text, patched, skipped = patch_conf_text(text)
    print(f"conf={conf}")
    print(f"patched={patched} already_had_rotor={skipped}")
    if args.dry_run:
        return
    if patched == 0:
        print("no changes")
        return
    conf.write_text(new_text, encoding="utf-8", newline="\n")
    print("wrote conf")


if __name__ == "__main__":
    main()
