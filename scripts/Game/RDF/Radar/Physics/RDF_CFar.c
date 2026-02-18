// CFAR implementations (PoC): optimized CA‑CFAR + OS‑CFAR (order‑statistic)
// - Uses a single azimuth sort (O(n^2) selection sort once) then sliding-window neighbor
//   selection to reduce per-cell cost to O(windowSize).
// - CA‑CFAR: threshold = mean(referencePower) * multiplier
// - OS‑CFAR: threshold = orderStatistic(referencePowers, rank) * multiplier

class RDF_CFar
{
    void RDF_CFar() {}

    // Simple in-memory cache for OS multiplier lookups (key -> multiplier)
    protected static ref map<string, float> s_OSMultiplierCache;
    // Path to optional offline lookup CSV (created by tools/generate_os_cfar_table.py)
    protected static string s_OSMultiplierTablePath = "scripts/Game/RDF/Radar/Data/os_cfar_multipliers.csv";
    // Whether we've attempted to load the offline file (prevents repeated IO attempts)
    protected static bool s_OSMultiplierFileLoaded = false;

    // Helper: normalize angular difference to [0, PI]
    protected static float AngularDiff(float a, float b)
    {
        float d = a - b;
        while (d > Math.PI) d -= 2.0 * Math.PI;
        while (d < -Math.PI) d += 2.0 * Math.PI;
        return Math.AbsFloat(d);
    }

    // Make a stable cache key for (window,rank,pfa)
    protected static string MakeOSCacheKey(int windowSize, int rank, float pfa)
    {
        return string.Format("w=%1|r=%2|p=%.0e", windowSize, rank, pfa);
    }

    // Load OS multiplier table from CSV file (silently ignored if file missing).
    static void LoadOSMultiplierTableFromFile(string path = "")
    {
        if (s_OSMultiplierFileLoaded)
            return;
        if (path == "")
            path = s_OSMultiplierTablePath;
        if (!s_OSMultiplierCache)
            s_OSMultiplierCache = new map<string, float>();
        if (!FileIO.FileExist(path))
        {
            s_OSMultiplierFileLoaded = true;
            return;
        }

        FileHandle fh = FileIO.OpenFile(path, FileMode.READ);
        if (!fh)
        {
            s_OSMultiplierFileLoaded = true;
            return;
        }

        int loaded = 0;
        while (!fh.EOF())
        {
            string line = fh.ReadLine();
            if (!line || line == string.Empty) continue;
            line = line.Trim();
            if (line.StartsWith("#")) continue;

            array<string> parts = new array<string>();
            line.Split(",", parts, false);
            if (parts.Count() < 4) continue;

            int w = parts.Get(0).ToInt();
            int r = parts.Get(1).ToInt();
            float p = parts.Get(2).ToFloat();
            float a = parts.Get(3).ToFloat();

            string key = MakeOSCacheKey(w, r, p);
            s_OSMultiplierCache.Insert(key, a);
            loaded++;
        }

        fh.Close();
        s_OSMultiplierFileLoaded = true;
        // Do not spam log in normal operation - tests can check cache explicitly.
    }

    // Retrieve cached OS multiplier or return -1.0 when missing.
    static float GetCachedOSMultiplier(int windowSize, int rank, float pfa)
    {
        // Ensure cache exists and attempt to load offline table on first access
        if (!s_OSMultiplierCache)
            s_OSMultiplierCache = new map<string, float>();
        if (!s_OSMultiplierFileLoaded)
            LoadOSMultiplierTableFromFile(s_OSMultiplierTablePath);

        string key = MakeOSCacheKey(windowSize, rank, pfa);
        if (!s_OSMultiplierCache.Contains(key))
            return -1.0;
        return s_OSMultiplierCache.Get(key);
    }

    // Precompute a table of OS multipliers for specified parameter sets.
    static void PrecomputeOSMultiplierTable(array<int> windows, array<int> ranks, array<float> pfas, int sims = 1024)
    {
        if (!s_OSMultiplierCache)
            s_OSMultiplierCache = new map<string, float>();
        if (!windows || !ranks || !pfas)
            return;
        for (int i = 0; i < windows.Count(); ++i)
        {
            int w = windows.Get(i);
            for (int j = 0; j < ranks.Count(); ++j)
            {
                int r = ranks.Get(j);
                for (int k = 0; k < pfas.Count(); ++k)
                {
                    float pfa = pfas.Get(k);
                    string key = MakeOSCacheKey(w, r, pfa);
                    if (s_OSMultiplierCache.Contains(key))
                        continue;
                    float alpha = EstimateOSMultiplier(w, r, pfa, sims);
                    s_OSMultiplierCache.Insert(key, Math.Max(alpha, 1e-12));
                }
            }
        }
    }

    // CA‑CFAR theoretical multiplier calculator.
    // For exponential background and CA‑CFAR using N reference cells,
    // the multiplier alpha satisfying Pfa is:
    //   alpha = N * (Pfa^{-1/N} - 1)
    static float CalculateCAThresholdMultiplier(int windowSize, float targetPfa)
    {
        if (windowSize <= 0 || targetPfa <= 0.0)
            return 1.0;
        float N = (float)windowSize;
        float term = Math.Pow(targetPfa, -1.0 / N);
        return N * (term - 1.0);
    }

