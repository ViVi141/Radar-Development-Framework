"""JPDA multi-target association (offline model; TODO §9 S2).

Soft association replacing hard nearest-neighbor (GNN). When two tracks' gates
overlap in dense multi-target, GNN can swap or steal plots; JPDA computes
marginal association probabilities and updates each track with the probability-
weighted innovation, so each track keeps its own identity.

Algorithm (target-level approx, matching the project philosophy):
  1. Gate each (track, plot) pair (range + azimuth).
  2. Cluster tracks/plots connected by valid pairs (independent clusters).
  3. Enumerate joint association events within each cluster; weight each event by
     the product of its Gaussian likelihoods, miss probability (1-Pd) per
     unassigned track, and clutter weight per unassigned plot.
  4. Marginalize to beta_ij (P(track i from plot j)) and miss beta_i0.

Pure logic (no world / entity data), import-light — same convention as tools/dem.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field


@dataclass
class JpdaGate:
    range_m: float = 60.0
    azimuth_deg: float = 4.0

    @property
    def sigma_range_m(self) -> float:
        return max(self.range_m / 3.0, 1.0)

    @property
    def sigma_azimuth_deg(self) -> float:
        return max(self.azimuth_deg / 3.0, 0.1)


@dataclass
class JpdaTrack:
    track_id: int
    range_m: float
    azimuth_deg: float
    range_rate_m_s: float = 0.0


@dataclass
class JpdaPlot:
    plot_id: int
    range_m: float
    azimuth_deg: float


@dataclass
class JpdaResult:
    # beta[t][p] = marginal association probability; rows sum to (1 - miss).
    betas: list[list[float]]
    # miss_prob[t] = P(track t has no measurement this scan).
    miss_probs: list[float]

    def row(self, t: int) -> list[float]:
        return self.betas[t]


def _angle_diff_deg(a: float, b: float) -> float:
    d = (a - b) % 360.0
    if d > 180.0:
        d -= 360.0
    return d


def _gaussian_likelihood(track: JpdaTrack, plot: JpdaPlot, gate: JpdaGate) -> float:
    dr = plot.range_m - track.range_m
    daz = _angle_diff_deg(plot.azimuth_deg, track.azimuth_deg)
    x = (dr / gate.sigma_range_m) ** 2 + (daz / gate.sigma_azimuth_deg) ** 2
    return math.exp(-0.5 * x)


def _gate_valid(track: JpdaTrack, plot: JpdaPlot, gate: JpdaGate) -> bool:
    if abs(plot.range_m - track.range_m) > gate.range_m:
        return False
    if abs(_angle_diff_deg(plot.azimuth_deg, track.azimuth_deg)) > gate.azimuth_deg:
        return False
    return True


def _clusters(
    n_tracks: int,
    n_plots: int,
    valid: list[list[bool]],
) -> list[tuple[list[int], list[int]]]:
    """Connected components of tracks/plots linked by valid pairs."""
    # Bipartite adjacency: track i <-> plot j (offset by n_tracks).
    adj: list[list[int]] = [[] for _ in range(n_tracks + n_plots)]
    for i in range(n_tracks):
        for j in range(n_plots):
            if valid[i][j]:
                adj[i].append(n_tracks + j)
                adj[n_tracks + j].append(i)

    seen: list[bool] = [False] * (n_tracks + n_plots)
    clusters: list[tuple[list[int], list[int]]] = []
    for root in range(n_tracks + n_plots):
        if seen[root] or not adj[root]:
            continue
        stack = [root]
        seen[root] = True
        tracks: list[int] = []
        plots: list[int] = []
        while stack:
            node = stack.pop()
            if node < n_tracks:
                tracks.append(node)
            else:
                plots.append(node - n_tracks)
            for nb in adj[node]:
                if not seen[nb]:
                    seen[nb] = True
                    stack.append(nb)
        clusters.append((sorted(tracks), sorted(plots)))
    return clusters


def _marginalize_cluster(
    track_ids: list[int],
    plot_ids: list[int],
    valid: list[list[bool]],
    likelihoods: list[list[float]],
    pd: float,
    clutter: float,
    max_events: int,
) -> tuple[dict[tuple[int, int], float], dict[int, float]]:
    """Enumerate joint events and return marginal betas + miss probs for a cluster."""
    miss_w = 1.0 - pd
    events: list[tuple[list[tuple[int, int]], float]] = []

    def rec(ti: int, used: set[int], pairs: list[tuple[int, int]], lik: float) -> None:
        if len(events) >= max_events:
            return
        if ti == len(track_ids):
            unassigned = [p for p in plot_ids if p not in used]
            extra = 1.0
            if unassigned:
                if clutter <= 0.0:
                    return  # infeasible: every plot must be assigned when clutter=0
                extra = clutter ** len(unassigned)
            events.append((list(pairs), lik * extra))
            return
        t = track_ids[ti]
        # Miss this track.
        rec(ti + 1, used, pairs, lik * miss_w)
        # Assign to each unused valid plot.
        for p in plot_ids:
            if p in used or not valid[t][p]:
                continue
            rec(ti + 1, used | {p}, pairs + [(t, p)], lik * likelihoods[t][p])

    rec(0, set(), [], 1.0)
    if not events:
        return {}, {}

    total = sum(lik for _, lik in events)
    beta: dict[tuple[int, int], float] = {}
    miss: dict[int, float] = {}
    for t in track_ids:
        miss[t] = 0.0
    for pairs, lik in events:
        assigned_tracks = {t for t, _ in pairs}
        for t in track_ids:
            if t not in assigned_tracks:
                miss[t] += lik / total
        for (t, p) in pairs:
            beta[(t, p)] = beta.get((t, p), 0.0) + lik / total
    return beta, miss


def _hard_assign_cluster(
    track_ids: list[int],
    plot_ids: list[int],
    likelihoods: list[list[float]],
    betas: list[list[float]],
    miss: list[float],
) -> None:
    """Greedy nearest: each track takes its best unused valid plot.

    Fallback when joint enumeration produces no feasible event (e.g. clutter=0
    with unassigned plots, or cluster pruned by max_events before any leaf).
    """
    used: set[int] = set()
    for t in track_ids:
        best_plot = -1
        best_lik = -1.0
        for p in plot_ids:
            if p in used:
                continue
            lik = likelihoods[t][p]
            if lik > best_lik:
                best_lik = lik
                best_plot = p
        if best_plot >= 0:
            used.add(best_plot)
            betas[t][best_plot] = 1.0
            miss[t] = 0.0


def jpda_associate(
    tracks: list[JpdaTrack],
    plots: list[JpdaPlot],
    gate: JpdaGate | None = None,
    pd: float = 0.9,
    clutter: float = 0.0,
    max_events: int = 4096,
) -> JpdaResult:
    if gate is None:
        gate = JpdaGate()
    n_tracks = len(tracks)
    n_plots = len(plots)

    valid = [[False] * n_plots for _ in range(n_tracks)]
    likelihoods = [[0.0] * n_plots for _ in range(n_tracks)]
    for i, track in enumerate(tracks):
        for j, plot in enumerate(plots):
            if _gate_valid(track, plot, gate):
                valid[i][j] = True
                likelihoods[i][j] = _gaussian_likelihood(track, plot, gate)

    betas = [[0.0] * n_plots for _ in range(n_tracks)]
    miss = [1.0] * n_tracks

    for track_ids, plot_ids in _clusters(n_tracks, n_plots, valid):
        beta_c, miss_c = _marginalize_cluster(
            track_ids, plot_ids, valid, likelihoods, pd, clutter, max_events
        )
        if not beta_c and not miss_c:
            # No feasible joint event — fall back to greedy hard assignment so
            # the cluster still produces useful betas instead of all-zero.
            _hard_assign_cluster(track_ids, plot_ids, likelihoods, betas, miss)
            continue
        for (t, p), value in beta_c.items():
            betas[t][p] = value
        for t, value in miss_c.items():
            miss[t] = value

    return JpdaResult(betas=betas, miss_probs=miss)


def weighted_innovation(
    track: JpdaTrack,
    plots: list[JpdaPlot],
    beta_row: list[float],
) -> tuple[float, float]:
    """Probability-weighted (range, azimuth) innovation for a track's alpha-beta update."""
    dr = 0.0
    daz = 0.0
    for j, plot in enumerate(plots):
        b = beta_row[j]
        if b <= 0.0:
            continue
        dr += b * (plot.range_m - track.range_m)
        daz += b * _angle_diff_deg(plot.azimuth_deg, track.azimuth_deg)
    return dr, daz
