// Lock-layer test: orbit a Mi-8 under PHYSICAL radar detection (SNR / LOS /
// optional DEM clutter), then verify SEARCH -> ACQUIRING -> TRACKING.
// Helicopters can be SetOrigin-moved in air; ground vehicles must not.
//
// The target gets no detection aid: no emitter flag, no RCS override, no
// registry pre-seeding. It must be found by the normal discovery sweep and
// detected from its own signature.
//
// Usage (Script Debugger):
//   RDF_RadarLockAutoTest.Start();
//   RDF_RadarLockAutoTest.StartKeepTarget();
class RDF_RadarLockAutoTest
{
    protected static ref RDF_RadarLockAutoTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_KeepTargetAfterTest;
    protected static bool s_LastPass;

    protected static const ResourceName AIR_TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";

    protected bool m_Running;
    protected float m_StartWallS;
    // Long enough for a cold scatterer table to classify the freshly spawned
    // airframe without any registry pre-seeding.
    protected float m_DurationS = 35.0;
    protected int m_LastScanSerial = -1;

    protected IEntity m_Subject;
    protected IEntity m_Target;
    protected vector m_RadarOrigin;
    protected vector m_OrbitCenter;

    // Racetrack centred down the boresight instead of a ring around the radar:
    // 120 m of lateral swing at 800 m is +-8.5 deg, so a realistic pencil beam
    // keeps the target illuminated for the whole run.
    protected float m_OrbitCenterRangeM = 800.0;
    protected float m_OrbitRadiusM = 120.0;
    protected float m_OrbitAltitudeM = 150.0;
    protected float m_OrbitRateRadS = 0.16;
    protected float m_OrbitPhaseRad = 0.0;
    protected float m_LastDebugPrintWallS;

