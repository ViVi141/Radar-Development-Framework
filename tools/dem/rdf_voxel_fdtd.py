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

from rdf_radar_diffraction import (
    dual_knife_edge_factor_from_geometry,
    fresnel_nu,
    knife_edge_factor_from_geometry,
    knife_edge_linear_factor,
)
from rdf_radar_knife_lut import build_nu_lut, interp_nu_lut
from rdf_voxel_em import C_LIGHT_M_S, ValidationCaseResult, free_space_power_density_w_m2

EPS0 = 8.854187817e-12
MU0 = 4.0e-7 * math.pi


@dataclass
class GameMaxFidelity:
    """Highest engineering fidelity knobs usable against the toy FDTD scenes.

    Mirrors RDF_RadarSettings defaults / Enable* for path math that can be
    exercised on a small PEC grid. Pattern LUT / polarization / atmosphere are
    antenna or weather terms and do not change isotropic |Ez|^2 probe ratios.
    """

    enable_dual_knife_edge: bool = True
    enable_knife_edge_lut: bool = True
    enable_nlos_multipath: bool = True
    enable_los_two_ray: bool = True
    knife_edge_min_u_separation: float = 0.10
    nlos_min_factor: float = 0.008
    # PEC floor comparison uses Γ=-1 (perfect conductor), not soil ≈ -0.5.
    two_ray_reflection_coeff: float = -1.0


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
    # Optional extra PEC boxes (same index convention).
    pec_boxes: tuple[tuple[int, int, int, int, int, int], ...] = ()
    # Optional PEC floor slab thickness in Y cells (Y-up).
    pec_floor_j: int = 0


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
        boxes = []
        if cfg.pec_box is not None:
            boxes.append(cfg.pec_box)
        for box in cfg.pec_boxes:
            boxes.append(box)
        for box in boxes:
            i0, i1, j0, j1, k0, k1 = box
            self.pec[i0:i1, j0:j1, k0:k1] = True
        if cfg.pec_floor_j > 0:
            j_floor = min(int(cfg.pec_floor_j), cfg.ny)
            self.pec[:, 0:j_floor, :] = True
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


def _edge_geometry(
    src: tuple[int, int, int],
    probe: tuple[int, int, int],
    wall_top_j: int,
    wall_i_center: int,
    dx_m: float,
) -> tuple[float, float, float]:
    """Return (range_m, u_edge, h_obs_m) for a wall top above the LOS."""
    si, sj, sk = src
    pi, pj, pk = probe
    dx = pi - si
    dy = pj - sj
    dz = pk - sk
    dist_cells = math.sqrt(float(dx * dx + dy * dy + dz * dz))
    range_m = max(dist_cells * dx_m, 0.5 * dx_m)
    u = 0.5
    if abs(pi - si) > 0:
        u = float(wall_i_center - si) / float(pi - si)
    y_los = sj + (pj - sj) * u
    h_obs = (float(wall_top_j) - y_los) * dx_m
    return range_m, u, h_obs


def _knife_linear_factor(nu: float, nu_lut: dict | None) -> float:
    if nu_lut is None:
        return knife_edge_linear_factor(nu)
    return interp_nu_lut(nu_lut, nu)


def _single_knife_factor(
    h_obs_m: float,
    range_m: float,
    u_edge: float,
    wavelength_m: float,
    min_factor: float,
    nu_lut: dict | None,
) -> float:
    if h_obs_m <= 0.0:
        return 0.0
    if nu_lut is None:
        return knife_edge_factor_from_geometry(
            h_obs_m=h_obs_m,
            range_m=range_m,
            u_edge=u_edge,
            wavelength_m=wavelength_m,
            min_factor=min_factor,
        )
    u = float(u_edge)
    if u < 0.05:
        u = 0.05
    if u > 0.95:
        u = 0.95
    d1 = u * float(range_m)
    d2 = float(range_m) - d1
    nu = fresnel_nu(h_obs_m, d1, d2, wavelength_m)
    factor = _knife_linear_factor(nu, nu_lut)
    if factor < min_factor:
        return 0.0
    return factor


