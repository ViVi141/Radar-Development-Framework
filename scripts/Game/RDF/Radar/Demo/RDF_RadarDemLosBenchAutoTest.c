// In-game DEM-LOS A/B benchmark (synchronous ScanOnce — debugger-safe).
// Compares scan wall time with m_EnableDemLosPrecheck ON vs OFF after DEM RAM warm.
//
// Script Debugger (Play):
//   RDF_RadarDemLosBenchAutoTest.Start();
class RDF_RadarDemLosBenchAutoTest
{
    protected static ref RDF_RadarDemLosBenchAutoTest s_Instance;
    protected static bool s_LastPass;

    protected static const ResourceName LOAD_TARGET_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
    protected static const int LOAD_TARGET_COUNT = 12;
    protected static const int WARMUP_SCANS = 2;
    protected static const int MEASURE_SCANS = 10;
    // Soft: precheck ON should not be slower than OFF by more than this margin.
    protected static const float PASS_ON_NOT_SLOWER_MS = 40.0;

    protected bool m_Running;
    protected IEntity m_Subject;
    protected vector m_RadarOrigin;
    protected ref array<IEntity> m_LoadTargets;

    protected float m_DemWarmMs;
    protected float m_AvgOnMs;
    protected float m_AvgOffMs;
    protected float m_BatchOnMs;
    protected float m_BatchOffMs;
    protected string m_ReuseOn;
    protected string m_ReuseOff;
    protected string m_DemStatus;
    protected bool m_PrevForceLocal;

    static RDF_RadarDemLosBenchAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarDemLosBenchAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarDemLosBenchAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal();
        Print("[RDF DEM-LOS Bench] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarDemLosBenchAutoTest inst = GetInstance();
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
            Print("[RDF DEM-LOS Bench] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("DemLosBench"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF DEM-LOS Bench] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("DemLosBench");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF DEM-LOS Bench] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("DemLosBench");
            return;
        }

        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();
        m_Running = true;
        m_LoadTargets = new array<IEntity>();
        m_ReuseOn = "";
        m_ReuseOff = "";
        m_DemStatus = "DEM OFF";

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.SetDemoEnabled(false);

        if (!ChooseRadarOrigin())
        {
            Print("[RDF DEM-LOS Bench] origin failed.", LogLevel.ERROR);
            StopInternal();
            return;
        }

        Print("[RDF DEM-LOS Bench] begin (A/B DemLosPrecheck ON vs OFF)");

        // Force shared SURF+HEIGHT resident before measuring.
        if (RDF_DemRuntimeCache.IsAsyncWarmPreloadRunning())
            RDF_DemRuntimeCache.FlushAsyncWarmPreload();
        if (!RDF_DemRuntimeCache.IsSharedSurfReady())
        {
            RDF_DemRuntimeCache.StartAsyncWarmPreload();
            RDF_DemRuntimeCache.FlushAsyncWarmPreload();
        }

        SpawnLoadTargets();
        SeedLoadTargetsInRegistry();

        ApplyBenchConfig(true);
        WarmDemViaSensor();
        CaptureDemStatus();

        Print("[RDF DEM-LOS Bench] phase ON demStatus=" + m_DemStatus);
        RunBatch(true);
        Print(string.Format(
            "[RDF DEM-LOS Bench] ON avgMs=%1 batchAvgMs=%2 stats=%3",
            (Math.Round(m_AvgOnMs * 10.0) * 0.1).ToString(),
            (Math.Round(m_BatchOnMs * 10.0) * 0.1).ToString(),
            m_ReuseOn));

        ApplyBenchConfig(false);
        Print("[RDF DEM-LOS Bench] phase OFF");
        RunBatch(false);
        Print(string.Format(
            "[RDF DEM-LOS Bench] OFF avgMs=%1 batchAvgMs=%2 stats=%3",
            (Math.Round(m_AvgOffMs * 10.0) * 0.1).ToString(),
            (Math.Round(m_BatchOffMs * 10.0) * 0.1).ToString(),
            m_ReuseOff));

        FinalizeAndReport();
        StopInternal();
    }

