// In-game DEM-LOS benchmark (synchronous ScanOnce — debugger-safe).
// Phases:
//   1) DemLos ON  + flush caches each scan
//   2) DemLos ON  + keep LOS/reuse caches (gameplay-like)
//   3) DemLos OFF + flush
//   4) DemLos OFF + keep caches
//
// Script Debugger (Play):
//   RDF_RadarDemLosBenchAutoTest.Start();
class RDF_RadarDemLosBenchPhase
{
    float m_AvgMs;
    float m_BatchMs;
    int m_SumDemBlk;
    int m_SumTrace;
    int m_SumLosHit;
    int m_SumReuse;
    string m_LastStats;
}

class RDF_RadarDemLosBenchAutoTest
{
    protected static ref RDF_RadarDemLosBenchAutoTest s_Instance;
    protected static bool s_LastPass;

    protected static const ResourceName LOAD_TARGET_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
    protected static const int LOAD_TARGET_COUNT = 16;
    protected static const int NLOS_TARGET_GOAL = 8;
    protected static const int WARMUP_SCANS = 2;
    protected static const int MEASURE_SCANS = 12;
    protected static const float PASS_ON_NOT_SLOWER_MS = 40.0;

    protected bool m_Running;
    protected IEntity m_Subject;
    protected vector m_RadarOrigin;
    protected ref array<IEntity> m_LoadTargets;

    protected float m_DemWarmMs;
    protected int m_NlosSpawned;
    protected string m_DemStatus;
    protected bool m_PrevForceLocal;

    protected ref RDF_RadarDemLosBenchPhase m_OnFlush;
    protected ref RDF_RadarDemLosBenchPhase m_OnCache;
    protected ref RDF_RadarDemLosBenchPhase m_OffFlush;
    protected ref RDF_RadarDemLosBenchPhase m_OffCache;

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
        m_DemStatus = "DEM OFF";
        m_NlosSpawned = 0;
        m_OnFlush = new RDF_RadarDemLosBenchPhase();
        m_OnCache = new RDF_RadarDemLosBenchPhase();
        m_OffFlush = new RDF_RadarDemLosBenchPhase();
        m_OffCache = new RDF_RadarDemLosBenchPhase();

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.SetDemoEnabled(false);

        if (!ChooseRadarOrigin())
        {
            Print("[RDF DEM-LOS Bench] origin failed.", LogLevel.ERROR);
            StopInternal();
            return;
        }

        Print("[RDF DEM-LOS Bench] begin (DemLos ON/OFF x flush/cache)");

        if (RDF_DemRuntimeCache.IsAsyncWarmPreloadRunning())
            RDF_DemRuntimeCache.FlushAsyncWarmPreload();
        if (!RDF_DemRuntimeCache.IsSharedSurfReady())
        {
            RDF_DemRuntimeCache.StartAsyncWarmPreload();
            RDF_DemRuntimeCache.FlushAsyncWarmPreload();
        }

        ApplyBenchConfig(true, false);
        WarmDemViaSensor();
        CaptureDemStatus();

        SpawnLoadTargets();
        SeedLoadTargetsInRegistry();

        Print("[RDF DEM-LOS Bench] demStatus=" + m_DemStatus
            + " nlos=" + m_NlosSpawned.ToString());

        RunNamedPhase("ON+flush", true, false, m_OnFlush);
        RunNamedPhase("ON+cache", true, true, m_OnCache);
        RunNamedPhase("OFF+flush", false, false, m_OffFlush);
        RunNamedPhase("OFF+cache", false, true, m_OffCache);

