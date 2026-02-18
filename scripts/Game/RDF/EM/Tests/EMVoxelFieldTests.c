class EMVoxelFieldTests
{
    void EMVoxelFieldTests() {}

    static void TestInjectAndRead()
    {
        Print("[EMVoxelFieldTests] TestInjectAndRead");
        EMVoxelField f = EMVoxelField.GetInstance();
        f.Clear();

        vector pos = Vector(12.3, 0.0, 45.6);
        f.InjectPowerAt(pos, 1.0, 0x1, Vector(1,0,0), 2.0);

        vector dirOut;
        int freqOut;
        float p = f.GetPowerAt(pos, dirOut, freqOut);
        Print(string.Format("  Injected 1.0 W -> Read power = %.6f W, dir=(%.2f,%.2f,%.2f), freqMask=0x%X", p, dirOut[0], dirOut[1], dirOut[2], freqOut));

        if (p <= 0.0)
            Print("  [FAIL] Expected power > 0");
        else
            Print("  [OK]");

        Print( string.Format("  Active cells = %1", f.GetActiveCellCount()) );
        Print("");
    }

    static void TestDecayAndExpiry()
    {
        Print("[EMVoxelFieldTests] TestDecayAndExpiry");
        EMVoxelField f = EMVoxelField.GetInstance();
        f.Clear();

        vector pos = Vector(0,0,0);
        f.InjectPowerAt(pos, 10.0, 0x1, Vector(0,1,0), 0.5);
        Print(string.Format("  After inject: cells=%1", f.GetActiveCellCount()));

        // tick a small dt -> power should decay but cell remains
        f.TickDecay(0.2);
        vector d; int m;
        float p1 = f.GetPowerAt(pos, d, m);
        Print(string.Format("  After 0.2s decay: power=%.6f W", p1));

        // tick past TTL -> should expire and be removed
        f.TickDecay(1.0);
        int cnt = f.GetActiveCellCount();
        Print(string.Format("  After +1.0s decay: cells=%1 (expect 0)", cnt));
        if (cnt == 0) Print("  [OK]"); else Print("  [FAIL]");

        Print("");
    }

    static void TestInjectAlongRay()
    {
        Print("[EMVoxelFieldTests] TestInjectAlongRay");
        EMVoxelField f = EMVoxelField.GetInstance();
        f.Clear();

        vector start = Vector(0,0,0);
        vector dir = Vector(1,0,0);
        float range = 500.0;
        float Pt = 1000.0;

        f.InjectAlongRay(start, dir, range, Pt, 0x1, 1.0, 50.0, 1e-4);
        int cells = f.GetActiveCellCount();
        Print(string.Format("  After InjectAlongRay: active cells = %1 (expect > 0)", cells));
        if (cells > 0) Print("  [OK]"); else Print("  [FAIL]");

        // Verify that power at a sample point along ray is > 0
        vector probe = Vector(120.0, 0.0, 0.0);
        vector dirOut; int maskOut;
        float p = f.GetPowerAt(probe, dirOut, maskOut);
        Print(string.Format("  Probe at x=120 -> power=%.6e W, dir=(%.2f,%.2f,%.2f)", p, dirOut[0], dirOut[1], dirOut[2]));
        if (p > 0.0) Print("  [OK]"); else Print("  [FAIL]");

        Print("");
    }

    static void TestInjectJammerAndPassiveSample()
    {
        Print("[EMVoxelFieldTests] TestInjectJammerAndPassiveSample");
        EMVoxelField f = EMVoxelField.GetInstance();
        f.Clear();

        vector center = Vector(200.0, 0.0, 0.0);
        f.InjectJammer(center, 5.0, 120.0, 0x1, 2.0);
        Print(string.Format("  After InjectJammer: active cells = %1", f.GetActiveCellCount()));

        vector probe = center + Vector(5.0, 0.0, 0.0);
        vector mainDir; int mask;
        float p = EMPassiveSensor.SamplePowerAtPosition(probe, mainDir, mask);
        Print(string.Format("  Passive probe power=%.6e W, dir=(%.2f,%.2f,%.2f), mask=0x%X", p, mainDir[0], mainDir[1], mainDir[2], mask));
        if (p > 0.0) Print("  [OK]"); else Print("  [FAIL]");

        // Test detection against a small sensitivity threshold
        float sens = 0.001; // 1 mW
        float measured; vector d; int m;
        bool det = EMPassiveSensor.IsDetected(probe, sens, measured, d, m);
        Print(string.Format("  IsDetected(%.3f W) = %1 (measured=%.6e)", sens, det, measured));

        Print("");
    }

    static void TestManualSidelobeInjection()
    {
        Print("[EMVoxelFieldTests] TestManualSidelobeInjection");
        EMVoxelField f = EMVoxelField.GetInstance();
        f.Clear();

        vector origin = Vector(0,0,0);
        vector mainDir = Vector(1,0,0);
        float range = 1000.0;
        float Pt = 1000.0;

        // main lobe
        f.InjectAlongRay(origin, mainDir, range, Pt, 0x1, 1.0, 50.0, 1e-4);
        int baseCells = f.GetActiveCellCount();

        // simulate three sidelobes off-axis
        vector s1 = Vector(Math.Cos(10.0 * Math.DEG2RAD), 0, Math.Sin(10.0 * Math.DEG2RAD));
        vector s2 = Vector(Math.Cos(-10.0 * Math.DEG2RAD), 0, Math.Sin(-10.0 * Math.DEG2RAD));
        vector s3 = Vector(Math.Cos(20.0 * Math.DEG2RAD), 0, Math.Sin(20.0 * Math.DEG2RAD));

        f.InjectAlongRay(origin, s1, range, Pt * 0.01, 0x1, 1.0, 50.0, 1e-4);
        f.InjectAlongRay(origin, s2, range, Pt * 0.01, 0x1, 1.0, 50.0, 1e-4);
        f.InjectAlongRay(origin, s3, range, Pt * 0.005, 0x1, 1.0, 50.0, 1e-4);

        int afterCells = f.GetActiveCellCount();
        Print(string.Format("  Cells before sidelobes=%1, after=%2", baseCells, afterCells));
        if (afterCells > baseCells) Print("  [OK]"); else Print("  [FAIL]");

        Print("");
    }

    static void RunAllTests()
    {
        Print("========================================");
        Print("  EM Voxel Field Tests");
        Print("========================================");
        TestInjectAndRead();
        TestDecayAndExpiry();
        Print("========================================");
        Print("  Tests complete.");
        Print("========================================");
    }
}
