#!/usr/bin/env python3
"""Tiny 3D Yee FDTD toy for offline voxel-EM feasibility checks.

Runs only at toy scale (tens of cells per axis). Intended to demonstrate:
  - wavefront travel near speed of light
  - PEC occlusion / shadowing
  - wall-clock + memory cost of even a tiny Maxwell grid

Not a production EM solver: first-order ABC is approximate; materials are
vacuum + PEC only; no dispersive media / far-field transforms.
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass

import numpy as np

from rdf_voxel_em import C_LIGHT_M_S, ValidationCaseResult

EPS0 = 8.854187817e-12
MU0 = 4.0e-7 * math.pi


@dataclass
class FdtdConfig:
    """Grid and source parameters for the toy solver."""

    nx: int = 48
    ny: int = 48
    nz: int = 48
    dx_m: float = 0.05
    courant: float = 0.5
    n_steps: int = 120
    # Soft Gaussian source centered on Ez.
    src_i: int = 12
    src_j: int = 24
    src_k: int = 24
    pulse_width_steps: float = 8.0
    pulse_delay_steps: float = 24.0
    # Optional PEC box [i0:i1, j0:j1, k0:k1) in cell indices.
    pec_box: tuple[int, int, int, int, int, int] | None = None


class FdtdGrid3D:
    """Leapfrog Yee update on equal-shaped field arrays (toy staggering)."""

    def __init__(self, cfg: FdtdConfig) -> None:
        if cfg.nx < 8 or cfg.ny < 8 or cfg.nz < 8:
            raise ValueError("FDTD toy needs at least 8 cells per axis")
        if cfg.dx_m <= 0.0:
            raise ValueError("dx_m must be positive")
        if cfg.courant <= 0.0 or cfg.courant >= 1.0 / math.sqrt(3.0):
            raise ValueError("courant must be in (0, 1/sqrt(3))")
        self.cfg = cfg
        self.dx = float(cfg.dx_m)
        self.dt = cfg.courant * self.dx / C_LIGHT_M_S
        shape = (cfg.nx, cfg.ny, cfg.nz)
        self.ex = np.zeros(shape, dtype=np.float64)
        self.ey = np.zeros(shape, dtype=np.float64)
        self.ez = np.zeros(shape, dtype=np.float64)
        self.hx = np.zeros(shape, dtype=np.float64)
        self.hy = np.zeros(shape, dtype=np.float64)
        self.hz = np.zeros(shape, dtype=np.float64)
        self.pec = np.zeros(shape, dtype=bool)
        if cfg.pec_box is not None:
            i0, i1, j0, j1, k0, k1 = cfg.pec_box
            self.pec[i0:i1, j0:j1, k0:k1] = True
        # Update coefficients.
        self._ce = self.dt / (EPS0 * self.dx)
        self._ch = self.dt / (MU0 * self.dx)

    @property
    def nbytes(self) -> int:
        return (
            int(self.ex.nbytes)
            + int(self.ey.nbytes)
            + int(self.ez.nbytes)
            + int(self.hx.nbytes)
            + int(self.hy.nbytes)
            + int(self.hz.nbytes)
            + int(self.pec.nbytes)
        )

    def _apply_pec(self) -> None:
        self.ex[self.pec] = 0.0
        self.ey[self.pec] = 0.0
        self.ez[self.pec] = 0.0

    def _abc_faces(self) -> None:
        """Crude first-order absorbing faces (copy from interior neighbor)."""
        # -x / +x
        self.ex[0, :, :] = self.ex[1, :, :]
        self.ey[0, :, :] = self.ey[1, :, :]
        self.ez[0, :, :] = self.ez[1, :, :]
        self.ex[-1, :, :] = self.ex[-2, :, :]
        self.ey[-1, :, :] = self.ey[-2, :, :]
        self.ez[-1, :, :] = self.ez[-2, :, :]
        # -y / +y
        self.ex[:, 0, :] = self.ex[:, 1, :]
        self.ey[:, 0, :] = self.ey[:, 1, :]
        self.ez[:, 0, :] = self.ez[:, 1, :]
        self.ex[:, -1, :] = self.ex[:, -2, :]
        self.ey[:, -1, :] = self.ey[:, -2, :]
        self.ez[:, -1, :] = self.ez[:, -2, :]
        # -z / +z
        self.ex[:, :, 0] = self.ex[:, :, 1]
        self.ey[:, :, 0] = self.ey[:, :, 1]
        self.ez[:, :, 0] = self.ez[:, :, 1]
        self.ex[:, :, -1] = self.ex[:, :, -2]
        self.ey[:, :, -1] = self.ey[:, :, -2]
        self.ez[:, :, -1] = self.ez[:, :, -2]

    def step(self, step_idx: int) -> None:
        """Advance one leapfrog step; inject soft Gaussian into Ez."""
        ce = self._ce
        ch = self._ch
        ex, ey, ez = self.ex, self.ey, self.ez
        hx, hy, hz = self.hx, self.hy, self.hz

        # H from curl E (interior).
        hx[:, :-1, :-1] += ch * (
            (ey[:, :-1, 1:] - ey[:, :-1, :-1]) - (ez[:, 1:, :-1] - ez[:, :-1, :-1])
        )
        hy[:-1, :, :-1] += ch * (
            (ez[1:, :, :-1] - ez[:-1, :, :-1]) - (ex[:-1, :, 1:] - ex[:-1, :, :-1])
        )
        hz[:-1, :-1, :] += ch * (
            (ex[:-1, 1:, :] - ex[:-1, :-1, :]) - (ey[1:, :-1, :] - ey[:-1, :-1, :])
        )

        # E from curl H (interior).
        ex[1:, 1:, 1:] += ce * (
            (hz[1:, 1:, 1:] - hz[1:, :-1, 1:]) - (hy[1:, 1:, 1:] - hy[1:, 1:, :-1])
        )
        ey[1:, 1:, 1:] += ce * (
            (hx[1:, 1:, 1:] - hx[1:, 1:, :-1]) - (hz[1:, 1:, 1:] - hz[:-1, 1:, 1:])
        )
        ez[1:, 1:, 1:] += ce * (
            (hy[1:, 1:, 1:] - hy[:-1, 1:, 1:]) - (hx[1:, 1:, 1:] - hx[1:, :-1, 1:])
        )

        # Soft source.
        cfg = self.cfg
        t = float(step_idx)
        arg = (t - cfg.pulse_delay_steps) / cfg.pulse_width_steps
        pulse = math.exp(-0.5 * arg * arg)
        si, sj, sk = cfg.src_i, cfg.src_j, cfg.src_k
        if not self.pec[si, sj, sk]:
            self.ez[si, sj, sk] += pulse

        self._apply_pec()
        self._abc_faces()
        self._apply_pec()

    def run(self) -> dict:
        """Run n_steps; return timing / energy / probe time series."""
        cfg = self.cfg
        probe_i = min(cfg.nx - 4, cfg.src_i + 16)
        probe_j = cfg.src_j
        probe_k = cfg.src_k
        shadow_i = min(cfg.nx - 3, 36)
        if cfg.pec_box is not None:
            shadow_i = min(cfg.nx - 3, cfg.pec_box[1] + 4)
        lit_i = max(2, cfg.src_i + 6)

        ez_probe = np.zeros(cfg.n_steps, dtype=np.float64)
        ez_lit = np.zeros(cfg.n_steps, dtype=np.float64)
        ez_shadow = np.zeros(cfg.n_steps, dtype=np.float64)
        energy = np.zeros(cfg.n_steps, dtype=np.float64)

        t0 = time.perf_counter()
        for n in range(cfg.n_steps):
            self.step(n)
            ez_probe[n] = self.ez[probe_i, probe_j, probe_k]
            ez_lit[n] = self.ez[lit_i, probe_j, probe_k]
            ez_shadow[n] = self.ez[shadow_i, probe_j, probe_k]
            energy[n] = float(
                np.sum(self.ex * self.ex + self.ey * self.ey + self.ez * self.ez)
            )
        elapsed = time.perf_counter() - t0

        # Arrival = first time |Ez| exceeds 5% of peak at probe.
        peak = float(np.max(np.abs(ez_probe)))
        arrival_step = -1
        if peak > 1e-12:
            thresh = 0.05 * peak
            hits = np.where(np.abs(ez_probe) >= thresh)[0]
            if hits.size > 0:
                arrival_step = int(hits[0])

        dist_m = (probe_i - cfg.src_i) * self.dx
        t_expected_s = dist_m / C_LIGHT_M_S
        if arrival_step >= 0:
            t_arrival_s = arrival_step * self.dt
        else:
            t_arrival_s = float("nan")

        return {
            "elapsed_s": elapsed,
            "bytes": self.nbytes,
            "dx_m": self.dx,
            "dt_s": self.dt,
            "n_steps": cfg.n_steps,
            "shape": [cfg.nx, cfg.ny, cfg.nz],
            "probe_ijk": [probe_i, probe_j, probe_k],
            "lit_ijk": [lit_i, probe_j, probe_k],
            "shadow_ijk": [shadow_i, probe_j, probe_k],
            "dist_src_to_probe_m": dist_m,
            "arrival_step": arrival_step,
            "t_arrival_s": t_arrival_s,
            "t_expected_s": t_expected_s,
            "probe_peak_abs": peak,
            "lit_peak_abs": float(np.max(np.abs(ez_lit))),
            "shadow_peak_abs": float(np.max(np.abs(ez_shadow))),
            "energy_peak": float(np.max(energy)),
            "ez_mid_slice": self.ez[:, cfg.src_j, :].copy(),
        }


def case_fdtd_propagation(
    rel_time_err_max: float = 0.35,
) -> ValidationCaseResult:
    """Wavefront arrival at a probe should be near R/c."""
    cfg = FdtdConfig(
        nx=48,
        ny=40,
        nz=40,
        dx_m=0.05,
        n_steps=140,
        src_i=10,
        src_j=20,
        src_k=20,
        pec_box=None,
    )
    grid = FdtdGrid3D(cfg)
    info = grid.run()
    # Drop heavy array from JSON metrics.
    slice_peak = float(np.max(np.abs(info["ez_mid_slice"])))
    del info["ez_mid_slice"]

    t_arr = info["t_arrival_s"]
    t_exp = info["t_expected_s"]
    if math.isnan(t_arr) or t_exp <= 0.0:
        rel_err = 1.0
        ok = False
    else:
        rel_err = abs(t_arr - t_exp) / t_exp
        ok = rel_err <= rel_time_err_max and info["probe_peak_abs"] > 0.0

    return ValidationCaseResult(
        name="fdtd_propagation",
        ok=ok,
        metrics={
            "rel_arrival_err": rel_err,
            "rel_time_err_max": rel_time_err_max,
            "slice_peak_abs": slice_peak,
            "run": info,
        },
    )


def case_fdtd_pec_shadow(
    shadow_ratio_max: float = 0.25,
) -> ValidationCaseResult:
    """PEC wall must suppress the field behind it vs the lit side."""
    # Wall across y/z at x cells [24, 26).
    cfg = FdtdConfig(
        nx=48,
        ny=40,
        nz=40,
        dx_m=0.05,
        n_steps=160,
        src_i=10,
        src_j=20,
        src_k=20,
        pec_box=(24, 26, 2, 38, 2, 38),
    )
    grid = FdtdGrid3D(cfg)
    info = grid.run()
    slice_abs = np.abs(info["ez_mid_slice"])
    del info["ez_mid_slice"]

    lit = info["lit_peak_abs"]
    shadow = info["shadow_peak_abs"]
    if lit <= 1e-15:
        ratio = 1.0
    else:
        ratio = shadow / lit
    ok = ratio <= shadow_ratio_max and lit > 0.0

    return ValidationCaseResult(
        name="fdtd_pec_shadow",
        ok=ok,
        metrics={
            "shadow_over_lit": ratio,
            "shadow_ratio_max": shadow_ratio_max,
            "lit_peak_abs": lit,
            "shadow_peak_abs": shadow,
            "slice_peak_abs": float(np.max(slice_abs)),
            "run": info,
        },
    )


def run_fdtd_demo_slice() -> tuple[np.ndarray, dict]:
    """Return |Ez| mid-slice and metadata for plotting (PEC scene)."""
    cfg = FdtdConfig(
        nx=48,
        ny=40,
        nz=40,
        dx_m=0.05,
        n_steps=160,
        src_i=10,
        src_j=20,
        src_k=20,
        pec_box=(24, 26, 2, 38, 2, 38),
    )
    grid = FdtdGrid3D(cfg)
    info = grid.run()
    ez = info.pop("ez_mid_slice")
    img = np.abs(np.transpose(ez))
    extent = (
        0.0,
        cfg.nx * cfg.dx_m,
        0.0,
        cfg.nz * cfg.dx_m,
    )
    meta = {
        "extent": list(extent),
        "dx_m": cfg.dx_m,
        "elapsed_s": info["elapsed_s"],
        "bytes": info["bytes"],
        "shadow_over_lit": (
            info["shadow_peak_abs"] / info["lit_peak_abs"]
            if info["lit_peak_abs"] > 1e-15
            else 1.0
        ),
    }
    return img, meta
