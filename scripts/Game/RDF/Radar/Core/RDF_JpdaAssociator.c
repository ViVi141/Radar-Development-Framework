// JPDA multi-target association (TODO §9 S2).
// Offline mirror: tools/dem/rdf_radar_jpda.py — keep behaviour identical.
//
// Soft association replacing hard nearest-neighbor (GNN). Gate (range+azimuth),
// cluster via union-find, enumerate joint association events (recursive, capped),
// marginalize to beta[t][p] + miss beta[t0].
//
// Enforce has no Math.Exp: exp(-0.5 x) = 0.5^(x / (2 ln 2)); see HwCalib.c.

class RDF_JpdaPoint
{
    float m_RangeM;
    float m_AzimuthDeg;
}

class RDF_JpdaAssociator
{
    protected static const float INV_TWO_LN2 = 0.7213475204444817;
    protected static const int MAX_CLUSTER_EVENTS = 4096;
    // Hard-assignment fallback above this cluster size (avoids enumeration blowup).
    protected static const int MAX_CLUSTER_TRACKS = 4;
    protected static const int MAX_CLUSTER_PLOTS = 4;

    protected ref array<ref array<bool>> m_Valid;
    protected ref array<ref array<float>> m_Likelihood;
    protected ref array<bool> m_PlotUsed;
    protected ref array<int> m_CurPairs;
    protected ref array<float> m_EventLik;
    protected ref array<ref array<int>> m_EventPairs;
    // Pool of beta-row arrays: reused across Associate calls to avoid N
    // per-frame allocations (one row per track). Rows are Clear()'d and
    // re-filled; surplus rows from a previous larger frame are trimmed.
    protected ref array<ref array<float>> m_BetaRowPool;
    protected int m_EventCount;

    void RDF_JpdaAssociator()
    {
        m_Valid = new array<ref array<bool>>();
        m_Likelihood = new array<ref array<float>>();
        m_PlotUsed = new array<bool>();
        m_CurPairs = new array<int>();
        m_EventLik = new array<float>();
        m_EventPairs = new array<ref array<int>>();
        m_BetaRowPool = new array<ref array<float>>();
        m_EventCount = 0;
    }

    protected static float AngleDiffDeg(float a, float b)
    {
        float d = a - b;
        while (d > 180.0)
            d = d - 360.0;
        while (d < -180.0)
            d = d + 360.0;
        return d;
    }

    protected static float GaussianLikelihood(
        float drange,
        float dazimuth,
        float sigmaRange,
        float sigmaAzimuth)
    {
        float x = (drange / sigmaRange) * (drange / sigmaRange)
            + (dazimuth / sigmaAzimuth) * (dazimuth / sigmaAzimuth);
        return Math.Pow(0.5, x * INV_TWO_LN2);
    }