    protected int m_ScanCount;
    protected int m_AcquireScans;
    protected int m_TrackScans;
    protected int m_LockedTargetScans;
    protected int m_DetectedPlotScans;
    protected int m_AnyPlotScans;
    protected int m_TrackPresentScans;
    protected int m_MaxDetectedPlots;
    protected int m_MaxAnyPlots;
    protected int m_MaxTracks;
    protected float m_MaxSnrDb = -300.0;
    protected bool m_TargetDiscovered;
    protected bool m_ReachedAcquiring;
    protected bool m_ReachedTracking;
    protected bool m_TargetAliveAtEnd;
    protected int m_LastLoggedState = -1;
    protected float m_LastProgressWallS;
    protected int m_RespawnCount;
    protected int m_RwrSearchScans;
    protected int m_RwrLockScans;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarLockAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarLockAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_KeepTargetAfterTest = false;
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void StartKeepTarget()
    {
        s_KeepTargetAfterTest = true;
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarLockAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar LockTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarLockAutoTest inst = GetInstance();
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
        RDF_RadarLockAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar LockTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Lock"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar LockTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Lock");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar LockTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Lock");
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
            Print("[RDF Radar LockTest] failed to choose radar origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Lock");
            return;
        }

        // Clock before spawn so orbit phase 0 matches spawn position.
        m_StartWallS = System.GetTickCount() * 0.001;

        if (!SpawnAirTarget())
        {
            Print("[RDF Radar LockTest] failed to spawn Mi-8 prefab.", LogLevel.ERROR);
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
        m_AcquireScans = 0;
        m_TrackScans = 0;
        m_LockedTargetScans = 0;
        m_DetectedPlotScans = 0;
        m_AnyPlotScans = 0;
        m_TrackPresentScans = 0;
        m_MaxDetectedPlots = 0;
        m_MaxAnyPlots = 0;
        m_MaxTracks = 0;
        m_MaxSnrDb = -300.0;
        m_TargetDiscovered = false;
        m_ReachedAcquiring = false;
        m_ReachedTracking = false;
        m_TargetAliveAtEnd = false;
        m_LastLoggedState = -1;
        m_RespawnCount = 0;
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            m_LastScanSerial = sensor.GetScanSerial();
        else
            m_LastScanSerial = -1;
        m_LastProgressWallS = m_StartWallS;
        m_LastDebugPrintWallS = m_StartWallS;
        m_RwrSearchScans = 0;
        m_RwrLockScans = 0;
        m_Running = true;

        RDF_RadarAutoTestMapOverlay.Start();
        if (m_Target)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_Target, "RDF Lock");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print(string.Format(
            "[RDF Radar LockTest] started PHYSICAL lock radarOrigin=%1 orbitCenter=%2 radius=%3 alt=%4",
            m_RadarOrigin.ToString(),
            m_OrbitCenter.ToString(),
            m_OrbitRadiusM.ToString(),
            m_OrbitAltitudeM.ToString()));
        Print("[RDF Radar LockTest] open map (M) for aircraft + trail/pred path markers.");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Lock");
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();

        if (m_Target && !s_KeepTargetAfterTest)
        {
            RDF_RadarScattererRegistry.Unregister(m_Target);
            SCR_EntityHelper.DeleteEntityAndChildren(m_Target);
            m_Target = null;
        }

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

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        UpdateAirTargetMotion(nowS);
        if (m_Target)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_Target, "RDF Lock");
        AccumulateLatestScan(nowS);

        if (nowS - m_StartWallS < m_DurationS)
            return;

        m_TargetAliveAtEnd = IsTargetAlive();
        FinalizeAndReport();
        StopInternal(true);
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

        // Scanner boresight is the subject's mat[2] (Enfusion forward).
        vector fwd = mat[2];
        float flatLen = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        vector flatFwd;
        if (flatLen < 0.001)
            flatFwd = Vector(0.0, 0.0, 1.0);
        else
            flatFwd = Vector(fwd[0] / flatLen, 0.0, fwd[2] / flatLen);

        m_OrbitCenter = Vector(
            m_RadarOrigin[0] + flatFwd[0] * m_OrbitCenterRangeM,
            m_RadarOrigin[1] + m_OrbitAltitudeM,
            m_RadarOrigin[2] + flatFwd[2] * m_OrbitCenterRangeM);
        return true;
    }

    protected float GetElapsedS()
    {
        if (m_StartWallS <= 0.0)
            return 0.0;
        float elapsed = System.GetTickCount() * 0.001 - m_StartWallS;
        if (elapsed < 0.0)
            return 0.0;
        return elapsed;
    }

    protected vector ComputeOrbitPos(float elapsedS)
    {
        float phase = elapsedS * m_OrbitRateRadS;
        return Vector(
            m_OrbitCenter[0] + Math.Cos(phase) * m_OrbitRadiusM,
            m_OrbitCenter[1] + 12.0 * Math.Sin(elapsedS * 0.07),
            m_OrbitCenter[2] + Math.Sin(phase) * m_OrbitRadiusM);
    }

    protected bool SpawnAirTarget()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(AIR_TARGET_PREFAB);
        if (!prefabRes)
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        vector spawnPos = ComputeOrbitPos(GetElapsedS());
        spawnParams.Transform[3] = spawnPos;

        m_Target = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_Target)
            return false;

        // No registry pre-seeding and no emitter flag: the scatterer sweep has to
        // find this airframe on its own, exactly as it would any spawned vehicle.
        Print("[RDF Radar LockTest] spawned Mi-8 at " + spawnPos.ToString());
        return true;
    }

    protected void ApplyTestRadarConfig()
    {
        // Physical STARE lock regression on a moving airframe (Sensor preset).
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateStareSettings(128);
        cfg.m_Range = 3000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 8.0;
        // Keep undetected plots so a miss shows its SNR instead of vanishing.
        cfg.m_KeepUndetected = true;
        // Ground clutter has its own regression (RDF_RadarAutoTest). The clutter
        // model illuminates the ground patch with the target's pattern gain, so
        // it has no elevation discrimination and buries an airborne target; that
        // would test the clutter model, not the lock state machine.
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableNlosMultipath = true;
        cfg.m_UseScattererRegistry = true;
        // Spawned target must be discovered by the sweep, so drain the classify
        // queue fast and leave room for a populated world in the table.
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableRwrReporting = true;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);

        // Fire-control pencil beam staring down the boresight. A wide beam would
        // need a proportionally wide clutter cell and coarse range gate, which is
        // what a tracking radar exists to avoid.
        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 20.0;
        hw.m_BandwidthHz = 20000000.0;
        hw.m_ScanRpm = 0.0;
        // Moving heli has Doppler; MTI may stay on. Keep off so early orbit
        // samples are not starved before velocity is established.
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        // Racetrack sits near 11 deg elevation and drifts a couple of degrees.
        hw.AddElevationBeam("lock_low", 6.0, 14.0, 0.0);
        hw.AddElevationBeam("lock_mid", 14.0, 16.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        if (RDF_RadarAutoTestSuite.IsRealisticChannel())
        {
            cfg.ApplyRealisticChannel();
            // Keep entity for lock association; CFAR+thermal stay on.
            cfg.m_KeepEntityTruth = true;
        }
        else
        {
            cfg.ApplyIdealChannel();
            cfg.m_EnableCfarGate = false;
            cfg.m_KeepEntityTruth = true;
            cfg.m_EnableMeasurementSynthesis = false;
        }
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void ConfigureLock()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        // Tracks from the previous suite step would otherwise let this test
        // report TRACKING and a locked point before it has seen anything.
        sensor.ResetSession();

        // Lock layer is part of the public Sensor API (see docs/RADAR_API.md).
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        lockMgr.SetAutoAcquire(true);
        lockMgr.SetTypeFilter(true, false, false);
        lockMgr.SetMaxLockRange(3000.0);
        lockMgr.SetLockSector(0.0);
        lockMgr.SetAcquireHits(2);
        lockMgr.SetCoastMaxSec(3.0);
    }

    protected void UpdateAirTargetMotion(float nowS)
    {
        if (!m_Target)
        {
            if (SpawnAirTarget())
            {
                m_RespawnCount = m_RespawnCount + 1;
                Print("[RDF Radar LockTest] Mi-8 respawned count=" + m_RespawnCount.ToString());
            }
            return;
        }

        float elapsedS = GetElapsedS();
        m_OrbitPhaseRad = elapsedS * m_OrbitRateRadS;
        vector pos = ComputeOrbitPos(elapsedS);
        m_Target.SetOrigin(pos);

        float vx = -Math.Sin(m_OrbitPhaseRad) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vz = Math.Cos(m_OrbitPhaseRad) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vy = 12.0 * 0.07 * Math.Cos(elapsedS * 0.07);
        Physics physics = m_Target.GetPhysics();
        if (physics)
            physics.SetVelocity(Vector(vx, vy, vz));

        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 0.2, 1, 0.2), flags, pos, 5.0);

        if (nowS - m_LastDebugPrintWallS > 3.0)
        {
            m_LastDebugPrintWallS = nowS;
            float dist = vector.Distance(pos, m_RadarOrigin);
            Print("[RDF Radar LockTest] heli pos=" + pos.ToString()
                + " range=" + dist.ToString()
                + " inTable=" + IsTargetInScattererTable().ToString()
                + " " + RDF_RadarScattererRegistry.GetStatsLine());
        }
    }

    // Discovery evidence: the sweep found the airframe without help from the test.
    protected bool IsTargetInScattererTable()
    {
        if (!m_Target)
            return false;
        return RDF_RadarScattererRegistry.Find(m_Target) != null;
    }

    protected bool IsTargetAlive()
    {
        if (!m_Target)
            return false;
        vector pos = m_Target.GetOrigin();
        if (pos[0] == 0.0 && pos[1] == 0.0 && pos[2] == 0.0)
            return false;
        return true;
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
                Print("[RDF Radar LockTest] waiting for scan; ensure Play mode is running.");
            }
            return;
        }
        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;
        if (IsTargetInScattererTable())
            m_TargetDiscovered = true;

        int detected = sensor.CountDetectedPlots();
        int anyPlots = 0;
        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        if (plots)
            anyPlots = plots.Count();
        if (anyPlots > 0)
            m_AnyPlotScans = m_AnyPlotScans + 1;
        if (anyPlots > m_MaxAnyPlots)
            m_MaxAnyPlots = anyPlots;
        if (detected > 0)
            m_DetectedPlotScans = m_DetectedPlotScans + 1;
        if (detected > m_MaxDetectedPlots)
            m_MaxDetectedPlots = detected;

        if (plots)
        {
            // Undetected plots are kept, so record their SNR too: a miss then
            // reports how far under threshold it landed instead of just -300.
            foreach (RDF_RadarTarget t : plots)
            {
                if (!t)
                    continue;
                if (t.m_SnrDb > m_MaxSnrDb)
                    m_MaxSnrDb = t.m_SnrDb;
            }
        }

        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        int trackCount = 0;
        if (tracks)
            trackCount = tracks.Count();
        if (trackCount > 0)
            m_TrackPresentScans = m_TrackPresentScans + 1;
        if (trackCount > m_MaxTracks)
            m_MaxTracks = trackCount;

        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        ERDF_RadarLockState state = lockMgr.GetState();
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING)
        {
            m_AcquireScans = m_AcquireScans + 1;
            m_ReachedAcquiring = true;
        }
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
        {
            m_TrackScans = m_TrackScans + 1;
            m_ReachedTracking = true;
        }

        IEntity lockedEntity;
        vector lockedPos;
        if (sensor.GetLockedTarget(lockedEntity, lockedPos))
            m_LockedTargetScans = m_LockedTargetScans + 1;

        if (m_Target)
        {
            if (RDF_RadarRwr.HasSearchWarning(m_Target))
                m_RwrSearchScans = m_RwrSearchScans + 1;
            if (RDF_RadarRwr.HasLockWarning(m_Target))
                m_RwrLockScans = m_RwrLockScans + 1;
        }

        if (state != m_LastLoggedState)
        {
            m_LastLoggedState = state;
            string plotDiag = "";
            if (plots && plots.Count() > 0)
            {
                RDF_RadarTarget p0 = plots.Get(0);
                if (p0)
                {
                    plotDiag = string.Format(
                        " snr=%1 losBlocked=%2 detectedFlag=%3 beam=%4",
                        p0.m_SnrDb.ToString(),
                        p0.m_LosBlocked.ToString(),
                        p0.m_Detected.ToString(),
                        p0.m_BeamName);
                }
            }
            Print("[RDF Radar LockTest] " + lockMgr.GetStatusShort()
                + " plots=" + anyPlots.ToString()
                + " detected=" + detected.ToString()
                + " tracks=" + trackCount.ToString()
                + " alive=" + IsTargetAlive().ToString()
                + plotDiag);
        }
    }

    protected void FinalizeAndReport()
    {
        bool passScans = m_ScanCount > 6;
        bool passAlive = m_TargetAliveAtEnd;
        bool passPlots = m_DetectedPlotScans > 0;
        bool passAnyPlots = m_AnyPlotScans > 0;
        bool passTracks = m_TrackPresentScans > 0;
        bool passAcquire = m_ReachedAcquiring;
        bool passTrack = m_ReachedTracking;
        bool passLockedPoint = m_LockedTargetScans > 0;
        bool passRwrSearch = m_RwrSearchScans > 0;
        bool passRwrLock = m_RwrLockScans > 0;
        // Physical path evidence: SNR must have been computed (not left at the
        // uninitialized -300 floor, and not the geometry placeholder ~0).
        bool passPhysicalSnr = m_MaxSnrDb > -299.0;
        if (passPhysicalSnr)
        {
            if (m_MaxSnrDb > -0.5 && m_MaxSnrDb < 0.5)
                passPhysicalSnr = false;
        }
        // The airframe was never registered or flagged by the test, so table
        // membership proves the normal discovery sweep found it.
        bool passDiscovery = m_TargetDiscovered;
        if (passTrack && !passAcquire)
            passAcquire = true;
        bool allPass = passScans && passAlive && passPlots && passTracks
            && passAcquire && passTrack && passLockedPoint && passPhysicalSnr
            && passDiscovery && passRwrSearch && passRwrLock;
        s_LastPass = allPass;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Lock Test (physical Mi-8 orbit)");
        lines.Insert("prefab " + AIR_TARGET_PREFAB);
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  target_alive " + BoolLabel(passAlive));
        lines.Insert("  discovered_unaided " + BoolLabel(passDiscovery));
        lines.Insert("  plots_any " + BoolLabel(passAnyPlots));
        lines.Insert("  plots_detected " + BoolLabel(passPlots));
        lines.Insert("  tracks_present " + BoolLabel(passTracks));
        lines.Insert("  reached_acquiring " + BoolLabel(passAcquire));
        lines.Insert("  reached_tracking " + BoolLabel(passTrack));
        lines.Insert("  locked_point_usable " + BoolLabel(passLockedPoint));
        lines.Insert("  rwr_search " + BoolLabel(passRwrSearch));
        lines.Insert("  rwr_lock " + BoolLabel(passRwrLock));
        lines.Insert("  physical_snr " + BoolLabel(passPhysicalSnr));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  scan_count " + m_ScanCount.ToString());
        lines.Insert("  any_plot_scans " + m_AnyPlotScans.ToString());
        lines.Insert("  detected_plot_scans " + m_DetectedPlotScans.ToString());
        lines.Insert("  track_present_scans " + m_TrackPresentScans.ToString());
        lines.Insert("  max_any_plots " + m_MaxAnyPlots.ToString());
        lines.Insert("  max_detected_plots " + m_MaxDetectedPlots.ToString());
        lines.Insert("  max_tracks " + m_MaxTracks.ToString());
        lines.Insert("  max_snr_db " + m_MaxSnrDb.ToString());
        lines.Insert("  acquire_scans " + m_AcquireScans.ToString());
        lines.Insert("  track_scans " + m_TrackScans.ToString());
        lines.Insert("  locked_target_scans " + m_LockedTargetScans.ToString());
        lines.Insert("  rwr_search_scans " + m_RwrSearchScans.ToString());
        lines.Insert("  rwr_lock_scans " + m_RwrLockScans.ToString());
        lines.Insert("  respawn_count " + m_RespawnCount.ToString());
        lines.Insert("  radar_origin " + m_RadarOrigin.ToString());
        lines.Insert("  map_overlay open M-key map for live aircraft marker");

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_lock_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar LockTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar LockTest] scans=%1 detected=%2 tracks=%3 acquire=%4 track=%5 locked=%6 maxSnr=%7 alive=%8",
            m_ScanCount.ToString(),
            m_MaxDetectedPlots.ToString(),
            m_MaxTracks.ToString(),
            m_AcquireScans.ToString(),
            m_TrackScans.ToString(),
            m_LockedTargetScans.ToString(),
            m_MaxSnrDb.ToString(),
            m_TargetAliveAtEnd.ToString()));
    }

    protected string BoolLabel(bool value)
    {
        if (value)
            return "PASS";
        return "FAIL";
    }
}