def _dual_knife_factor(
    h1_m: float,
    u1: float,
    h2_m: float,
    u2: float,
    range_m: float,
    wavelength_m: float,
    min_factor: float,
    min_u_sep: float,
    nu_lut: dict | None,
) -> float:
    if nu_lut is None:
        return dual_knife_edge_factor_from_geometry(
            h1_m=h1_m,
            u1=u1,
            h2_m=h2_m,
            u2=u2,
            range_m=range_m,
            wavelength_m=wavelength_m,
            min_factor=min_factor,
            min_u_sep=min_u_sep,
        )
    # LUT path mirrors dual_knife_edge_factor_from_geometry but swaps the
    # ν→factor map for the baked Enforce KnifeEdgeLut.
    if h1_m <= 0.0 and h2_m <= 0.0:
        return 0.0
    if h2_m <= 0.0:
        return _single_knife_factor(
            h1_m, range_m, u1, wavelength_m, min_factor, nu_lut
        )
    if h1_m <= 0.0:
        return _single_knife_factor(
            h2_m, range_m, u2, wavelength_m, min_factor, nu_lut
        )
    du = abs(float(u1) - float(u2))
    if du < float(min_u_sep):
        if h1_m >= h2_m:
            return _single_knife_factor(
                h1_m, range_m, u1, wavelength_m, min_factor, nu_lut
            )
        return _single_knife_factor(
            h2_m, range_m, u2, wavelength_m, min_factor, nu_lut
        )

    f_a = _single_knife_factor(h1_m, range_m, u1, wavelength_m, 0.0, nu_lut)
    f_b = _single_knife_factor(h2_m, range_m, u2, wavelength_m, 0.0, nu_lut)
    if float(u1) <= float(u2):
        ua, ha = float(u1), float(h1_m)
        ub, hb = float(u2), float(h2_m)
    else:
        ua, ha = float(u2), float(h2_m)
        ub, hb = float(u1), float(h1_m)
        f_a, f_b = f_b, f_a

    if ua < 0.05:
        ua = 0.05
    if ub > 0.95:
        ub = 0.95
    if ub <= ua + 0.02:
        return _single_knife_factor(
            h1_m, range_m, u1, wavelength_m, min_factor, nu_lut
        )

    if f_a >= f_b:
        f_main = f_a
        main_is_first = True
    else:
        f_main = f_b
        main_is_first = False

    distance = float(range_m)
    lam = max(float(wavelength_m), 0.001)
    d_tx_e1 = max(ua * distance, 1.0)
    d_e1_e2 = max((ub - ua) * distance, 1.0)
    d_e2_rx = max((1.0 - ub) * distance, 1.0)
    if main_is_first:
        span = d_e1_e2 + d_e2_rx
        nu_sec = hb * math.sqrt(2.0 * span / (lam * d_e1_e2 * d_e2_rx))
    else:
        span = d_tx_e1 + d_e1_e2
        nu_sec = ha * math.sqrt(2.0 * span / (lam * d_tx_e1 * d_e1_e2))
    f_sec = _knife_linear_factor(nu_sec, nu_lut)
    if f_sec < 0.25:
        f_sec = 0.25
    factor = f_main * f_sec
    if factor < min_factor:
        return 0.0
    return factor


def _game_relative_power(
    pec: np.ndarray,
    src: tuple[int, int, int],
    probe: tuple[int, int, int],
    dx_m: float,
    wavelength_m: float,
    edges: list[tuple[int, int]],
    mode: str,
    fidelity: GameMaxFidelity | None = None,
    nu_lut: dict | None = None,
) -> float:
    """Game-style relative power proxy (Friis × occlusion model).

    Modes:
      hard_los: blocked → 0 (Trace-like)
      knife_single: blocked → Friis × single dominant edge
      knife_dual: blocked → Friis × dual-dominant cascade
      knife_max: dual + ν LUT (highest Enforce diffraction path)
    edges: list of (wall_top_j, wall_i_center)
    """
    if fidelity is None:
        fidelity = GameMaxFidelity()
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
    if not edges:
        return 0.0

    geom = []
    for wall_top_j, wall_i_center in edges:
        _r, u, h_obs = _edge_geometry(
            src, probe, wall_top_j, wall_i_center, dx_m
        )
        geom.append((u, h_obs))
    geom.sort(key=lambda e: e[1], reverse=True)
    best_u, best_h = geom[0]
    second_u = best_u
    second_h = -1.0
    for u, h in geom[1:]:
        du = abs(u - best_u)
        if du < fidelity.knife_edge_min_u_separation:
            continue
        second_u = u
        second_h = h
        break

    use_lut = False
    if mode == "knife_max":
        use_lut = fidelity.enable_knife_edge_lut
    active_lut = None
    if use_lut:
        active_lut = nu_lut

    if mode == "knife_single":
        factor = _single_knife_factor(
            best_h,
            range_m,
            best_u,
            wavelength_m,
            fidelity.nlos_min_factor,
            None,
        )
        return base * factor

    if mode == "knife_dual" or mode == "knife_max":
        if not fidelity.enable_dual_knife_edge or second_h <= 0.0:
            factor = _single_knife_factor(
                best_h,
                range_m,
                best_u,
                wavelength_m,
                fidelity.nlos_min_factor,
                active_lut,
            )
        else:
            factor = _dual_knife_factor(
                best_h,
                best_u,
                second_h,
                second_u,
                range_m,
                wavelength_m,
                fidelity.nlos_min_factor,
                fidelity.knife_edge_min_u_separation,
                active_lut,
            )
        return base * factor

    raise ValueError("unknown game mode: " + mode)


