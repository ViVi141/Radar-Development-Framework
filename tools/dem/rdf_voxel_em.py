#!/usr/bin/env python3
"""Offline 3D voxel electromagnetic power-field validation helpers.

This is an engineering power-density grid, not a Maxwell / FDTD solver.
Purpose: measure whether a sparse ROI voxel field can reproduce free-space
spreading and hard occlusion well enough for RDF calibration experiments.

Contracts:
  - Occupancy voxels: 0 = air, >0 = solid with linear attenuation (1/m).
  - Power field stores isotropic power density (W/m^2) after ray deposit.
  - Analytic free-space reference uses Friis spreading Pt / (4 * pi * R^2).
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass, field

import numpy as np

C_LIGHT_M_S = 299792458.0


def lin_to_db(linear: float) -> float:
    if linear <= 1e-30:
        return -300.0
    return 10.0 * math.log10(linear)


def db_to_lin(db: float) -> float:
    return 10.0 ** (db / 10.0)


@dataclass
class VoxelGrid:
    """Dense axis-aligned voxel volume in world meters."""

    origin_m: tuple[float, float, float]
    cell_m: float
    shape: tuple[int, int, int]  # (nx, ny, nz)
    occupancy: np.ndarray = field(init=False)
    atten_per_m: np.ndarray = field(init=False)
    power_w_m2: np.ndarray = field(init=False)

    def __post_init__(self) -> None:
        if self.cell_m <= 0.0:
            raise ValueError("cell_m must be positive")
        nx, ny, nz = self.shape
        if nx < 2 or ny < 2 or nz < 2:
            raise ValueError("shape must be at least 2 in each axis")
        self.occupancy = np.zeros(self.shape, dtype=np.uint8)
        self.atten_per_m = np.zeros(self.shape, dtype=np.float32)
        self.power_w_m2 = np.zeros(self.shape, dtype=np.float64)

    @property
    def nbytes(self) -> int:
        return (
            int(self.occupancy.nbytes)
            + int(self.atten_per_m.nbytes)
            + int(self.power_w_m2.nbytes)
        )

    def clear_power(self) -> None:
        self.power_w_m2.fill(0.0)

    def world_to_index(self, x: float, y: float, z: float) -> tuple[int, int, int]:
        ox, oy, oz = self.origin_m
        ix = int(math.floor((x - ox) / self.cell_m))
        iy = int(math.floor((y - oy) / self.cell_m))
        iz = int(math.floor((z - oz) / self.cell_m))
        return ix, iy, iz

    def index_to_center(self, ix: int, iy: int, iz: int) -> tuple[float, float, float]:
        ox, oy, oz = self.origin_m
        half = 0.5 * self.cell_m
        return (
            ox + (ix + 0.5) * self.cell_m,
            oy + (iy + 0.5) * self.cell_m,
            oz + (iz + 0.5) * self.cell_m,
        )

    def in_bounds(self, ix: int, iy: int, iz: int) -> bool:
        nx, ny, nz = self.shape
        if ix < 0 or iy < 0 or iz < 0:
            return False
        if ix >= nx or iy >= ny or iz >= nz:
            return False
        return True

    def fill_box_obstacle(
        self,
        x0: float,
        y0: float,
        z0: float,
        x1: float,
        y1: float,
        z1: float,
        atten_per_m: float,
    ) -> int:
        """Mark a world AABB as solid. Returns number of voxels marked."""
        ix0, iy0, iz0 = self.world_to_index(x0, y0, z0)
        ix1, iy1, iz1 = self.world_to_index(x1, y1, z1)
        nx, ny, nz = self.shape
        ax0 = max(0, min(ix0, ix1))
        ay0 = max(0, min(iy0, iy1))
        az0 = max(0, min(iz0, iz1))
        ax1 = min(nx - 1, max(ix0, ix1))
        ay1 = min(ny - 1, max(iy0, iy1))
        az1 = min(nz - 1, max(iz0, iz1))
        if ax1 < ax0 or ay1 < ay0 or az1 < az0:
            return 0
        self.occupancy[ax0 : ax1 + 1, ay0 : ay1 + 1, az0 : az1 + 1] = 1
        self.atten_per_m[ax0 : ax1 + 1, ay0 : ay1 + 1, az0 : az1 + 1] = float(
            atten_per_m
        )
        return int((ax1 - ax0 + 1) * (ay1 - ay0 + 1) * (az1 - az0 + 1))


@dataclass
class EmSource:
    """Isotropic point source (engineering power, not antenna pattern)."""

    x_m: float
    y_m: float
    z_m: float
    power_w: float


def free_space_power_density_w_m2(power_w: float, range_m: float) -> float:
    if range_m < 1e-9:
        return power_w
    return power_w / (4.0 * math.pi * range_m * range_m)


def sample_unit_directions(n_az: int, n_el: int) -> np.ndarray:
    """Return (N, 3) unit vectors covering az [0, 2pi) and el [-pi/2, pi/2]."""
    if n_az < 4 or n_el < 3:
        raise ValueError("need n_az >= 4 and n_el >= 3")
    az = np.linspace(0.0, 2.0 * math.pi, n_az, endpoint=False)
    el = np.linspace(-0.5 * math.pi, 0.5 * math.pi, n_el)
    dirs = []
    for elev in el:
        cos_el = math.cos(float(elev))
        sin_el = math.sin(float(elev))
        for azimuth in az:
            dx = cos_el * math.cos(float(azimuth))
            dy = sin_el
            dz = cos_el * math.sin(float(azimuth))
            dirs.append((dx, dy, dz))
    return np.asarray(dirs, dtype=np.float64)


def _segment_optical_depth(
    grid: VoxelGrid,
    x0: float,
    y0: float,
    z0: float,
    x1: float,
    y1: float,
    z1: float,
    hard_atten: float = 5.0,
) -> tuple[float, bool]:
    """Integrate atten along segment. Returns (optical_depth, blocked)."""
    dx = x1 - x0
    dy = y1 - y0
    dz = z1 - z0
    dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist < 1e-12:
        return 0.0, False
    step = 0.5 * grid.cell_m
    n_steps = max(1, int(math.ceil(dist / step)))
    step = dist / float(n_steps)
    inv = 1.0 / dist
    ux, uy, uz = dx * inv, dy * inv, dz * inv
    optical = 0.0
    for i in range(n_steps):
        t = (float(i) + 0.5) * step
        x = x0 + ux * t
        y = y0 + uy * t
        z = z0 + uz * t
        ix, iy, iz = grid.world_to_index(x, y, z)
        if not grid.in_bounds(ix, iy, iz):
            return optical, True
        atten = float(grid.atten_per_m[ix, iy, iz])
        if atten > 0.0:
            optical += atten * step
        if grid.occupancy[ix, iy, iz] != 0 and atten >= hard_atten:
            return optical, True
    return optical, False


def fill_power_field_los(grid: VoxelGrid, source: EmSource) -> dict:
    """Fill every voxel from source via LOS Friis + material attenuation.

    Deterministic and better for validation than sparse angular ray fans.
    Complexity ~ O(N_voxels * path_samples).
    """
    grid.clear_power()
    nx, ny, nz = grid.shape
    t0 = time.perf_counter()
    filled = 0
    blocked = 0
    for ix in range(nx):
        for iy in range(ny):
            for iz in range(nz):
                x, y, z = grid.index_to_center(ix, iy, iz)
                dx = x - source.x_m
                dy = y - source.y_m
                dz = z - source.z_m
                range_m = math.sqrt(dx * dx + dy * dy + dz * dz)
                if range_m < 0.5 * grid.cell_m:
                    grid.power_w_m2[ix, iy, iz] = source.power_w
                    filled += 1
                    continue
                optical, is_blocked = _segment_optical_depth(
                    grid, source.x_m, source.y_m, source.z_m, x, y, z
                )
                if is_blocked:
                    # Residual leakage through finite atten; hard walls ~0.
                    path_factor = math.exp(-2.0 * optical)
                    if path_factor < 1e-6:
                        grid.power_w_m2[ix, iy, iz] = 0.0
                        blocked += 1
                        continue
                else:
                    path_factor = math.exp(-2.0 * optical)
                density = free_space_power_density_w_m2(source.power_w, range_m)
                grid.power_w_m2[ix, iy, iz] = density * path_factor
                filled += 1
    elapsed = time.perf_counter() - t0
    return {
        "filled": filled,
        "blocked": blocked,
        "elapsed_s": elapsed,
        "bytes": grid.nbytes,
        "voxels": nx * ny * nz,
    }


def deposit_power_ray_march(
    grid: VoxelGrid,
    source: EmSource,
    n_az: int = 72,
    n_el: int = 37,
    max_range_m: float | None = None,
) -> dict:
    """Optional sparse angular deposit (visualization / backend-style sampling).

    Prefer fill_power_field_los for quantitative checks — discrete rays can miss
    voxels at coarse angular resolution.
    """
    grid.clear_power()
    dirs = sample_unit_directions(n_az, n_el)
    n_rays = int(dirs.shape[0])
    step = 0.5 * grid.cell_m
    if max_range_m is None:
        nx, ny, nz = grid.shape
        extent = grid.cell_m * math.sqrt(float(nx * nx + ny * ny + nz * nz))
        max_range = extent
    else:
        max_range = float(max_range_m)

    t0 = time.perf_counter()
    steps_total = 0
    for dvec in dirs:
        dx, dy, dz = float(dvec[0]), float(dvec[1]), float(dvec[2])
        dist = step
        optical = 0.0
        while dist <= max_range:
            x = source.x_m + dx * dist
            y = source.y_m + dy * dist
            z = source.z_m + dz * dist
            ix, iy, iz = grid.world_to_index(x, y, z)
            if not grid.in_bounds(ix, iy, iz):
                break
            atten = float(grid.atten_per_m[ix, iy, iz])
            if atten > 0.0:
                optical += atten * step
            path_factor = math.exp(-2.0 * optical)
            sphere_area = 4.0 * math.pi * dist * dist
            density = (source.power_w * path_factor) / max(sphere_area, 1e-12)
            if density > grid.power_w_m2[ix, iy, iz]:
                grid.power_w_m2[ix, iy, iz] = density
            if grid.occupancy[ix, iy, iz] != 0 and atten > 5.0:
                break
            dist += step
            steps_total += 1
    elapsed = time.perf_counter() - t0
    return {
        "n_rays": n_rays,
        "steps_total": steps_total,
        "elapsed_s": elapsed,
        "max_range_m": max_range,
        "bytes": grid.nbytes,
    }


def probe_power(grid: VoxelGrid, x: float, y: float, z: float) -> float:
    ix, iy, iz = grid.world_to_index(x, y, z)
    if not grid.in_bounds(ix, iy, iz):
        return 0.0
    return float(grid.power_w_m2[ix, iy, iz])


def relative_error(approx: float, truth: float) -> float:
    denom = max(abs(truth), 1e-30)
    return abs(approx - truth) / denom


@dataclass
class ValidationCaseResult:
    name: str
    ok: bool
    metrics: dict


def case_free_space_agreement(
    cell_m: float = 5.0,
    power_w: float = 1000.0,
    probe_ranges_m: tuple[float, ...] = (20.0, 40.0, 60.0),
    max_rel_err: float = 0.08,
) -> ValidationCaseResult:
    """Free-space probes should track Friis density (voxel-center quantization)."""
    # Volume large enough for 60 m probes with margin.
    half = 80.0
    n = int(math.ceil((2.0 * half) / cell_m))
    grid = VoxelGrid(origin_m=(-half, -half, -half), cell_m=cell_m, shape=(n, n, n))
    source = EmSource(0.0, 0.0, 0.0, power_w)
    deposit = fill_power_field_los(grid, source)
    errors = []
    rows = []
    for r in probe_ranges_m:
        ix, iy, iz = grid.world_to_index(r, 0.0, 0.0)
        cx, cy, cz = grid.index_to_center(ix, iy, iz)
        approx = float(grid.power_w_m2[ix, iy, iz])
        # Truth at the same voxel center the grid stores.
        center_range = math.sqrt(cx * cx + cy * cy + cz * cz)
        truth = free_space_power_density_w_m2(power_w, center_range)
        err = relative_error(approx, truth)
        errors.append(err)
        rows.append(
            {
                "range_m": r,
                "center_range_m": center_range,
                "voxel_w_m2": approx,
                "fspl_w_m2": truth,
                "rel_err": err,
                "voxel_db": lin_to_db(approx),
                "fspl_db": lin_to_db(truth),
            }
        )
    worst = max(errors) if errors else 1.0
    ok = worst <= max_rel_err
    return ValidationCaseResult(
        name="free_space_agreement",
        ok=ok,
        metrics={
            "worst_rel_err": worst,
            "max_rel_err": max_rel_err,
            "probes": rows,
            "deposit": deposit,
            "grid_shape": list(grid.shape),
            "cell_m": cell_m,
            "bytes": grid.nbytes,
        },
    )


def case_occlusion_shadow(
    cell_m: float = 4.0,
    power_w: float = 1000.0,
    shadow_ratio_max: float = 0.05,
) -> ValidationCaseResult:
    """A thick wall must create a deep shadow relative to the clear side."""
    half = 60.0
    n = int(math.ceil((2.0 * half) / cell_m))
    grid = VoxelGrid(origin_m=(-half, -half, -half), cell_m=cell_m, shape=(n, n, n))
    # Wall slab at x in [15, 20], spanning most of y/z.
    marked = grid.fill_box_obstacle(15.0, -40.0, -40.0, 20.0, 40.0, 40.0, atten_per_m=20.0)
    source = EmSource(-30.0, 0.0, 0.0, power_w)
    deposit = fill_power_field_los(grid, source)
    lit = probe_power(grid, 0.0, 0.0, 0.0)
    shadow = probe_power(grid, 35.0, 0.0, 0.0)
    if lit <= 1e-30:
        ratio = 1.0
    else:
        ratio = shadow / lit
    ok = ratio <= shadow_ratio_max and marked > 0
    return ValidationCaseResult(
        name="occlusion_shadow",
        ok=ok,
        metrics={
            "marked_voxels": marked,
            "lit_w_m2": lit,
            "shadow_w_m2": shadow,
            "shadow_over_lit": ratio,
            "shadow_ratio_max": shadow_ratio_max,
            "deposit": deposit,
            "grid_shape": list(grid.shape),
            "cell_m": cell_m,
            "bytes": grid.nbytes,
        },
    )


def estimate_dense_cost(
    extent_m: tuple[float, float, float],
    cell_m: float,
    bytes_per_voxel: int = 13,
) -> dict:
    """Memory estimate for a dense power+occupancy volume."""
    ex, ey, ez = extent_m
    nx = max(1, int(math.ceil(ex / cell_m)))
    ny = max(1, int(math.ceil(ey / cell_m)))
    nz = max(1, int(math.ceil(ez / cell_m)))
    n = nx * ny * nz
    return {
        "cell_m": cell_m,
        "shape": [nx, ny, nz],
        "voxels": n,
        "bytes": n * bytes_per_voxel,
        "mib": (n * bytes_per_voxel) / (1024.0 * 1024.0),
    }


def estimate_fdtd_cost(
    extent_m: tuple[float, float, float],
    frequency_hz: float,
    cells_per_wavelength: float = 10.0,
    sim_time_s: float = 1.0e-6,
) -> dict:
    """Order-of-magnitude Yee FDTD cost for a boxed volume."""
    wavelength = C_LIGHT_M_S / frequency_hz
    cell = wavelength / cells_per_wavelength
    ex, ey, ez = extent_m
    nx = max(1, int(math.ceil(ex / cell)))
    ny = max(1, int(math.ceil(ey / cell)))
    nz = max(1, int(math.ceil(ez / cell)))
    n = nx * ny * nz
    # CFL ~ cell / (c * sqrt(3))
    dt = cell / (C_LIGHT_M_S * math.sqrt(3.0))
    steps = max(1, int(math.ceil(sim_time_s / dt)))
    # 6 field comps * 8 bytes; ignore materials.
    bytes_fields = n * 6 * 8
    return {
        "frequency_hz": frequency_hz,
        "wavelength_m": wavelength,
        "cell_m": cell,
        "shape": [nx, ny, nz],
        "voxels": n,
        "dt_s": dt,
        "steps_for_sim_time": steps,
        "bytes_fields": bytes_fields,
        "gib_fields": bytes_fields / (1024.0 ** 3),
        "cell_updates": float(n) * float(steps),
        "sim_time_s": sim_time_s,
    }


def case_scale_report() -> ValidationCaseResult:
    """Compare power-voxel ROI cost vs naive FDTD on a 2 km box at X-band."""
    extent = (2000.0, 400.0, 2000.0)
    power_rows = [
        estimate_dense_cost(extent, 20.0),
        estimate_dense_cost(extent, 10.0),
        estimate_dense_cost(extent, 5.0),
    ]
    fdtd = estimate_fdtd_cost(extent, frequency_hz=9.5e9, sim_time_s=1e-6)
    # Pass criterion: a 10 m power grid stays under 512 MiB while FDTD is huge.
    power_10 = power_rows[1]
    ok = power_10["mib"] < 512.0 and fdtd["gib_fields"] > 1.0
    return ValidationCaseResult(
        name="scale_report",
        ok=ok,
        metrics={
            "extent_m": list(extent),
            "power_grids": power_rows,
            "fdtd_xband_1us": fdtd,
            "verdict": (
                "power voxels feasible for ROI/offline; "
                "dense X-band FDTD not feasible at map scale"
            ),
        },
    )


def run_validation_suite() -> dict:
    """Run all validation cases and return a JSON-serializable report."""
    cases = [
        case_free_space_agreement(),
        case_occlusion_shadow(),
        case_scale_report(),
    ]
    results = []
    all_ok = True
    for case in cases:
        if not case.ok:
            all_ok = False
        results.append(
            {
                "name": case.name,
                "ok": case.ok,
                "metrics": case.metrics,
            }
        )
    return {
        "all_ok": all_ok,
        "model": "engineering_power_voxel_los_friis",
        "not_modeled": ["maxwell_fdtd", "polarization", "diffraction_fill"],
        "results": results,
    }


def horizontal_slice_db(
    grid: VoxelGrid,
    y_m: float,
) -> tuple[np.ndarray, tuple[float, float, float, float]]:
    """Return (nz, nx) dB slice at constant Y and extent (xmin,xmax,zmin,zmax)."""
    _ix, iy, _iz = grid.world_to_index(grid.origin_m[0], y_m, grid.origin_m[2])
    ny = grid.shape[1]
    if iy < 0:
        iy = 0
    if iy >= ny:
        iy = ny - 1
    plane = grid.power_w_m2[:, iy, :]
    db = 10.0 * np.log10(np.maximum(plane, 1e-30))
    # Transpose to (z, x) for imshow with origin lower.
    img = np.transpose(db)
    ox, _oy, oz = grid.origin_m
    nx, _ny, nz = grid.shape
    extent = (
        ox,
        ox + nx * grid.cell_m,
        oz,
        oz + nz * grid.cell_m,
    )
    return img, extent
