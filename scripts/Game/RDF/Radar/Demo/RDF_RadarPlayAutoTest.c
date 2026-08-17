// Play-like AutoTest: AutoRunner Tick + PPI HUD + showcase visuals + normal
// scatterer discovery (no Registry seed). Spawns ground vehicles + one orbiting
// Mi-8, walks OriginOffset to churn DEM tiles, and measures scanMs + full
// RadarTick wall time (HUD/presentation included).
//
// Not a substitute for multiplayer / network radar. Workbench still uses
// ForceLocalScan like other AutoTests.
//
// Usage (Script Debugger, Play mode):
//   RDF_RadarPlayAutoTest.Start();
// Included in RDF_RadarAutoTestSuite.StartAll() as the last step.
class RDF_RadarPlayAutoTest
{
    protected static ref RDF_RadarPlayAutoTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_LastPass;

    protected static const ResourceName GROUND_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
    protected static const ResourceName AIR_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";

    protected static const int GROUND_COUNT = 6;
    protected static const float DURATION_S = 45.0;
    protected static const float WARMUP_S = 8.0;
    // Wander after warmup so discovery/classification can settle first.
    protected static const float ORIGIN_WANDER_RADIUS_M = 180.0;
    protected static const float ORIGIN_WANDER_RATE_RAD_S = 0.10;
    protected static const float AIR_ORBIT_RADIUS_M = 420.0;
    protected static const float AIR_ORBIT_ALT_M = 140.0;
    protected static const float AIR_ORBIT_RATE_RAD_S = 0.16;

    // Soft budgets for Workbench hosts (catch pathology, not laptop FPS).
    protected static const float PASS_SCAN_AVG_MS = 80.0;
    protected static const float PASS_SCAN_P95_MS = 160.0;
    protected static const float PASS_TICK_AVG_MS = 120.0;
    protected static const float PASS_TICK_P95_MS = 220.0;
    protected static const int PASS_MIN_SCANS = 40;
    protected static const int PASS_MIN_AIR_DETECTED = 3;
    protected static const int PASS_MIN_GROUND_DETECTED = 2;

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_LastProgressWallS;
    protected float m_LastDebugPrintWallS;
    protected int m_LastScanSerial;

    protected IEntity m_Subject;
    protected IEntity m_AirTarget;
    protected vector m_RadarAnchor;
    protected ref array<IEntity> m_GroundTargets;

    protected int m_ScanCount;
    protected int m_AirDetectedCount;
    protected int m_GroundDetectedCount;
    protected int m_GroundSeenCount;
    protected int m_MaxPlots;
    protected bool m_AirDiscovered;
    protected int m_GroundDiscoveredCount;
    protected int m_RespawnCount;

    protected ref array<float> m_ScanDurationsMs;
    protected ref array<float> m_TickDurationsMs;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarPlayAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarPlayAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarPlayAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar PlayTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarPlayAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    static bool DidLastPass()
    {
        return s_LastPass;
    }

    protected static void StaticTick()
    {
        RDF_RadarPlayAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar PlayTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Play"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar PlayTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Play");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar PlayTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Play");
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
            Print("[RDF Radar PlayTest] failed to choose radar anchor.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Play");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;
        m_GroundTargets = new array<IEntity>();
        m_ScanDurationsMs = new array<float>();
        m_TickDurationsMs = new array<float>();
        m_ScanCount = 0;
        m_AirDetectedCount = 0;
        m_GroundDetectedCount = 0;
        m_GroundSeenCount = 0;
        m_MaxPlots = 0;
        m_AirDiscovered = false;
        m_GroundDiscoveredCount = 0;
        m_RespawnCount = 0;
        m_LastScanSerial = -1;
        m_LastProgressWallS = m_StartWallS;
        m_LastDebugPrintWallS = m_StartWallS;

