// Demo: PHYSICAL scan -> lock Mi-8 -> fire Hydra70 with ARH-style script
// guidance (boost / midcourse / terminal PN). Official guidance is not usable.
//
// Usage (Script Debugger, Play mode):
//   RDF_RadarRocketLockFireAutoTest.Start();
// Standalone — not in StartAll.
class RDF_RadarRocketLockFireAutoTest
{
    protected static ref RDF_RadarRocketLockFireAutoTest s_Instance;
    protected static bool s_TickRegistered;

    protected static const ResourceName AIR_TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";
    protected static const ResourceName ROCKET_PREFAB =
        "{ECD8628EBF7E5F6B}Prefabs/Weapons/Ammo/Ammo_Rocket_Hydra70.et";

    protected static const float LAUNCH_OFFSET_M = 8.0;
    protected static const float LAUNCH_HEIGHT_M = 4.0;
    protected static const float ROCKET_LAUNCH_MS = 180.0;
    protected static const float PROXIMITY_FUZE_M = 28.0;
    protected static const float FLYBY_TRIGGER_M = 40.0;
    protected static const float FLYBY_OPEN_M = 10.0;
    protected static const float ROCKET_MAX_FLIGHT_S = 8.0;
    protected static const float FIRE_COOLDOWN_S = 4.0;
    protected static const int MAX_ROCKETS = 3;

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_DurationS = 45.0;
    protected float m_LastTickWallS;
    protected float m_LastFireWallS;
    protected float m_LastDebugPrintWallS;
    protected int m_LastScanSerial = -1;

    protected IEntity m_Subject;
    protected IEntity m_Target;
    protected IEntity m_Rocket;
    protected ref RDF_RadarRocketGuidanceState m_RocketGuide;
    protected ERDF_RadarRocketPhase m_LastLoggedPhase;
    protected float m_RocketClosestMissM;
    protected float m_RocketLastMissM;
    protected vector m_TargetVel;
    protected vector m_RadarOrigin;
    protected vector m_OrbitCenter;
    protected float m_OrbitCenterRangeM = 800.0;
    protected float m_OrbitRadiusM = 120.0;
    protected float m_OrbitAltitudeM = 150.0;
    protected float m_OrbitRateRadS = 0.16;

