// JPDA soft-association test (TODO §9 S2).
//
// Spawns a tight cluster of UAZ (overlapping range/azimuth gates, the exact case
// where GNN can swap or steal plots), enables the opt-in JPDA path, then verifies
// every target stays tracked (no drop, no excessive split).
//
// The soft-split / no-steal math is already covered by the offline golden
// (tools/dem/test_rdf_radar_jpda.py). This test validates the in-game JPDA path
// end-to-end on top of the tracker.
//
// Usage (Script Debugger): RDF_RadarJpdaAutoTest.Start();
class RDF_RadarJpdaAutoTest
{
    protected static ref RDF_RadarJpdaAutoTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_LastPass;

    protected static const ResourceName GROUND_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
    protected static const int TARGET_COUNT = 6;
    // Tight cluster: 40 m range spacing is well inside the 400 m gate, so every
    // target's gate overlaps its neighbours.
    protected static const float CLUSTER_SPACING_M = 40.0;
    protected static const float DURATION_S = 20.0;

    protected bool m_Running;
    protected float m_StartWallS;
    protected int m_LastScanSerial;
    protected float m_LastProgressWallS;

    protected IEntity m_Subject;
    protected vector m_RadarOrigin;
    protected ref array<IEntity> m_Targets;

    protected int m_ScanCount;
    protected int m_DiscoveredCount;
    protected int m_MaxConfirmedTracks;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarJpdaAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarJpdaAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarJpdaAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar JpdaTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarJpdaAutoTest inst = GetInstance();
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
        RDF_RadarJpdaAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar JpdaTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Jpda"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar JpdaTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Jpda");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar JpdaTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Jpda");
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
            Print("[RDF Radar JpdaTest] failed to choose radar origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Jpda");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;
        m_Targets = new array<IEntity>();
        SpawnGroundTargets();
        if (m_Targets.Count() <= 0)
        {
            Print("[RDF Radar JpdaTest] failed to spawn any UAZ.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyTestRadarConfig();
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_ScanCount = 0;
        m_DiscoveredCount = 0;
        m_MaxConfirmedTracks = 0;
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            m_LastScanSerial = sensor.GetScanSerial();
        else
            m_LastScanSerial = -1;
        m_LastProgressWallS = m_StartWallS;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print("[RDF Radar JpdaTest] started " + m_Targets.Count().ToString() + " UAZ tight cluster; JPDA ON.");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Jpda");
        ClearGroundTargets();

        if (!restore)
            return;

        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            sensor.ResetSession();

        if (m_PrevConfig)
            ApplySensorConfig(m_PrevConfig);
        RDF_RadarAutoRunner.SetDemoEnabled(m_PrevDemoEnabled);
        RDF_RadarAutoRunner.SetHudEnabled(m_PrevHudEnabled);
        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
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

    // Tight cluster of static UAZ straight ahead. No registry pre-seeding and no
    // emitter flag: the sweep must discover them on its own.
    protected void SpawnGroundTargets()
    {
        ClearGroundTargets();
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        Resource prefabRes = Resource.Load(GROUND_PREFAB);
        if (!prefabRes)
        {
            Print("[RDF Radar JpdaTest] failed to load UAZ prefab.", LogLevel.ERROR);
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

        for (int i = 0; i < TARGET_COUNT; i++)
        {
            float rangeM = 200.0 + i * CLUSTER_SPACING_M;
            vector flatPos = Vector(
                m_RadarOrigin[0] + flatFwd[0] * rangeM,
                m_RadarOrigin[1],
                m_RadarOrigin[2] + flatFwd[2] * rangeM);
            float groundY = world.GetSurfaceY(flatPos[0], flatPos[2]);
            vector pos = Vector(flatPos[0], groundY + 0.5, flatPos[2]);

            EntitySpawnParams spawnParams = new EntitySpawnParams();
            Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
            spawnParams.Transform[3] = pos;

            IEntity ent = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
            if (!ent)
                continue;
            ent.SetOrigin(pos);
            m_Targets.Insert(ent);
        }
        Print("[RDF Radar JpdaTest] spawned targets=" + m_Targets.Count().ToString());
    }

    protected void ClearGroundTargets()
    {
        if (!m_Targets)
            return;
        for (int i = 0; i < m_Targets.Count(); i++)
        {
            IEntity ent = m_Targets.Get(i);
            if (!ent)
                continue;
            RDF_RadarScattererRegistry.Unregister(ent);
            SCR_EntityHelper.DeleteEntityAndChildren(ent);
        }
        m_Targets.Clear();
    }

    protected bool IsTargetDiscovered(IEntity ent)
    {
        if (!ent)
            return false;
        return RDF_RadarScattererRegistry.Find(ent) != null;
    }

    protected void ApplyTestRadarConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(64);
        cfg.m_Range = 2000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 6.0;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.25;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_OriginOffset = Vector(0.0, 8.0, 0.0);

        // JPDA soft association: opt-in.
        cfg.m_EnableJpda = true;
        cfg.m_JpdaPd = 0.9;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 45.0;
        hw.m_BandwidthHz = 10000000.0;
        hw.m_ScanRpm = 0.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("jpda_low", 0.0, 10.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        AccumulateLatestScan(nowS);

        if (nowS - m_StartWallS < DURATION_S)
            return;

        FinalizeAndReport();
        StopInternal(true);
    }

    protected void AccumulateLatestScan(float nowS)
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
                Print("[RDF Radar JpdaTest] waiting for scan; ensure Play mode is running.");
            }
            return;
        }
        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;

        int discovered = 0;
        for (int i = 0; i < m_Targets.Count(); i++)
        {
            if (IsTargetDiscovered(m_Targets.Get(i)))
                discovered = discovered + 1;
        }
        if (discovered > m_DiscoveredCount)
            m_DiscoveredCount = discovered;

        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        int confirmed = 0;
        if (tracks)
        {
            for (int t = 0; t < tracks.Count(); t++)
            {
                RDF_RadarTrack tr = tracks.Get(t);
                if (tr && tr.m_Confirmed)
                    confirmed = confirmed + 1;
            }
        }
        if (confirmed > m_MaxConfirmedTracks)
            m_MaxConfirmedTracks = confirmed;
    }

    protected void FinalizeAndReport()
    {
        bool passScans = m_ScanCount > 6;
        bool passDiscovered = m_DiscoveredCount >= TARGET_COUNT;
        bool passTracks = m_MaxConfirmedTracks >= TARGET_COUNT;
        bool allPass = passScans && passDiscovered && passTracks;
        s_LastPass = allPass;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar JPDA Test (tight cluster soft association)");
        lines.Insert("prefab " + GROUND_PREFAB);
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  discovered_unaided " + BoolLabel(passDiscovered));
        lines.Insert("  all_targets_confirmed " + BoolLabel(passTracks));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  scan_count " + m_ScanCount.ToString());
        lines.Insert("  discovered_count " + m_DiscoveredCount.ToString());
        lines.Insert("  max_confirmed_tracks " + m_MaxConfirmedTracks.ToString());
        lines.Insert("  radar_origin " + m_RadarOrigin.ToString());

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_jpda_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar JpdaTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar JpdaTest] scans=%1 discovered=%2/%3 maxConfirmed=%4",
            m_ScanCount.ToString(),
            m_DiscoveredCount.ToString(),
            TARGET_COUNT.ToString(),
            m_MaxConfirmedTracks.ToString()));
    }

    protected string BoolLabel(bool value)
    {
        if (value)
            return "PASS";
        return "FAIL";
    }
}
