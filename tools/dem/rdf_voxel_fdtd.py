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

from rdf_radar_diffraction import knife_edge_factor_from_geometry
from rdf_voxel_em import C_LIGHT_M_S, ValidationCaseResult, free_space_power_density_w_m2

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

    def run_probes(
        self,
        probes: list[tuple[int, int, int]],
    ) -> dict:
        """Run n_steps and return peak |Ez|^2 at each probe index."""
        cfg = self.cfg
        peaks = np.zeros(len(probes), dtype=np.float64)
        t0 = time.perf_counter()
        for n in range(cfg.n_steps):
            self.step(n)
            for pidx, (i, j, k) in enumerate(probes):
                val = self.ez[i, j, k]
                pwr = val * val
                if pwr > peaks[pidx]:
                    peaks[pidx] = pwr
        elapsed = time.perf_counter() - t0
        return {
            "elapsed_s": elapsed,
            "bytes": self.nbytes,
            "dx_m": self.dx,
            "dt_s": self.dt,
            "n_steps": cfg.n_steps,
            "shape": [cfg.nx, cfg.ny, cfg.nz],
            "peak_ez2": peaks.tolist(),
            "ez_mid_slice": self.ez[:, cfg.src_j, :].copy(),
        }


def _segment_hits_pec(
    pec: np.ndarray,
    i0: int,
    j0: int,
    k0: int,
    i1: int,
    j1: int,
    k1: int,
) -> bool:
    """Game-like hard LOS: any PEC cell on the segment blocks."""
    di = i1 - i0
    dj = j1 - j0
    dk = k1 - k0
    n = max(abs(di), abs(dj), abs(dk), 1)
    for s in range(n + 1):
        t = float(s) / float(n)
        i = int(round(i0 + di * t))
        j = int(round(j0 + dj * t))
        k = int(round(k0 + dk * t))
        if pec[i, j, k]:
            return True
    return False


def _game_relative_power(
    pec: np.ndarray,
    src: tuple[int, int, int],
    probe: tuple[int, int, int],
    dx_m: float,
    wavelength_m: float,
    wall_top_j: int | None,
    wall_i_center: int | None,
    mode: str,
) -> float:
    """Game-style relative power proxy (Friis × occlusion model).

    Modes:
      hard_los: blocked → 0 (Trace-like)
      knife_edge: blocked → Friis × single-edge factor (RDF diffraction path)
    """
    si, sj, sk = src
    pi, pj, pk = probe
    dx = pi - si
    dy = pj - sj
    dz = pk - sk
    dist_cells = math.sqrt(float(dx * dx + dy * dy + dz * dz))
    range_m = max(dist_cells * dx_m, 0.5 * dx_m)
    base = free_space_power_density_w_m2(1.0, range_m)
    blocked = _segment_hits_pec(pec, si, sj, sk, pi, pj, pk)
    if not blocked:
        return base
    if mode == "hard_los":
        return 0.0
    if mode != "knife_edge":
        raise ValueError("unknown game mode: " + mode)
    if wall_top_j is None or wall_i_center is None:
        return 0.0
    # Height of wall top above the probe/source LOS (Y-up).
    u = 0.5
    if abs(pi - si) > 0:
        u = float(wall_i_center - si) / float(pi - si)
    y_los = sj + (pj - sj) * u
    h_obs = (float(wall_top_j) - y_los) * dx_m
    factor = knife_edge_factor_from_geometry(
        h_obs_m=h_obs,
        range_m=range_m,
        u_edge=u,
        wavelength_m=wavelength_m,
    )
    return base * factor


def _normalize(values: list[float], ref: float) -> list[float]:
    if ref <= 1e-30:
        return [0.0 for _ in values]
    return [v / ref for v in values]