    protected void StopInternal()
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("DemLosBench");
        ClearLoadTargets();
        RDF_RadarAutoRunner.SetDemoEnabled(false);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.StopAutoRun();
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
            sensor.ResetSession();
        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
        Print("[RDF DEM-LOS Bench] idle");
    }

    protected void WarmDemViaSensor()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor || !m_Subject)
            return;

        int wall0 = System.GetTickCount();
        sensor.SetForceLocalScan(true);
        sensor.ScanOnce(m_Subject, null, 0.0);
        // EnsurePreloaded may still be async; flush again after first scan kicked it.
        if (RDF_DemRuntimeCache.IsAsyncWarmPreloadRunning())
            RDF_DemRuntimeCache.FlushAsyncWarmPreload();
        sensor.ScanOnce(m_Subject, null, 0.2);
        int wall1 = System.GetTickCount();
        m_DemWarmMs = wall1 - wall0;
        if (m_DemWarmMs < 0.0)
            m_DemWarmMs = 0.0;
        Print(string.Format(
            "[RDF DEM-LOS Bench] DEM warmMs=%1",
            (Math.Round(m_DemWarmMs * 10.0) * 0.1).ToString()));
    }

    protected void CaptureDemStatus()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor || !sensor.GetScanner())
            return;
        RDF_DemRuntimeCache dem = sensor.GetScanner().GetDemCache();
        if (!dem)
            return;
        dem.EnsurePreloaded();
        m_DemStatus = dem.GetStatusShort() + " " + dem.GetStatsLine();
    }

    protected void ApplyBenchConfig(bool demLosPrecheck)
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_Range = 2500.0;
        cfg.m_SectorHalfAngleDeg = 90.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_MaxLosTracesPerScan = 64;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        cfg.m_IncludeRadarEmitters = true;
        cfg.m_EnableDemClutter = true;
        cfg.m_EnableCfarGate = true;
        cfg.m_EnableMeasurementSynthesis = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_EnableDemLosPrecheck = demLosPrecheck;
        cfg.m_EnableNlosMultipath = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        cfg.m_FreshUpdateBudgetMin = 24;
        cfg.m_FreshUpdateBudgetMax = 48;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_ScattererClassifyPerTick = 128;
        cfg.m_ScattererRefreshPerTick = 256;
        cfg.StabilizeForRegression();
        // Stabilize turns off some fidelity; keep DEM LOS precheck as requested.
        cfg.m_EnableDemLosPrecheck = demLosPrecheck;
        cfg.Validate();

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;
        sensor.SetForceLocalScan(true);
        sensor.Configure(cfg);
    }

    protected void RunBatch(bool precheckOn)
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor || !m_Subject)
            return;

        BaseWorld world = GetGame().GetWorld();
        float worldTimeS = 0.0;
        if (world)
            worldTimeS = world.GetWorldTime() * 0.001;

        array<float> durations = new array<float>();
        int total = WARMUP_SCANS + MEASURE_SCANS;
        int measureWall0 = 0;
        int measured = 0;
        for (int i = 0; i < total; i++)
        {
            float t = worldTimeS + i * 0.2;
            if (i == WARMUP_SCANS)
                measureWall0 = System.GetTickCount();

            sensor.SetForceLocalScan(true);
            sensor.ScanOnce(m_Subject, null, t);
            float ms = sensor.GetLastScanDurationMs();
            if (i >= WARMUP_SCANS)
            {
                durations.Insert(ms);
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

        float avg = 0.0;
        if (durations.Count() > 0)
        {
            float sum = 0.0;
            for (int j = 0; j < durations.Count(); j++)
                sum = sum + durations.Get(j);
            avg = sum / durations.Count();
        }

        string reuse = "";
        if (sensor.GetScanner())
            reuse = sensor.GetScanner().GetScanReuseStatsShort();

        if (precheckOn)
        {
            m_AvgOnMs = avg;
            m_BatchOnMs = batchAvg;
            m_ReuseOn = reuse;
        }
        else
        {
            m_AvgOffMs = avg;
            m_BatchOffMs = batchAvg;
            m_ReuseOff = reuse;
        }
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
        Print("[RDF DEM-LOS Bench] seeded loadTargets=" + seeded.ToString());
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
            Print("[RDF DEM-LOS Bench] prefab load failed.", LogLevel.ERROR);
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
            float rangeM = 200.0 + i * 120.0;
            int lane = i - (i / 3) * 3;
            float lateral = (lane - 1) * 70.0;
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

    protected void FinalizeAndReport()
    {
        float delta = m_AvgOffMs - m_AvgOnMs;
        bool pass = m_AvgOnMs <= (m_AvgOffMs + PASS_ON_NOT_SLOWER_MS);
        s_LastPass = pass;

        array<string> lines = new array<string>();
        lines.Insert("RDF DEM-LOS Bench AutoTest");
        lines.Insert("mode synchronous_ScanOnce A/B");
        lines.Insert("dem_status " + m_DemStatus);
        lines.Insert("dem_warm_ms " + m_DemWarmMs.ToString());
        lines.Insert("load_targets " + LOAD_TARGET_COUNT.ToString());
        lines.Insert("measure_scans " + MEASURE_SCANS.ToString());
        lines.Insert("precheck_ON_avg_ms " + m_AvgOnMs.ToString());
        lines.Insert("precheck_ON_batch_avg_ms " + m_BatchOnMs.ToString());
        lines.Insert("precheck_ON_stats " + m_ReuseOn);
        lines.Insert("precheck_OFF_avg_ms " + m_AvgOffMs.ToString());
        lines.Insert("precheck_OFF_batch_avg_ms " + m_BatchOffMs.ToString());
        lines.Insert("precheck_OFF_stats " + m_ReuseOff);
        lines.Insert("delta_off_minus_on_ms " + delta.ToString());
        if (pass)
            lines.Insert("result=PASS");
        else
            lines.Insert("result=FAIL");

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/dem_los_bench_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        string label = "FAIL";
        if (pass)
            label = "PASS";
        Print("[RDF DEM-LOS Bench] " + label + "  report=" + reportPath);
        Print(string.Format(
            "[RDF DEM-LOS Bench] ON=%1ms OFF=%2ms saved≈%3ms  ON[%4] OFF[%5]",
            (Math.Round(m_AvgOnMs * 10.0) * 0.1).ToString(),
            (Math.Round(m_AvgOffMs * 10.0) * 0.1).ToString(),
            (Math.Round(delta * 10.0) * 0.1).ToString(),
            m_ReuseOn,
            m_ReuseOff));
    }
}