    protected int m_ScanCount;
    protected int m_TrackScans;
    protected int m_RocketsFired;
    protected int m_GuidanceTicks;
    protected bool m_ReachedTracking;
    protected bool m_FiredAtLeastOnce;
    protected float m_MinMissDistanceM = 1.0e9;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarRocketLockFireAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarRocketLockFireAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarRocketLockFireAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF RocketLockFire] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarRocketLockFireAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarRocketLockFireAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF RocketLockFire] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("RocketLockFire"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF RocketLockFire] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("RocketLockFire");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF RocketLockFire] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("RocketLockFire");
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
            Print("[RDF RocketLockFire] failed to choose radar origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("RocketLockFire");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastTickWallS = m_StartWallS;
        m_LastFireWallS = -1000.0;
        m_LastDebugPrintWallS = m_StartWallS;
        m_Rocket = null;
        m_RocketGuide = null;
        m_LastLoggedPhase = ERDF_RadarRocketPhase.RDF_ROCKET_BOOST;
        m_RocketClosestMissM = 1.0e9;
        m_RocketLastMissM = 1.0e9;
        m_TargetVel = Vector(0.0, 0.0, 0.0);
        m_RocketsFired = 0;
        m_GuidanceTicks = 0;
        m_ScanCount = 0;
        m_TrackScans = 0;
        m_ReachedTracking = false;
        m_FiredAtLeastOnce = false;
        m_MinMissDistanceM = 1.0e9;
        m_LastScanSerial = -1;

        if (!SpawnAirTarget())
        {
            Print("[RDF RocketLockFire] failed to spawn Mi-8.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyTestRadarConfig();
        ConfigureLock();
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_Running = true;
        RDF_RadarAutoTestMapOverlay.Start();
        if (m_Target)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_Target, "RDF Rkt");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 100, true);
        }

        Print("[RDF RocketLockFire] started — scan/lock Mi-8 then fire Hydra70 (ARH boost/mid/term PN).");
        Print("[RDF RocketLockFire] call: RDF_RadarRocketLockFireAutoTest.Start()");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("RocketLockFire");
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();
        ClearRocket();

        if (m_Target)
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

        Print(string.Format(
            "[RDF RocketLockFire] done track=%1 fired=%2 guidanceTicks=%3 minMiss=%4m",
            m_ReachedTracking.ToString(),
            m_RocketsFired.ToString(),
            m_GuidanceTicks.ToString(),
            Math.Round(m_MinMissDistanceM).ToString()));
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

    protected void ClearRocket()
    {
        if (m_Rocket)
            SCR_EntityHelper.DeleteEntityAndChildren(m_Rocket);
        m_Rocket = null;
        m_RocketGuide = null;
    }

    //------------------------------------------------------------------------------------------------
    // Proximity fuze: arm + user-trigger warhead (Hydra CollisionTrigger / ExplosionEffect).
    protected bool TryDetonateRocket(IEntity rocket, string reason)
    {
        if (!rocket)
            return false;

        BaseTriggerComponent trigger = BaseTriggerComponent.Cast(
            rocket.FindComponent(BaseTriggerComponent));
        if (!trigger)
        {
            Print("[RDF RocketLockFire] no BaseTriggerComponent — cannot detonate.", LogLevel.WARNING);
            return false;
        }

        if (trigger.WasTriggered())
            return true;

        trigger.SetLive();
        trigger.OnUserTrigger(rocket);
        Print(string.Format(
            "[RDF RocketLockFire] DETONATE (%1) triggered=%2",
            reason,
            trigger.WasTriggered().ToString()));
        return true;
    }

    //------------------------------------------------------------------------------------------------
    // Detonate then drop script handle (engine cleans up projectile after warhead).
    protected void DetonateAndReleaseRocket(string reason)
    {
        IEntity rocket = m_Rocket;
        m_Rocket = null;
        m_RocketGuide = null;
        if (!rocket)
            return;

        if (!TryDetonateRocket(rocket, reason))
            SCR_EntityHelper.DeleteEntityAndChildren(rocket);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        float dt = nowS - m_LastTickWallS;
        if (dt < 0.01)
            dt = 0.01;
        m_LastTickWallS = nowS;

        UpdateAirTargetMotion(nowS);
        if (m_Target)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_Target, "RDF Rkt");

        AccumulateLock(nowS);
        UpdateRocketGuidance(dt);
        MaybeFireRocket(nowS);

        if (nowS - m_StartWallS >= m_DurationS)
            StopInternal(true);
    }

    protected bool ChooseRadarOrigin()
    {
        if (!m_Subject)
            return false;
        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        m_RadarOrigin = mat[3] + Vector(0.0, 12.0, 0.0);
        vector flatFwd = Vector(mat[2][0], 0.0, mat[2][2]);
        float fl = flatFwd.Length();
        if (fl < 0.01)
            flatFwd = Vector(0.0, 0.0, 1.0);
        else
            flatFwd = flatFwd * (1.0 / fl);
        m_OrbitCenter = Vector(
            m_RadarOrigin[0] + flatFwd[0] * m_OrbitCenterRangeM,
            m_RadarOrigin[1] + m_OrbitAltitudeM,
            m_RadarOrigin[2] + flatFwd[2] * m_OrbitCenterRangeM);
        return true;
    }

    protected float GetElapsedS()
    {
        return System.GetTickCount() * 0.001 - m_StartWallS;
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
        spawnParams.Transform[3] = ComputeOrbitPos(GetElapsedS());
        m_Target = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_Target)
            return false;
        Print("[RDF RocketLockFire] spawned Mi-8 at " + m_Target.GetOrigin().ToString());
        return true;
    }

    protected void ApplyTestRadarConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateStareSettings(128);
        cfg.m_Range = 3000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 8.0;
        cfg.m_KeepUndetected = true;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_UseScattererRegistry = true;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableRwrReporting = true;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 20.0;
        hw.m_BandwidthHz = 20000000.0;
        hw.m_ScanRpm = 0.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("lock_low", 6.0, 14.0, 0.0);
        hw.AddElevationBeam("lock_mid", 14.0, 16.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.ApplyIdealChannel();
        cfg.m_EnableCfarGate = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void ConfigureLock()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        sensor.ResetSession();
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
            return;
        float elapsedS = GetElapsedS();
        float phase = elapsedS * m_OrbitRateRadS;
        vector pos = ComputeOrbitPos(elapsedS);
        m_Target.SetOrigin(pos);
        float vx = -Math.Sin(phase) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vz = Math.Cos(phase) * m_OrbitRadiusM * m_OrbitRateRadS;
        float vy = 12.0 * 0.07 * Math.Cos(elapsedS * 0.07);
        m_TargetVel = Vector(vx, vy, vz);
        Physics physics = m_Target.GetPhysics();
        if (physics)
            physics.SetVelocity(m_TargetVel);

        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 0.2, 1, 0.2), flags, pos, 5.0);
    }

    protected void AccumulateLock(float nowS)
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
            return;
        m_LastScanSerial = serial;
        m_ScanCount = m_ScanCount + 1;

        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        ERDF_RadarLockState state = lockMgr.GetState();
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
        {
            m_ReachedTracking = true;
            m_TrackScans = m_TrackScans + 1;
        }

        if (nowS - m_LastDebugPrintWallS > 3.0)
        {
            m_LastDebugPrintWallS = nowS;
            IEntity locked;
            vector aim;
            string lockFlag = "0";
            if (sensor.GetLockedTarget(locked, aim))
                lockFlag = "1";
            Print(string.Format(
                "[RDF RocketLockFire] scans=%1 track=%2 lock=%3 rockets=%4 minMiss=%5",
                m_ScanCount.ToString(),
                m_ReachedTracking.ToString(),
                lockFlag,
                m_RocketsFired.ToString(),
                Math.Round(m_MinMissDistanceM).ToString()));
        }
    }

    protected void MaybeFireRocket(float nowS)
    {
        if (!m_ReachedTracking)
            return;
        if (m_RocketsFired >= MAX_ROCKETS)
            return;
        if (m_Rocket)
            return;
        if (nowS - m_LastFireWallS < FIRE_COOLDOWN_S)
            return;

        IEntity locked;
        vector aimPos;
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor || !sensor.GetLockedTarget(locked, aimPos))
            return;

        if (!TryFireRocket(aimPos, locked))
            return;

        m_LastFireWallS = nowS;
        m_FiredAtLeastOnce = true;
    }

    protected bool TryFireRocket(vector aimPos, IEntity lockedTarget)
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world || !m_Subject)
            return false;

        Resource prefabRes = Resource.Load(ROCKET_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
        {
            Print("[RDF RocketLockFire] failed to load Hydra70 prefab.", LogLevel.ERROR);
            return false;
        }

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector origin = mat[3];
        vector flatFwd = Vector(mat[2][0], 0.0, mat[2][2]);
        float fl = flatFwd.Length();
        if (fl < 0.01)
            flatFwd = Vector(0.0, 0.0, 1.0);
        else
            flatFwd = flatFwd * (1.0 / fl);

        vector launchPos = origin
            + Vector(flatFwd[0] * LAUNCH_OFFSET_M, 0.0, flatFwd[2] * LAUNCH_OFFSET_M);
        launchPos[1] = origin[1] + LAUNCH_HEIGHT_M;

        vector toAim = aimPos - launchPos;
        float dist = toAim.Length();
        if (dist < 1.0)
            return false;
        vector dir = toAim * (1.0 / dist);

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = launchPos;
        IEntity rocket = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!rocket)
        {
            Print("[RDF RocketLockFire] spawn rocket failed.", LogLevel.ERROR);
            return false;
        }

        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = launchPos;
        rocket.SetTransform(basis);

        MissileMoveComponent missile = MissileMoveComponent.Cast(
            rocket.FindComponent(MissileMoveComponent));
        if (!missile)
        {
            Print("[RDF RocketLockFire] need MissileMoveComponent for ARH SetVelocity.", LogLevel.ERROR);
            SCR_EntityHelper.DeleteEntityAndChildren(rocket);
            return false;
        }

        missile.EnableSimulation(rocket);
        // Pass locked entity into Launch (engine may ignore; RDF ARH steers in tick).
        missile.Launch(dir, vector.Zero, 1.0, rocket, null, m_Subject, lockedTarget, null);
        missile.SetVelocity(dir * ROCKET_LAUNCH_MS);

        m_Rocket = rocket;
        m_RocketGuide = new RDF_RadarRocketGuidanceState();
        m_RocketGuide.Begin(System.GetTickCount() * 0.001);
        m_LastLoggedPhase = ERDF_RadarRocketPhase.RDF_ROCKET_BOOST;
        m_RocketClosestMissM = 1.0e9;
        m_RocketLastMissM = 1.0e9;
        m_RocketsFired = m_RocketsFired + 1;
        Print(string.Format(
            "[RDF RocketLockFire] FIRE #%1 aim=%2 range=%3m (Hydra70 + ARH PN)",
            m_RocketsFired.ToString(),
            aimPos.ToString(),
            Math.Round(dist).ToString()));
        return true;
    }

    protected void UpdateRocketGuidance(float dt)
    {
        if (!m_Rocket || !m_RocketGuide)
            return;

        float nowS = System.GetTickCount() * 0.001;
        IEntity locked;
        vector aimPos;
        vector aimVel = m_TargetVel;
        RDF_RadarSensor sensor = GetSensor();
        bool haveAim = false;
        if (sensor && sensor.GetLockedTarget(locked, aimPos))
            haveAim = true;
        else if (m_Target)
        {
            aimPos = m_Target.GetOrigin();
            locked = m_Target;
            haveAim = true;
        }

        if (!haveAim)
            return;

        if (RDF_RadarRocketGuidance.Update(
            m_Rocket,
            aimPos,
            aimVel,
            dt,
            nowS,
            m_RocketGuide))
        {
            m_GuidanceTicks = m_GuidanceTicks + 1;
        }

        if (m_RocketGuide.m_Phase != m_LastLoggedPhase)
        {
            m_LastLoggedPhase = m_RocketGuide.m_Phase;
            Print(string.Format(
                "[RDF RocketLockFire] phase -> %1 age=%2s seeker=%3",
                m_RocketGuide.GetPhaseName(),
                (Math.Round(m_RocketGuide.m_AgeS * 10.0) * 0.1).ToString(),
                m_RocketGuide.m_SeekerHasLock.ToString()));
        }

        float miss = vector.Distance(m_Rocket.GetOrigin(), aimPos);
        if (miss < m_MinMissDistanceM)
            m_MinMissDistanceM = miss;
        if (miss < m_RocketClosestMissM)
            m_RocketClosestMissM = miss;

        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 1, 0.4, 0.1), flags, m_Rocket.GetOrigin(), 3.0);

        // Proximity fuze (ARH-style near-miss detonation).
        if (miss < PROXIMITY_FUZE_M)
        {
            Print(string.Format(
                "[RDF RocketLockFire] proximity %1m phase=%2 — fuze",
                Math.Round(miss).ToString(),
                m_RocketGuide.GetPhaseName()));
            DetonateAndReleaseRocket("proximity");
            return;
        }

        // Flyby: CPA inside envelope then range opens — still detonate.
        if (m_RocketClosestMissM < FLYBY_TRIGGER_M
            && miss > m_RocketClosestMissM + FLYBY_OPEN_M)
        {
            Print(string.Format(
                "[RDF RocketLockFire] flyby cpa=%1m now=%2m phase=%3 — fuze",
                Math.Round(m_RocketClosestMissM).ToString(),
                Math.Round(miss).ToString(),
                m_RocketGuide.GetPhaseName()));
            DetonateAndReleaseRocket("flyby");
            return;
        }

        if (m_RocketGuide.m_AgeS >= ROCKET_MAX_FLIGHT_S)
        {
            DetonateAndReleaseRocket("timeout");
            return;
        }

        if (m_Rocket.GetOrigin()[1] < 0.5)
            DetonateAndReleaseRocket("ground");

        m_RocketLastMissM = miss;
    }
}