    // Estimate OS‑CFAR multiplier by Monte‑Carlo (PoC).
    // - windowSize: number of reference cells
    // - rank: order-statistic rank (1 = largest)
    // - targetPfa: desired false-alarm probability
    // - sims: Monte‑Carlo sample count (default 2048)
    // Returns multiplier alpha such that P{X > alpha * X_r} ≈ targetPfa under exponential noise.
    static float EstimateOSMultiplier(int windowSize, int rank, float targetPfa, int sims = 2048)
    {
        if (windowSize <= 0 || rank <= 0 || targetPfa <= 0.0)
            return 1.0;

        // Check cache first
        if (!s_OSMultiplierCache)
            s_OSMultiplierCache = new map<string, float>();
        string key = MakeOSCacheKey(windowSize, rank, targetPfa);
        if (s_OSMultiplierCache.Contains(key))
            return s_OSMultiplierCache.Get(key);

        int w = Math.Max(1, windowSize);
        int r = Math.Clamp(rank, 1, w);
        int S = Math.Clamp(sims, 128, 16384);

        array<float> ordStats = new array<float>(S);

        // Monte-Carlo: generate exponential(1) reference windows and collect r-th largest
        for (int s = 0; s < S; ++s)
        {
            array<float> refs = new array<float>(w);
            for (int i = 0; i < w; ++i)
            {
                float u = Math.RandomFloat(0.0, 1.0);
                float x = -Math.Log(Math.Max(u, 1e-12));
                refs.InsertAt(i, x);
            }

            // selection-sort descending small-w
            for (int a = 0; a < w - 1; ++a)
            {
                int maxIdx = a;
                for (int b = a + 1; b < w; ++b)
                {
                    if (refs.Get(b) > refs.Get(maxIdx))
                        maxIdx = b;
                }
                if (maxIdx != a)
                {
                    float tmp = refs.Get(a);
                    refs.Set(a, refs.Get(maxIdx));
                    refs.Set(maxIdx, tmp);
                }
            }

            float xr = refs.Get(Math.Min(r - 1, refs.Count() - 1));
            ordStats.InsertAt(s, xr);
        }

        // Binary search alpha so that mean(exp(-alpha * Xr)) == targetPfa
        float lo = 1e-6;
        float hi = 1e6;
        for (int iter = 0; iter < 40; ++iter)
        {
            float mid = 0.5 * (lo + hi);
            double acc = 0.0;
            for (int i = 0; i < ordStats.Count(); ++i)
                acc += Math.Exp(-mid * ordStats.Get(i));
            double pfaEst = acc / (double)ordStats.Count();
            if (pfaEst > targetPfa)
                lo = mid;
            else
                hi = mid;
        }

        float alpha = 0.5 * (lo + hi);
        // Cache the result for future lookups
        s_OSMultiplierCache.Insert(key, alpha);
        return alpha;
    }

    // Optimized CA-CFAR using azimuth sort + circular sliding window.
    static void ApplyCA_CFAR(array<ref RDF_LidarSample> samples, int windowSize = 16, float guardAngleDeg = 2.0, float multiplier = 6.0)
    {
        if (!samples || samples.Count() == 0)
            return;

        int n = samples.Count();
        int w = Math.Max(1, windowSize);
        float guardRad = Math.Max(0.0, guardAngleDeg) * Math.DEG2RAD;
        float mult = Math.Max(1.0, multiplier);

        // Precompute azimuths and received powers
        array<float> az = new array<float>(n);
        array<float> pwr = new array<float>(n);
        for (int i = 0; i < n; ++i)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(samples.Get(i));
            float a = 0.0;
            float pr = 0.0;
            if (rs && rs.m_Dir.Length() > 0.0)
            {
                a = Math.Atan2(rs.m_Dir[2], rs.m_Dir[0]);
                pr = Math.Max(rs.m_ReceivedPower, 0.0);
            }
            az.InsertAt(i, a);
            pwr.InsertAt(i, pr);
        }

        // Build order[] = indices sorted by az (selection sort - O(n^2) once)
        array<int> order = new array<int>(n);
        for (int i = 0; i < n; ++i) order.InsertAt(i, i);
        for (int i = 0; i < n - 1; ++i)
        {
            int minIdx = i;
            for (int j = i + 1; j < n; ++j)
            {
                if (az[order.Get(j)] < az[order.Get(minIdx)])
                    minIdx = j;
            }
            if (minIdx != i)
            {
                int tmp = order.Get(i);
                order.Set(i, order.Get(minIdx));
                order.Set(minIdx, tmp);
            }
        }

        // inverse mapping: posInOrder[originalIndex] = position in sorted array
        array<int> posInOrder = new array<int>(n);
        for (int i = 0; i < n; ++i)
            posInOrder.InsertAt(order.Get(i), i);

