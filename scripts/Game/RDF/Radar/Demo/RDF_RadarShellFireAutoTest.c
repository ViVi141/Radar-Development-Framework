// Live shell fire + radar WLR regression.
// Spawns Ammo_Shell_82mm_HE_O832DU, launches via ProjectileMoveComponent,
// then checks projectile plots / confirmed tracks / WLR vs truth launch.
//
// Shells get no detection aid: no RCS override, no registry pre-seeding. The
// ~0.01 m2 baked mortar signature has to carry the whole chain, which the
// counter-battery power budget below is sized for.
//
// Usage (Script Debugger, Play mode): RDF_RadarShellFireAutoTest.Start();
class RDF_RadarShellShotTruth
{
    vector m_LaunchPos;
    vector m_ImpactTruth;
    vector m_LaunchDir;
    float m_FireWallS;
    float m_SpeedCoef;
    bool m_ImpactTruthValid;
    bool m_SeenMoving;
    IEntity m_Shell;
}

class RDF_RadarShellFireAutoTest
{
    protected static ref RDF_RadarShellFireAutoTest s_Instance;
    protected static bool s_TickRegistered;

    protected static const ResourceName SHELL_PREFAB =
        "{98EC9C526AFBA282}Prefabs/Weapons/Ammo/Ammo_Shell_82mm_HE_O832DU.et";
    // Charge-2 style bullet coef from O832DU ring table (~1.736).
    protected static const float SHELL_SPEED_COEF = 1.736;
    protected static const float SHELL_ELEVATION_DEG = 55.0;
    protected static const float LAUNCH_OFFSET_M = 45.0;
    protected static const float LAUNCH_HEIGHT_M = 8.0;
    protected static const float FIRE_INTERVAL_S = 6.0;
    protected static const float TEST_DURATION_S = 42.0;
    protected static const float WLR_LAUNCH_PASS_M = 250.0;

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_LastFireWallS;
    protected float m_LastProgressWallS;
    protected int m_LastScanSerial = -1;

    protected IEntity m_Subject;
    protected vector m_RadarHintOrigin;
    protected vector m_FireAzimuthFlat;

    protected int m_ShellsFired;
    protected int m_ShellsSeenMoving;
    protected int m_ScanCount;
    protected int m_ProjectilePlotCount;
    protected int m_ProjectileDetectedCount;
    protected int m_MaxTrackHits;
    protected int m_ConfirmedProjectileTracks;
    protected int m_WlrValidCount;
    protected float m_BestLaunchErrM = 999999.0;
    protected float m_BestImpactErrM = 999999.0;
    protected float m_MaxSnrDb = -300.0;
    protected bool m_ShellDiscovered;
    protected float m_LastDebugWallS;

    protected ref array<ref RDF_RadarShellShotTruth> m_Shots;
    protected ref array<IEntity> m_LiveShells;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarShellFireAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarShellFireAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarShellFireAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF ShellFire AutoTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarShellFireAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarShellFireAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF ShellFire AutoTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("ShellFire"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF ShellFire AutoTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("ShellFire");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF ShellFire AutoTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("ShellFire");
            return;
        }

        m_PrevConfig = null;
        RDF_RadarSensor prevSensor = RDF_RadarAutoRunner.GetSensor();
        if (prevSensor)
            m_PrevConfig = prevSensor.GetSettings();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        m_RadarHintOrigin = mat[3];
        // Match RDF_RadarScanner.GetSubjectForward: mat[2] is scan boresight.
        vector fwd = mat[2];
        float flatLen = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        if (flatLen < 0.001)
        {
            m_FireAzimuthFlat = Vector(0.0, 0.0, 1.0);
        }
        else
        {
            m_FireAzimuthFlat = Vector(fwd[0] / flatLen, 0.0, fwd[2] / flatLen);
        }

        m_Shots = new array<ref RDF_RadarShellShotTruth>();
        m_LiveShells = new array<IEntity>();
        m_ShellsFired = 0;
        m_ShellsSeenMoving = 0;
        m_ScanCount = 0;
        m_ProjectilePlotCount = 0;
        m_ProjectileDetectedCount = 0;
        m_MaxTrackHits = 0;
        m_ConfirmedProjectileTracks = 0;
        m_WlrValidCount = 0;
        m_BestLaunchErrM = 999999.0;
        m_BestImpactErrM = 999999.0;
        m_MaxSnrDb = -300.0;
        m_ShellDiscovered = false;
        RDF_RadarSensor serialSensor = GetSensor();
        if (serialSensor)
            m_LastScanSerial = serialSensor.GetScanSerial();
        else
            m_LastScanSerial = -1;

