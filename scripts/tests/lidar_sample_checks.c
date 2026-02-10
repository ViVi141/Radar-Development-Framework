// Simple self-check utilities for sample strategies.
static void RDF_TestStrategyUnitLength(RDF_LidarSampleStrategy strategy, int count = 64)
{
    for (int i = 0; i < count; i++)
    {
        vector d = strategy.BuildDirection(i, count);
        float len = d.Length();
        float diff = len - 1.0;
        if (diff < -0.001 || diff > 0.001)
            Print("[RDF TEST] Direction not unit length: " + len);
    }
    Print("[RDF TEST] Unit length check complete for strategy: " + strategy.ClassName());
}

static void RDF_TestConicalBounds(RDF_ConicalSampleStrategy strategy, int count = 64)
{
    float halfRad = strategy.m_HalfAngleDeg * Math.DEG2RAD;
    float minZ = Math.Cos(halfRad) - 1e-4;
    for (int i = 0; i < count; i++)
    {
        vector d = strategy.BuildDirection(i, count);
        if (d[2] < minZ)
            Print("[RDF TEST] Conical sample below cap: z=" + d[2]);
    }
    Print("[RDF TEST] Conical bounds check complete (halfAngle=" + strategy.m_HalfAngleDeg + ")");
}

// Run quick checks for all built-in strategies.
static void RDF_RunAllSampleChecks()
{
    RDF_TestStrategyUnitLength(new RDF_UniformSampleStrategy(), 128);
    RDF_TestStrategyUnitLength(new RDF_HemisphereSampleStrategy(), 128);
    RDF_TestStrategyUnitLength(new RDF_StratifiedSampleStrategy(), 128);
    RDF_TestStrategyUnitLength(new RDF_ScanlineSampleStrategy(32), 128);
    RDF_TestStrategyUnitLength(new RDF_ConicalSampleStrategy(30.0), 128);
    RDF_TestConicalBounds(new RDF_ConicalSampleStrategy(30.0), 128);
    RDF_TestStrategyUnitLength(new RDF_SweepSampleStrategy(30.0, 20.0, 45.0), 128);

    Print("[RDF TEST] All sample checks finished.");
}