    // Main entry: fills outBetas[t][p] and outMiss[t].
    // clutter: per-unassigned-plot false-alarm weight. When clutter=0 (default),
    // every plot must be assigned at the leaf — this preserves the existing
    // "hard JPDA" semantics for nPlots <= nTracks. When nPlots > nTracks the
    // enumeration produces no feasible event and we fall back to greedy hard
    // assignment (see EnumerateCluster). Pass clutter > 0 to enable soft
    // clutter modelling (unassigned plots penalised by clutter^count).
    void Associate(
        notnull array<ref RDF_JpdaPoint> tracks,
        notnull array<ref RDF_JpdaPoint> plots,
        float gateRangeM,
        float gateAzimuthDeg,
        float pd,
        notnull array<ref array<float>> outBetas,
        notnull array<float> outMiss,
        float clutter = 0.0)
    {
        int nTracks = tracks.Count();
        int nPlots = plots.Count();

        // Resize outputs.
        outBetas.Clear();
        outMiss.Clear();
        for (int t = 0; t < nTracks; t++)
        {
            // Reuse a pooled row array instead of allocating one per track
            // per frame. Clear + refill keeps the capacity for next scan.
            array<float> row;
            if (t < m_BetaRowPool.Count())
                row = m_BetaRowPool.Get(t);
            else
            {
                row = new array<float>();
                m_BetaRowPool.Insert(row);
            }
            row.Clear();
            row.Reserve(nPlots);
            for (int p = 0; p < nPlots; p++)
                row.Insert(0.0);
            outBetas.Insert(row);
            outMiss.Insert(1.0);
        }
        // Trim surplus pooled rows from a previous larger frame.
        while (m_BetaRowPool.Count() > nTracks)
            m_BetaRowPool.Remove(m_BetaRowPool.Count() - 1);
        if (nTracks <= 0 || nPlots <= 0)
            return;

        // Build valid + likelihood matrices.
        EnsureValid(nTracks, nPlots);
        EnsureLikelihood(nTracks, nPlots);
        float sigmaRange = gateRangeM / 3.0;
        if (sigmaRange < 1.0)
            sigmaRange = 1.0;
        float sigmaAz = gateAzimuthDeg / 3.0;
        if (sigmaAz < 0.1)
            sigmaAz = 0.1;
        for (int t = 0; t < nTracks; t++)
        {
            RDF_JpdaPoint tr = tracks.Get(t);
            for (int p = 0; p < nPlots; p++)
            {
                RDF_JpdaPoint pl = plots.Get(p);
                float dr = pl.m_RangeM - tr.m_RangeM;
                if (dr < 0.0)
                    dr = -dr;
                float daz = AngleDiffDeg(pl.m_AzimuthDeg, tr.m_AzimuthDeg);
                if (daz < 0.0)
                    daz = -daz;
                bool ok = false;
                if (dr <= gateRangeM && daz <= gateAzimuthDeg)
                    ok = true;
                m_Valid.Get(t).Set(p, ok);
                float lik = 0.0;
                if (ok)
                    lik = GaussianLikelihood(dr, daz, sigmaRange, sigmaAz);
                m_Likelihood.Get(t).Set(p, lik);
            }
        }

        // Union-find cluster over tracks (0..nTracks-1) and plots (nTracks..).
        array<int> parent = new array<int>();
        for (int i = 0; i < nTracks + nPlots; i++)
            parent.Insert(i);
        for (int t = 0; t < nTracks; t++)
        {
            for (int p = 0; p < nPlots; p++)
            {
                if (m_Valid.Get(t).Get(p))
                    Union(parent, t, nTracks + p);
            }
        }

        // Group members by root.
        array<ref array<int>> groups = new array<ref array<int>>();
        array<int> groupRoot = new array<int>();
        for (int i = 0; i < nTracks + nPlots; i++)
        {
            int root = Find(parent, i);
            int gi = -1;
            for (int g = 0; g < groupRoot.Count(); g++)
            {
                if (groupRoot.Get(g) == root)
                {
                    gi = g;
                    break;
                }
            }
            if (gi < 0)
            {
                groupRoot.Insert(root);
                array<int> members = new array<int>();
                members.Insert(i);
                groups.Insert(members);
            }
            else
            {
                groups.Get(gi).Insert(i);
            }
        }

        // Process each cluster.
        for (int g = 0; g < groups.Count(); g++)
        {
            array<int> members = groups.Get(g);
            array<int> clusterTracks = new array<int>();
            array<int> clusterPlots = new array<int>();
            for (int m = 0; m < members.Count(); m++)
            {
                int node = members.Get(m);
                if (node < nTracks)
                    clusterTracks.Insert(node);
                else
                    clusterPlots.Insert(node - nTracks);
            }
            if (clusterTracks.Count() <= 0 || clusterPlots.Count() <= 0)
                continue;

            if (clusterTracks.Count() > MAX_CLUSTER_TRACKS
                || clusterPlots.Count() > MAX_CLUSTER_PLOTS)
            {
                // Fallback: hard-assign best plot per track (GNN within cluster).
                HardAssignCluster(clusterTracks, clusterPlots, outBetas, outMiss);
                continue;
            }

            EnumerateCluster(clusterTracks, clusterPlots, pd, clutter, outBetas, outMiss);
        }
    }