        SpawnGroundTargets();
        if (!SpawnAirTarget())
        {
            Print("[RDF Radar PlayTest] failed to spawn air target.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyPlayConfig();
        RDF_RadarAutoRunner.SetShowcaseVisuals(true);
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
        {
            sensor.ClearDemCache();
            Print("[RDF Radar PlayTest] DEM RAM preload...");
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

        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastProgressWallS = m_StartWallS;
        m_LastDebugPrintWallS = m_StartWallS;

        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_Running = true;
        RDF_RadarAutoTestMapOverlay.Start();
        if (m_AirTarget)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTarget, "RDF Play");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print(string.Format(
            "[RDF Radar PlayTest] started duration=%1s ground=%2 air=1 HUD+DEM+discovery",
            DURATION_S.ToString(),
            m_GroundTargets.Count().ToString()));
        Print("[RDF Radar PlayTest] open map (M); keep Play running (CallLater must tick).");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Play");
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();
        ClearSpawned();

        // Leave idle after play soak so the next DEM dwell does not look hung.
        RDF_RadarAutoRunner.SetDemoEnabled(false);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.StopAutoRun();

        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            sensor.ResetSession();

        if (restore && m_PrevConfig)
            ApplySensorConfig(m_PrevConfig);

        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
        Print("[RDF Radar PlayTest] idle (demo/HUD left OFF)");
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
        if (m_AirTarget)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTarget, "RDF Play");

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
                 "[RDF Radar PlayTest] t=%1s scans=%2 airDet=%3 groundDet=%4 groundSeen=%5 maxPlots=%6 scanMs=%7 tickMs=%8 %9",
                 (Math.Round(elapsedS * 10.0) * 0.1).ToString(),
                 m_ScanCount.ToString(),
                 m_AirDetectedCount.ToString(),
                 m_GroundDetectedCount.ToString(),
                 m_GroundSeenCount.ToString(),
                 m_MaxPlots.ToString(),
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

    protected void ApplyPlayConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_Range = 3500.0;
        cfg.m_SectorHalfAngleDeg = 120.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        cfg.m_IncludeRadarEmitters = true;
        cfg.m_EnableDemClutter = true;
        // Playable clutter: full scale=1 buries skin UAZ under Eden SURF (SNR≪0).
        cfg.m_DemClutterScale = 0.25;
        cfg.m_DetectionSnrDb = 3.0;
        cfg.m_EnableMeasurementSynthesis = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_KeepEntityTruth = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        cfg.m_FreshUpdateBudgetMin = 24;
        cfg.m_FreshUpdateBudgetMax = 64;
        cfg.m_ScattererDiscoveryIntervalS = 0.35;
        cfg.m_ScattererClassifyPerTick = 192;
        cfg.m_ScattererRefreshPerTick = 256;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_DemCacheMaxTiles = RDF_DemBakeConstants.RUNTIME_DEM_CACHE_MAX_TILES;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 50.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        // Ground lobe first: UAZ sits near 0° elev from an 8–12 m mast.
        hw.AddElevationBeam("play_gnd", 0.0, 24.0, 0.0);
        hw.AddElevationBeam("play_low", 8.0, 28.0, 0.0);
        hw.AddElevationBeam("play_mid", 20.0, 36.0, 0.0);
        hw.AddElevationBeam("play_high", 34.0, 40.0, -0.5);
        hw.Validate();
        cfg.m_Hardware = hw;

        cfg.StabilizeForRegression();
        cfg.m_KeepEntityTruth = true;
        cfg.m_DemClutterScale = 0.25;
        cfg.m_DetectionSnrDb = 3.0;
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

        if (elapsedS < WARMUP_S)
        {
            cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
            return;
        }

        float wanderS = elapsedS - WARMUP_S;
        float phase = wanderS * ORIGIN_WANDER_RATE_RAD_S;
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
            Print("[RDF Radar PlayTest] failed to load ground prefab.", LogLevel.ERROR);
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
            float rangeM = 160.0 + i * 90.0;
            int lane = i - (i / 3) * 3;
            float lateral = (lane - 1) * 45.0;
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

        // Intentionally NOT registered: discovery must find them.
        Print("[RDF Radar PlayTest] spawned ground=" + m_GroundTargets.Count().ToString()
            + " (no registry seed)");
    }

    protected vector ComputeOrbitPos(float elapsedS)
    {
        float phase = elapsedS * AIR_ORBIT_RATE_RAD_S;
        return Vector(
            m_RadarAnchor[0] + Math.Cos(phase) * AIR_ORBIT_RADIUS_M,
            m_RadarAnchor[1] + AIR_ORBIT_ALT_M + 12.0 * Math.Sin(elapsedS * 0.08),
            m_RadarAnchor[2] + Math.Sin(phase) * AIR_ORBIT_RADIUS_M);
    }

    protected bool SpawnAirTarget()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(AIR_PREFAB);
        if (!prefabRes)
            return false;

        float elapsedS = 0.0;
        if (m_StartWallS > 0.0)
        {
            elapsedS = System.GetTickCount() * 0.001 - m_StartWallS;
            if (elapsedS < 0.0)
                elapsedS = 0.0;
        }

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        vector spawnPos = ComputeOrbitPos(elapsedS);
        spawnParams.Transform[3] = spawnPos;