        // For each sample, gather up to w neighbours outside guard and compute mean
        for (int orig = 0; orig < n; ++orig)
        {
            RDF_RadarSample tgt = RDF_RadarSample.Cast(samples.Get(orig));
            if (!tgt)
                continue;

            if (pwr.Get(orig) <= 0.0)
            {
                tgt.m_Hit = false;
                continue;
            }

            int pos = posInOrder.Get(orig);
            int collected = 0;
            float sum = 0.0;

            // Expand alternately left/right around 'pos' in sorted order until window filled
            int left = (pos - 1 + n) % n;
            int right = (pos + 1) % n;
            int attempts = 0;
            while (collected < w && attempts < n)
            {
                int pickPos = (attempts % 2 == 0) ? left : right;
                if (attempts % 2 == 0) left = (left - 1 + n) % n; else right = (right + 1) % n;
                int idx = order.Get(pickPos);

                float angDiff = AngularDiff(az.Get(idx), az.Get(orig));
                if (angDiff <= guardRad)
                {
                    attempts++;
                    continue;
                }

                sum += pwr.Get(idx);
                collected++;
                attempts++;
            }

            if (collected == 0)
                continue;

            float meanNoise = sum / (float)collected;
            float threshold = meanNoise * mult;
            tgt.m_Hit = (tgt.m_ReceivedPower >= threshold);
        }
    }

    // OS-CFAR (Order-Statistic CFAR) - use the k-th largest value from the
    // reference window as the noise estimate (PoC chooses rank parameter).
    static void ApplyOS_CFAR(array<ref RDF_LidarSample> samples, int windowSize = 16, float guardAngleDeg = 2.0, float multiplier = 6.0, int rank = 8)
    {
        if (!samples || samples.Count() == 0)
            return;

        int n = samples.Count();
        int w = Math.Max(1, windowSize);
        int r = Math.Clamp(rank, 1, w);
        float guardRad = Math.Max(0.0, guardAngleDeg) * Math.DEG2RAD;
        float mult = Math.Max(1.0, multiplier);

        // Precompute azimuths and powers (re-use logic from CA implementation)
        array<float> az = new array<float>(n);
        array<float> pwr = new array<float>(n);
        for (int i = 0; i < n; ++i)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(samples.Get(i));
            float a = 0.0;
            float pr = 0.0;
            if (rs && rs.m_Dir.Length() > 0.0)
            {
                a = Math.Atan2(rs.m_Dir[2], rs.m_Dir[0]);
                pr = Math.Max(rs.m_ReceivedPower, 0.0);
            }
            az.InsertAt(i, a);
            pwr.InsertAt(i, pr);
        }

        // Sort indices by azimuth (selection sort once)
        array<int> order = new array<int>(n);
        for (int i = 0; i < n; ++i) order.InsertAt(i, i);
        for (int i = 0; i < n - 1; ++i)
        {
            int minIdx = i;
            for (int j = i + 1; j < n; ++j)
            {
                if (az[order.Get(j)] < az[order.Get(minIdx)])
                    minIdx = j;
            }
            if (minIdx != i)
            {
                int tmp = order.Get(i);
                order.Set(i, order.Get(minIdx));
                order.Set(minIdx, tmp);
            }
        }

        // inverse mapping
        array<int> posInOrder = new array<int>(n);
        for (int i = 0; i < n; ++i)
            posInOrder.InsertAt(order.Get(i), i);

        // For each cell, collect reference powers, compute order-statistic, compare
        for (int orig = 0; orig < n; ++orig)
        {
            RDF_RadarSample tgt = RDF_RadarSample.Cast(samples.Get(orig));
            if (!tgt)
                continue;

            if (pwr.Get(orig) <= 0.0)
            {
                tgt.m_Hit = false;
                continue;
            }

            int pos = posInOrder.Get(orig);
            array<float> refs = new array<float>();

            int left = (pos - 1 + n) % n;
            int right = (pos + 1) % n;
            int attempts = 0;
            while (refs.Count() < w && attempts < n)
            {
                int pickPos = (attempts % 2 == 0) ? left : right;
                if (attempts % 2 == 0) left = (left - 1 + n) % n; else right = (right + 1) % n;
                int idx = order.Get(pickPos);

                float angDiff = AngularDiff(az.Get(idx), az.Get(orig));
                if (angDiff <= guardRad)
                {
                    attempts++;
                    continue;
                }

                refs.Insert(refs.Count(), pwr.Get(idx));
                attempts++;
            }

            if (refs.Count() == 0)
                continue;

            // selection-sort refs (small w) and pick r-th largest (from top)
            int m = refs.Count();
            for (int a = 0; a < m - 1; ++a)
            {
                int maxIdx = a;
                for (int b = a + 1; b < m; ++b)
                {
                    if (refs.Get(b) > refs.Get(maxIdx))
                        maxIdx = b;
                }
                if (maxIdx != a)
                {
                    float tmp = refs.Get(a);
                    refs.Set(a, refs.Get(maxIdx));
                    refs.Set(maxIdx, tmp);
                }
            }

            int pickIndex = Math.Min(r - 1, refs.Count() - 1);
            float noiseEstimate = refs.Get(pickIndex);
            float threshold = noiseEstimate * mult;
            tgt.m_Hit = (tgt.m_ReceivedPower >= threshold);
        }
    }
}
