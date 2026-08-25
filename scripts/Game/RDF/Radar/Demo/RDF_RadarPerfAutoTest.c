// Automated scan / ballistics performance overhead regression.
// Runs SYNCHRONOUSLY via ScanOnce (same idea as ManualDemo.Probe) so Script
// Debugger remote execute is not blocked by paused CallLater ticks.
//
// Usage (Script Debugger, Play mode):
//   RDF_RadarPerfAutoTest.Start();
// Included in RDF_RadarAutoTestSuite.StartAll() as the last step.
class RDF_RadarPerfAutoTest
{
    protected static ref RDF_RadarPerfAutoTest s_Instance;
    protected static bool s_LastPass;

    // Ground vehicles avoid Mi-8 rotor damage CallLater (NULL root after delete).
    protected static const ResourceName LOAD_TARGET_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";

    protected static const int LOAD_TARGET_COUNT = 8;
    protected static const int LIGHT_SCANS = 10;
    protected static const int HEAVY_SCANS = 12;
    protected static const int LIGHT_WARMUP = 2;
    protected static const int HEAVY_WARMUP = 2;

    // Soft budgets — catch pathological regressions, not laptop FPS variance.
    // Ballistics includes 40 WLR solves; Workbench hosts often land ~100–200 ms.
    protected static const float PASS_BALLISTICS_MS = 250.0;
    protected static const float PASS_LIGHT_AVG_MS = 120.0;
    protected static const float PASS_HEAVY_AVG_MS = 350.0;
    protected static const float PASS_HEAVY_P95_MS = 600.0;
    protected static const int PASS_MIN_SCANS_LIGHT = 6;
    protected static const int PASS_MIN_SCANS_HEAVY = 8;

    protected bool m_Running;

    protected IEntity m_Subject;
    protected vector m_RadarOrigin;
    protected ref array<IEntity> m_LoadTargets;

