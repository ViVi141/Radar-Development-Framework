// Automated demo: blue Mi-8 engages red Mi-8, then blue intercepts an inbound
// Hydra launched by red. Radar scan + weapons originate from the blue heli
// (not the local player). Uses RDF_RadarWeaponBridge + ARH sample guidance.
//
// Usage (Script Debugger, Play mode):
//   RDF_RadarHeliDuelAutoTest.Start();
// Standalone — not in StartAll.
enum ERDF_HeliDuelPhase
{
    RDF_HELI_DUEL_LOCK_HOSTILE = 0,
    RDF_HELI_DUEL_ENGAGE_HOSTILE = 1,
    RDF_HELI_DUEL_LAUNCH_THREAT = 2,
    RDF_HELI_DUEL_LOCK_THREAT = 3,
    RDF_HELI_DUEL_INTERCEPT = 4,
    RDF_HELI_DUEL_DONE = 5
}

class RDF_RadarHeliDuelAutoTest
{
    protected static ref RDF_RadarHeliDuelAutoTest s_Instance;
    protected static bool s_TickRegistered;

    protected static const ResourceName HELI_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";
    protected static const ResourceName ROCKET_PREFAB =
        "{ECD8628EBF7E5F6B}Prefabs/Weapons/Ammo/Ammo_Rocket_Hydra70.et";

    protected static const float LAUNCH_OFFSET_M = 8.0;
    protected static const float LAUNCH_HEIGHT_M = 4.0;
    // Slower munitions so the flight is visible at long range.
    protected static const float ROCKET_LAUNCH_MS = 90.0;
    // Interceptor slightly faster so collision-course lead converges cleaner.
    protected static const float INTERCEPT_LAUNCH_MS = 130.0;
    protected static const float THREAT_LAUNCH_MS = 55.0;
    protected static const float PROXIMITY_FUZE_M = 32.0;
    protected static const float INTERCEPT_PROXIMITY_FUZE_M = 9.0;
    // Vanilla Hydra has no DamageManager — AOE cannot HP-kill another rocket.
    // Within this CPA, cook off the inbound via its own trigger (frag coupling).
    protected static const float WARHEAD_LETHAL_M = 5.0;
    protected static const float FLYBY_TRIGGER_M = 45.0;
    protected static const float FLYBY_OPEN_M = 12.0;
    protected static const float ROCKET_MAX_FLIGHT_S = 22.0;
    protected static const float THREAT_MAX_FLIGHT_S = 28.0;
    protected static const float FIRE_COOLDOWN_S = 6.0;
    protected static const float INTERCEPT_FIRE_COOLDOWN_S = 1.0;
    // Pacing: hold so the viewer can track each beat.
    protected static const float POST_LOCK_HOLD_S = 4.0;
    protected static const float POST_ENGAGE_HOLD_S = 5.0;
    protected static const float PRE_THREAT_HOLD_S = 3.0;
    protected static const float THREAT_WATCH_S = 6.0;
    protected static const float POST_INTERCEPT_HOLD_S = 3.0;
    protected static const int MAX_ENGAGE_ROCKETS = 1;
    protected static const int MAX_INTERCEPT_ROCKETS = 1;

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_DurationS = 140.0;
    protected float m_LastTickWallS;
    protected float m_LastFireWallS;
    protected float m_LastDebugPrintWallS;
    protected float m_PhaseEnteredWallS;
    protected float m_EngageHitWallS;
    protected float m_InterceptHitWallS;
    protected int m_LastScanSerial = -1;
    protected ERDF_HeliDuelPhase m_Phase;

    protected IEntity m_Subject;
    protected IEntity m_BlueHeli;
    protected IEntity m_RedHeli;
    protected IEntity m_OwnRocket;
    protected IEntity m_ThreatRocket;
    protected ref RDF_RadarRocketGuidanceState m_OwnGuide;
    protected ref RDF_RadarRocketGuidanceState m_ThreatGuide;
    protected ref RDF_RadarWeaponBridge m_WeaponBridge;
    protected ref RDF_RadarFireSolution m_FireSolution;

    protected vector m_RadarOrigin;
    protected vector m_RedOrbitCenter;
    protected vector m_BlueOrbitCenter;
    protected vector m_RedVel;
    protected vector m_BlueVel;
    // Hover stations ~1.2km apart (no orbit — orbit looked like constant spin).
    protected float m_RedOrbitRangeM = 1800.0;
    protected float m_RedOrbitRadiusM = 0.0;
    protected float m_RedOrbitAltM = 190.0;
    protected float m_RedOrbitRateRadS = 0.0;
    protected float m_BlueOrbitRangeM = 550.0;
    protected float m_BlueOrbitRadiusM = 0.0;
    protected float m_BlueOrbitAltM = 190.0;
    protected float m_BlueOrbitRateRadS = 0.0;
    protected vector m_BlueHoverPos;
    protected vector m_RedHoverPos;

