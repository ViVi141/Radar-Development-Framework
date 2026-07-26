// Lock-layer demo / test: spawn a STATIONARY ground vehicle near the player on
// flat terrain with clear LOS, run SEARCH with auto-acquire, and verify
// SEARCH -> ACQUIRING -> TRACKING with a usable aim point.
//
// Do NOT teleport vehicles with SetOrigin every tick — that drives them into
// terrain and triggers damage / explosion in Reforger physics.
//
// Usage (Script Debugger):
//   RDF_RadarLockAutoTest.Start();
//   RDF_RadarLockAutoTest.StartKeepTarget();
class RDF_RadarLockAutoTest
{
    protected static ref RDF_RadarLockAutoTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_KeepTargetAfterTest;

    protected static const ResourceName GROUND_TARGET_PREFAB =
        "{94DE32169691AC34}Prefabs/Vehicles/Wheeled/M151A2/M151A2_transport_MERDC.et";

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_DurationS = 16.0;
    protected int m_LastScanSerial = -1;

    protected IEntity m_Subject;
    protected IEntity m_Target;
    protected vector m_RadarOrigin;
    protected vector m_TargetSpawnPos;
    protected float m_PlaceRangeM = 90.0;

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
    protected bool m_ReachedAcquiring;
    protected bool m_ReachedTracking;
    protected bool m_TargetAliveAtEnd;
    protected int m_LastLoggedState = -1;
    protected float m_LastProgressWallS;

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
        GetInstance().StartInternal();
    }

    static void StartKeepTarget()
    {
        s_KeepTargetAfterTest = true;
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

        m_PrevConfig = RDF_RadarAutoRunner.GetDemoConfig();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = false;

        if (!ChooseFlatSpawn())
        {
            Print("[RDF Radar LockTest] failed to choose flat spawn near subject.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Lock");
            return;
        }

        if (!SpawnTarget())
        {
            Print("[RDF Radar LockTest] failed to spawn vehicle prefab.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        // Force local channel so Workbench RplComponent does not divert scans.
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
        m_ReachedAcquiring = false;
        m_ReachedTracking = false;
        m_TargetAliveAtEnd = false;
        m_LastLoggedState = -1;
        m_LastScanSerial = RDF_RadarAutoRunner.GetLastScanSerial();

        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastProgressWallS = m_StartWallS;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print(string.Format(
            "[RDF Radar LockTest] started radarOrigin=%1 target=%2 range=%3m",
            m_RadarOrigin.ToString(),
            m_TargetSpawnPos.ToString(),
            m_PlaceRangeM.ToString()));
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Lock");

        if (m_Target && !s_KeepTargetAfterTest)
        {
            RDF_RadarEmitterRegistry.Unregister(m_Target);
            SCR_EntityHelper.DeleteEntityAndChildren(m_Target);
            m_Target = null;
        }

        if (!restore)
            return;

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
            sensor.GetLockManager().Unlock();

        if (m_PrevConfig)
            RDF_RadarAutoRunner.SetDemoConfig(m_PrevConfig);
        RDF_RadarAutoRunner.SetDemoEnabled(m_PrevDemoEnabled);
        RDF_RadarAutoRunner.SetHudEnabled(m_PrevHudEnabled);
        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        // Stationary target: only refresh scatterer registration, never teleport.
        KeepTargetRegistered();
        DrawDebugMarker();
        AccumulateLatestScan(nowS);

        if (nowS - m_StartWallS < m_DurationS)
            return;

        m_TargetAliveAtEnd = IsTargetAlive();
        FinalizeAndReport();
        StopInternal(true);
    }

    // Pick a flat, near-level patch around the subject so LOS is not blocked by
    // a cliff between radar and the parked vehicle.
    protected bool ChooseFlatSpawn()
    {
        if (!m_Subject)
            return false;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector center = mat[3];
        m_RadarOrigin = center;

        vector best = "0 0 0";
        float bestScore = 9999999.0;
        bool found = false;
        float sampleOffset = 12.0;

        for (int i = 0; i < 16; i++)
        {
            float angle = i * Math.PI * 2.0 / 16.0;
            float x = center[0] + Math.Cos(angle) * m_PlaceRangeM;
            float z = center[2] + Math.Sin(angle) * m_PlaceRangeM;
            float y = world.GetSurfaceY(x, z);

            float hx1 = world.GetSurfaceY(x + sampleOffset, z);
            float hx2 = world.GetSurfaceY(x - sampleOffset, z);
            float hz1 = world.GetSurfaceY(x, z + sampleOffset);
            float hz2 = world.GetSurfaceY(x, z - sampleOffset);
            float localRough = Math.AbsFloat(hx1 - hx2) + Math.AbsFloat(hz1 - hz2);
            float heightDelta = Math.AbsFloat(y - center[1]);
            float score = localRough + heightDelta * 0.35;
            if (score < bestScore)
            {
                bestScore = score;
                best = Vector(x, y + 1.0, z);
                found = true;
            }
        }

        if (!found)
            return false;

        m_TargetSpawnPos = best;
        return true;
    }

    protected bool SpawnTarget()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(GROUND_TARGET_PREFAB);
        if (!prefabRes)
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = m_TargetSpawnPos;

        m_Target = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_Target)
            return false;

        // Park it: zero velocity if physics exists. Do not SetOrigin later.
        Physics physics = m_Target.GetPhysics();
        if (physics)
            physics.SetVelocity("0 0 0");

        RDF_RadarEmitterRegistry.Register(m_Target, m_TargetSpawnPos, false, 1.0);
        Print("[RDF Radar LockTest] parked vehicle at " + m_TargetSpawnPos.ToString());
        return true;
    }

    protected void ApplyTestRadarConfig()
    {
        // Lock-layer regression: prove SEARCH→ACQUIRE→TRACK on a real vehicle.
        // Use geometry detection only — SNR/CFAR/NLOS are covered by other tests.
        RDF_RadarSettings cfg = RDF_RadarDemoConfig.CreateSearch(128);
        cfg.m_Range = 2000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = false;
        cfg.m_KeepUndetected = true;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableNlosMultipath = true;
        cfg.m_ScattererClassifyPerTick = 128;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_KeepEntityTruth = true;
        cfg.Validate();
        RDF_RadarAutoRunner.SetDemoConfig(cfg);
    }

    protected void ConfigureLock()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        lockMgr.Unlock();
        lockMgr.SetAutoAcquire(true);
        lockMgr.SetTypeFilter(true, false, false);
        lockMgr.SetMaxLockRange(2000.0);
        lockMgr.SetLockSector(0.0);
        lockMgr.SetAcquireHits(2);
        lockMgr.SetCoastMaxSec(2.0);
    }

    protected void KeepTargetRegistered()
    {
        if (!IsTargetAlive())
            return;
        vector pos = m_Target.GetOrigin();
        RDF_RadarEmitterRegistry.Register(m_Target, pos, false, 1.0);
    }

    protected bool IsTargetAlive()
    {
        if (!m_Target)
            return false;
        // Destroyed / deleted entities become null after SCR_EntityHelper cleanup,
        // but damage wrecks may still exist — treat null origin as dead.
        vector pos = m_Target.GetOrigin();
        if (pos[0] == 0.0 && pos[1] == 0.0 && pos[2] == 0.0)
            return false;
        return true;
    }

    protected void DrawDebugMarker()
    {
        if (!IsTargetAlive())
            return;
        vector pos = m_Target.GetOrigin();
        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 0.2, 1, 0.2), flags, pos + "0 2 0", 2.5);
    }

    protected void AccumulateLatestScan(float nowS)
    {
        int serial = RDF_RadarAutoRunner.GetLastScanSerial();
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

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;

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
        if (lockMgr.GetLockedTarget(lockedEntity, lockedPos))
            m_LockedTargetScans = m_LockedTargetScans + 1;

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
                        " snr=%1 losBlocked=%2 detectedFlag=%3",
                        p0.m_SnrDb.ToString(),
                        p0.m_LosBlocked.ToString(),
                        p0.m_Detected.ToString());
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
        // If we jump straight to TRACKING in one scan (already confirmed),
        // count that as acquire success too.
        if (passTrack && !passAcquire)
            passAcquire = true;
        bool allPass = passScans && passAlive && passPlots && passTracks
            && passAcquire && passTrack && passLockedPoint;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Lock Test");
        lines.Insert("prefab " + GROUND_TARGET_PREFAB);
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  target_alive " + BoolLabel(passAlive));
        lines.Insert("  plots_any " + BoolLabel(passAnyPlots));
        lines.Insert("  plots_detected " + BoolLabel(passPlots));
        lines.Insert("  tracks_present " + BoolLabel(passTracks));
        lines.Insert("  reached_acquiring " + BoolLabel(passAcquire));
        lines.Insert("  reached_tracking " + BoolLabel(passTrack));
        lines.Insert("  locked_point_usable " + BoolLabel(passLockedPoint));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  scan_count " + m_ScanCount.ToString());
        lines.Insert("  any_plot_scans " + m_AnyPlotScans.ToString());
        lines.Insert("  detected_plot_scans " + m_DetectedPlotScans.ToString());
        lines.Insert("  track_present_scans " + m_TrackPresentScans.ToString());
        lines.Insert("  max_any_plots " + m_MaxAnyPlots.ToString());
        lines.Insert("  max_detected_plots " + m_MaxDetectedPlots.ToString());
        lines.Insert("  max_tracks " + m_MaxTracks.ToString());
        lines.Insert("  acquire_scans " + m_AcquireScans.ToString());
        lines.Insert("  track_scans " + m_TrackScans.ToString());
        lines.Insert("  locked_target_scans " + m_LockedTargetScans.ToString());
        lines.Insert("  radar_origin " + m_RadarOrigin.ToString());
        lines.Insert("  target_spawn " + m_TargetSpawnPos.ToString());

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_lock_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar LockTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar LockTest] scans=%1 anyPlots=%2 detected=%3 tracks=%4 acquire=%5 track=%6 locked=%7 alive=%8",
            m_ScanCount.ToString(),
            m_MaxAnyPlots.ToString(),
            m_MaxDetectedPlots.ToString(),
            m_MaxTracks.ToString(),
            m_AcquireScans.ToString(),
            m_TrackScans.ToString(),
            m_LockedTargetScans.ToString(),
            m_TargetAliveAtEnd.ToString()));
    }

    protected string BoolLabel(bool value)
    {
        if (value)
            return "PASS";
        return "FAIL";
    }
}
