import unittest

from rdf_radar_jpda import (
    JpdaGate,
    JpdaPlot,
    JpdaTrack,
    jpda_associate,
    weighted_innovation,
)


def track(tid, rng, az):
    return JpdaTrack(track_id=tid, range_m=rng, azimuth_deg=az)


def plot(pid, rng, az):
    return JpdaPlot(plot_id=pid, range_m=rng, azimuth_deg=az)


class TestJpdaAssociate(unittest.TestCase):
    def test_single_track_single_plot(self):
        # clutter=0 means the lone gated plot must come from the track -> beta=1.
        tracks = [track(1, 1000.0, 0.0)]
        plots = [plot(1, 1002.0, 0.2)]
        res = jpda_associate(tracks, plots)
        self.assertAlmostEqual(res.betas[0][0], 1.0, places=6)
        self.assertAlmostEqual(res.miss_probs[0], 0.0, places=6)

    def test_well_separated_is_hard_assignment(self):
        tracks = [track(1, 1000.0, 0.0), track(2, 2000.0, 0.0)]
        plots = [plot(1, 1000.0, 0.0), plot(2, 2000.0, 0.0)]
        res = jpda_associate(tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0))
        self.assertGreater(res.betas[0][0], 0.8)
        self.assertGreater(res.betas[1][1], 0.8)
        self.assertLess(res.betas[0][1], 1e-6)
        self.assertLess(res.betas[1][0], 1e-6)

    def test_overlapping_gates_soft_split(self):
        # Two close tracks share plots in both gates: neither hard-claims.
        tracks = [track(1, 1000.0, 0.0), track(2, 1040.0, 0.0)]
        plots = [plot(1, 1000.0, 0.0), plot(2, 1040.0, 0.0)]
        res = jpda_associate(tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0))
        self.assertGreater(res.betas[0][0], res.betas[0][1])
        self.assertLess(res.betas[0][0], 1.0)
        self.assertGreater(res.betas[1][1], res.betas[1][0])
        self.assertLess(res.betas[1][1], 1.0)

    def test_shared_plot_splits_between_tracks(self):
        # 2 tracks, 1 midpoint plot: the ambiguous plot is shared ~50/50 and both
        # tracks also carry a miss probability (partial coast). This is the JPDA
        # value over GNN, which would hard-assign the plot to one track.
        tracks = [track(1, 1000.0, 0.0), track(2, 1020.0, 0.0)]
        plots = [plot(1, 1010.0, 0.0)]
        res = jpda_associate(tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0))
        self.assertAlmostEqual(res.betas[0][0], 0.5, places=3)
        self.assertAlmostEqual(res.betas[1][0], 0.5, places=3)
        self.assertAlmostEqual(res.miss_probs[0], 0.5, places=3)
        self.assertAlmostEqual(res.miss_probs[1], 0.5, places=3)

    def test_marginals_normalize(self):
        tracks = [track(1, 1000.0, 0.0), track(2, 1040.0, 0.0), track(3, 1060.0, 0.0)]
        plots = [plot(1, 1000.0, 0.0), plot(2, 1040.0, 0.0)]
        res = jpda_associate(tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0))
        for i in range(len(tracks)):
            total = res.miss_probs[i] + sum(res.betas[i])
            self.assertAlmostEqual(total, 1.0, places=6)

    def test_clusters_are_independent(self):
        tracks = [track(1, 1000.0, 0.0), track(2, 3000.0, 0.0)]
        plots = [plot(1, 1000.0, 0.0), plot(2, 3000.0, 0.0)]
        res = jpda_associate(tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0))
        self.assertAlmostEqual(res.betas[0][0], 1.0, places=6)
        self.assertAlmostEqual(res.betas[1][1], 1.0, places=6)

    def test_track_with_no_plot_misses(self):
        tracks = [track(1, 1000.0, 0.0), track(2, 5000.0, 0.0)]
        plots = [plot(1, 1000.0, 0.0)]
        res = jpda_associate(tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0))
        self.assertAlmostEqual(res.miss_probs[1], 1.0, places=6)
        self.assertAlmostEqual(res.betas[0][0], 1.0, places=6)

    def test_weighted_innovation_blends(self):
        track_a = track(1, 1000.0, 0.0)
        plots = [plot(1, 1000.0, 0.0), plot(2, 1040.0, 0.0)]
        dr, _ = weighted_innovation(track_a, plots, [0.7, 0.1])
        # 0.7 * 0 + 0.1 * 40 = 4 (miss contributes 0).
        self.assertAlmostEqual(dr, 4.0, places=6)

    def test_more_plots_than_tracks_does_not_collapse(self):
        # Regression: with clutter=0 and nPlots > nTracks, the old leaf check
        # rejected every joint event (each leaves ≥1 plot unassigned), so
        # enumeration returned no events and betas stayed all-zero / miss
        # all-one — association silently failed. The fix falls back to greedy
        # hard assignment so the cluster still produces useful betas.
        tracks = [track(1, 1000.0, 0.0)]
        plots = [plot(1, 1002.0, 0.0), plot(2, 1004.0, 0.0)]
        res = jpda_associate(tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0))
        # The track must claim exactly one plot (the nearer one) and not miss.
        self.assertAlmostEqual(res.miss_probs[0], 0.0, places=6)
        claimed = sum(1.0 for b in res.betas[0] if b > 0.5)
        self.assertAlmostEqual(claimed, 1.0, places=6)
        self.assertGreater(res.betas[0][0], 0.5)
        self.assertLess(res.betas[0][1], 0.5)

    def test_more_plots_than_tracks_soft_clutter_splits(self):
        # With clutter > 0, unassigned plots are feasible (penalised), so the
        # track splits probability across both plots and keeps a miss tail.
        tracks = [track(1, 1000.0, 0.0)]
        plots = [plot(1, 1002.0, 0.0), plot(2, 1004.0, 0.0)]
        res = jpda_associate(
            tracks, plots, JpdaGate(range_m=60.0, azimuth_deg=4.0), clutter=0.01
        )
        total = res.miss_probs[0] + sum(res.betas[0])
        self.assertAlmostEqual(total, 1.0, places=6)
        # Nearer plot should carry more weight than the farther one.
        self.assertGreater(res.betas[0][0], res.betas[0][1])


if __name__ == "__main__":
    unittest.main()