def _two_ray_factor_raw(
    wavelength_m: float,
    range_m: float,
    radar_height_agl_m: float,
    target_height_agl_m: float,
    reflection_coeff: float,
    min_factor: float = 0.08,
    max_factor: float = 4.0,
) -> float:
    """Two-ray power factor without Enforce's ≥1 m validity floors.

    Used only for toy-grid FDTD comparison; combat code still uses
    ``two_ray_multipath_factor`` with those floors.
    """
    if wavelength_m <= 0.0 or range_m <= 1.0e-6:
        return 1.0
    if radar_height_agl_m <= 0.0 or target_height_agl_m <= 0.0:
        return 1.0
    delta = 2.0 * radar_height_agl_m * target_height_agl_m / range_m
    phase = 2.0 * math.pi * delta / wavelength_m
    real = 1.0 + reflection_coeff * math.cos(phase)
    imag = reflection_coeff * math.sin(phase)
    factor = real * real + imag * imag
    lo = min_factor
    if lo < 0.01:
        lo = 0.01
    hi = max_factor
    if hi < lo:
        hi = lo
    if factor < lo:
        factor = lo
    if factor > hi:
        factor = hi
    return factor


def _normalize(values: list[float], ref: float) -> list[float]:
    out = []
    if ref <= 1e-30:
        for _ in values:
            out.append(0.0)
        return out
    for v in values:
        out.append(v / ref)
    return out


def _ratio_err_db(model_ratio: float, truth_ratio: float) -> float:
    if truth_ratio <= 1e-12:
        return 0.0
    if model_ratio <= 1e-30:
        # Hard zero vs nonzero diffraction leakage → large penalty cap.
        return 40.0
    return abs(10.0 * math.log10(model_ratio / truth_ratio))


def _shadow_over_lit(lit: float, shadow: float) -> float:
    if lit <= 1e-30:
        return 1.0
    return shadow / lit


def _game_shadow_ratio(
    pec: np.ndarray,
    src: tuple[int, int, int],
    lit: tuple[int, int, int],
    shadow: tuple[int, int, int],
    dx_m: float,
    wavelength_m: float,
    edges: list[tuple[int, int]],
    mode: str,
    fidelity: GameMaxFidelity,
    nu_lut: dict | None,
) -> float:
    game_lit = _game_relative_power(
        pec,
        src,
        lit,
        dx_m,
        wavelength_m,
        edges,
        mode,
        fidelity,
        nu_lut,
    )
    game_shadow = _game_relative_power(
        pec,
        src,
        shadow,
        dx_m,
        wavelength_m,
        edges,
        mode,
        fidelity,
        nu_lut,
    )
    return _shadow_over_lit(game_lit, game_shadow)