    protected int m_ScanCount;
    protected int m_EngageRocketsFired;
    protected int m_InterceptRocketsFired;
    protected int m_GuidanceTicks;
    protected bool m_HostileLocked;
    protected bool m_ThreatLocked;
    protected bool m_EngageHit;
    protected bool m_InterceptHit;
    protected bool m_ThreatKilledByWarhead;
    protected float m_EngageMinMissM = 1.0e9;
    protected float m_InterceptMinMissM = 1.0e9;
    protected float m_OwnClosestMissM = 1.0e9;
    protected vector m_ThreatAimPos;
    protected vector m_ThreatAimVel;
    protected bool m_ThreatAimValid;
    protected float m_ThreatLostWallS;
    protected bool m_ThreatScriptDrive;
    protected float m_ThreatLaunchWallS;
    // Intercept: kinematic drive (same as threat) — PN+engine drag was leaving CPA 11–14m.
    protected bool m_OwnScriptDrive;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarHeliDuelAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarHeliDuelAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        RDF_RadarHeliDuelAutoTest inst = GetInstance();
        if (inst && inst.m_Running)
        {
            Print("[RDF HeliDuel] already running — restarting.");
            inst.StopInternal(true);
        }
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarHeliDuelAutoTest inst = GetInstance();
        if (!inst)
            return;
        if (!inst.m_Running)
        {
            // Clear a stuck gate / override even if m_Running was lost.
            RDF_RadarAutoTestGate.Release("HeliDuel");
            RDF_RadarAutoRunner.SetScanSubjectOverride(null);
            Print("[RDF HeliDuel] not running (cleared gate/override).");
            return;
        }
        inst.StopInternal(true);
        Print("[RDF HeliDuel] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarHeliDuelAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarHeliDuelAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF HeliDuel] already running — call Start() to restart, or Stop().");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("HeliDuel"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF HeliDuel] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("HeliDuel");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF HeliDuel] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("HeliDuel");
            return;
        }

        m_PrevConfig = null;
        RDF_RadarSensor prevSensor = RDF_RadarAutoRunner.GetSensor();
        if (prevSensor)
            m_PrevConfig = prevSensor.GetSettings();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        if (!ChooseOrigins())
        {
            Print("[RDF HeliDuel] failed to choose origins.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("HeliDuel");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastTickWallS = m_StartWallS;
        m_LastFireWallS = -1000.0;
        m_LastDebugPrintWallS = m_StartWallS;
        m_PhaseEnteredWallS = m_StartWallS;
        m_Phase = ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_HOSTILE;
        m_OwnRocket = null;
        m_ThreatRocket = null;
        m_OwnGuide = null;
        m_ThreatGuide = null;
        m_RedVel = Vector(0.0, 0.0, 0.0);
        m_BlueVel = Vector(0.0, 0.0, 0.0);
        m_ScanCount = 0;
        m_EngageRocketsFired = 0;
        m_InterceptRocketsFired = 0;
        m_GuidanceTicks = 0;
        m_HostileLocked = false;
        m_ThreatLocked = false;
        m_EngageHit = false;
        m_InterceptHit = false;
        m_ThreatKilledByWarhead = false;
        m_EngageHitWallS = -1000.0;
        m_InterceptHitWallS = -1000.0;
        m_EngageMinMissM = 1.0e9;
        m_InterceptMinMissM = 1.0e9;
        m_OwnClosestMissM = 1.0e9;
        m_ThreatAimPos = "0 0 0";
        m_ThreatAimVel = "0 0 0";
        m_ThreatAimValid = false;
        m_ThreatLostWallS = -1000.0;
        m_ThreatScriptDrive = false;
        m_OwnScriptDrive = false;
        m_ThreatLaunchWallS = -1000.0;
        m_LastScanSerial = -1;
        m_WeaponBridge = new RDF_RadarWeaponBridge();
        m_FireSolution = new RDF_RadarFireSolution();

        if (!SpawnBlueHeli())
        {
            Print("[RDF HeliDuel] failed to spawn blue Mi-8.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }
        if (!SpawnRedHeli())
        {
            Print("[RDF HeliDuel] failed to spawn red Mi-8.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        // Blue heli is the shooter / radar platform (not the local player).
        RDF_RadarAutoRunner.SetScanSubjectOverride(m_BlueHeli);

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyAirSearchConfig();
        ConfigureLockVehicles();
        BindWeaponBridge();
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_Running = true;
        RDF_RadarAutoTestMapOverlay.Start();
        if (m_RedHeli)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_RedHeli, "RDF Red");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 100, true);
        }

        Print("[RDF HeliDuel] started — intercept uses kinematic script drive (tight CPA).");
        Print("[RDF HeliDuel] call: RDF_RadarHeliDuelAutoTest.Start()");
        PrintPhase("LOCK_HOSTILE (heli vs heli)");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("HeliDuel");
        RDF_RadarAutoRunner.SetScanSubjectOverride(null);
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();
        ClearOwnRocket();
        ClearThreatRocket();
        DeleteHeli(m_BlueHeli);
        m_BlueHeli = null;
        DeleteHeli(m_RedHeli);
        m_RedHeli = null;

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

        string engageOk = "0";
        if (m_EngageHit)
            engageOk = "1";
        string interceptOk = "0";
        if (m_InterceptHit)
            interceptOk = "1";
        string threatKill = "0";
        if (m_ThreatKilledByWarhead)
            threatKill = "1";
        Print(string.Format(
            "[RDF HeliDuel] done hostileLock=%1 engageHit=%2 threatLock=%3 interceptHit=%4 threatWarheadKill=%5 engageMiss=%6m interceptMiss=%7m",
            m_HostileLocked.ToString(),
            engageOk,
            m_ThreatLocked.ToString(),
            interceptOk,
            threatKill,
            Math.Round(m_EngageMinMissM).ToString(),
            Math.Round(m_InterceptMinMissM).ToString()));
    }

    protected void DeleteHeli(IEntity heli)
    {
        if (!heli)
            return;
        RDF_RadarScattererRegistry.Unregister(heli);
        SCR_EntityHelper.DeleteEntityAndChildren(heli);
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

    protected void BindWeaponBridge()
    {
        if (!m_WeaponBridge)
            m_WeaponBridge = new RDF_RadarWeaponBridge();
        m_WeaponBridge.BindSensor(GetSensor());
        m_WeaponBridge.SetRequireTrackingForFire(true);
        m_WeaponBridge.SetPreferArmAim(false);
    }

    protected void ClearOwnRocket()
    {
        if (m_OwnRocket)
            SCR_EntityHelper.DeleteEntityAndChildren(m_OwnRocket);
        m_OwnRocket = null;
        m_OwnGuide = null;
        m_OwnScriptDrive = false;
    }

    protected void ClearThreatRocket()
    {
        if (m_ThreatRocket)
            SCR_EntityHelper.DeleteEntityAndChildren(m_ThreatRocket);
        m_ThreatRocket = null;
        m_ThreatGuide = null;
        m_ThreatScriptDrive = false;
    }

    // Hand threat back to engine physics (no Delete).
    // Used when the interceptor warhead should interact with the inbound.
    protected void ReleaseThreatToPhysics(string reason)
    {
        if (!m_ThreatRocket)
            return;

        m_ThreatScriptDrive = false;
        m_ThreatGuide = null;
        CacheThreatAimFromEntity();
        Print(string.Format(
            "[RDF HeliDuel] threat released to physics (%1) — await warhead",
            reason));
    }

    // Hydra rockets are not damageable actors. Within lethal CPA, couple the
    // interceptor boom into the inbound's own CollisionTrigger (cook-off).
    protected bool TryWarheadCookOffThreat(float missM)
    {
        if (!m_ThreatRocket)
            return false;
        if (missM > WARHEAD_LETHAL_M)
        {
            Print(string.Format(
                "[RDF HeliDuel] warhead miss %1m > lethal %2m — no cook-off",
                Math.Round(missM).ToString(),
                Math.Round(WARHEAD_LETHAL_M).ToString()));
            return false;
        }

        IEntity threat = m_ThreatRocket;
        if (!TryDetonateRocket(threat, "warhead-frag-cookoff"))
            return false;

        m_ThreatKilledByWarhead = true;
        m_ThreatRocket = null;
        m_ThreatGuide = null;
        m_ThreatScriptDrive = false;
        m_ThreatAimValid = false;
        Print(string.Format(
            "[RDF HeliDuel] threat cook-off from warhead at %1m (no DamageManager on Hydra)",
            Math.Round(missM).ToString()));
        return true;
    }

    protected void PrintPhase(string name)
    {
        Print(string.Format("[RDF HeliDuel] phase -> %1", name));
    }

    protected void EnterPhase(ERDF_HeliDuelPhase phase, string name)
    {
        m_Phase = phase;
        m_PhaseEnteredWallS = System.GetTickCount() * 0.001;
        // Engage cooldown must not block the first interceptor shot.
        if (phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT
            || phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_THREAT)
        {
            m_LastFireWallS = -1000.0;
        }
        PrintPhase(name);
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

        UpdateBlueMotion(nowS);
        UpdateRedMotion(nowS);
        if (m_RedHeli)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_RedHeli, "RDF Red");

        UpdateThreatGuidance(dt);
        UpdateOwnRocketGuidance(dt);
        TickPhase(nowS);
        MaybeDebugPrint(nowS);

        if (nowS - m_StartWallS >= m_DurationS)
        {
            EnterPhase(ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE, "DONE (timeout)");
            StopInternal(true);
        }
    }

    protected void TickPhase(float nowS)
    {
        if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_HOSTILE)
            TickLockHostile(nowS);
        else if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_ENGAGE_HOSTILE)
            TickEngageHostile(nowS);
        else if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LAUNCH_THREAT)
            TickLaunchThreat(nowS);
        else if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_THREAT)
            TickLockThreat(nowS);
        else if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT)
            TickIntercept(nowS);
        else if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE)
            StopInternal(true);
    }

    protected bool ChooseOrigins()
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

        m_RedOrbitCenter = Vector(
            m_RadarOrigin[0] + flatFwd[0] * m_RedOrbitRangeM,
            m_RadarOrigin[1] + m_RedOrbitAltM,
            m_RadarOrigin[2] + flatFwd[2] * m_RedOrbitRangeM);
        m_BlueOrbitCenter = Vector(
            m_RadarOrigin[0] + flatFwd[0] * m_BlueOrbitRangeM,
            m_RadarOrigin[1] + m_BlueOrbitAltM,
            m_RadarOrigin[2] + flatFwd[2] * m_BlueOrbitRangeM);
        m_BlueHoverPos = m_BlueOrbitCenter;
        m_RedHoverPos = m_RedOrbitCenter;
        return true;
    }

    protected float GetElapsedS()
    {
        return System.GetTickCount() * 0.001 - m_StartWallS;
    }

    protected vector ComputeRedOrbitPos(float elapsedS)
    {
        return m_RedHoverPos;
    }

    protected vector ComputeBlueOrbitPos(float elapsedS)
    {
        return m_BlueHoverPos;
    }

    protected IEntity SpawnHeliAt(vector pos, string label)
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return null;
        Resource prefabRes = Resource.Load(HELI_PREFAB);
        if (!prefabRes)
            return null;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = pos;
        IEntity heli = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!heli)
            return null;
        Print(string.Format("[RDF HeliDuel] spawned %1 at %2", label, heli.GetOrigin().ToString()));
        return heli;
    }

    protected bool SpawnBlueHeli()
    {
        m_BlueHeli = SpawnHeliAt(ComputeBlueOrbitPos(0.0), "blue Mi-8");
        if (!m_BlueHeli)
            return false;
        return true;
    }

    protected bool SpawnRedHeli()
    {
        m_RedHeli = SpawnHeliAt(ComputeRedOrbitPos(0.0), "red Mi-8");
        if (!m_RedHeli)
            return false;
        return true;
    }

    protected void ApplyAirSearchConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateStareSettings(128);
        cfg.m_Range = 3500.0;
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
        hw.AddElevationBeam("air_low", 6.0, 14.0, 0.0);
        hw.AddElevationBeam("air_mid", 14.0, 16.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.StabilizeForRegression();
        cfg.m_EnableCfarGate = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.Validate();
        ApplySensorConfig(cfg);
        BindWeaponBridge();
    }

    protected void ConfigureLockVehicles()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        sensor.ResetSession();
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        lockMgr.SetAutoAcquire(true);
        lockMgr.SetTypeFilter(true, false, false);
        lockMgr.SetMaxLockRange(3500.0);
        lockMgr.SetLockSector(0.0);
        lockMgr.SetAcquireHits(2);
        lockMgr.SetCoastMaxSec(3.0);
    }

    protected void ConfigureLockProjectiles()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        lockMgr.Unlock();
        lockMgr.SetAutoAcquire(true);
        lockMgr.SetTypeFilter(false, true, false);
        lockMgr.SetMaxLockRange(3500.0);
        lockMgr.SetLockSector(0.0);
        lockMgr.SetAcquireHits(1);
        lockMgr.SetCoastMaxSec(2.0);
        BindWeaponBridge();
    }

    protected void FaceHeliToward(IEntity heli, vector lookAt)
    {
        if (!heli)
            return;
        vector pos = heli.GetOrigin();
        vector to = lookAt - pos;
        to[1] = 0.0;
        float len = to.Length();
        if (len < 0.1)
            return;
        vector dir = to * (1.0 / len);
        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = pos;
        heli.SetTransform(basis);
    }

    protected void HoverHeli(IEntity heli, vector hoverPos, vector facePos, out vector outVel)
    {
        outVel = Vector(0.0, 0.0, 0.0);
        if (!heli)
            return;

        heli.SetOrigin(hoverPos);
        FaceHeliToward(heli, facePos);
        Physics physics = heli.GetPhysics();
        if (physics)
        {
            physics.SetVelocity(Vector(0.0, 0.0, 0.0));
            physics.SetAngularVelocity(Vector(0.0, 0.0, 0.0));
        }
    }

    protected void UpdateRedMotion(float nowS)
    {
        vector face = m_BlueHoverPos;
        if (m_BlueHeli)
            face = m_BlueHeli.GetOrigin();
        HoverHeli(m_RedHeli, m_RedHoverPos, face, m_RedVel);
        if (m_RedHeli)
        {
            int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
            Shape.CreateSphere(ARGBF(1, 1, 0.25, 0.2), flags, m_RedHeli.GetOrigin(), 9.0);
        }
    }

    protected void UpdateBlueMotion(float nowS)
    {
        vector face = m_RedHoverPos;
        if (m_RedHeli)
            face = m_RedHeli.GetOrigin();
        HoverHeli(m_BlueHeli, m_BlueHoverPos, face, m_BlueVel);
        if (m_BlueHeli)
        {
            int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
            Shape.CreateSphere(ARGBF(1, 0.2, 0.6, 1), flags, m_BlueHeli.GetOrigin(), 8.0);
        }
    }

    protected void TickLockHostile(float nowS)
    {
        NoteScan();
        if (!m_WeaponBridge)
            return;
        if (!m_WeaponBridge.TryGetFireSolution(m_FireSolution))
            return;
        if (!m_FireSolution.m_CanAuthorizeFire)
            return;

        m_HostileLocked = true;
        EnterPhase(
            ERDF_HeliDuelPhase.RDF_HELI_DUEL_ENGAGE_HOSTILE,
            "ENGAGE_HOSTILE (hold, then blue fires)");
    }

    protected void TickEngageHostile(float nowS)
    {
        NoteScan();

        if (m_EngageHit && !m_OwnRocket)
        {
            if (m_EngageHitWallS < 0.0)
                m_EngageHitWallS = nowS;
            if (nowS - m_EngageHitWallS < POST_ENGAGE_HOLD_S)
                return;
            EnterPhase(
                ERDF_HeliDuelPhase.RDF_HELI_DUEL_LAUNCH_THREAT,
                "LAUNCH_THREAT (hold, then red fires)");
            return;
        }

        if (m_EngageRocketsFired >= MAX_ENGAGE_ROCKETS && !m_OwnRocket)
        {
            if (m_EngageHitWallS < 0.0)
                m_EngageHitWallS = nowS;
            if (nowS - m_EngageHitWallS < POST_ENGAGE_HOLD_S)
                return;
            EnterPhase(
                ERDF_HeliDuelPhase.RDF_HELI_DUEL_LAUNCH_THREAT,
                "LAUNCH_THREAT (engage spent → red fires)");
            return;
        }

        if (nowS - m_PhaseEnteredWallS > 35.0 && !m_OwnRocket)
        {
            EnterPhase(
                ERDF_HeliDuelPhase.RDF_HELI_DUEL_LAUNCH_THREAT,
                "LAUNCH_THREAT (engage timeout → continue)");
            return;
        }

        // Watch the lock before the first shot.
        if (nowS - m_PhaseEnteredWallS < POST_LOCK_HOLD_S)
            return;

        MaybeFireAtHostile(nowS);
    }

    protected void TickLaunchThreat(float nowS)
    {
        if (m_ThreatRocket)
        {
            ConfigureLockProjectiles();
            EnterPhase(
                ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_THREAT,
                "LOCK_THREAT (watch inbound)");
            return;
        }

        if (nowS - m_PhaseEnteredWallS < PRE_THREAT_HOLD_S)
            return;

        if (!TryLaunchThreatMissile())
        {
            if (nowS - m_PhaseEnteredWallS > PRE_THREAT_HOLD_S + 5.0)
            {
                EnterPhase(ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE, "DONE (threat spawn failed)");
            }
            return;
        }
        ConfigureLockProjectiles();
        EnterPhase(
            ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_THREAT,
            "LOCK_THREAT (watch inbound)");
    }

    protected void TickLockThreat(float nowS)
    {
        NoteScan();
        RefreshThreatAlive(nowS);
        if (!IsThreatAlive() && !m_ThreatAimValid)
        {
            EnterPhase(ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE, "DONE (threat gone early)");
            return;
        }

        bool lockedThreat = false;
        if (m_WeaponBridge && m_WeaponBridge.TryGetFireSolution(m_FireSolution))
        {
            if (m_FireSolution.m_CanAuthorizeFire && m_FireSolution.m_Target == m_ThreatRocket)
                lockedThreat = true;
        }

        // Let the inbound Hydra fly across the gap before blue shoots.
        if (nowS - m_PhaseEnteredWallS < THREAT_WATCH_S)
            return;

        if (lockedThreat)
        {
            m_ThreatLocked = true;
            EnterPhase(
                ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT,
                "INTERCEPT (radar lock on threat)");
            return;
        }

        m_ThreatLocked = false;
        EnterPhase(
            ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT,
            "INTERCEPT (script threat aim)");
    }

    protected void TickIntercept(float nowS)
    {
        NoteScan();
        RefreshThreatAlive(nowS);

        if (m_InterceptHit)
        {
            if (m_InterceptHitWallS < 0.0)
                m_InterceptHitWallS = nowS;
            RefreshThreatAlive(nowS);
            if (!IsThreatAlive())
                m_ThreatKilledByWarhead = true;

            if (nowS - m_InterceptHitWallS < POST_INTERCEPT_HOLD_S)
                return;

            if (m_ThreatKilledByWarhead)
                Print("[RDF HeliDuel] threat gone after warhead — fragments/physics kill");
            else if (!IsThreatAlive())
            {
                m_ThreatKilledByWarhead = true;
                Print("[RDF HeliDuel] threat gone after warhead — fragments/physics kill");
            }
            else
                Print("[RDF HeliDuel] threat still alive after warhead (Hydra blast may not kill projectiles)");

            EnterPhase(ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE, "DONE (intercept fuze)");
            return;
        }
        if (m_InterceptRocketsFired >= MAX_INTERCEPT_ROCKETS && !m_OwnRocket)
        {
            EnterPhase(ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE, "DONE (intercept rockets spent)");
            return;
        }

        bool canAimThreat = false;
        if (IsThreatAlive())
            canAimThreat = true;
        else if (m_ThreatAimValid && (nowS - m_ThreatLostWallS) < 2.5)
            canAimThreat = true;

        if (!canAimThreat && !m_OwnRocket)
        {
            if (m_InterceptRocketsFired <= 0 && (nowS - m_PhaseEnteredWallS) < 4.0)
                canAimThreat = m_ThreatAimValid;
        }

        if (!canAimThreat && !m_OwnRocket)
        {
            EnterPhase(ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE, "DONE (threat ended)");
            return;
        }
        if (nowS - m_PhaseEnteredWallS > 35.0 && !m_OwnRocket)
        {
            EnterPhase(ERDF_HeliDuelPhase.RDF_HELI_DUEL_DONE, "DONE (intercept timeout)");
            return;
        }

        MaybeFireInterceptor(nowS);
    }

    protected bool IsThreatAlive()
    {
        if (!m_ThreatRocket)
            return false;
        GenericEntity generic = GenericEntity.Cast(m_ThreatRocket);
        if (!generic)
            return false;
        if (!generic.FindComponent(MissileMoveComponent))
            return false;
        return true;
    }

    protected void RefreshThreatAlive(float nowS)
    {
        if (!m_ThreatRocket)
            return;
        if (IsThreatAlive())
        {
            CacheThreatAimFromEntity();
            return;
        }
        if (m_InterceptHit)
            m_ThreatKilledByWarhead = true;
        Print("[RDF HeliDuel] threat entity despawned — keep cached aim briefly");
        m_ThreatRocket = null;
        m_ThreatGuide = null;
        m_ThreatScriptDrive = false;
        m_ThreatLostWallS = nowS;
    }

    protected void CacheThreatAimFromEntity()
    {
        if (!m_ThreatRocket)
            return;
        m_ThreatAimPos = m_ThreatRocket.GetOrigin();
        m_ThreatAimVel = Vector(0.0, 0.0, 0.0);
        MissileMoveComponent move = MissileMoveComponent.Cast(
            m_ThreatRocket.FindComponent(MissileMoveComponent));
        if (move)
            m_ThreatAimVel = move.GetVelocity();
        m_ThreatAimValid = true;
    }

    protected void NoteScan()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
            return;
        m_LastScanSerial = serial;
        m_ScanCount = m_ScanCount + 1;
    }

    protected void MaybeDebugPrint(float nowS)
    {
        if (nowS - m_LastDebugPrintWallS < 3.0)
            return;
        m_LastDebugPrintWallS = nowS;

        string auth = "0";
        if (m_FireSolution && m_FireSolution.m_CanAuthorizeFire)
            auth = "1";
        string phaseName = PhaseName(m_Phase);
        Print(string.Format(
            "[RDF HeliDuel] %1 scans=%2 auth=%3 engage=%4 intercept=%5 threat=%6",
            phaseName,
            m_ScanCount.ToString(),
            auth,
            m_EngageRocketsFired.ToString(),
            m_InterceptRocketsFired.ToString(),
            (m_ThreatRocket != null).ToString()));
    }

    protected string PhaseName(ERDF_HeliDuelPhase phase)
    {
        if (phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_HOSTILE)
            return "LOCK_HOSTILE";
        if (phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_ENGAGE_HOSTILE)
            return "ENGAGE_HOSTILE";
        if (phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LAUNCH_THREAT)
            return "LAUNCH_THREAT";
        if (phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_THREAT)
            return "LOCK_THREAT";
        if (phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT)
            return "INTERCEPT";
        return "DONE";
    }

    protected void MaybeFireAtHostile(float nowS)
    {
        if (m_OwnRocket)
            return;
        if (m_EngageRocketsFired >= MAX_ENGAGE_ROCKETS)
            return;
        if (nowS - m_LastFireWallS < FIRE_COOLDOWN_S)
            return;
        if (!m_WeaponBridge || !m_WeaponBridge.TryGetFireSolution(m_FireSolution))
            return;
        if (!m_FireSolution.m_CanAuthorizeFire)
            return;

        vector aimPos = m_FireSolution.m_AimPos;
        IEntity locked = m_FireSolution.m_Target;
        if (!locked && m_RedHeli)
        {
            locked = m_RedHeli;
            aimPos = m_RedHeli.GetOrigin();
        }
        if (!TryFireOwnRocket(aimPos, locked, false))
            return;
        m_LastFireWallS = nowS;
    }

    protected void MaybeFireInterceptor(float nowS)
    {
        if (m_OwnRocket)
            return;
        if (m_InterceptRocketsFired >= MAX_INTERCEPT_ROCKETS)
            return;
        if (nowS - m_LastFireWallS < INTERCEPT_FIRE_COOLDOWN_S)
            return;

        vector aimPos;
        IEntity locked = null;
        bool haveAim = false;

        if (IsThreatAlive())
        {
            CacheThreatAimFromEntity();
            aimPos = m_ThreatAimPos;
            locked = m_ThreatRocket;
            haveAim = true;
        }
        else if (m_ThreatAimValid)
        {
            aimPos = m_ThreatAimPos;
            haveAim = true;
        }

        if (m_WeaponBridge && m_WeaponBridge.TryGetFireSolution(m_FireSolution))
        {
            if (m_FireSolution.m_Valid && m_FireSolution.m_Target == m_ThreatRocket)
            {
                aimPos = m_FireSolution.m_AimPos;
                locked = m_FireSolution.m_Target;
                haveAim = true;
            }
        }

        if (!haveAim)
            return;

        if (!TryFireOwnRocket(aimPos, locked, true))
            return;
        m_LastFireWallS = nowS;
    }

    protected bool TryFireOwnRocket(vector aimPos, IEntity lockedTarget, bool isIntercept)
    {
        BaseWorld world = GetGame().GetWorld();
        IEntity shooter = m_BlueHeli;
        if (!world || !shooter)
            return false;

        Resource prefabRes = Resource.Load(ROCKET_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
        {
            Print("[RDF HeliDuel] failed to load Hydra70 prefab.", LogLevel.ERROR);
            return false;
        }

        vector origin = shooter.GetOrigin();
        vector toAim0 = aimPos - origin;
        float dist0 = toAim0.Length();
        if (dist0 < 1.0)
            return false;
        vector dir0 = toAim0 * (1.0 / dist0);

        // Clear of blue airframe — same pattern as red threat launch.
        vector launchPos = origin + dir0 * 18.0 + Vector(0.0, 4.0, 0.0);
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
            return false;

        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = launchPos;
        rocket.SetTransform(basis);

        MissileMoveComponent missile = MissileMoveComponent.Cast(
            rocket.FindComponent(MissileMoveComponent));
        if (!missile)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(rocket);
            return false;
        }

        missile.EnableSimulation(rocket);
        missile.Launch(dir, vector.Zero, 1.0, rocket, null, shooter, lockedTarget, null);
        float launchMs = ROCKET_LAUNCH_MS;
        if (isIntercept)
            launchMs = INTERCEPT_LAUNCH_MS;
        missile.SetVelocity(dir * launchMs);

        m_OwnRocket = rocket;
        m_OwnGuide = new RDF_RadarRocketGuidanceState();
        m_OwnGuide.Begin(System.GetTickCount() * 0.001);
        m_OwnScriptDrive = isIntercept;
        if (isIntercept)
        {
            // Age/timeout only — flight is kinematic ScriptDriveOwnIntercept.
            m_OwnGuide.m_LoftHeightM = 0.0;
            m_OwnGuide.m_MaxSpeedMs = INTERCEPT_LAUNCH_MS;
            m_OwnGuide.m_MinSpeedMs = INTERCEPT_LAUNCH_MS;
        }
        m_OwnClosestMissM = 1.0e9;

        string fromTag = "blue Mi-8";
        if (isIntercept)
        {
            m_InterceptRocketsFired = m_InterceptRocketsFired + 1;
            Print(string.Format(
                "[RDF HeliDuel] INTERCEPT FIRE #%1 from %2 range=%3m (script drive)",
                m_InterceptRocketsFired.ToString(),
                fromTag,
                Math.Round(dist).ToString()));
        }
        else
        {
            m_EngageRocketsFired = m_EngageRocketsFired + 1;
            Print(string.Format(
                "[RDF HeliDuel] ENGAGE FIRE #%1 from %2 range=%3m",
                m_EngageRocketsFired.ToString(),
                fromTag,
                Math.Round(dist).ToString()));
        }
        return true;
    }

    protected bool TryLaunchThreatMissile()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world || !m_RedHeli)
            return false;

        Resource prefabRes = Resource.Load(ROCKET_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
            return false;

        vector aimPos = m_RadarOrigin;
        if (m_BlueHeli)
            aimPos = m_BlueHeli.GetOrigin();

        vector redPos = m_RedHeli.GetOrigin();
        vector toAim = aimPos - redPos;
        float dist = toAim.Length();
        if (dist < 1.0)
            return false;
        vector dir = toAim * (1.0 / dist);

        // Clear of red airframe — in-hull spawn is deleted by physics immediately.
        vector launchPos = redPos + dir * 18.0 + Vector(0.0, 6.0, 0.0);
        toAim = aimPos - launchPos;
        dist = toAim.Length();
        if (dist < 1.0)
            return false;
        dir = toAim * (1.0 / dist);

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = launchPos;
        IEntity rocket = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!rocket)
            return false;

        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = launchPos;
        rocket.SetTransform(basis);

        MissileMoveComponent missile = MissileMoveComponent.Cast(
            rocket.FindComponent(MissileMoveComponent));
        if (!missile)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(rocket);
            return false;
        }

        // Script-driven threat: keep MissileMoveComponent for classifier, integrate pose ourselves.
        missile.EnableSimulation(rocket);
        IEntity threatTarget = m_BlueHeli;
        if (!threatTarget)
            threatTarget = m_Subject;
        missile.Launch(dir, vector.Zero, 1.0, rocket, null, m_RedHeli, threatTarget, null);
        missile.SetVelocity(dir * THREAT_LAUNCH_MS);

        m_ThreatRocket = rocket;
        m_ThreatGuide = null;
        m_ThreatScriptDrive = true;
        m_ThreatAimPos = launchPos;
        m_ThreatAimVel = dir * THREAT_LAUNCH_MS;
        m_ThreatAimValid = true;
        m_ThreatLostWallS = -1000.0;
        m_ThreatLaunchWallS = System.GetTickCount() * 0.001;
        Print(string.Format(
            "[RDF HeliDuel] THREAT LAUNCH from red range=%1m (script drive)",
            Math.Round(dist).ToString()));
        return true;
    }

    protected void UpdateThreatGuidance(float dt)
    {
        if (!m_ThreatRocket)
            return;

        float nowS = System.GetTickCount() * 0.001;
        if (!IsThreatAlive())
        {
            RefreshThreatAlive(nowS);
            return;
        }

        vector aimPos = m_RadarOrigin;
        if (m_BlueHeli)
            aimPos = m_BlueHeli.GetOrigin();

        vector pos = m_ThreatRocket.GetOrigin();
        vector toAim = aimPos - pos;
        float rangeM = toAim.Length();
        vector dir = Vector(0.0, 0.0, 1.0);
        if (rangeM > 0.5)
            dir = toAim * (1.0 / rangeM);

        if (m_ThreatScriptDrive)
        {
            vector vel = dir * THREAT_LAUNCH_MS;
            pos = pos + vel * dt;

            vector basis[4];
            Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
            basis[3] = pos;
            m_ThreatRocket.SetTransform(basis);

            MissileMoveComponent missile = MissileMoveComponent.Cast(
                m_ThreatRocket.FindComponent(MissileMoveComponent));
            if (missile)
                missile.SetVelocity(vel);

            m_ThreatAimPos = pos;
            m_ThreatAimVel = vel;
            m_ThreatAimValid = true;
        }
        else if (m_ThreatGuide)
        {
            RDF_RadarRocketGuidance.Update(
                m_ThreatRocket, aimPos, m_BlueVel, dt, nowS, m_ThreatGuide);
            CacheThreatAimFromEntity();
        }
        else
        {
            // Coast under engine physics (e.g. after interceptor warhead).
            CacheThreatAimFromEntity();
        }

        float miss = vector.Distance(m_ThreatAimPos, aimPos);
        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 1, 0.1, 0.1), flags, m_ThreatAimPos, 2.5);

        if (miss < PROXIMITY_FUZE_M)
        {
            Print(string.Format(
                "[RDF HeliDuel] THREAT proximity %1m — blue hit risk",
                Math.Round(miss).ToString()));
            DetonateEntity(m_ThreatRocket, "threat-proximity");
            m_ThreatRocket = null;
            m_ThreatGuide = null;
            m_ThreatScriptDrive = false;
            m_ThreatLostWallS = nowS;
            return;
        }

        float threatAgeS = nowS - m_ThreatLaunchWallS;
        if (m_ThreatLaunchWallS > 0.0 && threatAgeS >= THREAT_MAX_FLIGHT_S)
        {
            DetonateEntity(m_ThreatRocket, "threat-timeout");
            m_ThreatRocket = null;
            m_ThreatGuide = null;
            m_ThreatScriptDrive = false;
            m_ThreatLostWallS = nowS;
        }
    }

    protected void ScriptDriveOwnIntercept(float dt, float nowS, vector aimPos, vector aimVel)
    {
        if (!m_OwnRocket || !m_OwnGuide)
            return;

        m_OwnGuide.m_AgeS = nowS - m_OwnGuide.m_LaunchWallS;
        if (m_OwnGuide.m_AgeS < 0.0)
            m_OwnGuide.m_AgeS = 0.0;

        vector pos = m_OwnRocket.GetOrigin();
        vector r = aimPos - pos;
        float rangeM = r.Length();
        if (rangeM < 0.5)
            return;

        float speed = INTERCEPT_LAUNCH_MS;
        vector losHat = r * (1.0 / rangeM);
        float closing = speed - vector.Dot(aimVel, losHat);
        if (closing < 8.0)
            closing = 8.0;

        vector steer = aimPos + aimVel * (rangeM / closing);
        // Terminal: chase body so CPA collapses into the warhead gate.
        if (rangeM < 60.0)
            steer = aimPos;

        vector toSteer = steer - pos;
        float steerLen = toSteer.Length();
        if (steerLen < 0.5)
            return;
        vector dir = toSteer * (1.0 / steerLen);
        vector vel = dir * speed;
        pos = pos + vel * dt;

        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = pos;
        m_OwnRocket.SetTransform(basis);

        MissileMoveComponent missile = MissileMoveComponent.Cast(
            m_OwnRocket.FindComponent(MissileMoveComponent));
        if (missile)
            missile.SetVelocity(vel);

        m_GuidanceTicks = m_GuidanceTicks + 1;
    }

    protected void UpdateOwnRocketGuidance(float dt)
    {
        if (!m_OwnRocket || !m_OwnGuide)
            return;

        float nowS = System.GetTickCount() * 0.001;
        vector aimPos;
        vector aimVel = Vector(0.0, 0.0, 0.0);
        bool haveAim = false;

        if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT
            || m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_LOCK_THREAT)
        {
            if (IsThreatAlive())
            {
                // Threat script drive already wrote m_ThreatAim* this tick —
                // do not overwrite with engine GetVelocity (can lag / drift).
                if (!m_ThreatScriptDrive)
                    CacheThreatAimFromEntity();
                aimPos = m_ThreatAimPos;
                aimVel = m_ThreatAimVel;
                haveAim = true;
            }
            else if (m_ThreatAimValid)
            {
                aimPos = m_ThreatAimPos;
                aimVel = m_ThreatAimVel;
                haveAim = true;
            }
        }
        else
        {
            if (m_WeaponBridge && m_WeaponBridge.TryGetFireSolution(m_FireSolution))
            {
                if (m_FireSolution.m_Valid)
                {
                    aimPos = m_FireSolution.m_AimPos;
                    aimVel = m_FireSolution.m_AimVel;
                    haveAim = true;
                }
            }
            if (!haveAim && m_RedHeli)
            {
                aimPos = m_RedHeli.GetOrigin();
                aimVel = m_RedVel;
                haveAim = true;
            }
        }

        if (!haveAim)
            return;

        if (m_OwnScriptDrive)
            ScriptDriveOwnIntercept(dt, nowS, aimPos, aimVel);
        else if (RDF_RadarRocketGuidance.Update(
            m_OwnRocket, aimPos, aimVel, dt, nowS, m_OwnGuide))
        {
            m_GuidanceTicks = m_GuidanceTicks + 1;
        }

        // CPA vs live threat body (not lead point / uplink).
        vector missRef = aimPos;
        if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT && IsThreatAlive())
            missRef = m_ThreatRocket.GetOrigin();

        float miss = vector.Distance(m_OwnRocket.GetOrigin(), missRef);
        if (miss < m_OwnClosestMissM)
            m_OwnClosestMissM = miss;

        if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_ENGAGE_HOSTILE)
        {
            if (miss < m_EngageMinMissM)
                m_EngageMinMissM = miss;
        }
        else if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT)
        {
            if (miss < m_InterceptMinMissM)
                m_InterceptMinMissM = miss;
        }

        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 1, 0.85, 0.15), flags, m_OwnRocket.GetOrigin(), 3.0);

        float fuzeM = PROXIMITY_FUZE_M;
        if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT)
            fuzeM = INTERCEPT_PROXIMITY_FUZE_M;

        if (miss <= fuzeM)
        {
            string reason = "engage-proximity";
            if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT)
            {
                reason = "intercept-proximity";
                m_InterceptHit = true;
                ReleaseThreatToPhysics("intercept-proximity");
                Print(string.Format(
                    "[RDF HeliDuel] OWN proximity %1m (%2) fuze=%3m",
                    Math.Round(miss).ToString(),
                    reason,
                    Math.Round(fuzeM).ToString()));
                DetonateAndReleaseOwn(reason);
                TryWarheadCookOffThreat(miss);
                return;
            }

            m_EngageHit = true;
            Print(string.Format(
                "[RDF HeliDuel] OWN proximity %1m (%2) fuze=%3m",
                Math.Round(miss).ToString(),
                reason,
                Math.Round(fuzeM).ToString()));
            DetonateAndReleaseOwn(reason);
            return;
        }

        // Engage: boom after CPA opening for a visible near-miss.
        // Intercept: never force a far boom — that wastes the warhead at
        // 15–40m where Hydra HEDP cannot kill another rocket.
        if (m_Phase != ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT
            && m_OwnClosestMissM < FLYBY_TRIGGER_M
            && miss > m_OwnClosestMissM + FLYBY_OPEN_M)
        {
            m_EngageHit = true;
            DetonateAndReleaseOwn("engage-flyby");
            return;
        }

        if (m_Phase == ERDF_HeliDuelPhase.RDF_HELI_DUEL_INTERCEPT
            && m_OwnClosestMissM < FLYBY_TRIGGER_M
            && miss > m_OwnClosestMissM + FLYBY_OPEN_M)
        {
            Print(string.Format(
                "[RDF HeliDuel] intercept flyby CPA=%1m (no far detonate; need <=%2m)",
                Math.Round(m_OwnClosestMissM).ToString(),
                Math.Round(INTERCEPT_PROXIMITY_FUZE_M).ToString()));
            ReleaseThreatToPhysics("intercept-flyby-no-boom");
            IEntity spent = m_OwnRocket;
            m_OwnRocket = null;
            m_OwnGuide = null;
            m_OwnScriptDrive = false;
            if (spent)
                SCR_EntityHelper.DeleteEntityAndChildren(spent);
            return;
        }

        if (m_OwnGuide.m_AgeS >= ROCKET_MAX_FLIGHT_S)
        {
            DetonateAndReleaseOwn("timeout");
            return;
        }
        if (m_OwnRocket.GetOrigin()[1] < 0.5)
            DetonateAndReleaseOwn("ground");
    }

    protected bool TryDetonateRocket(IEntity rocket, string reason)
    {
        if (!rocket)
            return false;
        BaseTriggerComponent trigger = BaseTriggerComponent.Cast(
            rocket.FindComponent(BaseTriggerComponent));
        if (!trigger)
            return false;
        if (trigger.WasTriggered())
            return true;
        trigger.SetLive();
        trigger.OnUserTrigger(rocket);
        Print(string.Format(
            "[RDF HeliDuel] DETONATE (%1) triggered=%2",
            reason,
            trigger.WasTriggered().ToString()));
        return true;
    }

    protected void DetonateEntity(IEntity rocket, string reason)
    {
        if (!rocket)
            return;
        if (!TryDetonateRocket(rocket, reason))
            SCR_EntityHelper.DeleteEntityAndChildren(rocket);
    }

    protected void DetonateAndReleaseOwn(string reason)
    {
        IEntity rocket = m_OwnRocket;
        m_OwnRocket = null;
        m_OwnGuide = null;
        m_OwnScriptDrive = false;
        if (!rocket)
            return;
        DetonateEntity(rocket, reason);
    }
}
