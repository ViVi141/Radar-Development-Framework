// Dwell / resource-management test (TODO §9 S1).
//
// Spawns a grid of UAZ with no detection aid (no emitter flag, no RCS override,
// no registry pre-seeding), enables the layered dwell scheduler with a small
// beam-time budget, then auto-locks the nearest vehicle. Verifies that:
//   * the fire-control dwell keeps the locked target locked (usable aim),
//   * the track dwells keep confirmed tracks alive under budget contention,
//   * the normal discovery sweep found the targets (discovered_unaided).
//
// The scheduler's priority / EDF / budget / deadline math is already covered by
// the offline golden (tools/dem/test_rdf_radar_dwell.py). This test validates the
// in-game integration on top of the existing scan loop.
//
// Usage (Script Debugger): RDF_RadarDwellAutoTest.Start();
class RDF_RadarDwellAutoTest
{
    protected static ref RDF_RadarDwellAutoTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_LastPass;

    protected static const ResourceName GROUND_PREFAB =
        "{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et";
    protected static const int TARGET_COUNT = 12;
    // Long enough for a cold scatterer table to discover + confirm 12 targets.
    protected static const float DURATION_S = 20.0;
    // Beam-time budget: small enough that not all track dwells fit every scan
    // (real contention), large enough that fire-control always fits.
    protected static const float DWELL_BUDGET_MS = 8.0;
    protected static const float FIRE_DWELL_MS = 4.0;
    protected static const float FIRE_DWELL_PERIOD_S = 0.25;
    protected static const float TRACK_DWELL_MS = 2.0;
    protected static const float TRACK_DWELL_PERIOD_S = 1.0;
    // Pass threshold: at least this many targets must be confirmed at some scan.
    protected static const int MIN_CONFIRMED_TRACKS = 6;

    protected bool m_Running;
    protected float m_StartWallS;
    protected int m_LastScanSerial;
    protected float m_LastProgressWallS;

    protected IEntity m_Subject;
    protected vector m_RadarOrigin;
    protected ref array<IEntity> m_Targets;

