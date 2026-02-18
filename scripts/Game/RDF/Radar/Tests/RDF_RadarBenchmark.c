// Performance benchmark for the radar scanner.
// Measures wall-clock time for a full scan at various ray counts and
// prints rays-per-millisecond throughput for comparison with the base LiDAR.
class RDF_RadarBenchmark
{
    void RDF_RadarBenchmark() {}

    // Benchmark a single scan at the given ray count.
    // Requires a valid in-world player entity to act as the scan subject.
    static void BenchmarkScan(int rayCount)
    {
        Print(string.Format("[Benchmark] Radar scan - %d rays", rayCount));

        IEntity subject = GetGame().GetPlayerManager().GetPlayerControlledEntity(0);
        if (!subject)
        {
            Print("  [Skip] No player-controlled entity in world.");
            return;
        }

        // Configure a representative X-band scanner.
        RDF_RadarSettings settings = new RDF_RadarSettings();
        settings.m_RayCount = rayCount;
        settings.m_Range    = 500.0;

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency  = 10000000000.0;
        em.m_TransmitPower     = 1000.0;
        em.m_AntennaGain       = 30.0;
        em.CalculateWavelength();
        settings.m_EMWaveParams = em;

        // Disable optional physics to isolate core ray-trace cost first.
        settings.m_EnableAtmosphericModel  = false;
        settings.m_EnableDopplerProcessing = false;
        settings.m_UseRCSModel             = false;
        settings.Validate();

        RDF_RadarScanner scanner = new RDF_RadarScanner(settings);
        array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();

        float t0      = System.GetTickCount();
        scanner.Scan(subject, samples);
        float elapsed = System.GetTickCount() - t0;

        int hits = 0;
        foreach (RDF_LidarSample s : samples)
        {
            if (s.m_Hit)
                hits++;
        }

        Print(string.Format("  Rays: %d   Hits: %d   Time: %.2f ms   Throughput: %.1f rays/ms",
            rayCount, hits, elapsed, (float)rayCount / Math.Max(elapsed, 0.001)));

        // Re-run with full physics pipeline.
        settings.m_EnableAtmosphericModel  = true;
        settings.m_EnableDopplerProcessing = true;
        settings.m_UseRCSModel             = true;
        settings.Validate();

        samples.Clear();
        float t1          = System.GetTickCount();
        scanner.Scan(subject, samples);
        float elapsedFull = System.GetTickCount() - t1;

        Print(string.Format("  [Full physics] Time: %.2f ms   Throughput: %.1f rays/ms",
            elapsedFull, (float)rayCount / Math.Max(elapsedFull, 0.001)));
        Print("");
    }

    // Compare LiDAR vs Radar overhead for an equivalent ray count.
    static void BenchmarkComparison(int rayCount)
    {
        Print(string.Format("[Benchmark] LiDAR vs Radar comparison - %d rays", rayCount));

        IEntity subject = GetGame().GetPlayerManager().GetPlayerControlledEntity(0);
        if (!subject)
        {
            Print("  [Skip] No player-controlled entity.");
            return;
        }

        array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();

        // LiDAR baseline.
        RDF_LidarSettings lidarSettings = new RDF_LidarSettings();
        lidarSettings.m_RayCount = rayCount;
        lidarSettings.m_Range    = 500.0;
        lidarSettings.Validate();

        RDF_LidarScanner lidarScanner = new RDF_LidarScanner(lidarSettings);
        float t0      = System.GetTickCount();
        lidarScanner.Scan(subject, samples);
        float lidarMs = System.GetTickCount() - t0;

        // Radar with full physics.
        RDF_RadarSettings radarSettings = new RDF_RadarSettings();
        radarSettings.m_RayCount                = rayCount;
        radarSettings.m_Range                   = 500.0;
        radarSettings.m_EnableDopplerProcessing = true;
        radarSettings.m_UseRCSModel             = true;
        radarSettings.m_EMWaveParams = new RDF_EMWaveParameters();
        radarSettings.m_EMWaveParams.m_CarrierFrequency = 10000000000.0;
        radarSettings.m_EMWaveParams.m_TransmitPower    = 1000.0;
        radarSettings.m_EMWaveParams.m_AntennaGain      = 30.0;
        radarSettings.m_EMWaveParams.CalculateWavelength();
        radarSettings.Validate();

        RDF_RadarScanner radarScanner = new RDF_RadarScanner(radarSettings);
        samples.Clear();
        float t1       = System.GetTickCount();
        radarScanner.Scan(subject, samples);
        float radarMs  = System.GetTickCount() - t1;

        float overhead = 0.0;
        if (lidarMs > 0.001)
            overhead = radarMs / lidarMs;

        Print(string.Format("  LiDAR: %.2f ms   Radar+Physics: %.2f ms   Overhead: %.2fx",
            lidarMs, radarMs, overhead));
        Print("");
    }

    // Run a suite of benchmarks at standard ray counts.
    static void RunAllBenchmarks()
    {
        Print("========================================");
        Print("  RDF Radar Performance Benchmarks");
        Print("========================================");
        Print("");

        BenchmarkScan(128);
        BenchmarkScan(512);
        BenchmarkScan(1024);
        BenchmarkScan(2048);

        Print("--- Overhead Comparison ---");
        BenchmarkComparison(512);

        Print("========================================");
        Print("  Benchmarks complete.");
        Print("========================================");
    }
}
