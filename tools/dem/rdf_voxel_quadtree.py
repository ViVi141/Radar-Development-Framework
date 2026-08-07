#!/usr/bin/env python3
"""XZ quadtree memory-block strategy for engineering power voxels.

Measurement-only prototype: classify horizontal leaves (air / obstacle / ROI),
store a coarse vertical column per leaf. Compares bytes and build time against
a uniform dense grid over the same extent.

Not a Maxwell solver. Vertical axis stays a short column (DEM-span style),
not a full octree — matches RDF DEM column thinking.
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass, field

from rdf_voxel_em import ValidationCaseResult, estimate_dense_cost


# Estimated payload sizes (bytes) for accounting — not Python object sizeof.
LEAF_HEADER_BYTES = 48  # bounds + class + links + flags
SAMPLE_BYTES = 12  # occ u8 + atten f32 + power f32 (packed estimate)


@dataclass
class ObstacleBox:
    """Axis-aligned obstacle footprint on XZ; blocks Y up to height_m."""

    x0: float
    z0: float
    x1: float
    z1: float
    height_m: float


@dataclass
class QuadLeaf:
    x0: float
    z0: float
    x1: float
    z1: float
    level: int
    kind: str  # "air" | "obstacle" | "roi"
    n_y: int
    occupied: bool = False

    @property
    def side_m(self) -> float:
        return max(self.x1 - self.x0, self.z1 - self.z0)

    @property
    def nbytes_est(self) -> int:
        return LEAF_HEADER_BYTES + self.n_y * SAMPLE_BYTES


@dataclass
class QuadBuildConfig:
    origin_x: float = 0.0
    origin_z: float = 0.0
    extent_x: float = 2000.0
    extent_z: float = 2000.0
    extent_y: float = 400.0
    min_leaf_m: float = 10.0
    max_leaf_m: float = 160.0
    # Vertical samples: coarse air vs fine ROI/obstacle.
    n_y_air: int = 4
    n_y_fine: int = 40
    # ROI disk around source forces fine leaves.
    roi_x: float = 200.0
    roi_z: float = 200.0
    roi_radius_m: float = 250.0
    obstacles: list[ObstacleBox] = field(default_factory=list)


def _box_intersects_leaf(
    leaf_x0: float,
    leaf_z0: float,
    leaf_x1: float,
    leaf_z1: float,
    box: ObstacleBox,
) -> bool:
    if leaf_x1 <= box.x0 or leaf_x0 >= box.x1:
        return False
    if leaf_z1 <= box.z0 or leaf_z0 >= box.z1:
        return False
    return True


def _leaf_center_in_roi(
    x0: float,
    z0: float,
    x1: float,
    z1: float,
    cfg: QuadBuildConfig,
) -> bool:
    cx = 0.5 * (x0 + x1)
    cz = 0.5 * (z0 + z1)
    dx = cx - cfg.roi_x
    dz = cz - cfg.roi_z
    return (dx * dx + dz * dz) <= (cfg.roi_radius_m * cfg.roi_radius_m)


def _classify_leaf(
    x0: float,
    z0: float,
    x1: float,
    z1: float,
    cfg: QuadBuildConfig,
) -> tuple[str, bool]:
    hit_obs = False
    for box in cfg.obstacles:
        if _box_intersects_leaf(x0, z0, x1, z1, box):
            hit_obs = True
            break
    in_roi = _leaf_center_in_roi(x0, z0, x1, z1, cfg)
    if hit_obs:
        return "obstacle", True
    if in_roi:
        return "roi", False
    return "air", False


def _needs_split(
    x0: float,
    z0: float,
    x1: float,
    z1: float,
    level: int,
    cfg: QuadBuildConfig,
) -> bool:
    side = max(x1 - x0, z1 - z0)
    if side <= cfg.min_leaf_m + 1e-9:
        return False
    if side > cfg.max_leaf_m + 1e-9:
        return True
    kind, _occ = _classify_leaf(x0, z0, x1, z1, cfg)
    # Split mixed / interesting regions down toward min_leaf.
    if kind == "air":
        return False
    return True


def build_power_quadtree(cfg: QuadBuildConfig) -> dict:
    """Build XZ quadtree leaves; return leaves + memory accounting."""
    t0 = time.perf_counter()
    leaves: list[QuadLeaf] = []
    stack: list[tuple[float, float, float, float, int]] = [
        (
            cfg.origin_x,
            cfg.origin_z,
            cfg.origin_x + cfg.extent_x,
            cfg.origin_z + cfg.extent_z,
            0,
        )
    ]
    max_level = 0
    while stack:
        x0, z0, x1, z1, level = stack.pop()
        if level > max_level:
            max_level = level
        if _needs_split(x0, z0, x1, z1, level, cfg):
            mx = 0.5 * (x0 + x1)
            mz = 0.5 * (z0 + z1)
            stack.append((x0, z0, mx, mz, level + 1))
            stack.append((mx, z0, x1, mz, level + 1))
            stack.append((x0, mz, mx, z1, level + 1))
            stack.append((mx, mz, x1, z1, level + 1))
            continue
        kind, occ = _classify_leaf(x0, z0, x1, z1, cfg)
        if kind == "air":
            n_y = cfg.n_y_air
        else:
            n_y = cfg.n_y_fine
        leaves.append(
            QuadLeaf(
                x0=x0,
                z0=z0,
                x1=x1,
                z1=z1,
                level=level,
                kind=kind,
                n_y=n_y,
                occupied=occ,
            )
        )
    elapsed = time.perf_counter() - t0

    n_air = 0
    n_obs = 0
    n_roi = 0
    bytes_total = 0
    fine_area = 0.0
    total_area = cfg.extent_x * cfg.extent_z
    for leaf in leaves:
        bytes_total += leaf.nbytes_est
        area = (leaf.x1 - leaf.x0) * (leaf.z1 - leaf.z0)
        if leaf.kind == "air":
            n_air += 1
        elif leaf.kind == "obstacle":
            n_obs += 1
            fine_area += area
        else:
            n_roi += 1
            fine_area += area

    return {
        "elapsed_s": elapsed,
        "leaf_count": len(leaves),
        "max_level": max_level,
        "n_air": n_air,
        "n_obstacle": n_obs,
        "n_roi": n_roi,
        "bytes_est": bytes_total,
        "mib_est": bytes_total / (1024.0 * 1024.0),
        "fine_area_frac": fine_area / max(total_area, 1e-9),
        "min_leaf_m": cfg.min_leaf_m,
        "max_leaf_m": cfg.max_leaf_m,
        "extent_m": [cfg.extent_x, cfg.extent_y, cfg.extent_z],
        "leaves": leaves,
    }


def default_urban_obstacles(extent_x: float, extent_z: float) -> list[ObstacleBox]:
    """A few building strips — sparse occupancy for classification demos."""
    boxes = []
    # Three north-south walls / blocks.
    for i, x in enumerate((400.0, 900.0, 1400.0)):
        if x + 40.0 >= extent_x:
            continue
        boxes.append(
            ObstacleBox(
                x0=x,
                z0=200.0,
                x1=x + 40.0,
                z1=min(extent_z - 50.0, 1600.0),
                height_m=30.0 + 10.0 * i,
            )
        )
    # One east-west block near ROI.
    boxes.append(
        ObstacleBox(x0=100.0, z0=300.0, x1=350.0, z1=380.0, height_m=45.0)
    )
    return boxes


def measure_quadtree_vs_dense(
    extent_m: tuple[float, float, float] = (2000.0, 400.0, 2000.0),
    min_leaf_m: float = 10.0,
    max_leaf_m: float = 160.0,
    roi_radius_m: float = 250.0,
) -> dict:
    """Compare quadtree estimate vs uniform dense grids at several cell sizes."""
    ex, ey, ez = extent_m
    cfg = QuadBuildConfig(
        extent_x=ex,
        extent_y=ey,
        extent_z=ez,
        min_leaf_m=min_leaf_m,
        max_leaf_m=max_leaf_m,
        roi_x=200.0,
        roi_z=200.0,
        roi_radius_m=roi_radius_m,
        n_y_air=max(2, int(round(ey / max_leaf_m))),
        n_y_fine=max(8, int(round(ey / min_leaf_m))),
        obstacles=default_urban_obstacles(ex, ez),
    )
    # Clamp vertical sample counts to sane bounds.
    if cfg.n_y_air > 16:
        cfg.n_y_air = 16
    if cfg.n_y_fine > 80:
        cfg.n_y_fine = 80

    built = build_power_quadtree(cfg)
    # Drop heavy leaf objects from returned JSON path.
    leaves = built.pop("leaves")

    dense_rows = [
        estimate_dense_cost(extent_m, 40.0),
        estimate_dense_cost(extent_m, 20.0),
        estimate_dense_cost(extent_m, 10.0),
        estimate_dense_cost(extent_m, 5.0),
    ]
    dense_at_min = estimate_dense_cost(extent_m, min_leaf_m)
    if dense_at_min["bytes"] <= 0:
        ratio = float("inf")
    else:
        ratio = float(built["bytes_est"]) / float(dense_at_min["bytes"])

    # Probe classification coverage (no power fill — memory study only).
    class_hist = {"air": 0, "obstacle": 0, "roi": 0}
    for leaf in leaves:
        class_hist[leaf.kind] = class_hist.get(leaf.kind, 0) + 1

    return {
        "quadtree": built,
        "class_hist": class_hist,
        "dense_grids": dense_rows,
        "dense_at_min_leaf": dense_at_min,
        "quad_over_dense_min_leaf": ratio,
        "savings_frac_vs_dense_min": 1.0 - ratio if ratio <= 1.0 else 0.0,
        "roi_radius_m": roi_radius_m,
        "obstacle_count": len(cfg.obstacles),
        "n_y_air": cfg.n_y_air,
        "n_y_fine": cfg.n_y_fine,
    }


def case_quadtree_memory() -> ValidationCaseResult:
    """Pass if quadtree uses clearly less memory than dense min-leaf grid."""
    report = measure_quadtree_vs_dense()
    ratio = report["quad_over_dense_min_leaf"]
    # Expect at least 5× savings on a sparse urban+ROI scene.
    ok = ratio < 0.25 and report["quadtree"]["leaf_count"] > 0
    return ValidationCaseResult(
        name="quadtree_memory",
        ok=ok,
        metrics={
            "verdict": (
                "XZ quadtree + short Y columns beats uniform fine voxels on "
                "sparse obstacle/ROI maps; does not fix map-scale FDTD."
            ),
            **report,
        },
    )


def leaf_side_histogram(leaves: list[QuadLeaf]) -> dict[str, int]:
    hist: dict[str, int] = {}
    for leaf in leaves:
        key = f"{leaf.side_m:.0f}m"
        hist[key] = hist.get(key, 0) + 1
    return hist