    protected int m_ScanCount;
    protected int m_DiscoveredCount;
    protected int m_TrackStateScans;
    protected int m_LockedPointScans;
    protected int m_MaxConfirmedTracks;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarDwellAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarDwellAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarDwellAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar DwellTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarDwellAutoTest inst = GetInstance();
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
        RDF_RadarDwellAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar DwellTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Dwell"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar DwellTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Dwell");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar DwellTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Dwell");
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
            Print("[RDF Radar DwellTest] failed to choose radar origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Dwell");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;
        m_Targets = new array<IEntity>();
        SpawnGroundTargets();
        if (m_Targets.Count() <= 0)
        {
            Print("[RDF Radar DwellTest] failed to spawn any UAZ.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyTestRadarConfig();
        ConfigureLock();
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_ScanCount = 0;
        m_DiscoveredCount = 0;
        m_TrackStateScans = 0;
        m_LockedPointScans = 0;
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

        Print(string.Format(
            "[RDF Radar DwellTest] started targets=%1 budget=%2ms fire=%3ms/%4s track=%5ms/%6s",
            m_Targets.Count().ToString(),
            DWELL_BUDGET_MS.ToString(),
            FIRE_DWELL_MS.ToString(),
            FIRE_DWELL_PERIOD_S.ToString(),
            TRACK_DWELL_MS.ToString(),
            TRACK_DWELL_PERIOD_S.ToString()));
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Dwell");

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

    // Grid of static UAZ ahead of the radar. No registry pre-seeding and no
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
            Print("[RDF Radar DwellTest] failed to load UAZ prefab.", LogLevel.ERROR);
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

        for (int i = 0; i < TARGET_COUNT; i++)
        {
            float rangeM = 140.0 + i * 70.0;
            int lane = i - (i / 4) * 4;
            float lateral = (lane - 1.5) * 55.0;
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
            m_Targets.Insert(ent);
        }
        Print("[RDF Radar DwellTest] spawned targets=" + m_Targets.Count().ToString());
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
        // Search preset; dwell scheduler ON with a small budget. Detection is
        // kept easy (no clutter / no MTI / low SNR) so the test measures the
        // scheduler, not the detection physics.
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_Range = 2000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 6.0;
        cfg.m_KeepUndetected = false;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.25;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_OriginOffset = Vector(0.0, 8.0, 0.0);

        // Dwell scheduler: layered, opt-in. Small budget forces contention so
        // fire-control must win over track dwells every scan.
        cfg.m_EnableDwellScheduler = true;
        cfg.m_DwellBudgetMs = DWELL_BUDGET_MS;
        cfg.m_FireControlDwellPeriodS = FIRE_DWELL_PERIOD_S;
        cfg.m_FireControlDwellMs = FIRE_DWELL_MS;
        cfg.m_TrackDwellPeriodS = TRACK_DWELL_PERIOD_S;
        cfg.m_TrackDwellMs = TRACK_DWELL_MS;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 45.0;
        hw.m_BandwidthHz = 10000000.0;
        hw.m_ScanRpm = 0.0;
        // Static UAZ have no Doppler; keep MTI off so they are not filtered.
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("dwell_low", 0.0, 10.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void ConfigureLock()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        // Clear leftovers so this test does not inherit tracks/lock from a
        // previous suite step (AutoRunner is shared).
        sensor.ResetSession();

        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        lockMgr.SetAutoAcquire(true);
        lockMgr.SetTypeFilter(true, false, false);
        lockMgr.SetMaxLockRange(2000.0);
        lockMgr.SetLockSector(0.0);
        lockMgr.SetAcquireHits(2);
        lockMgr.SetCoastMaxSec(3.0);
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
                Print("[RDF Radar DwellTest] waiting for scan; ensure Play mode is running.");
            }
            return;
        }
        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;

        // Discovery: how many spawned targets are in the scatterer table now.
        int discovered = 0;
        for (int i = 0; i < m_Targets.Count(); i++)
        {
            if (IsTargetDiscovered(m_Targets.Get(i)))
                discovered = discovered + 1;
        }
        if (discovered > m_DiscoveredCount)
            m_DiscoveredCount = discovered;

        // Confirmed tracks: track dwells must keep these alive under contention.
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

        // Fire-control: the locked target must yield a usable aim point.
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        ERDF_RadarLockState state = lockMgr.GetState();
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
            m_TrackStateScans = m_TrackStateScans + 1;
        IEntity lockedEntity;
        vector lockedPos;
        if (sensor.GetLockedTarget(lockedEntity, lockedPos))
            m_LockedPointScans = m_LockedPointScans + 1;
    }

    protected void FinalizeAndReport()
    {
        bool passScans = m_ScanCount > 6;
        bool passDiscovered = m_DiscoveredCount >= TARGET_COUNT;
        bool passFireControl = m_LockedPointScans > 0;
        bool passTrackDwell = m_MaxConfirmedTracks >= MIN_CONFIRMED_TRACKS;
        bool allPass = passScans && passDiscovered && passFireControl && passTrackDwell;
        s_LastPass = allPass;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Dwell Test (layered scheduler + beam-time budget)");
        lines.Insert("prefab " + GROUND_PREFAB);
        lines.Insert("budget_ms " + DWELL_BUDGET_MS.ToString());
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  discovered_unaided " + BoolLabel(passDiscovered));
        lines.Insert("  fire_control_usable " + BoolLabel(passFireControl));
        lines.Insert("  track_dwells_confirmed " + BoolLabel(passTrackDwell));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  scan_count " + m_ScanCount.ToString());
        lines.Insert("  discovered_count " + m_DiscoveredCount.ToString());
        lines.Insert("  track_state_scans " + m_TrackStateScans.ToString());
        lines.Insert("  locked_point_scans " + m_LockedPointScans.ToString());
        lines.Insert("  max_confirmed_tracks " + m_MaxConfirmedTracks.ToString());
        lines.Insert("  radar_origin " + m_RadarOrigin.ToString());

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_dwell_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar DwellTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar DwellTest] scans=%1 discovered=%2/%3 trackState=%4 locked=%5 maxConfirmed=%6",
            m_ScanCount.ToString(),
            m_DiscoveredCount.ToString(),
            TARGET_COUNT.ToString(),
            m_TrackStateScans.ToString(),
            m_LockedPointScans.ToString(),
            m_MaxConfirmedTracks.ToString()));
    }

    protected string BoolLabel(bool value)
    {
        if (value)
            return "PASS";
        return "FAIL";
    }
}
