// Performance stress AutoTest: heavy AutoRunner + HUD path.
// More vehicles, full DEM clutter, wide search, aggressive Origin wander,
// and high discovery/fresh budgets. Measures scanMs / RadarTick wall time
// (including HUD). Ground targets are Registry-seeded so the scan path is
// loaded immediately (this is a soak, not a discovery fairness test).
//
// Standalone only — not in StartAll (too long / noisy for the logic suite).
//
// Usage (Script Debugger, Play mode):
//   RDF_RadarStressAutoTest.Start();
class RDF_RadarStressAutoTest
{
    protected static ref RDF_RadarStressAutoTest s_Instance;
    protected static bool s_TickRegistered;

    protected static const ResourceName GROUND_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
    protected static const ResourceName AIR_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";

    protected static const int GROUND_COUNT = 24;
    protected static const int AIR_COUNT = 3;
    protected static const float DURATION_S = 60.0;
    protected static const float WARMUP_S = 10.0;
    protected static const float ORIGIN_WANDER_RADIUS_M = 520.0;
    protected static const float ORIGIN_WANDER_RATE_RAD_S = 0.22;
    protected static const float AIR_ORBIT_RADIUS_M = 480.0;
    protected static const float AIR_ORBIT_ALT_M = 160.0;
    protected static const float AIR_ORBIT_RATE_RAD_S = 0.20;

    // Soft ceilings: catch freezes / pathological regressions, not laptop FPS.
    protected static const float PASS_TICK_AVG_MS = 25.0;
    protected static const float PASS_TICK_P95_MS = 80.0;
    protected static const float PASS_TICK_MAX_MS = 250.0;
    protected static const float PASS_SCAN_AVG_MS = 22.0;
    protected static const float PASS_SCAN_P95_MS = 70.0;
    protected static const int PASS_MIN_SCANS = 60;
    protected static const float HITCH_16_MS = 16.7;
    protected static const float HITCH_33_MS = 33.4;

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_LastProgressWallS;
    protected float m_LastDebugPrintWallS;
    protected int m_LastScanSerial;

    protected IEntity m_Subject;
    protected vector m_RadarAnchor;
    protected ref array<IEntity> m_GroundTargets;
    protected ref array<IEntity> m_AirTargets;

    protected int m_ScanCount;
    protected int m_MaxPlots;
    protected int m_MaxScat;
    protected int m_Hitch16;
    protected int m_Hitch33;