        ApplyTestRadarConfig();
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        // Do not inherit tracks from the previous suite step.
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            sensor.ResetSession();

        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastFireWallS = m_StartWallS - FIRE_INTERVAL_S;
        m_LastProgressWallS = m_StartWallS;
        m_LastDebugWallS = m_StartWallS;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 100, true);
        }

        Print(string.Format(
            "[RDF ShellFire AutoTest] started subject=%1 shell=%2 elev=%3 coef=%4",
            m_RadarHintOrigin.ToString(),
            SHELL_PREFAB,
            SHELL_ELEVATION_DEG.ToString(),
            SHELL_SPEED_COEF.ToString()));

        // Fire immediately so the first arc is in coverage ASAP.
        TryFireShell();
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        CleanupLiveShells();
        RDF_RadarAutoTestGate.Release("ShellFire");

        if (!restore)
            return;

        if (m_PrevConfig)
            ApplySensorConfig(m_PrevConfig);
        RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
        RDF_RadarAutoRunner.SetDemoEnabled(m_PrevDemoEnabled);
        RDF_RadarAutoRunner.SetHudEnabled(m_PrevHudEnabled);
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

    protected void CleanupLiveShells()
    {
        if (!m_LiveShells)
            return;
        for (int i = 0; i < m_LiveShells.Count(); i++)
        {
            IEntity shell = m_LiveShells.Get(i);
            if (!shell)
                continue;
            RDF_RadarScattererRegistry.Unregister(shell);
            SCR_EntityHelper.DeleteEntityAndChildren(shell);
        }
        m_LiveShells.Clear();
    }

    protected void RefreshLiveShellScatterers(float nowS)
    {
        if (!m_LiveShells)
            return;

        int alive = 0;
        int inTable = 0;
        m_ShellsSeenMoving = 0;
        for (int i = m_LiveShells.Count() - 1; i >= 0; i--)
        {
            IEntity shell = m_LiveShells.Get(i);
            if (!shell)
            {
                m_LiveShells.Remove(i);
                continue;
            }

            vector pos = shell.GetOrigin();
            // Shells are never registered by the test; the sweep must find them.
            if (RDF_RadarScattererRegistry.Find(shell))
            {
                inTable = inTable + 1;
                m_ShellDiscovered = true;
            }
            alive = alive + 1;

            int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
            Shape.CreateSphere(ARGBF(1, 1, 0.15, 0.15), flags, pos, 2.5);
        }

        if (m_Shots)
        {
            for (int s = 0; s < m_Shots.Count(); s++)
            {
                RDF_RadarShellShotTruth shot = m_Shots.Get(s);
                if (!shot)
                    continue;
                if (shot.m_SeenMoving)
                {
                    m_ShellsSeenMoving = m_ShellsSeenMoving + 1;
                    continue;
                }
                if (!shot.m_Shell)
                    continue;
                vector pos = shot.m_Shell.GetOrigin();
                float dx = pos[0] - shot.m_LaunchPos[0];
                float dy = pos[1] - shot.m_LaunchPos[1];
                float dz = pos[2] - shot.m_LaunchPos[2];
                float moved = Math.Sqrt(dx * dx + dy * dy + dz * dz);
                if (moved > 15.0)
                {
                    shot.m_SeenMoving = true;
                    m_ShellsSeenMoving = m_ShellsSeenMoving + 1;
                    Print(string.Format(
                        "[RDF ShellFire AutoTest] shell in flight moved=%1m pos=%2",
                        moved.ToString(),
                        pos.ToString()));
                }
            }
        }

        if (nowS - m_LastDebugWallS < 3.0)
            return;
        m_LastDebugWallS = nowS;

        string scat = RDF_RadarScattererRegistry.GetStatsLine();
        int lastN = 0;
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
        {
            array<ref RDF_RadarTarget> last = sensor.GetPlots();
            if (last)
                lastN = last.Count();
        }
        Print(string.Format(
            "[RDF ShellFire AutoTest] liveShells=%1 inTable=%2 movingSeen=%3 plots=%4 det=%5 tracks=%6 lastTargets=%7 forceLocal=1 %8",
            alive.ToString(),
            inTable.ToString(),
            m_ShellsSeenMoving.ToString(),
            m_ProjectilePlotCount.ToString(),
            m_ProjectileDetectedCount.ToString(),
            m_ConfirmedProjectileTracks.ToString(),
            lastN.ToString(),
            scat));
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        RefreshLiveShellScatterers(nowS);
        DrawTruthMarkers();
        AccumulateLatestScan();
        EvaluateTracks();

        if (nowS - m_LastFireWallS >= FIRE_INTERVAL_S)
        {
            if (nowS - m_StartWallS < TEST_DURATION_S - 8.0)
                TryFireShell();
        }

        if (nowS - m_StartWallS < TEST_DURATION_S)
            return;

        FinalizeAndReport();
        StopInternal(true);
    }

    protected void ApplyTestRadarConfig()
    {
        // Sensor WLR preset, then widen sector / keep entity truth for assertions.
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateWlrSettings(128);
        cfg.m_Range = 8000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.15;
        cfg.m_IncludeVehicles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_IncludeProjectiles = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_KeepUndetected = true;
        cfg.m_DetectionSnrDb = -30.0;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMechanicalScan = false;
        cfg.m_EnableMeasurementSynthesis = true;
        // Synthesis otherwise rewrites type to ANONYMOUS and strips projectile WLR.
        cfg.m_KeepEntityTruth = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        cfg.m_MaxLosTracesPerScan = 96;
        // Shells live for seconds, so the sweep has to reach them quickly.
        cfg.m_ScattererDiscoveryIntervalS = 0.25;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_TrackConfirmHits = 2;
        cfg.m_TrackMaxMisses = 8;
        cfg.m_EnableBallisticPrediction = true;
        cfg.m_ShellAirDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
        cfg.m_EnableWeaponLocate = true;
        cfg.m_WeaponLocateMinHits = 2;

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 45.0;
        hw.m_ScanRpm = 0.0;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("mortar_low", 15.0, 28.0, 0.0);
        hw.AddElevationBeam("mortar_mid", 35.0, 30.0, 0.0);
        hw.AddElevationBeam("mortar_high", 55.0, 28.0, -0.5);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected vector BuildLaunchDirection()
    {
        float elevRad = SHELL_ELEVATION_DEG * Math.DEG2RAD;
        float c = Math.Cos(elevRad);
        float s = Math.Sin(elevRad);
        return Vector(
            m_FireAzimuthFlat[0] * c,
            s,
            m_FireAzimuthFlat[2] * c);
    }

    protected bool TryFireShell()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world || !m_Subject)
            return false;

        Resource prefabRes = Resource.Load(SHELL_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
        {
            Print("[RDF ShellFire AutoTest] failed to load shell prefab.", LogLevel.ERROR);
            return false;
        }

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector origin = mat[3];
        vector launchPos = origin
            + Vector(m_FireAzimuthFlat[0] * LAUNCH_OFFSET_M, 0.0, m_FireAzimuthFlat[2] * LAUNCH_OFFSET_M);
        float surfaceY = world.GetSurfaceY(launchPos[0], launchPos[2]);
        launchPos[1] = surfaceY + LAUNCH_HEIGHT_M;

        vector dir = BuildLaunchDirection();
        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = launchPos;

        IEntity shell = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!shell)
        {
            Print("[RDF ShellFire AutoTest] SpawnEntityPrefab failed.", LogLevel.ERROR);
            return false;
        }

        // Orient along launch direction (forward = dir).
        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = launchPos;
        shell.SetTransform(basis);

        ProjectileMoveComponent move = ProjectileMoveComponent.Cast(
            shell.FindComponent(ProjectileMoveComponent));
        if (!move)
        {
            Print("[RDF ShellFire AutoTest] no ProjectileMoveComponent.", LogLevel.ERROR);
            SCR_EntityHelper.DeleteEntityAndChildren(shell);
            return false;
        }

        // Cartridge/gadget shells stay inert until simulation is enabled.
        move.EnableSimulation(shell);
        move.SetBulletCoef(SHELL_SPEED_COEF);
        move.Launch(dir, vector.Zero, 1.0, shell, null, null, null, null);

        RDF_RadarShellShotTruth truth = new RDF_RadarShellShotTruth();
        truth.m_LaunchPos = launchPos;
        truth.m_LaunchDir = dir;
        truth.m_FireWallS = System.GetTickCount() * 0.001;
        truth.m_SpeedCoef = SHELL_SPEED_COEF;
        truth.m_ImpactTruthValid = false;

        // Prefab InitSpeed=76; muzzle state for truth impact estimate.
        float initSpeed = 76.0 * SHELL_SPEED_COEF;
        vector muzzleVel = dir * initSpeed;
        RDF_RadarGlobalWind wind = RDF_RadarBallistics.SampleGlobalWind();
        RDF_RadarWlrFix predict = RDF_RadarBallistics.SolveLaunchAndImpact(
            launchPos,
            muzzleVel,
            surfaceY,
            0.0,
            RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE,
            wind);
        if (predict && predict.m_ImpactValid)
        {
            truth.m_ImpactTruth = predict.m_ImpactPos;
            truth.m_ImpactTruthValid = true;
        }

        m_Shots.Insert(truth);
        truth.m_Shell = shell;
        m_LiveShells.Insert(shell);
        m_ShellsFired = m_ShellsFired + 1;
        m_LastFireWallS = truth.m_FireWallS;

        string impactFlag = "0";
        if (truth.m_ImpactTruthValid)
            impactFlag = "1";
        Print(string.Format(
            "[RDF ShellFire AutoTest] FIRE #%1 launch=%2 impactTruthValid=%3",
            m_ShellsFired.ToString(),
            launchPos.ToString(),
            impactFlag));
        return true;
    }

    protected void DrawTruthMarkers()
    {
        if (!m_Shots)
            return;
        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        for (int i = 0; i < m_Shots.Count(); i++)
        {
            RDF_RadarShellShotTruth shot = m_Shots.Get(i);
            if (!shot)
                continue;
            Shape.CreateSphere(ARGBF(1, 1, 0.45, 0.1), flags, shot.m_LaunchPos, 1.2);
            if (shot.m_ImpactTruthValid)
                Shape.CreateSphere(ARGBF(1, 0.2, 0.9, 1), flags, shot.m_ImpactTruth, 1.2);
        }
    }

    protected void AccumulateLatestScan()
    {
        float nowS = System.GetTickCount() * 0.001;
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;

        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
        {
            if (nowS - m_LastProgressWallS > 5.0)
            {
                m_LastProgressWallS = nowS;
                Print("[RDF ShellFire AutoTest] waiting for scan; ensure Play mode is running.");
            }
            return;
        }

        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;

        array<ref RDF_RadarTarget> targets = sensor.GetPlots();
        if (!targets)
            return;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;

            bool isProjectile = t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
            bool nearShell = IsPlotNearAnyLiveShell(t, 120.0);
            if (!isProjectile && !nearShell)
                continue;

            m_ProjectilePlotCount = m_ProjectilePlotCount + 1;
            if (t.m_Detected)
                m_ProjectileDetectedCount = m_ProjectileDetectedCount + 1;
            if (t.m_SnrDb > m_MaxSnrDb)
                m_MaxSnrDb = t.m_SnrDb;
        }
    }

    protected bool IsPlotNearAnyLiveShell(RDF_RadarTarget plot, float gateM)
    {
        if (!plot || !m_LiveShells)
            return false;
        for (int i = 0; i < m_LiveShells.Count(); i++)
        {
            IEntity shell = m_LiveShells.Get(i);
            if (!shell)
                continue;
            vector sp = shell.GetOrigin();
            float dx = plot.m_Position[0] - sp[0];
            float dy = plot.m_Position[1] - sp[1];
            float dz = plot.m_Position[2] - sp[2];
            float dist = Math.Sqrt(dx * dx + dy * dy + dz * dz);
            if (dist <= gateM)
                return true;
        }
        return false;
    }

    protected void EvaluateTracks()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;

        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        if (!tracks)
            return;

        m_ConfirmedProjectileTracks = 0;
        m_WlrValidCount = 0;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr || !tr.IsProjectileTrack())
                continue;
            if (tr.m_HitCount > m_MaxTrackHits)
                m_MaxTrackHits = tr.m_HitCount;
            if (!tr.m_Confirmed)
                continue;

            m_ConfirmedProjectileTracks = m_ConfirmedProjectileTracks + 1;
            RDF_RadarWlrFix fix = tr.m_LastWlrFix;
            if (!fix || !fix.m_LaunchValid)
                continue;

            m_WlrValidCount = m_WlrValidCount + 1;
            float launchErr = NearestLaunchErrorM(fix.m_LaunchPos);
            if (launchErr < m_BestLaunchErrM)
                m_BestLaunchErrM = launchErr;

            if (fix.m_ImpactValid)
            {
                float impactErr = NearestImpactErrorM(fix.m_ImpactPos);
                if (impactErr < m_BestImpactErrM)
                    m_BestImpactErrM = impactErr;
            }
        }
    }

    protected float NearestLaunchErrorM(vector wlrLaunch)
    {
        float best = 999999.0;
        if (!m_Shots)
            return best;
        for (int i = 0; i < m_Shots.Count(); i++)
        {
            RDF_RadarShellShotTruth shot = m_Shots.Get(i);
            if (!shot)
                continue;
            float dx = wlrLaunch[0] - shot.m_LaunchPos[0];
            float dz = wlrLaunch[2] - shot.m_LaunchPos[2];
            float err = Math.Sqrt(dx * dx + dz * dz);
            if (err < best)
                best = err;
        }
        return best;
    }

    protected float NearestImpactErrorM(vector wlrImpact)
    {
        float best = 999999.0;
        if (!m_Shots)
            return best;
        for (int i = 0; i < m_Shots.Count(); i++)
        {
            RDF_RadarShellShotTruth shot = m_Shots.Get(i);
            if (!shot || !shot.m_ImpactTruthValid)
                continue;
            float dx = wlrImpact[0] - shot.m_ImpactTruth[0];
            float dz = wlrImpact[2] - shot.m_ImpactTruth[2];
            float err = Math.Sqrt(dx * dx + dz * dz);
            if (err < best)
                best = err;
        }
        return best;
    }

    protected string BoolLabel(bool ok)
    {
        if (ok)
            return "PASS";
        return "FAIL";
    }

    protected void FinalizeAndReport()
    {
        bool passFired = m_ShellsFired >= 3;
        bool passMoving = m_ShellsSeenMoving >= 1;
        bool passScans = m_ScanCount >= 20;
        bool passPlots = m_ProjectileDetectedCount >= 2;
        bool passTrack = m_ConfirmedProjectileTracks >= 1;
        bool passWlr = m_WlrValidCount >= 1 && m_BestLaunchErrM <= WLR_LAUNCH_PASS_M;
        // Shells were never registered by the test, so table membership proves
        // the normal discovery sweep picked them up in flight.
        bool passDiscovery = m_ShellDiscovered;
        bool allPass = passFired && passMoving && passScans && passPlots && passTrack
            && passWlr && passDiscovery;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Shell Fire AutoTest");
        lines.Insert("prefab " + SHELL_PREFAB);
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  shells_fired " + BoolLabel(passFired));
        lines.Insert("  shells_in_flight " + BoolLabel(passMoving));
        lines.Insert("  discovered_unaided " + BoolLabel(passDiscovery));
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  projectile_detected " + BoolLabel(passPlots));
        lines.Insert("  confirmed_projectile_track " + BoolLabel(passTrack));
        lines.Insert("  wlr_launch_near_truth " + BoolLabel(passWlr));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  shells_fired " + m_ShellsFired.ToString());
        lines.Insert("  shells_in_flight " + m_ShellsSeenMoving.ToString());
        lines.Insert("  scans " + m_ScanCount.ToString());
        lines.Insert("  projectile_plots " + m_ProjectilePlotCount.ToString());
        lines.Insert("  projectile_detected " + m_ProjectileDetectedCount.ToString());
        lines.Insert("  max_track_hits " + m_MaxTrackHits.ToString());
        lines.Insert("  confirmed_projectile_tracks " + m_ConfirmedProjectileTracks.ToString());
        lines.Insert("  wlr_valid " + m_WlrValidCount.ToString());
        lines.Insert("  best_launch_err_m " + m_BestLaunchErrM.ToString());
        lines.Insert("  best_impact_err_m " + m_BestImpactErrM.ToString());
        lines.Insert("  max_snr_db " + m_MaxSnrDb.ToString());

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_shellfire_autotest_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF ShellFire AutoTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF ShellFire AutoTest] fired=%1 moving=%2 scans=%3 plots=%4 det=%5 tracks=%6 wlr=%7 launchErr=%8",
            m_ShellsFired.ToString(),
            m_ShellsSeenMoving.ToString(),
            m_ScanCount.ToString(),
            m_ProjectilePlotCount.ToString(),
            m_ProjectileDetectedCount.ToString(),
            m_ConfirmedProjectileTracks.ToString(),
            m_WlrValidCount.ToString(),
            m_BestLaunchErrM.ToString()));
    }
}