    protected ref array<float> m_LightDurationsMs;
    protected ref array<float> m_HeavyDurationsMs;
    protected float m_BallisticsMs;
    protected float m_LightBatchAvgMs;
    protected float m_HeavyBatchAvgMs;
    protected string m_LastReuseLight;
    protected string m_LastReuseHeavy;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarPerfAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarPerfAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarPerfAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar PerfTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarPerfAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    static bool DidLastPass()
    {
        return s_LastPass;
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar PerfTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Perf"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar PerfTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Perf");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar PerfTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Perf");
            return;
        }

        m_PrevConfig = null;
        RDF_RadarSensor prevSensor = RDF_RadarAutoRunner.GetSensor();
        if (prevSensor)
            m_PrevConfig = prevSensor.GetSettings();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        if (!ChooseRadarOrigin())
        {
            Print("[RDF Radar PerfTest] failed to choose radar origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Perf");
            return;
        }

        m_Running = true;
        m_LoadTargets = new array<IEntity>();
        m_LightDurationsMs = new array<float>();
        m_HeavyDurationsMs = new array<float>();
        m_LastReuseLight = "";
        m_LastReuseHeavy = "";
        m_LightBatchAvgMs = 0.0;
        m_HeavyBatchAvgMs = 0.0;

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.SetDemoEnabled(false);

        Print("[RDF Radar PerfTest] begin (synchronous ScanOnce; debugger-safe)");

        m_BallisticsMs = RunBallisticsMicrobench();
        Print(string.Format(
            "[RDF Radar PerfTest] ballisticsMs=%1 origin=%2",
            (Math.Round(m_BallisticsMs * 10.0) * 0.1).ToString(),
            m_RadarOrigin.ToString()));

        ApplyLightConfig();
        Print("[RDF Radar PerfTest] phase LIGHT...");
        RunForcedScanBatch(LIGHT_WARMUP, LIGHT_SCANS, m_LightDurationsMs, true);
        RDF_RadarSensor sensor = GetSensor();
        if (sensor && sensor.GetScanner())
            m_LastReuseLight = sensor.GetScanner().GetScanReuseStatsShort();
        Print(string.Format(
            "[RDF Radar PerfTest] LIGHT n=%1 avgMs=%2 batchAvgMs=%3",
            m_LightDurationsMs.Count().ToString(),
            (Math.Round(ArrayAvg(m_LightDurationsMs) * 10.0) * 0.1).ToString(),
            (Math.Round(m_LightBatchAvgMs * 10.0) * 0.1).ToString()));

        SpawnLoadTargets();
        SeedLoadTargetsInRegistry();
        ApplyHeavyConfig();
        Print("[RDF Radar PerfTest] phase HEAVY loadTargets="
            + m_LoadTargets.Count().ToString());

        // First heavy dwell often loads SURF/DEM; isolate that from the average.
        Print("[RDF Radar PerfTest] HEAVY DEM warm scan...");
        float demWarmMs = ForceOneScanMs(0.0);
        Print(string.Format(
            "[RDF Radar PerfTest] HEAVY DEM warmMs=%1",
            (Math.Round(demWarmMs * 10.0) * 0.1).ToString()));

        RunForcedScanBatch(HEAVY_WARMUP, HEAVY_SCANS, m_HeavyDurationsMs, false);
        sensor = GetSensor();
        if (sensor && sensor.GetScanner())
            m_LastReuseHeavy = sensor.GetScanner().GetScanReuseStatsShort();
        Print(string.Format(
            "[RDF Radar PerfTest] HEAVY n=%1 avgMs=%2 p95Ms=%3 batchAvgMs=%4",
            m_HeavyDurationsMs.Count().ToString(),
            (Math.Round(ArrayAvg(m_HeavyDurationsMs) * 10.0) * 0.1).ToString(),
            (Math.Round(ArrayP95(m_HeavyDurationsMs) * 10.0) * 0.1).ToString(),
            (Math.Round(m_HeavyBatchAvgMs * 10.0) * 0.1).ToString()));

        FinalizeAndReport();
        StopInternal(true);
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Perf");
        ClearLoadTargets();

        // Honour the Suite restore contract: when restore=true (normal suite
        // completion or standalone stop), put Demo/HUD back how we found them
        // so the next step / user starts from a clean slate. The Suite's
        // FinishSuite disables Demo after all steps, so restoring here only
        // matters for standalone runs. When restore=false (abort), leave idle.
        if (restore)
        {
            RDF_RadarAutoRunner.SetDemoEnabled(m_PrevDemoEnabled);
            RDF_RadarAutoRunner.SetHudEnabled(m_PrevHudEnabled);
        }
        else
        {
            RDF_RadarAutoRunner.SetDemoEnabled(false);
            RDF_RadarAutoRunner.SetHudEnabled(false);
        }
        RDF_RadarAutoRunner.StopAutoRun();

        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            sensor.ResetSession();

        if (restore && m_PrevConfig)
            ApplySensorConfig(m_PrevConfig);

        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
        Print("[RDF Radar PerfTest] idle");
    }

    protected RDF_RadarSensor GetSensor()
    {
        return RDF_RadarAutoRunner.GetSensor();
    }

    protected void ApplySensorConfig(RDF_RadarSettings cfg)
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor || !cfg)
            return;
        sensor.SetForceLocalScan(true);
        sensor.Configure(cfg);
    }

    // Perf load: inject UAZ into the registry so HEAVY measures scan cost, not
    // discovery fairness (Register is otherwise reserved for emitters).
    protected void SeedLoadTargetsInRegistry()
    {
        if (!m_LoadTargets)
            return;

        int seeded = 0;
        for (int i = 0; i < m_LoadTargets.Count(); i++)
        {
            IEntity ent = m_LoadTargets.Get(i);
            if (!ent)
                continue;
            vector pos = ent.GetOrigin();
            RDF_RadarScatterer entry = RDF_RadarScattererRegistry.Register(
                ent, pos, false, 0.0);
            if (entry)
                seeded = seeded + 1;
        }
        Print("[RDF Radar PerfTest] seeded loadTargets=" + seeded.ToString());
    }

    protected float ForceOneScanMs(float worldTimeS)
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor || !m_Subject)
            return 0.0;

        float t = worldTimeS;
        if (t <= 0.0)
        {
            BaseWorld world = GetGame().GetWorld();
            if (world)
                t = world.GetWorldTime() * 0.001;
        }

        int wall0 = System.GetTickCount();
        sensor.SetForceLocalScan(true);
        sensor.ScanOnce(m_Subject, null, t);
        int wall1 = System.GetTickCount();
        float wallMs = wall1 - wall0;
        if (wallMs < 0.0)
            wallMs = 0.0;

        float sensorMs = sensor.GetLastScanDurationMs();
        if (sensorMs > wallMs)
            return sensorMs;
        return wallMs;
    }

    // Bypass UpdateInterval; record per-scan + batch wall duration.
    protected void RunForcedScanBatch(
        int warmupCount,
        int measureCount,
        notnull array<float> outDurations,
        bool lightBatch)
    {
        outDurations.Clear();
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor || !m_Subject)
            return;

        BaseWorld world = GetGame().GetWorld();
        float worldTimeS = 0.0;
        if (world)
            worldTimeS = world.GetWorldTime() * 0.001;

        int total = warmupCount + measureCount;
        int measureWall0 = 0;
        int measured = 0;
        for (int i = 0; i < total; i++)
        {
            // Advance sim clock slightly so trackers see distinct times.
            float t = worldTimeS + i * 0.2;
            if (i == warmupCount)
                measureWall0 = System.GetTickCount();

            sensor.SetForceLocalScan(true);
            sensor.ScanOnce(m_Subject, null, t);
            float ms = sensor.GetLastScanDurationMs();
            if (i >= warmupCount)
            {
                outDurations.Insert(ms);
                measured = measured + 1;
            }
        }

        float batchAvg = 0.0;
        if (measured > 0)
        {
            int measureWall1 = System.GetTickCount();
            float batchTotal = measureWall1 - measureWall0;
            if (batchTotal < 0.0)
                batchTotal = 0.0;
            batchAvg = batchTotal / measured;
        }

        if (lightBatch)
            m_LightBatchAvgMs = batchAvg;
        else
            m_HeavyBatchAvgMs = batchAvg;
    }

    protected bool ChooseRadarOrigin()
    {
        if (!m_Subject)
            return false;
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector center = mat[3];
        float surfaceY = world.GetSurfaceY(center[0], center[2]);
        m_RadarOrigin = Vector(center[0], surfaceY + 8.0, center[2]);
        return true;
    }

    protected void ApplyLightConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(64);
        cfg.m_Range = 1500.0;
        cfg.m_SectorHalfAngleDeg = 45.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_MaxLosTracesPerScan = 24;
        // Perf measures raw single-scan wall time; disable the cross-frame LOS
        // queue AND the adaptive governor so the recorded ms reflects the
        // configured per-scan budget (smoothing/adaptation validated by Stress /
        // Play instead).
        cfg.m_EnableLosFrameQueue = false;
        cfg.m_EnableAdaptiveBudget = false;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = false;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        cfg.m_FreshUpdateBudgetMin = 8;
        cfg.m_FreshUpdateBudgetMax = 16;
        cfg.StabilizeForRegression();
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void ApplyHeavyConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_Range = 2500.0;
        cfg.m_SectorHalfAngleDeg = 90.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_MaxLosTracesPerScan = 64;
        // Same as light: measure raw per-scan cost, not queue/adaptive-smoothed.
        cfg.m_EnableLosFrameQueue = false;
        cfg.m_EnableAdaptiveBudget = false;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        cfg.m_IncludeRadarEmitters = true;
        cfg.m_EnableDemClutter = true;
        cfg.m_EnableCfarGate = true;
        cfg.m_EnableMeasurementSynthesis = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        cfg.m_FreshUpdateBudgetMin = 24;
        cfg.m_FreshUpdateBudgetMax = 48;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_ScattererClassifyPerTick = 128;
        cfg.m_ScattererRefreshPerTick = 256;
        cfg.StabilizeForRegression();
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void SpawnLoadTargets()
    {
        ClearLoadTargets();
        if (!m_Subject)
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        Resource prefabRes = Resource.Load(LOAD_TARGET_PREFAB);
        if (!prefabRes)
        {
            Print("[RDF Radar PerfTest] failed to load load-target prefab.", LogLevel.ERROR);
            return;
        }

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector fwd = mat[2];
        float flatLen = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        vector flatFwd;
        if (flatLen < 0.001)
            flatFwd = Vector(0.0, 0.0, 1.0);
        else
            flatFwd = Vector(fwd[0] / flatLen, 0.0, fwd[2] / flatLen);
        vector right = Vector(flatFwd[2], 0.0, -flatFwd[0]);

        for (int i = 0; i < LOAD_TARGET_COUNT; i++)
        {
            float rangeM = 250.0 + i * 150.0;
            int lane = i - (i / 3) * 3;
            float lateral = (lane - 1) * 60.0;
            vector flatPos = Vector(
                m_RadarOrigin[0] + flatFwd[0] * rangeM + right[0] * lateral,
                m_RadarOrigin[1],
                m_RadarOrigin[2] + flatFwd[2] * rangeM + right[2] * lateral);
            float groundY = world.GetSurfaceY(flatPos[0], flatPos[2]);
            vector pos = Vector(flatPos[0], groundY + 0.5, flatPos[2]);

            EntitySpawnParams spawnParams = new EntitySpawnParams();
            Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
            spawnParams.Transform[3] = pos;

            IEntity ent = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
            if (!ent)
                continue;
            ent.SetOrigin(pos);
            m_LoadTargets.Insert(ent);
        }
    }

    protected void ClearLoadTargets()
    {
        if (!m_LoadTargets)
            return;

        // Snapshot then clear ownership so a late frame cannot double-delete.
        array<IEntity> doomed = new array<IEntity>();
        for (int i = 0; i < m_LoadTargets.Count(); i++)
        {
            IEntity ent = m_LoadTargets.Get(i);
            if (ent)
                doomed.Insert(ent);
        }
        m_LoadTargets.Clear();

        for (int j = 0; j < doomed.Count(); j++)
        {
            IEntity ent = doomed.Get(j);
            if (!ent)
                continue;
            RDF_RadarScattererRegistry.Unregister(ent);
            SCR_EntityHelper.DeleteEntityAndChildren(ent);
        }
    }

    protected float RunBallisticsMicrobench()
    {
        array<vector> positions = new array<vector>();
        array<float> times = new array<float>();
        float t = 0.0;
        float x = 0.0;
        float y = 2.0;
        float vx = 160.0;
        float vy = 120.0;
        for (int i = 0; i < 16; i++)
        {
            positions.Insert(Vector(x, y, 0.0));
            times.Insert(t);
            t = t + 0.25;
            x = x + vx * 0.25;
            y = y + vy * 0.25 + 0.5 * RDF_RadarBallistics.GRAVITY_M_S2 * 0.25 * 0.25;
            vy = vy + RDF_RadarBallistics.GRAVITY_M_S2 * 0.25;
        }

        RDF_RadarGlobalWind wind = new RDF_RadarGlobalWind();
        wind.m_SpeedMs = 0.0;
        int wall0 = System.GetTickCount();
        int reps = 40;
        for (int r = 0; r < reps; r++)
        {
            RDF_RadarWlrFix fix = RDF_RadarBallistics.SolveLaunchAndImpactFromHistory(
                positions,
                times,
                0.0,
                RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE,
                wind,
                RDF_RadarBallistics.GRAVITY_M_S2,
                5,
                1.0,
                80.0,
                20);
            if (!fix)
                continue;
        }
        int wall1 = System.GetTickCount();
        float total = wall1 - wall0;
        if (total < 0.0)
            total = 0.0;
        return total;
    }

    protected float ArrayAvg(array<float> values)
    {
        if (!values || values.Count() == 0)
            return 0.0;
        float sum = 0.0;
        for (int i = 0; i < values.Count(); i++)
            sum = sum + values.Get(i);
        return sum / values.Count();
    }

    protected float ArrayMax(array<float> values)
    {
        if (!values || values.Count() == 0)
            return 0.0;
        float best = values.Get(0);
        for (int i = 1; i < values.Count(); i++)
        {
            float v = values.Get(i);
            if (v > best)
                best = v;
        }
        return best;
    }

    protected float ArrayP95(array<float> values)
    {
        return RDF_RadarTestUtil.ArrayP95(values);
    }

    protected string BoolLabel(bool ok)
    {
        return RDF_RadarTestUtil.BoolLabel(ok);
    }

    protected void FinalizeAndReport()
    {
        float lightAvg = ArrayAvg(m_LightDurationsMs);
        float lightMax = ArrayMax(m_LightDurationsMs);
        float heavyAvg = ArrayAvg(m_HeavyDurationsMs);
        float heavyMax = ArrayMax(m_HeavyDurationsMs);
        float heavyP95 = ArrayP95(m_HeavyDurationsMs);
        int lightN = 0;
        int heavyN = 0;
        if (m_LightDurationsMs)
            lightN = m_LightDurationsMs.Count();
        if (m_HeavyDurationsMs)
            heavyN = m_HeavyDurationsMs.Count();

        bool passBallistics = m_BallisticsMs <= PASS_BALLISTICS_MS;
        bool passLightN = lightN >= PASS_MIN_SCANS_LIGHT;
        bool passHeavyN = heavyN >= PASS_MIN_SCANS_HEAVY;
        bool passLightAvg = lightAvg <= PASS_LIGHT_AVG_MS;
        bool passHeavyAvg = heavyAvg <= PASS_HEAVY_AVG_MS;
        bool passHeavyP95 = heavyP95 <= PASS_HEAVY_P95_MS;
        bool allPass = passBallistics && passLightN && passHeavyN
            && passLightAvg && passHeavyAvg && passHeavyP95;
        s_LastPass = allPass;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Perf AutoTest");
        lines.Insert("mode synchronous_ScanOnce");
        lines.Insert("ballistics_reps40_ms " + m_BallisticsMs.ToString());
        lines.Insert("light_n " + lightN.ToString());
        lines.Insert("light_avg_ms " + lightAvg.ToString());
        lines.Insert("light_batch_avg_ms " + m_LightBatchAvgMs.ToString());
        lines.Insert("light_max_ms " + lightMax.ToString());
        lines.Insert("light_reuse " + m_LastReuseLight);
        lines.Insert("heavy_n " + heavyN.ToString());
        lines.Insert("heavy_avg_ms " + heavyAvg.ToString());
        lines.Insert("heavy_batch_avg_ms " + m_HeavyBatchAvgMs.ToString());
        lines.Insert("heavy_p95_ms " + heavyP95.ToString());
        lines.Insert("heavy_max_ms " + heavyMax.ToString());
        lines.Insert("heavy_reuse " + m_LastReuseHeavy);
        lines.Insert("load_targets " + LOAD_TARGET_COUNT.ToString());
        lines.Insert("thresholds_ms ballistics<=" + PASS_BALLISTICS_MS.ToString()
            + " lightAvg<=" + PASS_LIGHT_AVG_MS.ToString()
            + " heavyAvg<=" + PASS_HEAVY_AVG_MS.ToString()
            + " heavyP95<=" + PASS_HEAVY_P95_MS.ToString());
        lines.Insert("  ballistics " + BoolLabel(passBallistics));
        lines.Insert("  light_scans " + BoolLabel(passLightN));
        lines.Insert("  heavy_scans " + BoolLabel(passHeavyN));
        lines.Insert("  light_avg " + BoolLabel(passLightAvg));
        lines.Insert("  heavy_avg " + BoolLabel(passHeavyAvg));
        lines.Insert("  heavy_p95 " + BoolLabel(passHeavyP95));
        if (allPass)
            lines.Insert("result=PASS");
        else
            lines.Insert("result=FAIL");

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_perf_autotest_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar PerfTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar PerfTest] ballistics=%1ms lightAvg=%2ms (n=%3) heavyAvg=%4ms p95=%5ms (n=%6)",
            (Math.Round(m_BallisticsMs * 10.0) * 0.1).ToString(),
            (Math.Round(lightAvg * 10.0) * 0.1).ToString(),
            lightN.ToString(),
            (Math.Round(heavyAvg * 10.0) * 0.1).ToString(),
            (Math.Round(heavyP95 * 10.0) * 0.1).ToString(),
            heavyN.ToString()));
    }
}
