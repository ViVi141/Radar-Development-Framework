// Automated demo: ground vehicle as a SAM stand-in scans, identifies, and
// engages six orbiting Mi-8 targets with Hydra70 + RDF ARH sample guidance.
// Radar + fire originate from the SAM vehicle (SetScanSubjectOverride), not
// the local player. Vanilla has no dedicated SAM launcher — BTR70 is a stand-in.
//
// Usage (Script Debugger, Play mode):
//   RDF_RadarSamEngageAutoTest.Start();
// Standalone — not in StartAll.
enum ERDF_SamEngagePhase
{
    RDF_SAM_SCAN = 0,
    RDF_SAM_IDENTIFY = 1,
    RDF_SAM_ENGAGE = 2,
    RDF_SAM_DONE = 3
}

class RDF_RadarSamEngageAutoTest
{
    protected static ref RDF_RadarSamEngageAutoTest s_Instance;
    protected static bool s_TickRegistered;

    // Ground SAM stand-in (no vanilla SAM prefab).
    protected static const ResourceName SAM_PREFAB =
        "{C012BB3488BEA0C2}Prefabs/Vehicles/Wheeled/BTR70/BTR70.et";
    protected static const ResourceName AIR_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";
    protected static const ResourceName ROCKET_PREFAB =
        "{ECD8628EBF7E5F6B}Prefabs/Weapons/Ammo/Ammo_Rocket_Hydra70.et";

    protected static const int AIR_COUNT = 6;
    protected static const int MAX_ROCKETS = 6;
    protected static const float LAUNCH_OFFSET_M = 10.0;
    protected static const float LAUNCH_HEIGHT_M = 3.5;
    protected static const float ROCKET_LAUNCH_MS = 110.0;
    protected static const float PROXIMITY_FUZE_M = 36.0;
    protected static const float FLYBY_TRIGGER_M = 50.0;
    protected static const float FLYBY_OPEN_M = 14.0;
    protected static const float ROCKET_MAX_FLIGHT_S = 20.0;
    protected static const float FIRE_COOLDOWN_S = 4.0;
    protected static const float SCAN_MIN_S = 5.0;
    protected static const float IDENTIFY_HOLD_S = 3.0;
    protected static const float POST_HIT_HOLD_S = 1.5;
    protected static const float AIR_ORBIT_RADIUS_M = 700.0;
    protected static const float AIR_ORBIT_ALT_M = 180.0;
    protected static const float AIR_ORBIT_RATE_RAD_S = 0.045;
    protected static const float SAM_FORWARD_RANGE_M = 40.0;

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_DurationS = 180.0;
    protected float m_LastTickWallS;
    protected float m_LastFireWallS;
    protected float m_LastDebugPrintWallS;
    protected float m_PhaseEnteredWallS;
    protected float m_EngageHitWallS;
    protected int m_LastScanSerial = -1;
    protected ERDF_SamEngagePhase m_Phase;

    protected IEntity m_Subject;
    protected IEntity m_SamVehicle;
    protected ref array<IEntity> m_AirTargets;
    protected ref array<bool> m_AirHit;
    protected IEntity m_Rocket;
    protected ref RDF_RadarRocketGuidanceState m_RocketGuide;
    protected ref RDF_RadarWeaponBridge m_WeaponBridge;
    protected ref RDF_RadarFireSolution m_FireSolution;

    protected vector m_RadarAnchor;
    protected vector m_SamPos;
    protected vector m_FlatFwd;

