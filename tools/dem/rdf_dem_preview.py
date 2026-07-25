#!/usr/bin/env python3
"""Render RDF DEM overview and terrain radar visibility preview."""

from __future__ import annotations

import argparse
import glob
import os
import re

import matplotlib
import matplotlib.pyplot as plt
import numpy as np

matplotlib.use("Agg")

TILE_RE = re.compile(r"dataset_(\d+)_(\d+)\.npz$")


def load_dem(tile_dir: str) -> dict[str, np.ndarray | float]:
    files = glob.glob(os.path.join(tile_dir, "dataset_*.npz"))
    if not files:
        raise SystemExit(f"No dataset tiles found in {tile_dir}")

    metadata: list[tuple[str, int, int, int, int]] = []
    width = 0
    height = 0
    cell_m = 4.0

    for path in files:
        match = TILE_RE.search(os.path.basename(path))
        if not match:
            continue

        with np.load(path) as tile:
            tile_height, tile_width = tile["terrain_y"].shape
            origin_ix = int(tile["origin_ix"])
            origin_iz = int(tile["origin_iz"])
            if "cell_m" in tile:
                cell_m = float(tile["cell_m"])

        metadata.append((path, origin_ix, origin_iz, tile_width, tile_height))
        width = max(width, origin_ix + tile_width)
        height = max(height, origin_iz + tile_height)

    terrain = np.full((height, width), np.nan, dtype=np.float32)
    slope = np.zeros((height, width), dtype=np.float32)
    water_type = np.zeros((height, width), dtype=np.uint8)
    occupancy = np.zeros((height, width), dtype=np.float32)
    density = np.full((height, width), 0.5, dtype=np.float32)
    surface = np.zeros((height, width), dtype=np.uint8)
    has_material = False

    for path, origin_ix, origin_iz, tile_width, tile_height in metadata:
        with np.load(path) as tile:
            rows = slice(origin_iz, origin_iz + tile_height)
            cols = slice(origin_ix, origin_ix + tile_width)
            terrain[rows, cols] = tile["terrain_y"]
            slope[rows, cols] = tile["slope_deg"]
            water_type[rows, cols] = tile["water_type"]
            occupancy[rows, cols] = tile["occ_ground"]
            if "density_gcm3" in tile.files:
                density[rows, cols] = tile["density_gcm3"]
                has_material = True
            if "surface_class" in tile.files:
                surface[rows, cols] = tile["surface_class"]
                has_material = True

    terrain[terrain < -200.0] = np.nan
    return {
        "terrain": terrain,
        "slope": slope,
        "water_type": water_type,
        "occupancy": occupancy,
        "density": density,
        "surface": surface,
        "has_material": has_material,
        "cell_m": cell_m,
    }


def choose_radar_site(terrain: np.ndarray) -> tuple[int, int]:
    valid = np.isfinite(terrain)
    candidate = np.where(valid, terrain, -1e9)
    margin = 128
    candidate[:margin, :] = -1e9
    candidate[-margin:, :] = -1e9
    candidate[:, :margin] = -1e9
    candidate[:, -margin:] = -1e9
    iz, ix = np.unravel_index(np.argmax(candidate), candidate.shape)
    return int(iz), int(ix)


def compute_viewshed(
    terrain: np.ndarray,
    cell_m: float,
    radar_iz: int,
    radar_ix: int,
    radar_height_m: float,
    max_range_m: float,
    azimuth_count: int,
) -> np.ndarray:
    height, width = terrain.shape
    viewshed = np.zeros((height, width), dtype=np.uint8)
    radar_y = float(terrain[radar_iz, radar_ix]) + radar_height_m
    step_m = cell_m
    max_steps = int(max_range_m / step_m)

    for azimuth_index in range(azimuth_count):
        angle = 2.0 * np.pi * azimuth_index / azimuth_count
        direction_x = np.cos(angle)
        direction_z = np.sin(angle)
        maximum_elevation = -1e9

        for step_index in range(1, max_steps + 1):
            range_m = step_index * step_m
            ix = int(round(radar_ix + direction_x * step_index))
            iz = int(round(radar_iz + direction_z * step_index))
            if ix < 0 or ix >= width or iz < 0 or iz >= height:
                break

            terrain_y = terrain[iz, ix]
            if not np.isfinite(terrain_y):
                continue

            elevation = (float(terrain_y) - radar_y) / range_m
            if elevation >= maximum_elevation - 0.0001:
                viewshed[iz, ix] = 1
                maximum_elevation = max(maximum_elevation, elevation)

    viewshed[radar_iz, radar_ix] = 1
    return viewshed