    protected ref array<float> m_ScanDurationsMs;
    protected ref array<float> m_TickDurationsMs;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarStressAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarStressAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarStressAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar StressTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarStressAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarStressAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar StressTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Stress"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar StressTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Stress");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar StressTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Stress");
            return;
        }

        m_PrevConfig = null;
        RDF_RadarSensor prevSensor = RDF_RadarAutoRunner.GetSensor();
        if (prevSensor)
            m_PrevConfig = prevSensor.GetSettings();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        if (!ChooseRadarAnchor())
        {
            Print("[RDF Radar StressTest] failed to choose radar anchor.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Stress");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;
        m_GroundTargets = new array<IEntity>();
        m_AirTargets = new array<IEntity>();
        m_ScanDurationsMs = new array<float>();
        m_TickDurationsMs = new array<float>();
        m_ScanCount = 0;
        m_MaxPlots = 0;
        m_MaxScat = 0;
        m_Hitch16 = 0;
        m_Hitch33 = 0;
        m_LastScanSerial = -1;
        m_LastProgressWallS = m_StartWallS;
        m_LastDebugPrintWallS = m_StartWallS;

        SpawnGroundTargets();
        SeedGroundInRegistry();
        SpawnAirTargets();

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyStressConfig();
        RDF_RadarAutoRunner.SetShowcaseVisuals(true);
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
        {
            sensor.ClearDemCache();
            // Pay the SURF RAM cost here, not inside the first timed RadarTick.
            Print("[RDF Radar StressTest] DEM RAM preload...");
            if (RDF_DemRuntimeCache.IsAsyncWarmPreloadRunning())
                RDF_DemRuntimeCache.FlushAsyncWarmPreload();
            sensor.EnsureDemPreloaded();
            if (!RDF_DemRuntimeCache.IsSharedSurfReady()
                && RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ASYNC)
            {
                RDF_DemRuntimeCache.StartAsyncWarmPreload();
                RDF_DemRuntimeCache.FlushAsyncWarmPreload();
                sensor.EnsureDemPreloaded();
            }
            sensor.ResetSession();
            m_LastScanSerial = sensor.GetScanSerial();
        }

        // Soak clock starts after preload so duration is steady-state time.
        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastProgressWallS = m_StartWallS;
        m_LastDebugPrintWallS = m_StartWallS;

        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_Running = true;
        RDF_RadarAutoTestMapOverlay.Start();
        if (m_AirTargets.Count() > 0)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTargets.Get(0), "RDF Stress");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print(string.Format(
            "[RDF Radar StressTest] started duration=%1s ground=%2 air=%3 clutter=1.0 HUD ON",
            DURATION_S.ToString(),
            m_GroundTargets.Count().ToString(),
            m_AirTargets.Count().ToString()));
        Print("[RDF Radar StressTest] keep Play running; this is a soak, not a detection test.");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Stress");
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();
        ClearSpawned();

        RDF_RadarAutoRunner.SetDemoEnabled(false);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.StopAutoRun();

        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            sensor.ResetSession();

        if (restore && m_PrevConfig)
            ApplySensorConfig(m_PrevConfig);

        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
        Print("[RDF Radar StressTest] idle (demo/HUD left OFF)");
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

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        float elapsedS = nowS - m_StartWallS;
        if (elapsedS < 0.0)
            elapsedS = 0.0;

        UpdateAirMotion(elapsedS);
        UpdateOriginWander(elapsedS);
        if (m_AirTargets && m_AirTargets.Count() > 0 && m_AirTargets.Get(0))
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTargets.Get(0), "RDF Stress");

        AccumulateLatestScan(nowS, elapsedS);

        if (nowS - m_LastDebugPrintWallS > 5.0)
        {
            m_LastDebugPrintWallS = nowS;
            RDF_RadarSensor sensor = GetSensor();
            float scanMs = 0.0;
            float tickMs = RDF_RadarAutoRunner.GetLastTickDurationMs();
            if (sensor)
                scanMs = sensor.GetLastScanDurationMs();
            Print(string.Format(
                "[RDF Radar StressTest] t=%1s scans=%2 maxPlots=%3 maxScat=%4 hitch16=%5 hitch33=%6 scanMs=%7 tickMs=%8 %9",
                (Math.Round(elapsedS * 10.0) * 0.1).ToString(),
                m_ScanCount.ToString(),
                m_MaxPlots.ToString(),
                m_MaxScat.ToString(),
                m_Hitch16.ToString(),
                m_Hitch33.ToString(),
                (Math.Round(scanMs * 10.0) * 0.1).ToString(),
                (Math.Round(tickMs * 10.0) * 0.1).ToString(),
                RDF_RadarScattererRegistry.GetStatsLine()));
        }

        if (elapsedS < DURATION_S)
            return;

        FinalizeAndReport();
        StopInternal(true);
    }

    protected bool ChooseRadarAnchor()
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
        m_RadarAnchor = Vector(center[0], surfaceY + 8.0, center[2]);
        return true;
    }

    protected void ApplyStressConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(256);
        cfg.m_Range = 5000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.15;
        cfg.m_MaxLosTracesPerScan = 128;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        cfg.m_IncludeRadarEmitters = true;
        cfg.m_EnableDemClutter = true;
        cfg.m_DemClutterScale = 1.0;
        cfg.m_DetectionSnrDb = 3.0;
        cfg.m_EnableMeasurementSynthesis = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_KeepEntityTruth = true;
        cfg.m_UseScattererRegistry = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        cfg.m_FreshUpdateBudgetMin = 48;
        cfg.m_FreshUpdateBudgetMax = 128;
        cfg.m_ScattererDiscoveryIntervalS = 0.25;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 4096;
        cfg.m_DemCacheMaxTiles = RDF_DemBakeConstants.RUNTIME_DEM_CACHE_MAX_TILES;
        cfg.m_DemTileLoadsPerScan = 8;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 60.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("stress_gnd", 0.0, 28.0, 0.0);
        hw.AddElevationBeam("stress_low", 10.0, 32.0, 0.0);
        hw.AddElevationBeam("stress_mid", 22.0, 36.0, 0.0);
        hw.AddElevationBeam("stress_high", 36.0, 40.0, -0.5);
        hw.Validate();
        cfg.m_Hardware = hw;

        if (RDF_RadarAutoTestSuite.IsRealisticChannel())
        {
            cfg.ApplyRealisticChannel();
            cfg.m_KeepEntityTruth = true;
            cfg.m_DemClutterScale = 1.0;
        }
        else
        {
            cfg.ApplyIdealChannel();
            cfg.m_KeepEntityTruth = true;
            cfg.m_DemClutterScale = 1.0;
        }
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void UpdateOriginWander(float elapsedS)
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        RDF_RadarSettings cfg = sensor.GetSettings();
        if (!cfg)
            return;

        // Immediate wander: force DEM tile thrash for soak.
        float phase = elapsedS * ORIGIN_WANDER_RATE_RAD_S;
        float x = Math.Cos(phase) * ORIGIN_WANDER_RADIUS_M;
        float z = Math.Sin(phase) * ORIGIN_WANDER_RADIUS_M;
        cfg.m_OriginOffset = Vector(x, 12.0, z);
    }

    protected void SpawnGroundTargets()
    {
        ClearGroundTargets();
        if (!m_Subject)
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        Resource prefabRes = Resource.Load(GROUND_PREFAB);
        if (!prefabRes)
        {
            Print("[RDF Radar StressTest] failed to load ground prefab.", LogLevel.ERROR);
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

        for (int i = 0; i < GROUND_COUNT; i++)
        {
            float rangeM = 140.0 + i * 70.0;
            int lane = i - (i / 4) * 4;
            float lateral = (lane - 1.5) * 55.0;
            vector flatPos = Vector(
                m_RadarAnchor[0] + flatFwd[0] * rangeM + right[0] * lateral,
                m_RadarAnchor[1],
                m_RadarAnchor[2] + flatFwd[2] * rangeM + right[2] * lateral);
            float groundY = world.GetSurfaceY(flatPos[0], flatPos[2]);
            vector pos = Vector(flatPos[0], groundY + 0.5, flatPos[2]);

            EntitySpawnParams spawnParams = new EntitySpawnParams();
            Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
            spawnParams.Transform[3] = pos;

            IEntity ent = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
            if (!ent)
                continue;
            ent.SetOrigin(pos);
            m_GroundTargets.Insert(ent);
        }

        Print("[RDF Radar StressTest] spawned ground=" + m_GroundTargets.Count().ToString());
    }

    protected void SeedGroundInRegistry()
    {
        if (!m_GroundTargets)
            return;

        int seeded = 0;
        for (int i = 0; i < m_GroundTargets.Count(); i++)
        {
            IEntity ent = m_GroundTargets.Get(i);
            if (!ent)
                continue;
            RDF_RadarScatterer entry = RDF_RadarScattererRegistry.Register(
                ent, ent.GetOrigin(), false, 0.0);
            if (entry)
                seeded = seeded + 1;
        }
        Print("[RDF Radar StressTest] seeded ground=" + seeded.ToString());
    }

    protected vector ComputeOrbitPos(float elapsedS, int airIndex)
    {
        float phase = elapsedS * AIR_ORBIT_RATE_RAD_S + airIndex * 2.094;
        float radius = AIR_ORBIT_RADIUS_M + airIndex * 80.0;
        float alt = AIR_ORBIT_ALT_M + airIndex * 25.0;
        return Vector(
            m_RadarAnchor[0] + Math.Cos(phase) * radius,
            m_RadarAnchor[1] + alt + 10.0 * Math.Sin(elapsedS * 0.09 + airIndex),
            m_RadarAnchor[2] + Math.Sin(phase) * radius);
    }

    protected void SpawnAirTargets()
    {
        ClearAirTargets();
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        Resource prefabRes = Resource.Load(AIR_PREFAB);
        if (!prefabRes)
        {
            Print("[RDF Radar StressTest] failed to load air prefab.", LogLevel.ERROR);
            return;
        }

        for (int i = 0; i < AIR_COUNT; i++)
        {
            EntitySpawnParams spawnParams = new EntitySpawnParams();
            Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
            vector spawnPos = ComputeOrbitPos(0.0, i);
            spawnParams.Transform[3] = spawnPos;

            IEntity ent = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
            if (!ent)
                continue;
            m_AirTargets.Insert(ent);
            // Seed air too: stress scan cost, not discovery fairness.
            RDF_RadarScattererRegistry.Register(ent, spawnPos, false, 0.0);
        }

        Print("[RDF Radar StressTest] spawned+seeded air=" + m_AirTargets.Count().ToString());
    }

    protected void UpdateAirMotion(float elapsedS)
    {
        if (!m_AirTargets)
            return;

        for (int i = 0; i < m_AirTargets.Count(); i++)
        {
            IEntity air = m_AirTargets.Get(i);
            if (!air)
                continue;

            float phase = elapsedS * AIR_ORBIT_RATE_RAD_S + i * 2.094;
            float radius = AIR_ORBIT_RADIUS_M + i * 80.0;
            vector pos = ComputeOrbitPos(elapsedS, i);
            air.SetOrigin(pos);

            float vx = -Math.Sin(phase) * radius * AIR_ORBIT_RATE_RAD_S;
            float vz = Math.Cos(phase) * radius * AIR_ORBIT_RATE_RAD_S;
            float vy = 10.0 * 0.09 * Math.Cos(elapsedS * 0.09 + i);
            Physics physics = air.GetPhysics();
            if (physics)
                physics.SetVelocity(Vector(vx, vy, vz));
        }
    }

    protected void ClearGroundTargets()
    {
        if (!m_GroundTargets)
            return;

        array<IEntity> doomed = new array<IEntity>();
        for (int i = 0; i < m_GroundTargets.Count(); i++)
        {
            IEntity ent = m_GroundTargets.Get(i);
            if (ent)
                doomed.Insert(ent);
        }
        m_GroundTargets.Clear();

        for (int j = 0; j < doomed.Count(); j++)
        {
            IEntity ent = doomed.Get(j);
            if (!ent)
                continue;
            RDF_RadarScattererRegistry.Unregister(ent);
            SCR_EntityHelper.DeleteEntityAndChildren(ent);
        }
    }

    protected void ClearAirTargets()
    {
        if (!m_AirTargets)
            return;

        array<IEntity> doomed = new array<IEntity>();
        for (int i = 0; i < m_AirTargets.Count(); i++)
        {
            IEntity ent = m_AirTargets.Get(i);
            if (ent)
                doomed.Insert(ent);
        }
        m_AirTargets.Clear();

        for (int j = 0; j < doomed.Count(); j++)
        {
            IEntity ent = doomed.Get(j);
            if (!ent)
                continue;
            RDF_RadarScattererRegistry.Unregister(ent);
            SCR_EntityHelper.DeleteEntityAndChildren(ent);
        }
    }

    protected void ClearSpawned()
    {
        ClearGroundTargets();
        ClearAirTargets();
    }

    protected void AccumulateLatestScan(float nowS, float elapsedS)
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;

        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
        {
            if (nowS - m_LastProgressWallS > 5.0)
            {
                m_LastProgressWallS = nowS;
                Print("[RDF Radar StressTest] waiting for AutoRunner scans; keep Play mode running.");
            }
            return;
        }

        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;

        float scanMs = sensor.GetLastScanDurationMs();
        float tickMs = RDF_RadarAutoRunner.GetLastTickDurationMs();
        // Drop pathological one-shots (e.g. preload accidentally timed as a scan).
        bool sampleOk = true;
        if (scanMs > 1000.0 || tickMs > 1000.0)
            sampleOk = false;
        if (elapsedS >= WARMUP_S && sampleOk)
        {
            m_ScanDurationsMs.Insert(scanMs);
            m_TickDurationsMs.Insert(tickMs);
            if (tickMs >= HITCH_16_MS)
                m_Hitch16 = m_Hitch16 + 1;
            if (tickMs >= HITCH_33_MS)
                m_Hitch33 = m_Hitch33 + 1;
        }

        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        int plotN = 0;
        if (plots)
            plotN = plots.Count();
        if (plotN > m_MaxPlots)
            m_MaxPlots = plotN;

        int scatN = RDF_RadarScattererRegistry.GetEntryCount();
        if (scatN > m_MaxScat)
            m_MaxScat = scatN;
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
        int idx = Math.Floor((n - 1) * 0.95);
        if (idx < 0)
            idx = 0;
        if (idx >= n)
            idx = n - 1;
        return sorted.Get(idx);
    }

    protected string BoolLabel(bool ok)
    {
        if (ok)
            return "PASS";
        return "FAIL";
    }

    protected void FinalizeAndReport()
    {
        float scanAvg = ArrayAvg(m_ScanDurationsMs);
        float scanP95 = ArrayP95(m_ScanDurationsMs);
        float scanMax = ArrayMax(m_ScanDurationsMs);
        float tickAvg = ArrayAvg(m_TickDurationsMs);
        float tickP95 = ArrayP95(m_TickDurationsMs);
        float tickMax = ArrayMax(m_TickDurationsMs);
        int sampleN = 0;
        if (m_TickDurationsMs)
            sampleN = m_TickDurationsMs.Count();

        bool passScans = m_ScanCount >= PASS_MIN_SCANS;
        bool passScanAvg = scanAvg <= PASS_SCAN_AVG_MS;
        bool passScanP95 = scanP95 <= PASS_SCAN_P95_MS;
        bool passTickAvg = tickAvg <= PASS_TICK_AVG_MS;
        bool passTickP95 = tickP95 <= PASS_TICK_P95_MS;
        bool passTickMax = tickMax <= PASS_TICK_MAX_MS;
        bool allPass = passScans && passScanAvg && passScanP95
            && passTickAvg && passTickP95 && passTickMax;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Stress AutoTest");
        lines.Insert("mode AutoRunner+HUD+DEM1.0+seeded_load");
        lines.Insert("duration_s " + DURATION_S.ToString());
        lines.Insert("warmup_s " + WARMUP_S.ToString());
        lines.Insert("ground " + GROUND_COUNT.ToString());
        lines.Insert("air " + AIR_COUNT.ToString());
        lines.Insert("scan_count " + m_ScanCount.ToString());
        lines.Insert("sample_n " + sampleN.ToString());
        lines.Insert("scan_avg_ms " + scanAvg.ToString());
        lines.Insert("scan_p95_ms " + scanP95.ToString());
        lines.Insert("scan_max_ms " + scanMax.ToString());
        lines.Insert("tick_avg_ms " + tickAvg.ToString());
        lines.Insert("tick_p95_ms " + tickP95.ToString());
        lines.Insert("tick_max_ms " + tickMax.ToString());
        lines.Insert("hitch_ge_16ms " + m_Hitch16.ToString());
        lines.Insert("hitch_ge_33ms " + m_Hitch33.ToString());
        lines.Insert("max_plots " + m_MaxPlots.ToString());
        lines.Insert("max_scat " + m_MaxScat.ToString());
        lines.Insert("thresholds tickAvg<=" + PASS_TICK_AVG_MS.ToString()
            + " tickP95<=" + PASS_TICK_P95_MS.ToString()
            + " tickMax<=" + PASS_TICK_MAX_MS.ToString());
        lines.Insert("  scans " + BoolLabel(passScans));
        lines.Insert("  scan_avg " + BoolLabel(passScanAvg));
        lines.Insert("  scan_p95 " + BoolLabel(passScanP95));
        lines.Insert("  tick_avg " + BoolLabel(passTickAvg));
        lines.Insert("  tick_p95 " + BoolLabel(passTickP95));
        lines.Insert("  tick_max " + BoolLabel(passTickMax));
        if (allPass)
            lines.Insert("result=PASS");
        else
            lines.Insert("result=FAIL");

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_stress_autotest_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar StressTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar StressTest] scans=%1 tickAvg=%2ms p95=%3ms max=%4ms hitch16=%5 hitch33=%6 maxScat=%7",
            m_ScanCount.ToString(),
            (Math.Round(tickAvg * 10.0) * 0.1).ToString(),
            (Math.Round(tickP95 * 10.0) * 0.1).ToString(),
            (Math.Round(tickMax * 10.0) * 0.1).ToString(),
            m_Hitch16.ToString(),
            m_Hitch33.ToString(),
            m_MaxScat.ToString()));
    }
}