    protected void HardAssignCluster(
        notnull array<int> trackIds,
        notnull array<int> plotIds,
        notnull array<ref array<float>> outBetas,
        notnull array<float> outMiss)
    {
        // Greedy nearest: each track takes its best unused valid plot.
        array<bool> used = new array<bool>();
        for (int p = 0; p < plotIds.Count(); p++)
            used.Insert(false);
        for (int ti = 0; ti < trackIds.Count(); ti++)
        {
            int t = trackIds.Get(ti);
            int bestPlot = -1;
            float bestLik = -1.0;
            for (int pi = 0; pi < plotIds.Count(); pi++)
            {
                if (used.Get(pi))
                    continue;
                int p = plotIds.Get(pi);
                float lik = m_Likelihood.Get(t).Get(p);
                if (lik > bestLik)
                {
                    bestLik = lik;
                    bestPlot = pi;
                }
            }
            if (bestPlot >= 0)
            {
                int p = plotIds.Get(bestPlot);
                used.Set(bestPlot, true);
                outBetas.Get(t).Set(p, 1.0);
                outMiss.Set(t, 0.0);
            }
        }
    }

    // Recursive joint-event enumeration + marginalization for one (small) cluster.
    protected void EnumerateCluster(
        notnull array<int> trackIds,
        notnull array<int> plotIds,
        float pd,
        float clutter,
        notnull array<ref array<float>> outBetas,
        notnull array<float> outMiss)
    {
        m_EventLik.Clear();
        m_EventPairs.Clear();
        m_EventCount = 0;
        m_CurPairs.Clear();
        EnsurePlotUsed(plotIds);

        EnumerateRec(
            trackIds,
            plotIds,
            0,
            1.0,
            pd,
            clutter);

        if (m_EventCount <= 0)
        {
            // No feasible joint event (e.g. clutter=0 with unassigned plots, or
            // all events pruned by MAX_CLUSTER_EVENTS before any leaf). Fall
            // back to greedy hard assignment so the cluster still produces
            // useful betas instead of all-zero / all-miss.
            HardAssignCluster(trackIds, plotIds, outBetas, outMiss);
            return;
        }

        float total = 0.0;
        for (int e = 0; e < m_EventLik.Count(); e++)
            total = total + m_EventLik.Get(e);
        if (total <= 0.0)
            return;

        // Miss numerators.
        array<float> missNum = new array<float>();
        for (int t = 0; t < trackIds.Count(); t++)
            missNum.Insert(0.0);
        // Beta numerators (global plot index → track → value).
        for (int e = 0; e < m_EventPairs.Count(); e++)
        {
            array<int> pairs = m_EventPairs.Get(e);
            float lik = m_EventLik.Get(e) / total;
            // Mark which tracks are assigned in this event.
            array<int> assignedTrackIdx = new array<int>();
            for (int k = 0; k + 1 < pairs.Count(); k = k + 2)
            {
                int tGlobal = pairs.Get(k);
                int pGlobal = pairs.Get(k + 1);
                // Find track index in trackIds.
                for (int ti = 0; ti < trackIds.Count(); ti++)
                {
                    if (trackIds.Get(ti) == tGlobal)
                    {
                        assignedTrackIdx.Insert(ti);
                        outBetas.Get(tGlobal).Set(pGlobal,
                            outBetas.Get(tGlobal).Get(pGlobal) + lik);
                        break;
                    }
                }
            }
            // Unassigned tracks in this event accumulate miss.
            for (int ti = 0; ti < trackIds.Count(); ti++)
            {
                bool assigned = false;
                for (int a = 0; a < assignedTrackIdx.Count(); a++)
                {
                    if (assignedTrackIdx.Get(a) == ti)
                    {
                        assigned = true;
                        break;
                    }
                }
                if (!assigned)
                    missNum.Set(ti, missNum.Get(ti) + lik);
            }
        }

        // Write back cluster miss probabilities (isolated tracks keep miss=1
        // from Associate()).
        for (int ti = 0; ti < trackIds.Count(); ti++)
            outMiss.Set(trackIds.Get(ti), missNum.Get(ti));
    }