        m_AirTarget = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_AirTarget)
            return false;

        Print("[RDF Radar PlayTest] spawned Mi-8 at " + spawnPos.ToString());
        return true;
    }

    protected void UpdateAirMotion(float elapsedS)
    {
        if (!m_AirTarget)
        {
            if (SpawnAirTarget())
            {
                m_RespawnCount = m_RespawnCount + 1;
                Print("[RDF Radar PlayTest] air respawned count=" + m_RespawnCount.ToString());
            }
            return;
        }

        float phase = elapsedS * AIR_ORBIT_RATE_RAD_S;
        vector pos = ComputeOrbitPos(elapsedS);
        m_AirTarget.SetOrigin(pos);

        float vx = -Math.Sin(phase) * AIR_ORBIT_RADIUS_M * AIR_ORBIT_RATE_RAD_S;
        float vz = Math.Cos(phase) * AIR_ORBIT_RADIUS_M * AIR_ORBIT_RATE_RAD_S;
        float vy = 12.0 * 0.08 * Math.Cos(elapsedS * 0.08);
        Physics physics = m_AirTarget.GetPhysics();
        if (physics)
            physics.SetVelocity(Vector(vx, vy, vz));
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

    protected void ClearSpawned()
    {
        ClearGroundTargets();
        if (m_AirTarget)
        {
            RDF_RadarScattererRegistry.Unregister(m_AirTarget);
            SCR_EntityHelper.DeleteEntityAndChildren(m_AirTarget);
            m_AirTarget = null;
        }
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
                Print("[RDF Radar PlayTest] waiting for AutoRunner scans; keep Play mode running.");
            }
            return;
        }

        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;

        if (m_AirTarget && RDF_RadarScattererRegistry.Find(m_AirTarget))
            m_AirDiscovered = true;

        m_GroundDiscoveredCount = 0;
        for (int gd = 0; gd < m_GroundTargets.Count(); gd++)
        {
            IEntity gEnt = m_GroundTargets.Get(gd);
            if (gEnt && RDF_RadarScattererRegistry.Find(gEnt))
                m_GroundDiscoveredCount = m_GroundDiscoveredCount + 1;
        }

        float scanMs = sensor.GetLastScanDurationMs();
        float tickMs = RDF_RadarAutoRunner.GetLastTickDurationMs();
        if (elapsedS >= WARMUP_S)
        {
            m_ScanDurationsMs.Insert(scanMs);
            m_TickDurationsMs.Insert(tickMs);
        }

        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        int plotN = 0;
        if (plots)
            plotN = plots.Count();
        if (plotN > m_MaxPlots)
            m_MaxPlots = plotN;

        if (!plots)
            return;

        vector origin = m_RadarAnchor;
        RDF_RadarScanContext ctx = sensor.GetScanContext();
        if (ctx)
            origin = ctx.m_Origin;

        foreach (RDF_RadarTarget t : plots)
        {
            if (!t)
                continue;

            bool nearGround = false;
            for (int g = 0; g < m_GroundTargets.Count(); g++)
            {
                IEntity ground = m_GroundTargets.Get(g);
                if (!ground)
                    continue;
                if (IsPlotNearEntity(t, ground, origin, 220.0))
                {
                    nearGround = true;
                    break;
                }
            }

            if (nearGround)
                m_GroundSeenCount = m_GroundSeenCount + 1;

            if (!t.m_Detected)
                continue;

            if (m_AirTarget && IsPlotNearEntity(t, m_AirTarget, origin, 280.0))
                m_AirDetectedCount = m_AirDetectedCount + 1;

            if (nearGround)
                m_GroundDetectedCount = m_GroundDetectedCount + 1;
        }
    }

    protected bool IsPlotNearEntity(
        RDF_RadarTarget plot,
        IEntity entity,
        vector radarOrigin,
        float gateM)
    {
        if (!plot || !entity)
            return false;
        if (plot.m_Entity == entity)
            return true;

        vector truthPos = entity.GetOrigin();
        vector truthDelta = truthPos - radarOrigin;
        float truthRange = truthDelta.Length();
        float dRange = plot.m_Distance - truthRange;
        if (dRange < 0.0)
            dRange = -dRange;
        if (dRange > gateM)
            return false;

        float truthAz = Math.Atan2(truthDelta[2], truthDelta[0]) * 57.2957795;
        float dAz = plot.m_AzimuthDeg - truthAz;
        while (dAz > 180.0)
            dAz = dAz - 360.0;
        while (dAz < -180.0)
            dAz = dAz + 360.0;
        if (dAz < 0.0)
            dAz = -dAz;
        if (dAz > 10.0)
            return false;
        return true;
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
        // Epsilon bias: (n-1)*0.95 in float32 can land ~1 ULP below an integer
        // boundary; Math.Floor would then pick the index one below the true P95.
        int idx = Math.Floor((n - 1) * 0.95 + 0.0001);
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
        float tickAvg = ArrayAvg(m_TickDurationsMs);
        float tickP95 = ArrayP95(m_TickDurationsMs);
        int sampleN = 0;
        if (m_ScanDurationsMs)
            sampleN = m_ScanDurationsMs.Count();

         bool passScans = m_ScanCount >= PASS_MIN_SCANS;
         bool passAir = m_AirDetectedCount >= PASS_MIN_AIR_DETECTED;
         bool passGround = m_GroundDetectedCount >= PASS_MIN_GROUND_DETECTED;
         bool passDiscover = m_AirDiscovered && m_GroundDiscoveredCount >= 2;
         bool passScanAvg = scanAvg <= PASS_SCAN_AVG_MS;
         bool passScanP95 = scanP95 <= PASS_SCAN_P95_MS;
         bool passTickAvg = tickAvg <= PASS_TICK_AVG_MS;
         bool passTickP95 = tickP95 <= PASS_TICK_P95_MS;
         bool allPass = passScans && passAir && passGround && passDiscover
             && passScanAvg && passScanP95 && passTickAvg && passTickP95;
         s_LastPass = allPass;

         array<string> lines = new array<string>();
         lines.Insert("RDF Radar Play AutoTest");
         lines.Insert("mode AutoRunner+HUD+DEM+discovery");
         lines.Insert("duration_s " + DURATION_S.ToString());
         lines.Insert("warmup_s " + WARMUP_S.ToString());
         lines.Insert("dem_clutter_scale 0.25");
         lines.Insert("ground_spawned " + GROUND_COUNT.ToString());
         lines.Insert("scan_count " + m_ScanCount.ToString());
         lines.Insert("sample_n " + sampleN.ToString());
         lines.Insert("scan_avg_ms " + scanAvg.ToString());
         lines.Insert("scan_p95_ms " + scanP95.ToString());
         lines.Insert("tick_avg_ms " + tickAvg.ToString());
         lines.Insert("tick_p95_ms " + tickP95.ToString());
         lines.Insert("air_detected " + m_AirDetectedCount.ToString());
         lines.Insert("ground_detected " + m_GroundDetectedCount.ToString());
         lines.Insert("ground_seen " + m_GroundSeenCount.ToString());
         lines.Insert("ground_discovered " + m_GroundDiscoveredCount.ToString());
         lines.Insert("max_plots " + m_MaxPlots.ToString());
         lines.Insert("air_discovered " + m_AirDiscovered.ToString());
         lines.Insert("air_respawn " + m_RespawnCount.ToString());
         lines.Insert("thresholds scanAvg<=" + PASS_SCAN_AVG_MS.ToString()
             + " scanP95<=" + PASS_SCAN_P95_MS.ToString()
             + " tickAvg<=" + PASS_TICK_AVG_MS.ToString()
             + " tickP95<=" + PASS_TICK_P95_MS.ToString());
         lines.Insert("  scans " + BoolLabel(passScans));
         lines.Insert("  air_detected " + BoolLabel(passAir));
         lines.Insert("  ground_detected " + BoolLabel(passGround));
         lines.Insert("  discovery " + BoolLabel(passDiscover));
         lines.Insert("  scan_avg " + BoolLabel(passScanAvg));
         lines.Insert("  scan_p95 " + BoolLabel(passScanP95));
         lines.Insert("  tick_avg " + BoolLabel(passTickAvg));
         lines.Insert("  tick_p95 " + BoolLabel(passTickP95));
         if (allPass)
             lines.Insert("result=PASS");
         else
             lines.Insert("result=FAIL");

         FileIO.MakeDirectory("$profile:RDF");
         FileIO.MakeDirectory("$profile:RDF/RadarTests");
         string reportPath = "$profile:RDF/RadarTests/radar_play_autotest_"
             + System.GetTickCount().ToString() + ".txt";
         SCR_FileIOHelper.WriteFileContent(reportPath, lines);

         Print("[RDF Radar PlayTest] " + BoolLabel(allPass) + "  report=" + reportPath);
         Print(string.Format(
             "[RDF Radar PlayTest] scans=%1 airDet=%2 groundDet=%3 groundSeen=%4 scanAvg=%5ms tickAvg=%6ms p95Tick=%7ms",
             m_ScanCount.ToString(),
             m_AirDetectedCount.ToString(),
             m_GroundDetectedCount.ToString(),
             m_GroundSeenCount.ToString(),
             (Math.Round(scanAvg * 10.0) * 0.1).ToString(),
             (Math.Round(tickAvg * 10.0) * 0.1).ToString(),
             (Math.Round(tickP95 * 10.0) * 0.1).ToString()));
     }
 }