        FinalizeAndReport();
        StopInternal();
    }

    protected void RunNamedPhase(
        string label,
        bool demLosPrecheck,
        bool allowCache,
        RDF_RadarDemLosBenchPhase outPhase)
    {
        string demLabel = "0";
        if (demLosPrecheck)
            demLabel = "1";
        string cacheLabel = "flush";
        if (allowCache)
            cacheLabel = "cache";
        Print("[RDF DEM-LOS Bench] phase " + label
            + " (DemLos=" + demLabel
            + " " + cacheLabel + ")");
        ApplyBenchConfig(demLosPrecheck, allowCache);
        RunBatch(allowCache, outPhase);
        PrintPhase(label, outPhase);
    }

    protected void PrintPhase(string label, RDF_RadarDemLosBenchPhase phase)
    {
        if (!phase)
            return;
        Print(string.Format(
            "[RDF DEM-LOS Bench] %1 avgMs=%2 batch=%3 demBlk=%4 trace=%5 losHit=%6 reuse=%7 last=%8",
            label,
            (Math.Round(phase.m_AvgMs * 10.0) * 0.1).ToString(),
            (Math.Round(phase.m_BatchMs * 10.0) * 0.1).ToString(),
            phase.m_SumDemBlk.ToString(),
            phase.m_SumTrace.ToString(),
            phase.m_SumLosHit.ToString(),
            phase.m_SumReuse.ToString(),
            phase.m_LastStats));
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
        if (sensor.GetScanner())
            sensor.GetScanner().ClearScanOptimizationCaches();
        sensor.ScanOnce(m_Subject, null, 0.0);
        if (RDF_DemRuntimeCache.IsAsyncWarmPreloadRunning())
            RDF_DemRuntimeCache.FlushAsyncWarmPreload();
        if (sensor.GetScanner())
            sensor.GetScanner().ClearScanOptimizationCaches();
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

    // allowCache=false: force full LOS each scan (flush + short TTL).
    // allowCache=true: gameplay-like LOS/reuse ages (do not flush between scans).
    protected void ApplyBenchConfig(bool demLosPrecheck, bool allowCache)
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_Range = 2800.0;
        cfg.m_SectorHalfAngleDeg = 90.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_MaxLosTracesPerScan = 64;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_EnableDemLosPrecheck = demLosPrecheck;
        cfg.m_EnableNlosMultipath = true;
        cfg.m_DemLosPrecheckSamples = 12;
        cfg.m_KnifeEdgeClearanceSlackM = 1.0;
        cfg.m_OriginOffset = Vector(0.0, 4.0, 0.0);
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_ScattererClassifyPerTick = 128;
        cfg.m_ScattererRefreshPerTick = 256;

        if (allowCache)
        {
            cfg.m_FreshUpdateBudgetMin = 24;
            cfg.m_FreshUpdateBudgetMax = 48;
            cfg.m_PriorityBand1IntervalS = 0.10;
            cfg.m_PriorityBand2IntervalS = 0.30;
            cfg.m_LosCacheMaxAgeS = 0.25;
            cfg.m_TargetReuseMaxAgeS = 0.60;
            cfg.m_PhysicalReuseMaxAgeS = 0.20;
        }
        else
        {
            cfg.m_FreshUpdateBudgetMin = 64;
            cfg.m_FreshUpdateBudgetMax = 128;
            cfg.m_PriorityBand1IntervalS = 0.0;
            cfg.m_PriorityBand2IntervalS = 0.0;
            cfg.m_LosCacheMaxAgeS = 0.05;
            cfg.m_TargetReuseMaxAgeS = 0.05;
            cfg.m_PhysicalReuseMaxAgeS = 0.05;
        }

        cfg.StabilizeForRegression();
        cfg.m_EnableDemLosPrecheck = demLosPrecheck;
        cfg.Validate();

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;
        sensor.SetForceLocalScan(true);
        sensor.Configure(cfg);
        if (sensor.GetScanner())
            sensor.GetScanner().ClearScanOptimizationCaches();
    }

    protected void RunBatch(bool allowCache, RDF_RadarDemLosBenchPhase outPhase)
    {
        if (!outPhase)
            return;

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
        int sumDemBlk = 0;
        int sumTrace = 0;
        int sumLosHit = 0;
        int sumReuse = 0;
        string lastReuse = "";

        // Cache phases: one clear then warm fills the cache; measure keeps it.
        if (sensor.GetScanner())
            sensor.GetScanner().ClearScanOptimizationCaches();

        for (int i = 0; i < total; i++)
        {
            float t = worldTimeS + i * 0.2;
            if (i == WARMUP_SCANS)
                measureWall0 = System.GetTickCount();

            if (!allowCache)
            {
                if (sensor.GetScanner())
                    sensor.GetScanner().ClearScanOptimizationCaches();
            }

            sensor.SetForceLocalScan(true);
            sensor.ScanOnce(m_Subject, null, t);
            float ms = sensor.GetLastScanDurationMs();
            if (i >= WARMUP_SCANS)
            {
                durations.Insert(ms);
                measured = measured + 1;
                if (sensor.GetScanner())
                {
                    sumDemBlk = sumDemBlk + sensor.GetScanner().GetLastDemLosBlocks();
                    sumTrace = sumTrace + sensor.GetScanner().GetLastTraceMoves();
                    sumLosHit = sumLosHit + sensor.GetScanner().GetLastLosCacheHits();
                    sumReuse = sumReuse + sensor.GetScanner().GetLastReuseHits();
                    lastReuse = sensor.GetScanner().GetScanReuseStatsShort();
                }
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

        outPhase.m_AvgMs = avg;
        outPhase.m_BatchMs = batchAvg;
        outPhase.m_SumDemBlk = sumDemBlk;
        outPhase.m_SumTrace = sumTrace;
        outPhase.m_SumLosHit = sumLosHit;
        outPhase.m_SumReuse = sumReuse;
        outPhase.m_LastStats = lastReuse;
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
        m_RadarOrigin = Vector(center[0], surfaceY + 3.0, center[2]);
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
        Print("[RDF DEM-LOS Bench] seeded loadTargets=" + seeded.ToString()
            + " nlos=" + m_NlosSpawned.ToString());
    }

    protected void SpawnLoadTargets()
    {
        ClearLoadTargets();
        m_NlosSpawned = 0;
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

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        RDF_DemRuntimeCache dem = null;
        RDF_RadarSettings demSettings = null;
        if (sensor && sensor.GetScanner())
        {
            dem = sensor.GetScanner().GetDemCache();
            demSettings = sensor.GetSettings();
        }

        int spawned = 0;
        int nlosWanted = NLOS_TARGET_GOAL;
        if (nlosWanted > LOAD_TARGET_COUNT)
            nlosWanted = LOAD_TARGET_COUNT;

        for (int tryN = 0; tryN < 80; tryN++)
        {
            if (m_NlosSpawned >= nlosWanted)
                break;
            if (spawned >= LOAD_TARGET_COUNT)
                break;

            int rangeStep = tryN - (tryN / 12) * 12;
            float rangeM = 350.0 + rangeStep * 90.0;
            int lane = tryN - (tryN / 5) * 5;
            float lateral = (lane - 2) * 95.0;
            vector flatPos = Vector(
                m_RadarOrigin[0] + flatFwd[0] * rangeM + right[0] * lateral,
                m_RadarOrigin[1],
                m_RadarOrigin[2] + flatFwd[2] * rangeM + right[2] * lateral);
            float groundY = SampleGroundY(world, dem, flatPos[0], flatPos[2]);
            vector pos = Vector(flatPos[0], groundY + 0.5, flatPos[2]);

            if (!IsDemBlocked(dem, demSettings, m_RadarOrigin, pos))
                continue;
            if (!SpawnOne(prefabRes, world, pos))
                continue;
            m_NlosSpawned = m_NlosSpawned + 1;
            spawned = spawned + 1;
        }

        int fill = 0;
        while (spawned < LOAD_TARGET_COUNT && fill < 64)
        {
            float rangeM = 200.0 + fill * 100.0;
            int lane = fill - (fill / 3) * 3;
            float lateral = (lane - 1) * 70.0;
            vector flatPos = Vector(
                m_RadarOrigin[0] + flatFwd[0] * rangeM + right[0] * lateral,
                m_RadarOrigin[1],
                m_RadarOrigin[2] + flatFwd[2] * rangeM + right[2] * lateral);
            float groundY = SampleGroundY(world, dem, flatPos[0], flatPos[2]);
            vector pos = Vector(flatPos[0], groundY + 0.5, flatPos[2]);
            if (SpawnOne(prefabRes, world, pos))
                spawned = spawned + 1;
            fill = fill + 1;
        }
    }

    protected float SampleGroundY(BaseWorld world, RDF_DemRuntimeCache dem, float x, float z)
    {
        if (dem)
        {
            RDF_DemRuntimeCellSample sample;
            if (dem.TrySampleAt(x, z, sample))
            {
                if (sample && sample.m_Valid)
                    return sample.m_TerrainY;
            }
        }
        if (world)
            return world.GetSurfaceY(x, z);
        return 0.0;
    }

    protected bool IsDemBlocked(
        RDF_DemRuntimeCache dem,
        RDF_RadarSettings settings,
        vector origin,
        vector losEnd)
    {
        if (!dem || !settings)
            return false;
        if (!dem.IsReady())
            return false;
        float hitFraction = 1.0;
        return RDF_RadarScanGeometry.TryDemTerrainBlock(
            origin, losEnd, dem, settings, hitFraction);
    }

    protected bool SpawnOne(Resource prefabRes, BaseWorld world, vector pos)
    {
        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = pos;

        IEntity ent = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!ent)
            return false;
        ent.SetOrigin(pos);
        m_LoadTargets.Insert(ent);
        return true;
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

    protected void AppendPhaseLines(
        array<string> lines,
        string prefix,
        RDF_RadarDemLosBenchPhase phase)
    {
        if (!lines || !phase)
            return;
        lines.Insert(prefix + "_avg_ms " + phase.m_AvgMs.ToString());
        lines.Insert(prefix + "_batch_avg_ms " + phase.m_BatchMs.ToString());
        lines.Insert(prefix + "_demBlk_sum " + phase.m_SumDemBlk.ToString());
        lines.Insert(prefix + "_trace_sum " + phase.m_SumTrace.ToString());
        lines.Insert(prefix + "_losHit_sum " + phase.m_SumLosHit.ToString());
        lines.Insert(prefix + "_reuse_sum " + phase.m_SumReuse.ToString());
        lines.Insert(prefix + "_last_stats " + phase.m_LastStats);
    }

    protected void FinalizeAndReport()
    {
        bool passPath = false;
        if (m_OnFlush && m_OffFlush)
        {
            passPath = m_OffFlush.m_SumTrace > 0;
            if (m_OnFlush.m_SumDemBlk <= 0 && m_OnFlush.m_SumTrace <= 0)
                passPath = false;
            if (m_OffFlush.m_SumDemBlk > 0)
                passPath = false;
        }

        bool passTiming = true;
        if (m_OnFlush && m_OffFlush)
        {
            if (m_OnFlush.m_AvgMs > (m_OffFlush.m_AvgMs + PASS_ON_NOT_SLOWER_MS))
                passTiming = false;
        }

        bool pass = passTiming;
        if (!passPath)
            pass = false;
        s_LastPass = pass;

        float cacheSaveOnMs = 0.0;
        float cacheSaveOffMs = 0.0;
        if (m_OnFlush && m_OnCache)
            cacheSaveOnMs = m_OnFlush.m_AvgMs - m_OnCache.m_AvgMs;
        if (m_OffFlush && m_OffCache)
            cacheSaveOffMs = m_OffFlush.m_AvgMs - m_OffCache.m_AvgMs;

        array<string> lines = new array<string>();
        lines.Insert("RDF DEM-LOS Bench AutoTest");
        lines.Insert("mode synchronous_ScanOnce DemLos_x_cache_matrix");
        lines.Insert("dem_status " + m_DemStatus);
        lines.Insert("dem_warm_ms " + m_DemWarmMs.ToString());
        lines.Insert("load_targets " + LOAD_TARGET_COUNT.ToString());
        lines.Insert("nlos_spawned " + m_NlosSpawned.ToString());
        lines.Insert("measure_scans " + MEASURE_SCANS.ToString());
        AppendPhaseLines(lines, "ON_flush", m_OnFlush);
        AppendPhaseLines(lines, "ON_cache", m_OnCache);
        AppendPhaseLines(lines, "OFF_flush", m_OffFlush);
        AppendPhaseLines(lines, "OFF_cache", m_OffCache);
        lines.Insert("cache_save_ON_ms " + cacheSaveOnMs.ToString());
        lines.Insert("cache_save_OFF_ms " + cacheSaveOffMs.ToString());
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
        if (m_OnFlush && m_OnCache && m_OffFlush && m_OffCache)
        {
            Print(string.Format(
                "[RDF DEM-LOS Bench] ON flush=%1ms cache=%2ms (save≈%3ms)  OFF flush=%4ms cache=%5ms (save≈%6ms)",
                (Math.Round(m_OnFlush.m_AvgMs * 10.0) * 0.1).ToString(),
                (Math.Round(m_OnCache.m_AvgMs * 10.0) * 0.1).ToString(),
                (Math.Round(cacheSaveOnMs * 10.0) * 0.1).ToString(),
                (Math.Round(m_OffFlush.m_AvgMs * 10.0) * 0.1).ToString(),
                (Math.Round(m_OffCache.m_AvgMs * 10.0) * 0.1).ToString(),
                (Math.Round(cacheSaveOffMs * 10.0) * 0.1).ToString()));
            Print(string.Format(
                "[RDF DEM-LOS Bench] trace ON flush=%1 cache=%2 | OFF flush=%3 cache=%4 | losHit ON cache=%5 OFF cache=%6",
                m_OnFlush.m_SumTrace.ToString(),
                m_OnCache.m_SumTrace.ToString(),
                m_OffFlush.m_SumTrace.ToString(),
                m_OffCache.m_SumTrace.ToString(),
                m_OnCache.m_SumLosHit.ToString(),
                m_OffCache.m_SumLosHit.ToString()));
        }
    }
}