def render(
    dem: dict[str, np.ndarray | float],
    output_path: str,
    max_range_m: float,
    azimuth_count: int,
) -> None:
    terrain = np.asarray(dem["terrain"])
    slope = np.asarray(dem["slope"])
    water_type = np.asarray(dem["water_type"])
    occupancy = np.asarray(dem["occupancy"])
    density = np.asarray(dem["density"])
    surface = np.asarray(dem["surface"])
    has_material = bool(dem.get("has_material", False))
    cell_m = float(dem["cell_m"])

    radar_iz, radar_ix = choose_radar_site(terrain)
    viewshed = compute_viewshed(
        terrain,
        cell_m,
        radar_iz,
        radar_ix,
        radar_height_m=25.0,
        max_range_m=max_range_m,
        azimuth_count=azimuth_count,
    )

    extent_km = [
        0,
        terrain.shape[1] * cell_m / 1000.0,
        0,
        terrain.shape[0] * cell_m / 1000.0,
    ]
    radar_x_km = radar_ix * cell_m / 1000.0
    radar_z_km = radar_iz * cell_m / 1000.0

    fig, axes = plt.subplots(2, 2, figsize=(14, 12), constrained_layout=True)

    elevation_plot = axes[0, 0].imshow(
        terrain, origin="lower", extent=extent_km, cmap="terrain"
    )
    axes[0, 0].plot(radar_x_km, radar_z_km, "r^", markersize=10)
    axes[0, 0].set_title("RDF DEM elevation (4 m)")
    fig.colorbar(elevation_plot, ax=axes[0, 0], label="Terrain Y (m)")

    if has_material:
        density_plot = axes[0, 1].imshow(
            np.clip(density, 0.0, 8.0),
            origin="lower",
            extent=extent_km,
            cmap="plasma",
            vmin=0.0,
            vmax=4.0,
        )
        axes[0, 1].set_title("Material density (g/cm³)")
        fig.colorbar(density_plot, ax=axes[0, 1], label="Density")

        # 0 unknown,1 water,2 veg,3 soil,4 sand,5 gravel,6 asphalt,7 hard,8 wood,9 metal,10 snow,11 fabric
        surf_colors = [
            "#555555",  # unknown
            "#2584d8",  # water
            "#2ea043",  # veg
            "#8b6b3d",  # soil
            "#d4b56a",  # sand
            "#9a9a9a",  # gravel
            "#333333",  # asphalt
            "#b0b0b0",  # hard
            "#8b5a2b",  # wood
            "#c0c0c0",  # metal
            "#e8f4ff",  # snow
            "#c47ac0",  # fabric
        ]
        class_plot = axes[1, 0].imshow(
            surface,
            origin="lower",
            extent=extent_km,
            cmap=matplotlib.colors.ListedColormap(surf_colors),
            vmin=0,
            vmax=11,
        )
        axes[1, 0].set_title("Surface class (material bake)")
        colorbar = fig.colorbar(class_plot, ax=axes[1, 0], ticks=list(range(12)))
        colorbar.ax.set_yticklabels(
            [
                "unk",
                "water",
                "veg",
                "soil",
                "sand",
                "gravel",
                "asphalt",
                "hard",
                "wood",
                "metal",
                "snow",
                "fabric",
            ]
        )
    else:
        slope_plot = axes[0, 1].imshow(
            np.clip(slope, 0, 60),
            origin="lower",
            extent=extent_km,
            cmap="magma",
            vmin=0,
            vmax=60,
        )
        axes[0, 1].set_title("Terrain slope")
        fig.colorbar(slope_plot, ax=axes[0, 1], label="Slope (deg)")

        classification = np.zeros(terrain.shape, dtype=np.uint8)
        classification[occupancy > 0.5] = 1
        classification[water_type > 0] = 2
        class_plot = axes[1, 0].imshow(
            classification,
            origin="lower",
            extent=extent_km,
            cmap=matplotlib.colors.ListedColormap(["#80745f", "#287a38", "#2584d8"]),
            vmin=0,
            vmax=2,
        )
        axes[1, 0].set_title("Surface classification: ground / obstacle / water")
        colorbar = fig.colorbar(class_plot, ax=axes[1, 0], ticks=[0, 1, 2])
        colorbar.ax.set_yticklabels(["Ground", "Obstacle", "Water"])

    axes[1, 1].imshow(
        terrain, origin="lower", extent=extent_km, cmap="gray", alpha=0.55
    )
    visible_mask = np.ma.masked_where(viewshed == 0, viewshed)
    axes[1, 1].imshow(
        visible_mask,
        origin="lower",
        extent=extent_km,
        cmap=matplotlib.colors.ListedColormap(["#ffea00"]),
        alpha=0.8,
    )
    axes[1, 1].plot(radar_x_km, radar_z_km, "r^", markersize=10)
    axes[1, 1].set_title(
        f"Terrain radar visibility ({max_range_m / 1000.0:.1f} km, "
        f"{azimuth_count} beams)"
    )

    for ax in axes.flat:
        ax.set_xlabel("X (km)")
        ax.set_ylabel("Z (km)")

    valid = np.isfinite(terrain)
    water_fraction = float((water_type[valid] > 0).mean()) * 100.0
    obstacle_fraction = float((occupancy[valid] > 0.5).mean()) * 100.0
    visible_fraction = float(viewshed.sum()) / max(float(valid.sum()), 1.0) * 100.0
    material_note = "material=V2" if has_material else "material=missing (V1)"
    fig.suptitle(
        "RDF GM_Eden DEM scan preview\n"
        f"grid={terrain.shape[1]}x{terrain.shape[0]}, "
        f"valid={valid.mean() * 100.0:.1f}%, "
        f"water={water_fraction:.1f}%, obstacle={obstacle_fraction:.1f}%, "
        f"visible={visible_fraction:.2f}%, {material_note}",
        fontsize=14,
    )

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    fig.savefig(output_path, dpi=130)
    plt.close(fig)

    print(f"radar site: ix={radar_ix}, iz={radar_iz}, y={terrain[radar_iz, radar_ix]:.1f} m")
    print(f"water cells: {water_fraction:.2f}%")
    print(f"obstacle cells: {obstacle_fraction:.2f}%")
    print(f"visible cells: {visible_fraction:.2f}%")
    print(f"wrote: {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--world", default="GM_Eden")
    parser.add_argument("--max-range-m", type=float, default=6000.0)
    parser.add_argument("--azimuths", type=int, default=1440)
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    tile_dir = os.path.join(script_dir, "TrainData", args.world, "tiles")
    output_path = os.path.join(script_dir, "out", f"{args.world}_dem_preview.png")
    dem = load_dem(tile_dir)
    render(dem, output_path, args.max_range_m, args.azimuths)


if __name__ == "__main__":
    main()