    protected int m_ScanCount;
    protected int m_RocketsFired;
    protected int m_Hits;
    protected int m_MaxVehicleTracks;
    protected int m_MaxAirIdentified;
    protected int m_GuidanceTicks;
    protected bool m_Identified;
    protected float m_MinMissM = 1.0e9;
    protected float m_RocketClosestMissM = 1.0e9;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarSamEngageAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarSamEngageAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        RDF_RadarSamEngageAutoTest inst = GetInstance();
        if (inst && inst.m_Running)
        {
            Print("[RDF SamEngage] already running — restarting.");
            inst.StopInternal(true);
        }
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarSamEngageAutoTest inst = GetInstance();
        if (!inst)
            return;
        if (!inst.m_Running)
        {
            RDF_RadarAutoRunner.SetScanSubjectOverride(null);
            return;
        }
        inst.StopInternal(true);
        Print("[RDF SamEngage] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarSamEngageAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarSamEngageAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF SamEngage] already running — call Start() to restart, or Stop().");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("SamEngage"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF SamEngage] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("SamEngage");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF SamEngage] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("SamEngage");
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
            Print("[RDF SamEngage] failed to choose origins.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("SamEngage");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;
        m_LastTickWallS = m_StartWallS;
        m_LastFireWallS = -1000.0;
        m_LastDebugPrintWallS = m_StartWallS;
        m_PhaseEnteredWallS = m_StartWallS;
        m_Phase = ERDF_SamEngagePhase.RDF_SAM_SCAN;
        m_Rocket = null;
        m_RocketGuide = null;
        m_ScanCount = 0;
        m_RocketsFired = 0;
        m_Hits = 0;
        m_MaxVehicleTracks = 0;
        m_MaxAirIdentified = 0;
        m_GuidanceTicks = 0;
        m_Identified = false;
        m_EngageHitWallS = -1000.0;
        m_MinMissM = 1.0e9;
        m_RocketClosestMissM = 1.0e9;
        m_LastScanSerial = -1;
        m_AirTargets = new array<IEntity>();
        m_AirHit = new array<bool>();
        m_WeaponBridge = new RDF_RadarWeaponBridge();
        m_FireSolution = new RDF_RadarFireSolution();

        if (!SpawnSamVehicle())
        {
            Print("[RDF SamEngage] failed to spawn SAM vehicle.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }
        if (!SpawnAirTargets())
        {
            Print("[RDF SamEngage] failed to spawn air targets.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        RDF_RadarAutoRunner.SetScanSubjectOverride(m_SamVehicle);
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplySamSearchConfig();
        ConfigureLockAirVehicles();
        BindWeaponBridge();
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_Running = true;
        RDF_RadarAutoTestMapOverlay.Start();
        if (m_AirTargets.Count() > 0)
            RDF_RadarAutoTestMapOverlay.SetAircraft(m_AirTargets.Get(0), "RDF SAM tgt");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 100, true);
        }

        Print("[RDF SamEngage] started — BTR SAM vs 6 Mi-8 (scan / identify / engage).");
        Print("[RDF SamEngage] call: RDF_RadarSamEngageAutoTest.Start()");
        PrintPhase("SCAN (SHORAD search)");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("SamEngage");
        RDF_RadarAutoRunner.SetScanSubjectOverride(null);
        RDF_RadarAutoTestMapOverlay.ClearAircraft();
        RDF_RadarAutoTestMapOverlay.Stop();
        ClearRocket();
        ClearAirTargets();
        DeleteEntity(m_SamVehicle);
        m_SamVehicle = null;

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
            "[RDF SamEngage] done identified=%1 hits=%2/%3 rockets=%4 maxTracks=%5 maxId=%6 bestMiss=%7m",
            m_Identified.ToString(),
            m_Hits.ToString(),
            AIR_COUNT.ToString(),
            m_RocketsFired.ToString(),
            m_MaxVehicleTracks.ToString(),
            m_MaxAirIdentified.ToString(),
            Math.Round(m_MinMissM).ToString()));
    }

    protected void DeleteEntity(IEntity ent)
    {
        if (!ent)
            return;
        RDF_RadarScattererRegistry.Unregister(ent);
        SCR_EntityHelper.DeleteEntityAndChildren(ent);
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

    protected void ClearRocket()
    {
        if (m_Rocket)
            SCR_EntityHelper.DeleteEntityAndChildren(m_Rocket);
        m_Rocket = null;
        m_RocketGuide = null;
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
        if (m_AirHit)
            m_AirHit.Clear();

        for (int j = 0; j < doomed.Count(); j++)
            DeleteEntity(doomed.Get(j));
    }

    protected void PrintPhase(string name)
    {
        Print(string.Format("[RDF SamEngage] phase -> %1", name));
    }

    protected void EnterPhase(ERDF_SamEngagePhase phase, string name)
    {
        m_Phase = phase;
        m_PhaseEnteredWallS = System.GetTickCount() * 0.001;
        if (phase == ERDF_SamEngagePhase.RDF_SAM_ENGAGE)
            m_LastFireWallS = -1000.0;
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

        UpdateAirMotion(GetElapsedS());
        UpdateRocketGuidance(dt);
        TickPhase(nowS);
        MaybeDebugPrint(nowS);

        if (nowS - m_StartWallS >= m_DurationS)
        {
            EnterPhase(ERDF_SamEngagePhase.RDF_SAM_DONE, "DONE (timeout)");
            StopInternal(true);
        }
    }

    protected void TickPhase(float nowS)
    {
        if (m_Phase == ERDF_SamEngagePhase.RDF_SAM_SCAN)
            TickScan(nowS);
        else if (m_Phase == ERDF_SamEngagePhase.RDF_SAM_IDENTIFY)
            TickIdentify(nowS);
        else if (m_Phase == ERDF_SamEngagePhase.RDF_SAM_ENGAGE)
            TickEngage(nowS);
        else if (m_Phase == ERDF_SamEngagePhase.RDF_SAM_DONE)
            StopInternal(true);
    }

    protected bool ChooseOrigins()
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
        m_RadarAnchor = Vector(center[0], surfaceY + 2.0, center[2]);

        m_FlatFwd = Vector(mat[2][0], 0.0, mat[2][2]);
        float fl = m_FlatFwd.Length();
        if (fl < 0.01)
            m_FlatFwd = Vector(0.0, 0.0, 1.0);
        else
            m_FlatFwd = m_FlatFwd * (1.0 / fl);

        vector samFlat = Vector(
            m_RadarAnchor[0] + m_FlatFwd[0] * SAM_FORWARD_RANGE_M,
            0.0,
            m_RadarAnchor[2] + m_FlatFwd[2] * SAM_FORWARD_RANGE_M);
        float samY = world.GetSurfaceY(samFlat[0], samFlat[2]);
        m_SamPos = Vector(samFlat[0], samY + 0.5, samFlat[2]);
        return true;
    }

    protected float GetElapsedS()
    {
        return System.GetTickCount() * 0.001 - m_StartWallS;
    }

    protected bool SpawnSamVehicle()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(SAM_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
        {
            Print("[RDF SamEngage] failed to load BTR70 prefab.", LogLevel.ERROR);
            return false;
        }

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = m_SamPos;
        IEntity sam = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!sam)
            return false;

        FaceEntityToward(sam, m_SamPos + m_FlatFwd * 100.0);
        m_SamVehicle = sam;
        RDF_RadarScattererRegistry.Register(sam, m_SamPos, false, 0.0);

        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 0.15, 0.9, 0.2), flags, m_SamPos + Vector(0, 3, 0), 6.0);

        Print(string.Format(
            "[RDF SamEngage] spawned SAM (BTR70) at %1",
            m_SamPos.ToString()));
        return true;
    }

    protected void FaceEntityToward(IEntity ent, vector lookAt)
    {
        if (!ent)
            return;
        vector pos = ent.GetOrigin();
        vector to = lookAt - pos;
        to[1] = 0.0;
        float len = to.Length();
        if (len < 0.1)
            return;
        vector dir = to * (1.0 / len);
        vector basis[4];
        Math3D.DirectionAndUpMatrix(dir, vector.Up, basis);
        basis[3] = pos;
        ent.SetTransform(basis);
    }

    protected vector ComputeOrbitPos(float elapsedS, int airIndex)
    {
        float phase = elapsedS * AIR_ORBIT_RATE_RAD_S + airIndex * (Math.PI2 / AIR_COUNT);
        float radius = AIR_ORBIT_RADIUS_M + airIndex * 45.0;
        float alt = AIR_ORBIT_ALT_M + airIndex * 18.0;
        return Vector(
            m_SamPos[0] + Math.Cos(phase) * radius,
            m_SamPos[1] + alt + 8.0 * Math.Sin(elapsedS * 0.07 + airIndex),
            m_SamPos[2] + Math.Sin(phase) * radius);
    }

    protected bool SpawnAirTargets()
    {
        ClearAirTargets();
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(AIR_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
        {
            Print("[RDF SamEngage] failed to load Mi-8 prefab.", LogLevel.ERROR);
            return false;
        }

        int spawned = 0;
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
            m_AirHit.Insert(false);
            RDF_RadarScattererRegistry.Register(ent, spawnPos, false, 0.0);
            spawned = spawned + 1;
            Print(string.Format(
                "[RDF SamEngage] spawned Mi-8 #%1 at %2",
                spawned.ToString(),
                spawnPos.ToString()));
        }

        if (spawned < 1)
            return false;
        Print(string.Format("[RDF SamEngage] air targets=%1", spawned.ToString()));
        return true;
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
            if (m_AirHit && i < m_AirHit.Count() && m_AirHit.Get(i))
                continue;

            float phase = elapsedS * AIR_ORBIT_RATE_RAD_S + i * (Math.PI2 / AIR_COUNT);
            float radius = AIR_ORBIT_RADIUS_M + i * 45.0;
            vector pos = ComputeOrbitPos(elapsedS, i);
            air.SetOrigin(pos);

            float vx = -Math.Sin(phase) * radius * AIR_ORBIT_RATE_RAD_S;
            float vz = Math.Cos(phase) * radius * AIR_ORBIT_RATE_RAD_S;
            float vy = 8.0 * 0.07 * Math.Cos(elapsedS * 0.07 + i);
            Physics physics = air.GetPhysics();
            if (physics)
            {
                physics.SetVelocity(Vector(vx, vy, vz));
                physics.SetAngularVelocity(Vector(0.0, 0.0, 0.0));
            }

            int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
            Shape.CreateSphere(ARGBF(1, 1, 0.35, 0.15), flags, pos, 7.0);
        }
    }

    protected void ApplySamSearchConfig()
    {
        // Pulse-Doppler so circling Mi-8s keep paint through CPA (vr≈0).
        RDF_RadarSettings cfg = RDF_RadarSensor.CreatePulseDopplerSettings(160);
        cfg.m_Range = 4000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 6.0;
        cfg.m_KeepUndetected = true;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_UseScattererRegistry = true;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.4;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableRwrReporting = true;
        cfg.m_MaxLosTracesPerScan = 96;
        // Mast / antenna height above BTR hull.
        cfg.m_OriginOffset = Vector(0.0, 6.0, 0.0);

        RDF_RadarHardware hw = cfg.m_Hardware;
        if (!hw)
            hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 30.0;
        hw.m_BandwidthHz = 20000000.0;
        hw.m_ScanRpm = 12.0;
        hw.m_EnableMti = true;
        hw.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;
        hw.m_DopplerBinCount = 16;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("sam_low", 4.0, 16.0, 0.0);
        hw.AddElevationBeam("sam_mid", 12.0, 18.0, 0.0);
        hw.AddElevationBeam("sam_high", 22.0, 20.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.ApplyIdealChannel();
        cfg.m_EnableCfarGate = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.Validate();
        ApplySensorConfig(cfg);
        BindWeaponBridge();
    }

    protected void ConfigureLockAirVehicles()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;
        sensor.ResetSession();
        RDF_RadarLockManager lockMgr = sensor.GetLockManager();
        lockMgr.SetAutoAcquire(true);
        lockMgr.SetTypeFilter(true, false, false);
        lockMgr.SetMaxLockRange(4000.0);
        lockMgr.SetLockSector(0.0);
        lockMgr.SetAcquireHits(2);
        lockMgr.SetCoastMaxSec(3.0);
    }

    protected int CountAliveAirTargets()
    {
        if (!m_AirTargets)
            return 0;
        int n = 0;
        for (int i = 0; i < m_AirTargets.Count(); i++)
        {
            if (!m_AirTargets.Get(i))
                continue;
            if (m_AirHit && i < m_AirHit.Count() && m_AirHit.Get(i))
                continue;
            n = n + 1;
        }
        return n;
    }

    protected int CountIdentifiedAirTracks()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor || !m_AirTargets)
            return 0;

        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        if (!tracks)
            return 0;

        int idCount = 0;
        for (int t = 0; t < tracks.Count(); t++)
        {
            RDF_RadarTrack track = tracks.Get(t);
            if (!track)
                continue;
            if (track.m_Type != ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
                continue;
            if (!track.m_Confirmed)
                continue;
            if (!track.m_Entity)
                continue;
            if (!IsAirTargetEntity(track.m_Entity))
                continue;
            idCount = idCount + 1;
        }
        return idCount;
    }

    protected int CountVehicleTracks()
    {
        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return 0;
        array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
        if (!tracks)
            return 0;
        int n = 0;
        for (int t = 0; t < tracks.Count(); t++)
        {
            RDF_RadarTrack track = tracks.Get(t);
            if (!track)
                continue;
            if (track.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
                n = n + 1;
        }
        return n;
    }

    protected bool IsAirTargetEntity(IEntity ent)
    {
        if (!ent || !m_AirTargets)
            return false;
        for (int i = 0; i < m_AirTargets.Count(); i++)
        {
            if (m_AirTargets.Get(i) == ent)
                return true;
        }
        return false;
    }

    protected int FindAirIndex(IEntity ent)
    {
        if (!ent || !m_AirTargets)
            return -1;
        for (int i = 0; i < m_AirTargets.Count(); i++)
        {
            if (m_AirTargets.Get(i) == ent)
                return i;
        }
        return -1;
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

        int vTracks = CountVehicleTracks();
        if (vTracks > m_MaxVehicleTracks)
            m_MaxVehicleTracks = vTracks;
        int idAir = CountIdentifiedAirTracks();
        if (idAir > m_MaxAirIdentified)
            m_MaxAirIdentified = idAir;
    }

    protected void MaybeDebugPrint(float nowS)
    {
        if (nowS - m_LastDebugPrintWallS < 3.0)
            return;
        m_LastDebugPrintWallS = nowS;

        int auth = 0;
        if (m_WeaponBridge && m_WeaponBridge.TryGetFireSolution(m_FireSolution))
        {
            if (m_FireSolution.m_CanAuthorizeFire)
                auth = 1;
        }

        Print(string.Format(
            "[RDF SamEngage] %1 scans=%2 auth=%3 idAir=%4 alive=%5 hits=%6 rockets=%7",
            PhaseName(),
            m_ScanCount.ToString(),
            auth.ToString(),
            CountIdentifiedAirTracks().ToString(),
            CountAliveAirTargets().ToString(),
            m_Hits.ToString(),
            m_RocketsFired.ToString()));
    }

    protected string PhaseName()
    {
        if (m_Phase == ERDF_SamEngagePhase.RDF_SAM_SCAN)
            return "SCAN";
        if (m_Phase == ERDF_SamEngagePhase.RDF_SAM_IDENTIFY)
            return "IDENTIFY";
        if (m_Phase == ERDF_SamEngagePhase.RDF_SAM_ENGAGE)
            return "ENGAGE";
        return "DONE";
    }

    protected void TickScan(float nowS)
    {
        NoteScan();
        if (nowS - m_PhaseEnteredWallS < SCAN_MIN_S)
            return;

        int idAir = CountIdentifiedAirTracks();
        if (idAir < 1 && m_ScanCount < 20)
            return;

        EnterPhase(
            ERDF_SamEngagePhase.RDF_SAM_IDENTIFY,
            string.Format("IDENTIFY (vehicle tracks=%1 airId=%2)",
                CountVehicleTracks().ToString(),
                idAir.ToString()));
    }

    protected void TickIdentify(float nowS)
    {
        NoteScan();
        int idAir = CountIdentifiedAirTracks();
        if (idAir >= 2 || (idAir >= 1 && nowS - m_PhaseEnteredWallS >= IDENTIFY_HOLD_S))
        {
            m_Identified = true;
            Print(string.Format(
                "[RDF SamEngage] IDENTIFIED air tracks=%1 / %2 (max=%3)",
                idAir.ToString(),
                AIR_COUNT.ToString(),
                m_MaxAirIdentified.ToString()));
            EnterPhase(
                ERDF_SamEngagePhase.RDF_SAM_ENGAGE,
                "ENGAGE (sequential Hydra vs Mi-8)");
            return;
        }

        if (nowS - m_PhaseEnteredWallS > 25.0)
        {
            if (idAir >= 1)
                m_Identified = true;
            EnterPhase(
                ERDF_SamEngagePhase.RDF_SAM_ENGAGE,
                "ENGAGE (identify timeout — fire on lock)");
        }
    }

    protected void TickEngage(float nowS)
    {
        NoteScan();

        if (CountAliveAirTargets() <= 0 && !m_Rocket)
        {
            EnterPhase(ERDF_SamEngagePhase.RDF_SAM_DONE, "DONE (all air engaged)");
            return;
        }
        if (m_RocketsFired >= MAX_ROCKETS && !m_Rocket)
        {
            EnterPhase(ERDF_SamEngagePhase.RDF_SAM_DONE, "DONE (rockets spent)");
            return;
        }
        if (nowS - m_PhaseEnteredWallS > 140.0 && !m_Rocket)
        {
            EnterPhase(ERDF_SamEngagePhase.RDF_SAM_DONE, "DONE (engage timeout)");
            return;
        }

        if (m_EngageHitWallS > 0.0 && nowS - m_EngageHitWallS < POST_HIT_HOLD_S)
            return;

        MaybeFire(nowS);
    }

    protected void MaybeFire(float nowS)
    {
        if (m_Rocket)
            return;
        if (m_RocketsFired >= MAX_ROCKETS)
            return;
        if (nowS - m_LastFireWallS < FIRE_COOLDOWN_S)
            return;
        if (!m_WeaponBridge || !m_FireSolution)
            return;
        if (!m_WeaponBridge.TryGetFireSolution(m_FireSolution))
            return;
        if (!m_FireSolution.m_CanAuthorizeFire)
            return;

        IEntity locked = m_FireSolution.m_Target;
        if (!IsAirTargetEntity(locked))
            return;

        int idx = FindAirIndex(locked);
        if (idx >= 0 && m_AirHit && idx < m_AirHit.Count() && m_AirHit.Get(idx))
        {
            // Already engaged / falling — break lock so auto-acquire moves on.
            RDF_RadarSensor sensor = GetSensor();
            if (sensor)
            {
                RDF_RadarLockManager lockMgr = sensor.GetLockManager();
                if (lockMgr)
                    lockMgr.Unlock();
            }
            return;
        }

        vector aimPos = m_FireSolution.m_AimPos;
        if (!TryFireRocket(aimPos, locked))
            return;
        m_LastFireWallS = nowS;
        m_EngageHitWallS = -1000.0;
    }

    protected bool TryFireRocket(vector aimPos, IEntity lockedTarget)
    {
        BaseWorld world = GetGame().GetWorld();
        IEntity shooter = m_SamVehicle;
        if (!world || !shooter)
            return false;

        Resource prefabRes = Resource.Load(ROCKET_PREFAB);
        if (!prefabRes || !prefabRes.IsValid())
        {
            Print("[RDF SamEngage] failed to load Hydra70 prefab.", LogLevel.ERROR);
            return false;
        }

        vector origin = shooter.GetOrigin();
        vector toAim0 = aimPos - origin;
        float dist0 = toAim0.Length();
        if (dist0 < 1.0)
            return false;
        vector dir0 = toAim0 * (1.0 / dist0);

        vector launchPos = origin + dir0 * LAUNCH_OFFSET_M + Vector(0.0, LAUNCH_HEIGHT_M, 0.0);
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
        missile.SetVelocity(dir * ROCKET_LAUNCH_MS);

        m_Rocket = rocket;
        m_RocketGuide = new RDF_RadarRocketGuidanceState();
        m_RocketGuide.Begin(System.GetTickCount() * 0.001);
        m_RocketGuide.m_LoftHeightM = 40.0;
        m_RocketGuide.m_GoActiveRangeM = 450.0;
        m_RocketClosestMissM = 1.0e9;
        m_RocketsFired = m_RocketsFired + 1;

        Print(string.Format(
            "[RDF SamEngage] FIRE #%1 range=%2m target=%3",
            m_RocketsFired.ToString(),
            Math.Round(dist).ToString(),
            aimPos.ToString()));
        return true;
    }

    protected void UpdateRocketGuidance(float dt)
    {
        if (!m_Rocket || !m_RocketGuide)
            return;

        float nowS = System.GetTickCount() * 0.001;
        vector aimPos;
        vector aimVel = Vector(0.0, 0.0, 0.0);
        bool haveAim = false;
        IEntity locked;

        if (m_WeaponBridge && m_WeaponBridge.TryGetFireSolution(m_FireSolution))
        {
            if (m_FireSolution.m_Valid && IsAirTargetEntity(m_FireSolution.m_Target))
            {
                aimPos = m_FireSolution.m_AimPos;
                aimVel = m_FireSolution.m_AimVel;
                locked = m_FireSolution.m_Target;
                haveAim = true;
            }
        }

        if (!haveAim)
        {
            RDF_RadarSensor sensor = GetSensor();
            if (sensor && sensor.GetLockedTarget(locked, aimPos))
            {
                if (IsAirTargetEntity(locked))
                    haveAim = true;
            }
        }

        if (!haveAim)
        {
            // Coast toward last known closest alive heli.
            IEntity fallback = FindNearestAliveAir();
            if (fallback)
            {
                aimPos = fallback.GetOrigin();
                locked = fallback;
                haveAim = true;
            }
        }

        if (!haveAim)
            return;

        if (RDF_RadarRocketGuidance.Update(
            m_Rocket, aimPos, aimVel, dt, nowS, m_RocketGuide))
        {
            m_GuidanceTicks = m_GuidanceTicks + 1;
        }

        float miss = vector.Distance(m_Rocket.GetOrigin(), aimPos);
        if (miss < m_RocketClosestMissM)
            m_RocketClosestMissM = miss;
        if (miss < m_MinMissM)
            m_MinMissM = miss;

        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
        Shape.CreateSphere(ARGBF(1, 1, 0.85, 0.2), flags, m_Rocket.GetOrigin(), 3.0);

        if (miss <= PROXIMITY_FUZE_M)
        {
            Print(string.Format(
                "[RDF SamEngage] HIT proximity %1m (fuze=%2m)",
                Math.Round(miss).ToString(),
                Math.Round(PROXIMITY_FUZE_M).ToString()));
            IEntity hitTgt = locked;
            DetonateAndReleaseRocket("proximity");
            MarkTargetHit(hitTgt);
            return;
        }

        if (m_RocketClosestMissM < FLYBY_TRIGGER_M
            && miss > m_RocketClosestMissM + FLYBY_OPEN_M)
        {
            Print(string.Format(
                "[RDF SamEngage] flyby CPA=%1m — count as engage",
                Math.Round(m_RocketClosestMissM).ToString()));
            IEntity hitTgt = locked;
            DetonateAndReleaseRocket("flyby");
            MarkTargetHit(hitTgt);
            return;
        }

        if (m_RocketGuide.m_AgeS >= ROCKET_MAX_FLIGHT_S)
        {
            DetonateAndReleaseRocket("timeout");
            return;
        }
        if (m_Rocket.GetOrigin()[1] < 0.5)
            DetonateAndReleaseRocket("ground");
    }

    protected IEntity FindNearestAliveAir()
    {
        if (!m_Rocket || !m_AirTargets)
            return null;
        vector rpos = m_Rocket.GetOrigin();
        IEntity best;
        float bestD = 1.0e9;
        for (int i = 0; i < m_AirTargets.Count(); i++)
        {
            IEntity air = m_AirTargets.Get(i);
            if (!air)
                continue;
            if (m_AirHit && i < m_AirHit.Count() && m_AirHit.Get(i))
                continue;
            float d = vector.Distance(rpos, air.GetOrigin());
            if (d < bestD)
            {
                bestD = d;
                best = air;
            }
        }
        return best;
    }

    protected void MarkTargetHit(IEntity target)
    {
        int idx = FindAirIndex(target);
        if (idx < 0)
        {
            target = FindNearestAliveAir();
            idx = FindAirIndex(target);
        }
        if (idx < 0)
            return;
        if (m_AirHit && idx < m_AirHit.Count() && m_AirHit.Get(idx))
            return;

        if (m_AirHit && idx < m_AirHit.Count())
            m_AirHit.Set(idx, true);
        m_Hits = m_Hits + 1;
        m_EngageHitWallS = System.GetTickCount() * 0.001;

        IEntity air = m_AirTargets.Get(idx);
        Print(string.Format(
            "[RDF SamEngage] air #%1 engaged — release script hold (hits=%2/%3)",
            (idx + 1).ToString(),
            m_Hits.ToString(),
            AIR_COUNT.ToString()));

        // Stop orbit SetOrigin; leave entity in world for engine physics / fall.
        if (air)
            ReleaseAirToPhysics(air);

        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
        {
            RDF_RadarLockManager lockMgr = sensor.GetLockManager();
            if (lockMgr)
                lockMgr.Unlock();
        }
    }

    // Drop script motion/velocity hold so the airframe can tumble / fall.
    protected void ReleaseAirToPhysics(IEntity air)
    {
        if (!air)
            return;

        RDF_RadarScattererRegistry.Unregister(air);

        Physics physics = air.GetPhysics();
        if (physics)
        {
            vector v = physics.GetVelocity();
            // Keep some horizontal remnant, add downward so it leaves the orbit.
            physics.SetVelocity(Vector(v[0] * 0.35, -12.0, v[2] * 0.35));
            physics.SetAngularVelocity(Vector(0.4, 0.2, 0.15));
        }
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
            "[RDF SamEngage] DETONATE (%1) triggered=%2",
            reason,
            trigger.WasTriggered().ToString()));
        return true;
    }

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
}
