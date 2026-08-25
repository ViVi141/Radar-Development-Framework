// RWR paint / lock warning smoke test (Script Debugger):
//   RDF_RadarRwrAutoTest.Start();
// Spawns a target under the local subject's radar, expects SEARCH then LOCK
// warnings on the target entity. Not part of StartAll.
class RDF_RadarRwrAutoTest
{
    protected static ref RDF_RadarRwrAutoTest s_Instance;
    protected static bool s_TickRegistered;

    protected static const ResourceName AIR_TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_DurationS = 24.0;
    protected int m_LastScanSerial = -1;

    protected IEntity m_Subject;
    protected IEntity m_Target;
    protected vector m_TargetPos;

    protected int m_ScanCount;
    protected int m_SearchWarnScans;
    protected int m_LockWarnScans;
    protected bool m_SawSearch;
    protected bool m_SawLock;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarRwrAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarRwrAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarRwrAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF RWR Test] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarRwrAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarRwrAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF RWR Test] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Rwr"))
            return;

        if (!GetGame().GetWorld())
        {
            Print("[RDF RWR Test] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Rwr");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF RWR Test] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Rwr");
            return;
        }

        m_PrevConfig = null;
        RDF_RadarSensor prevSensor = RDF_RadarAutoRunner.GetSensor();
        if (prevSensor)
            m_PrevConfig = prevSensor.GetSettings();
        m_PrevDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PrevHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        m_StartWallS = System.GetTickCount() * 0.001;
        if (!SpawnTarget())
        {
            Print("[RDF RWR Test] spawn failed.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyConfig();
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_ScanCount = 0;
        m_SearchWarnScans = 0;
        m_LockWarnScans = 0;
        m_SawSearch = false;
        m_SawLock = false;
        m_LastScanSerial = -1;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print("[RDF RWR Test] started — expect SEARCH then LOCK on target.");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        if (m_Target)
        {
            // Unregister from the scatterer registry BEFORE deleting so the
            // sweep does not leave a stale entry pointing at a freed entity
            // (matches Lock/Air/ShellFire/Jpda/Dwell/Play cleanup pattern).
            RDF_RadarScattererRegistry.Unregister(m_Target);
            SCR_EntityHelper.DeleteEntityAndChildren(m_Target);
            m_Target = null;
        }

        if (restore)
        {
            RDF_RadarAutoRunner.SetForceLocalScan(m_PrevForceLocal);
            RDF_RadarAutoRunner.SetHudEnabled(m_PrevHudEnabled);
            RDF_RadarAutoRunner.SetDemoEnabled(m_PrevDemoEnabled);
            if (m_PrevConfig)
                RDF_RadarAutoRunner.SetDemoConfig(m_PrevConfig);
            else
                RDF_RadarAutoRunner.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH);
        }

        RDF_RadarAutoTestGate.Release("Rwr");
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        if (nowS - m_StartWallS > m_DurationS)
        {
            FinishAndReport();
            return;
        }

        if (m_Target)
            m_Target.SetOrigin(m_TargetPos);

        Accumulate();
    }

    protected bool SpawnTarget()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world || !m_Subject)
            return false;

        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector origin = mat[3];
        vector forward = mat[2];
        forward[1] = 0.0;
        float flen = forward.Length();
        if (flen < 0.001)
            forward = Vector(0, 0, 1);
        else
            forward = forward * (1.0 / flen);

        m_TargetPos = origin + forward * 500.0;
        m_TargetPos[1] = origin[1] + 100.0;

        Resource prefabRes = Resource.Load(AIR_TARGET_PREFAB);
        if (!prefabRes)
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = m_TargetPos;
        m_Target = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        return m_Target != null;
    }

    protected void ApplyConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateStareSettings(128);
        cfg.m_Range = 3000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnableRwrReporting = true;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_DetectionSnrDb = 0.0;
        cfg.m_KeepEntityTruth = true;
        cfg.m_KeepUndetected = true;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        if (cfg.m_Hardware)
        {
            cfg.m_Hardware.m_EnableMti = false;
            cfg.m_Hardware.m_AzimuthBeamwidthDeg = 40.0;
            cfg.m_Hardware.Validate();
        }
        cfg.Validate();

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
            sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_STARE, 128);
        RDF_RadarAutoRunner.SetDemoConfig(cfg);

        sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
        {
            RDF_RadarLockManager lockMgr = sensor.GetLockManager();
            lockMgr.SetAutoAcquire(true);
            lockMgr.SetTypeFilter(true, false, false);
            lockMgr.SetAcquireHits(1);
            lockMgr.SetMaxLockRange(3000.0);
            lockMgr.SetLockSector(0.0);
            lockMgr.SetArmRequireLiveEmitter(false);
        }
    }

    protected void Accumulate()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor || !m_Target)
            return;

        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
            return;
        m_LastScanSerial = serial;
        m_ScanCount = m_ScanCount + 1;

        if (RDF_RadarRwr.HasSearchWarning(m_Target))
        {
            m_SawSearch = true;
            m_SearchWarnScans = m_SearchWarnScans + 1;
        }
        if (RDF_RadarRwr.HasLockWarning(m_Target))
        {
            m_SawLock = true;
            m_LockWarnScans = m_LockWarnScans + 1;
        }
    }

    protected void FinishAndReport()
    {
        bool passSearch = m_SawSearch && m_SearchWarnScans > 0;
        bool passLock = m_SawLock && m_LockWarnScans > 0;
        bool allPass = passSearch && passLock && m_ScanCount > 5;

        Print(string.Format(
            "[RDF RWR Test] %1 scans=%2 search=%3 lock=%4 status=%5",
            BoolLabel(allPass),
            m_ScanCount.ToString(),
            m_SearchWarnScans.ToString(),
            m_LockWarnScans.ToString(),
            RDF_RadarRwr.GetStatusShort(m_Target)));
        Print("[RDF RWR Test] checks: search=" + BoolLabel(passSearch)
            + " lock=" + BoolLabel(passLock));

        StopInternal(true);
    }

    protected string BoolLabel(bool v)
    {
        return RDF_RadarTestUtil.BoolLabel(v);
    }
}
