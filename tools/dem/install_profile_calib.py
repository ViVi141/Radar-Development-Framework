#!/usr/bin/env python3
"""Copy offline calib JSON into the Arma Reforger profile for in-game bake-back.

Sources (under tools/dem/calib/):
  prf_clutter_shorad.json  -> HwCalib.json      (default)
  PatternLut.json          -> PatternLut.json   (--all)
  KnifeEdgeLut.json        -> KnifeEdgeLut.json (--all)
  SitePathLut.json         -> SitePathLut.json  (--with-site / --all)

In-game paths: $profile:RDF/RadarData/<name>.json

Examples:
  python tools/dem/install_profile_calib.py
  python tools/dem/install_profile_calib.py --all
  python tools/dem/install_profile_calib.py --with-site
  python tools/dem/install_profile_calib.py --profile-dir "D:\\Profiles\\MyServer"
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_CALIB = os.path.join(_HERE, "calib")


def default_profile_dir() -> str:
    home = os.path.expanduser("~")
    return os.path.join(home, "Documents", "My Games", "ArmaReforger", "profile")


def copy_one(src_name: str, dest_name: str, dest_dir: str) -> bool:
    src = os.path.join(_CALIB, src_name)
    if not os.path.isfile(src):
        print("skip (missing):", src_name)
        return False
    dest = os.path.join(dest_dir, dest_name)
    shutil.copy2(src, dest)
    print("copied", src_name, "->", dest)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Install RDF calib JSON into $profile:RDF/RadarData/"
    )
    parser.add_argument(
        "--profile-dir",
        default=default_profile_dir(),
        help="Arma Reforger profile root (RDF/RadarData is created under it)",
    )
    parser.add_argument(
        "--with-site",
        action="store_true",
        help="Also copy SitePathLut.json (origin must match your fixed radar)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Copy HwCalib + Pattern + KnifeEdge + SitePathLut",
    )
    args = parser.parse_args()

    dest_dir = os.path.join(args.profile_dir, "RDF", "RadarData")
    os.makedirs(dest_dir, exist_ok=True)

    copied = 0
    if copy_one("prf_clutter_shorad.json", "HwCalib.json", dest_dir):
        copied = copied + 1
    else:
        print("error: required calib/prf_clutter_shorad.json missing", file=sys.stderr)
        return 1

    if args.all:
        if copy_one("PatternLut.json", "PatternLut.json", dest_dir):
            copied = copied + 1
        if copy_one("KnifeEdgeLut.json", "KnifeEdgeLut.json", dest_dir):
            copied = copied + 1

    if args.all or args.with_site:
        if copy_one("SitePathLut.json", "SitePathLut.json", dest_dir):
            copied = copied + 1
            print(
                "note: SitePathLut is origin-locked; rebuild with "
                "rdf_radar_pattern_site_validate.py for your site"
            )

    print("done: copied=%d dest=%s" % (copied, dest_dir))
    print(
        "In-game: ApplyGameplayFidelity(AIRBORNE) loads HwCalib when present; "
        "SHORAD/WLR/ESM packs call TryEnableSitePathLutIfBaked()"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