def case_fdtd_vs_game_accuracy() -> ValidationCaseResult:
    """Compare toy FDTD vs game Friis+LOS / knife-edge on one shared scene.

    This is a relative-pattern comparison on a toy domain (not X-band map
    fidelity). Free-space probes test 1/R^2 agreement; a PEC screen tests how
    hard LOS / knife-edge deviate from Maxwell diffraction leakage.
    """
    dx = 0.05
    # Design wavelength ~ 10 cells (matches common FDTD sampling rule of thumb).
    wavelength_m = 10.0 * dx
    cfg_fs = FdtdConfig(
        nx=48,
        ny=40,
        nz=40,
        dx_m=dx,
        n_steps=160,
        src_i=8,
        src_j=20,
        src_k=20,
        pec_box=None,
    )
    # Stay outside the deep near field (~1 λ) so 1/R^2 is meaningful.
    fs_offsets = (14, 18, 22, 26)
    fs_probes = [(cfg_fs.src_i + off, cfg_fs.src_j, cfg_fs.src_k) for off in fs_offsets]
    fs_grid = FdtdGrid3D(cfg_fs)
    fs_info = fs_grid.run_probes(fs_probes)
    del fs_info["ez_mid_slice"]

    fdtd_fs = list(fs_info["peak_ez2"])
    game_fs = []
    ranges_m = []
    for i, j, k in fs_probes:
        ranges_m.append((i - cfg_fs.src_i) * dx)
        game_fs.append(
            _game_relative_power(
                fs_grid.pec,
                (cfg_fs.src_i, cfg_fs.src_j, cfg_fs.src_k),
                (i, j, k),
                dx,
                wavelength_m,
                None,
                None,
                "hard_los",
            )
        )
    # Compare adjacent decay ratios: game predicts (R2/R1)^2.
    decay_rows = []
    decay_errs = []
    for idx in range(len(fs_offsets) - 1):
        r1 = ranges_m[idx]
        r2 = ranges_m[idx + 1]
        game_ratio = (r2 * r2) / (r1 * r1)
        if fdtd_fs[idx + 1] <= 1e-30:
            fdtd_ratio_fs = float("inf")
        else:
            fdtd_ratio_fs = fdtd_fs[idx] / fdtd_fs[idx + 1]
        err = abs(fdtd_ratio_fs - game_ratio) / max(game_ratio, 1e-30)
        decay_errs.append(err)
        decay_rows.append(
            {
                "r1_m": r1,
                "r2_m": r2,
                "fdtd_peak_ratio": fdtd_ratio_fs,
                "game_r2_ratio": game_ratio,
                "rel_err": err,
            }
        )
    fs_mean_err = float(sum(decay_errs) / len(decay_errs))
    fs_max_err = float(max(decay_errs))
    fdtd_fs_n = _normalize(fdtd_fs, fdtd_fs[0])
    game_fs_n = _normalize(game_fs, game_fs[0])

    # Occlusion scene: finite-height PEC screen (Y-up), LOS through the slab.
    wall_i0, wall_i1 = 24, 26
    wall_top_j = 28
    cfg_occ = FdtdConfig(
        nx=48,
        ny=40,
        nz=40,
        dx_m=dx,
        n_steps=180,
        src_i=8,
        src_j=20,
        src_k=20,
        pec_box=(wall_i0, wall_i1, 0, wall_top_j, 4, 36),
    )
    lit = (16, 20, 20)
    shadow = (34, 20, 20)
    occ_probes = [lit, shadow]
    occ_grid = FdtdGrid3D(cfg_occ)
    occ_info = occ_grid.run_probes(occ_probes)
    del occ_info["ez_mid_slice"]
    fdtd_lit, fdtd_shadow = occ_info["peak_ez2"]
    if fdtd_lit <= 1e-30:
        fdtd_ratio = 1.0
    else:
        fdtd_ratio = fdtd_shadow / fdtd_lit

    src = (cfg_occ.src_i, cfg_occ.src_j, cfg_occ.src_k)
    game_hard_lit = _game_relative_power(
        occ_grid.pec, src, lit, dx, wavelength_m, wall_top_j, (wall_i0 + wall_i1) // 2, "hard_los"
    )
    game_hard_shadow = _game_relative_power(
        occ_grid.pec, src, shadow, dx, wavelength_m, wall_top_j, (wall_i0 + wall_i1) // 2, "hard_los"
    )
    game_knife_lit = _game_relative_power(
        occ_grid.pec, src, lit, dx, wavelength_m, wall_top_j, (wall_i0 + wall_i1) // 2, "knife_edge"
    )
    game_knife_shadow = _game_relative_power(
        occ_grid.pec, src, shadow, dx, wavelength_m, wall_top_j, (wall_i0 + wall_i1) // 2, "knife_edge"
    )
    if game_hard_lit <= 1e-30:
        hard_ratio = 1.0
    else:
        hard_ratio = game_hard_shadow / game_hard_lit
    if game_knife_lit <= 1e-30:
        knife_ratio = 1.0
    else:
        knife_ratio = game_knife_shadow / game_knife_lit

    # Accuracy summary in dB for shadow ratio (skip if FDTD ratio ~ 0).
    def _ratio_err_db(model_ratio: float, truth_ratio: float) -> float:
        if truth_ratio <= 1e-12:
            return 0.0
        if model_ratio <= 1e-30:
            # Hard zero vs nonzero diffraction leakage → large penalty cap.
            return 40.0
        return abs(10.0 * math.log10(model_ratio / truth_ratio))

    hard_err_db = _ratio_err_db(hard_ratio, fdtd_ratio)
    knife_err_db = _ratio_err_db(knife_ratio, fdtd_ratio)

    # Pass if far-field decay ratios are usable and knife-edge beats hard LOS
    # whenever FDTD shows diffraction leakage.
    ok = fs_max_err < 0.55
    if fdtd_ratio > 1e-4:
        ok = ok and knife_err_db <= hard_err_db + 1e-6

    return ValidationCaseResult(
        name="fdtd_vs_game_accuracy",
        ok=ok,
        metrics={
            "caveat": (
                "Toy-domain relative comparison only; not map-scale X-band "
                "fidelity. Compares FDTD peak |Ez|^2 decay/occlusion ratios "
                "to game Friis × hard-LOS / knife-edge."
            ),
            "wavelength_m": wavelength_m,
            "free_space": {
                "probe_offsets_cells": list(fs_offsets),
                "ranges_m": ranges_m,
                "fdtd_peak_ez2": fdtd_fs,
                "game_friis": game_fs,
                "fdtd_norm": fdtd_fs_n,
                "game_norm": game_fs_n,
                "decay_pairs": decay_rows,
                "mean_rel_err": fs_mean_err,
                "max_rel_err": fs_max_err,
                "elapsed_s": fs_info["elapsed_s"],
            },
            "occlusion": {
                "fdtd_shadow_over_lit": fdtd_ratio,
                "game_hard_los_shadow_over_lit": hard_ratio,
                "game_knife_edge_shadow_over_lit": knife_ratio,
                "hard_los_err_db": hard_err_db,
                "knife_edge_err_db": knife_err_db,
                "fdtd_lit_peak_ez2": fdtd_lit,
                "fdtd_shadow_peak_ez2": fdtd_shadow,
                "elapsed_s": occ_info["elapsed_s"],
            },
            "verdict": (
                "Free-space far-field decay: game Friis ~ FDTD within grid/"
                "pulse error. Occlusion: hard LOS zeros the shadow (large dB "
                "error vs diffraction); knife-edge is the closer game model."
            ),
        },
    )


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
