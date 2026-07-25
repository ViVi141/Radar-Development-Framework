#!/usr/bin/env python3
"""Print paths and steps for RDF in-game DEM bake (Workbench profile)."""

from __future__ import annotations

import os


def main() -> None:
    home = os.path.expanduser("~")
    wb_profile = os.path.join(
        home,
        "Documents",
        "My Games",
        "ArmaReforgerWorkbench",
        "profile",
        "RDF",
    )
    flag = os.path.join(wb_profile, "BakeDemFull.flag")
    dem_root = os.path.join(wb_profile, "DemData")

    print("RDF DEM in-game bake (ANNA-style tile scan)")
    print("")
    print("IMPORTANT: Use Workbench Play. Standalone Arma Reforger uses a different profile.")
    print("")
    print("1) World Editor Plugins menu -> RDF Bake DEM Heightfield")
    print("   (writes BakeDemFull.flag; or create manually:)")
    print(f"   {flag}")
    print("   Or in script: SCR_BaseGameMode.RequestDemBake();")
    print("")
    print("2) Open world (e.g. GM_Eden) in Workbench and press Play.")
    print("   Keep Play running. Watch Script Console for [RDF DEM Bake] progress=...")
    print("")
    print("3) Output:")
    print(f"   {dem_root}\\<worldKey>\\tiles\\tile_*.csv")
    print(f"   {dem_root}\\<worldKey>\\manifest.csv")
    print(f"   {dem_root}\\<worldKey>\\bake_complete.txt")
    print("")
    print("4) Tile columns (V3):")
    print("   ... density_gcm3 surface_class n_spans")
    print("   s0_lo s0_hi ... s7_lo s7_hi  (absolute world Y intervals)")
    print("   surface_class: 0 unk ... 11 fabric")
    print("   spans: ground slab + merged entity AABBs in column (bridge/canopy)")
    print("")
    print("5) Re-bake V3: DELETE tiles/, run plugin RDF Bake DEM Heightfield")
    print("")
    print("6) Pack:")
    print("   python tools/dem/rdf_dem_pack.py --world GM_Eden")


if __name__ == "__main__":
    main()