    protected void EnumerateRec(
        notnull array<int> trackIds,
        notnull array<int> plotIds,
        int trackIndex,
        float currentLik,
        float pd,
        float clutter)
    {
        if (m_EventCount >= MAX_CLUSTER_EVENTS)
            return;
        if (trackIndex >= trackIds.Count())
        {
            // Leaf: unassigned plots are clutter, weighted by clutter^count.
            // When clutter=0 and any plot is unassigned, the event is
            // infeasible (skip). When clutter>0, the event is feasible but
            // penalised, so nPlots > nTracks no longer kills association.
            int unassigned = 0;
            for (int pi = 0; pi < plotIds.Count(); pi++)
            {
                if (!m_PlotUsed.Get(plotIds.Get(pi)))
                    unassigned = unassigned + 1;
            }
            float leafLik = currentLik;
            if (unassigned > 0)
            {
                if (clutter <= 0.0)
                    return;
                leafLik = leafLik * Math.Pow(clutter, unassigned);
            }
            if (leafLik <= 0.0)
                return;
            array<int> eventPairs = new array<int>();
            for (int k = 0; k < m_CurPairs.Count(); k++)
                eventPairs.Insert(m_CurPairs.Get(k));
            m_EventPairs.Insert(eventPairs);
            m_EventLik.Insert(leafLik);
            m_EventCount = m_EventCount + 1;
            return;
        }

        int t = trackIds.Get(trackIndex);
        float missW = 1.0 - pd;

        // Option 1: this track misses.
        EnumerateRec(trackIds, plotIds, trackIndex + 1, currentLik * missW, pd, clutter);

        // Option 2: assign to each unused valid plot.
        for (int pi = 0; pi < plotIds.Count(); pi++)
        {
            int p = plotIds.Get(pi);
            if (m_PlotUsed.Get(p))
                continue;
            if (!m_Valid.Get(t).Get(p))
                continue;
            m_PlotUsed.Set(p, true);
            m_CurPairs.Insert(t);
            m_CurPairs.Insert(p);
            EnumerateRec(
                trackIds,
                plotIds,
                trackIndex + 1,
                currentLik * m_Likelihood.Get(t).Get(p),
                pd,
                clutter);
            m_CurPairs.Remove(m_CurPairs.Count() - 1);
            m_CurPairs.Remove(m_CurPairs.Count() - 1);
            m_PlotUsed.Set(p, false);
        }
    }

    protected void EnsureValid(int nTracks, int nPlots)
    {
        m_Valid.Clear();
        for (int t = 0; t < nTracks; t++)
        {
            array<bool> row = new array<bool>();
            row.Reserve(nPlots);
            for (int p = 0; p < nPlots; p++)
                row.Insert(false);
            m_Valid.Insert(row);
        }
    }

    protected void EnsureLikelihood(int nTracks, int nPlots)
    {
        m_Likelihood.Clear();
        for (int t = 0; t < nTracks; t++)
        {
            array<float> row = new array<float>();
            row.Reserve(nPlots);
            for (int p = 0; p < nPlots; p++)
                row.Insert(0.0);
            m_Likelihood.Insert(row);
        }
    }

    protected void EnsurePlotUsed(notnull array<int> plotIds)
    {
        m_PlotUsed.Clear();
        int maxId = 0;
        for (int pi = 0; pi < plotIds.Count(); pi++)
        {
            if (plotIds.Get(pi) > maxId)
                maxId = plotIds.Get(pi);
        }
        for (int i = 0; i <= maxId; i++)
            m_PlotUsed.Insert(false);
    }

    protected static int Find(notnull array<int> parent, int node)
    {
        int root = node;
        while (parent.Get(root) != root)
            root = parent.Get(root);
        // Path compression.
        int cur = node;
        while (parent.Get(cur) != cur)
        {
            int nxt = parent.Get(cur);
            parent.Set(cur, root);
            cur = nxt;
        }
        return root;
    }

    protected static void Union(notnull array<int> parent, int a, int b)
    {
        int ra = Find(parent, a);
        int rb = Find(parent, b);
        if (ra != rb)
            parent.Set(ra, rb);
    }
}
