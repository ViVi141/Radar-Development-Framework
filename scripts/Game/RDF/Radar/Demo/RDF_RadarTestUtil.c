// Shared helpers for the Radar AutoTest suite. Eliminates the per-test
// duplication of BoolLabel (12+ copies) and ArrayP95 (3 copies). Keep the
// semantics identical to the originals so report output is unchanged.
class RDF_RadarTestUtil
{
    //------------------------------------------------------------------------------------------------
    static string BoolLabel(bool ok)
    {
        if (ok)
            return "PASS";
        return "FAIL";
    }

    //------------------------------------------------------------------------------------------------
    // 95th percentile of a float array via sort-then-index. Matches the
    // original ArrayP95 in Perf/Stress/Play: O(n²) selection sort (fine for
    // the small sample counts in a single test run), epsilon-biased index to
    // avoid float32 ULP landing just below an integer boundary.
    static float ArrayP95(array<float> values)
    {
        if (!values || values.Count() == 0)
            return 0.0;
        array<float> sorted = new array<float>();
        for (int i = 0; i < values.Count(); i++)
            sorted.Insert(values.Get(i));

        int n = sorted.Count();
        for (int a = 0; a < n; a++)
        {
            for (int b = a + 1; b < n; b++)
            {
                if (sorted.Get(b) < sorted.Get(a))
                {
                    float tmp = sorted.Get(a);
                    sorted.Set(a, sorted.Get(b));
                    sorted.Set(b, tmp);
                }
            }
        }
        // Epsilon bias: (n-1)*0.95 in float32 can land ~1 ULP below an integer
        // boundary; Math.Floor would then pick the index one below the true P95.
        int idx = Math.Floor((n - 1) * 0.95 + 0.0001);
        if (idx < 0)
            idx = 0;
        if (idx >= n)
            idx = n - 1;
        return sorted.Get(idx);
    }
}