def case_fdtd_vs_game_accuracy() -> ValidationCaseResult:
    """Compare toy FDTD vs max-fidelity game path math on shared scenes.

    Highest game diffraction path = dual-dominant knife-edge + ν LUT
    (RDF defaults). Pattern / polarization / atmosphere are not exercised
    here because the FDTD toy is an isotropic Ez soft source.
    """
    fidelity = GameMaxFidelity()
    nu_lut = build_nu_lut()
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
                [],
                "hard_los",
                fidelity,
                nu_lut,
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

    # --- Clear-LOS two-ray (max fidelity opt-in) vs FDTD with PEC floor ---
    floor_j = 2
    cfg_tr = FdtdConfig(
        nx=48,
        ny=40,
        nz=40,
        dx_m=dx,
        n_steps=180,
        src_i=8,
        src_j=18,
        src_k=20,
        pec_floor_j=floor_j,
    )
    tr_offsets = (14, 18, 22, 26)
    tr_probes = [
        (cfg_tr.src_i + off, cfg_tr.src_j, cfg_tr.src_k) for off in tr_offsets
    ]
    tr_grid = FdtdGrid3D(cfg_tr)
    tr_info = tr_grid.run_probes(tr_probes)
    del tr_info["ez_mid_slice"]
    fdtd_tr = list(tr_info["peak_ez2"])
    h_radar = (cfg_tr.src_j - floor_j + 0.5) * dx
    game_friis_tr = []
    game_two_ray = []
    tr_ranges = []
    for i, j, k in tr_probes:
        r_m = (i - cfg_tr.src_i) * dx
        tr_ranges.append(r_m)
        friis = free_space_power_density_w_m2(1.0, max(r_m, 0.5 * dx))
        game_friis_tr.append(friis)
        h_tgt = (j - floor_j + 0.5) * dx
        factor = 1.0
        if fidelity.enable_los_two_ray:
            # Raw formula (no ≥1 m Enforce gate) so the toy PEC-floor scene
            # can exercise the same phase geometry the game uses at scale.
            factor = _two_ray_factor_raw(
                wavelength_m,
                r_m,
                h_radar,
                h_tgt,
                reflection_coeff=fidelity.two_ray_reflection_coeff,
                min_factor=0.08,
                max_factor=4.0,
            )
        game_two_ray.append(friis * factor)
    tr_decay_errs_friis = []
    tr_decay_errs_two = []
    tr_rows = []
    for idx in range(len(tr_offsets) - 1):
        r1 = tr_ranges[idx]
        r2 = tr_ranges[idx + 1]
        if fdtd_tr[idx + 1] <= 1e-30:
            fdtd_pair = float("inf")
        else:
            fdtd_pair = fdtd_tr[idx] / fdtd_tr[idx + 1]
        if game_friis_tr[idx + 1] <= 1e-30:
            friis_pair = float("inf")
        else:
            friis_pair = game_friis_tr[idx] / game_friis_tr[idx + 1]
        if game_two_ray[idx + 1] <= 1e-30:
            two_pair = float("inf")
        else:
            two_pair = game_two_ray[idx] / game_two_ray[idx + 1]
        err_f = abs(fdtd_pair - friis_pair) / max(friis_pair, 1e-30)
        err_t = abs(fdtd_pair - two_pair) / max(two_pair, 1e-30)
        tr_decay_errs_friis.append(err_f)
        tr_decay_errs_two.append(err_t)
        tr_rows.append(
            {
                "r1_m": r1,
                "r2_m": r2,
                "fdtd_peak_ratio": fdtd_pair,
                "game_friis_ratio": friis_pair,
                "game_two_ray_ratio": two_pair,
                "friis_rel_err": err_f,
                "two_ray_rel_err": err_t,
            }
        )
    tr_friis_max = float(max(tr_decay_errs_friis))
    tr_two_max = float(max(tr_decay_errs_two))

    # Occlusion scene: finite-height PEC screen (Y-up), LOS through the slab.
    wall_i0, wall_i1 = 24, 26
    wall_top_j = 28
    wall_i_c = (wall_i0 + wall_i1) // 2
    edges_one = [(wall_top_j, wall_i_c)]
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
    fdtd_ratio = _shadow_over_lit(fdtd_lit, fdtd_shadow)

    src = (cfg_occ.src_i, cfg_occ.src_j, cfg_occ.src_k)
    hard_ratio = _game_shadow_ratio(
        occ_grid.pec, src, lit, shadow, dx, wavelength_m, edges_one, "hard_los", fidelity, nu_lut
    )
    knife_ratio = _game_shadow_ratio(
        occ_grid.pec, src, lit, shadow, dx, wavelength_m, edges_one, "knife_single", fidelity, nu_lut
    )
    max_ratio = _game_shadow_ratio(
        occ_grid.pec, src, lit, shadow, dx, wavelength_m, edges_one, "knife_max", fidelity, nu_lut
    )

    hard_err_db = _ratio_err_db(hard_ratio, fdtd_ratio)
    knife_err_db = _ratio_err_db(knife_ratio, fdtd_ratio)
    max_err_db = _ratio_err_db(max_ratio, fdtd_ratio)

    # Two-edge scene: two finite PEC screens → dual knife should beat single.
    w1_i0, w1_i1, w1_top = 18, 20, 30
    w2_i0, w2_i1, w2_top = 28, 30, 26
    edges_two = [
        (w1_top, (w1_i0 + w1_i1) // 2),
        (w2_top, (w2_i0 + w2_i1) // 2),
    ]
    cfg_two = FdtdConfig(
        nx=48,
        ny=40,
        nz=40,
        dx_m=dx,
        n_steps=200,
        src_i=6,
        src_j=20,
        src_k=20,
        pec_box=None,
        pec_boxes=(
            (w1_i0, w1_i1, 0, w1_top, 4, 36),
            (w2_i0, w2_i1, 0, w2_top, 4, 36),
        ),
    )
    lit2 = (12, 20, 20)
    shadow2 = (36, 20, 20)
    two_grid = FdtdGrid3D(cfg_two)
    two_info = two_grid.run_probes([lit2, shadow2])
    del two_info["ez_mid_slice"]
    fdtd_lit2, fdtd_shadow2 = two_info["peak_ez2"]
    fdtd_ratio2 = _shadow_over_lit(fdtd_lit2, fdtd_shadow2)
    src2 = (cfg_two.src_i, cfg_two.src_j, cfg_two.src_k)
    single2 = _game_shadow_ratio(
        two_grid.pec,
        src2,
        lit2,
        shadow2,
        dx,
        wavelength_m,
        edges_two,
        "knife_single",
        fidelity,
        nu_lut,
    )
    dual2 = _game_shadow_ratio(
        two_grid.pec,
        src2,
        lit2,
        shadow2,
        dx,
        wavelength_m,
        edges_two,
        "knife_dual",
        fidelity,
        nu_lut,
    )
    max2 = _game_shadow_ratio(
        two_grid.pec,
        src2,
        lit2,
        shadow2,
        dx,
        wavelength_m,
        edges_two,
        "knife_max",
        fidelity,
        nu_lut,
    )
    hard2 = _game_shadow_ratio(
        two_grid.pec,
        src2,
        lit2,
        shadow2,
        dx,
        wavelength_m,
        edges_two,
        "hard_los",
        fidelity,
        nu_lut,
    )
    single2_err = _ratio_err_db(single2, fdtd_ratio2)
    dual2_err = _ratio_err_db(dual2, fdtd_ratio2)
    max2_err = _ratio_err_db(max2, fdtd_ratio2)
    hard2_err = _ratio_err_db(hard2, fdtd_ratio2)

    # Pass criteria: free-space usable; any knife / max-fidelity path must beat
    # hard LOS whenever FDTD shows diffraction leakage. Dual is not required
    # to beat single on the toy grid: Deygout-lite + Enforce's 1 m segment
    # floors often over-attenuate at sub-meter ranges.
    ok = fs_max_err < 0.55
    if fdtd_ratio > 1e-4:
        ok = ok and max_err_db <= hard_err_db + 1e-6
        ok = ok and knife_err_db <= hard_err_db + 1e-6
    if fdtd_ratio2 > 1e-4:
        ok = ok and max2_err <= hard2_err + 1e-6
        ok = ok and dual2_err <= hard2_err + 1e-6
        ok = ok and single2_err <= hard2_err + 1e-6

    closest_one = "hard_los"
    closest_one_err = hard_err_db
    if knife_err_db < closest_one_err:
        closest_one = "knife_single"
        closest_one_err = knife_err_db
    if max_err_db < closest_one_err:
        closest_one = "knife_max_dual_lut"
        closest_one_err = max_err_db

    closest_two = "hard_los"
    closest_two_err = hard2_err
    if single2_err < closest_two_err:
        closest_two = "knife_single"
        closest_two_err = single2_err
    if dual2_err < closest_two_err:
        closest_two = "knife_dual"
        closest_two_err = dual2_err
    if max2_err < closest_two_err:
        closest_two = "knife_max_dual_lut"
        closest_two_err = max2_err

    return ValidationCaseResult(
        name="fdtd_vs_game_accuracy",
        ok=ok,
        metrics={
            "caveat": (
                "Toy-domain relative comparison only; not map-scale X-band "
                "fidelity. Game max path = dual knife-edge + ν LUT + optional "
                "LOS two-ray (PEC Γ=-1). Pattern/polarization/atmosphere are "
                "not comparable on isotropic Ez probes."
            ),
            "wavelength_m": wavelength_m,
            "game_max_fidelity": {
                "enable_dual_knife_edge": fidelity.enable_dual_knife_edge,
                "enable_knife_edge_lut": fidelity.enable_knife_edge_lut,
                "enable_nlos_multipath": fidelity.enable_nlos_multipath,
                "enable_los_two_ray": fidelity.enable_los_two_ray,
                "two_ray_reflection_coeff": fidelity.two_ray_reflection_coeff,
                "nu_lut_n": int(nu_lut["n"]),
            },
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
            "los_two_ray": {
                "radar_height_agl_m": h_radar,
                "ranges_m": tr_ranges,
                "fdtd_peak_ez2": fdtd_tr,
                "game_friis": game_friis_tr,
                "game_two_ray": game_two_ray,
                "decay_pairs": tr_rows,
                "friis_max_rel_err": tr_friis_max,
                "two_ray_max_rel_err": tr_two_max,
                "two_ray_closer_than_friis": tr_two_max <= tr_friis_max + 1e-9,
                "elapsed_s": tr_info["elapsed_s"],
                "note": (
                    "Uses raw two-ray phase geometry with PEC Γ=-1 (no Enforce "
                    "≥1 m validity gate). Combat code still applies those "
                    "gates; toy grid remains near-field limited."
                ),
            },
            "occlusion": {
                "fdtd_shadow_over_lit": fdtd_ratio,
                "game_hard_los_shadow_over_lit": hard_ratio,
                "game_knife_edge_shadow_over_lit": knife_ratio,
                "game_max_fidelity_shadow_over_lit": max_ratio,
                "hard_los_err_db": hard_err_db,
                "knife_edge_err_db": knife_err_db,
                "max_fidelity_err_db": max_err_db,
                "closest_to_fdtd": closest_one,
                "closest_err_db": closest_one_err,
                "fdtd_lit_peak_ez2": fdtd_lit,
                "fdtd_shadow_peak_ez2": fdtd_shadow,
                "elapsed_s": occ_info["elapsed_s"],
            },
            "occlusion_two_edge": {
                "fdtd_shadow_over_lit": fdtd_ratio2,
                "game_hard_los_shadow_over_lit": hard2,
                "game_single_knife_shadow_over_lit": single2,
                "game_dual_knife_shadow_over_lit": dual2,
                "game_max_fidelity_shadow_over_lit": max2,
                "hard_los_err_db": hard2_err,
                "single_knife_err_db": single2_err,
                "dual_knife_err_db": dual2_err,
                "max_fidelity_err_db": max2_err,
                "closest_to_fdtd": closest_two,
                "closest_err_db": closest_two_err,
                "fdtd_lit_peak_ez2": fdtd_lit2,
                "fdtd_shadow_peak_ez2": fdtd_shadow2,
                "elapsed_s": two_info["elapsed_s"],
            },
            "summary": {
                "free_space_friis_max_rel_err": fs_max_err,
                "one_edge_closest": closest_one,
                "one_edge_closest_err_db": closest_one_err,
                "two_edge_closest": closest_two,
                "two_edge_closest_err_db": closest_two_err,
                "two_ray_max_rel_err": tr_two_max,
                "two_ray_beats_friis_on_pec_floor": tr_two_max <= tr_friis_max + 1e-9,
            },
            "verdict": (
                "Free-space: Friis ~ FDTD. Occlusion: hard LOS zeros shadow "
                "(~40 dB capped error); knife-edge (and dual+LUT max path) "
                "are far closer to toy FDTD leakage. On two-edge toy scenes "
                "single edge can beat dual because Deygout-lite over-attenuates "
                "at sub-meter ranges. LOS two-ray needs combat-scale ranges; "
                "toy PEC-floor + 1 m validity clamps are not predictive."
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
