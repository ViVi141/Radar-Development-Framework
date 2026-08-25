// ESM + anti-radiation API regression (Script Debugger):
//   RDF_RadarEsmArmAutoTest.Start();
// Not part of StartAll — run alone when validating ESM / GetArmAim.
class RDF_RadarEsmArmAutoTest
{
    protected static ref RDF_RadarEsmArmAutoTest s_Instance;
    protected static bool s_TickRegistered;

    protected static const ResourceName AIR_TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";

    protected bool m_Running;
    protected float m_StartWallS;
    protected float m_DurationS = 28.0;
    protected int m_LastScanSerial = -1;
    protected int m_Phase;

    protected IEntity m_Subject;
    protected IEntity m_Emitter;
    protected vector m_EmitterPos;

    protected int m_ScanCount;
    protected int m_EmitterPlotScans;
    protected int m_ArmAimOkScans;
    protected int m_ArmAimAfterSilenceFail;
    protected bool m_SawArmAim;
    protected bool m_SilenceApplied;
    protected bool m_UnlockAfterSilence;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarEsmArmAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarEsmArmAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarEsmArmAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF ESM/ARM Test] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarEsmArmAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarEsmArmAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF ESM/ARM Test] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("EsmArm"))
            return;

        if (!GetGame().GetWorld())
        {
            Print("[RDF ESM/ARM Test] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("EsmArm");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF ESM/ARM Test] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("EsmArm");
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
        if (!SpawnEmitterTarget())
        {
            Print("[RDF ESM/ARM Test] failed to spawn emitter prefab.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyEsmConfig();
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_ScanCount = 0;
        m_EmitterPlotScans = 0;
        m_ArmAimOkScans = 0;
        m_ArmAimAfterSilenceFail = 0;
        m_SawArmAim = false;
        m_SilenceApplied = false;
        m_UnlockAfterSilence = false;
        m_Phase = 0;
        m_LastScanSerial = -1;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print("[RDF ESM/ARM Test] started — ESM listen + GetArmAim / silence unlock.");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        if (m_Emitter)
        {
            RDF_RadarEmitterRegistry.SetEmitting(m_Emitter, false);
            RDF_RadarEmitterRegistry.Unregister(m_Emitter);
            // Also drop the emitter from the scatterer registry (sweep may
            // have added it as a regular scatterer); matches Rwr/Lock/Air
            // cleanup pattern.
            RDF_RadarScattererRegistry.Unregister(m_Emitter);
            SCR_EntityHelper.DeleteEntityAndChildren(m_Emitter);
            m_Emitter = null;
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

        RDF_RadarAutoTestGate.Release("EsmArm");
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

        if (m_Emitter)
        {
            m_Emitter.SetOrigin(m_EmitterPos);
            if (!m_SilenceApplied)
            {
                RDF_RadarEmitterRegistry.RegisterWithRadio(
                    m_Emitter,
                    m_EmitterPos,
                    true,
                    1.0,
                    9000000000.0,
                    120000.0,
                    32.0);
            }
        }

        AccumulateLatestScan();
    }

    protected bool SpawnEmitterTarget()
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

        m_EmitterPos = origin + forward * 600.0;
        m_EmitterPos[1] = origin[1] + 80.0;

        Resource prefabRes = Resource.Load(AIR_TARGET_PREFAB);
        if (!prefabRes)
            return false;

        EntitySpawnParams spawnParams = new EntitySpawnParams();
        Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
        spawnParams.Transform[3] = m_EmitterPos;
        m_Emitter = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
        if (!m_Emitter)
            return false;

        RDF_RadarEmitterRegistry.RegisterWithRadio(
            m_Emitter,
            m_EmitterPos,
            true,
            1.0,
            9000000000.0,
            120000.0,
            32.0);
        Print("[RDF ESM/ARM Test] emitter at " + m_EmitterPos.ToString());
        return true;
    }

    protected void ApplyEsmConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateEsmSettings(128);
        cfg.m_Range = 4000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_DetectionSnrDb = 0.0;
        cfg.m_KeepUndetected = true;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableCfarGate = false;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_MaxLosTracesPerScan = 96;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        cfg.Validate();

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
            sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM, 128);

        RDF_RadarAutoRunner.SetDemoConfig(cfg);

        sensor = RDF_RadarAutoRunner.GetSensor();
        if (sensor)
        {
            RDF_RadarLockManager lockMgr = sensor.GetLockManager();
            lockMgr.SetTypeFilter(false, false, true);
            lockMgr.SetAutoAcquire(true);
            lockMgr.SetArmRequireLiveEmitter(true);
            lockMgr.SetAcquireHits(1);
            lockMgr.SetMaxLockRange(4000.0);
            lockMgr.SetLockSector(0.0);
        }
    }

    protected void AccumulateLatestScan()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
            return;

        int serial = sensor.GetScanSerial();
        if (serial == m_LastScanSerial)
            return;
        m_LastScanSerial = serial;
        m_ScanCount = m_ScanCount + 1;

        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        bool sawEmitterPlot = false;
        if (plots)
        {
            for (int i = 0; i < plots.Count(); i++)
            {
                RDF_RadarTarget t = plots.Get(i);
                if (!t)
                    continue;
                if (t.m_Type != ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
                    continue;
                if (!t.m_Detected)
                    continue;
                sawEmitterPlot = true;
                break;
            }
        }
        if (sawEmitterPlot)
            m_EmitterPlotScans = m_EmitterPlotScans + 1;

        IEntity aimEnt;
        vector aimPos;
        bool radiating;
        bool armOk = sensor.GetArmAim(aimEnt, aimPos, radiating);

        if (!m_SilenceApplied)
        {
            if (armOk && radiating)
            {
                m_SawArmAim = true;
                m_ArmAimOkScans = m_ArmAimOkScans + 1;
            }

            // After enough successful ARM reads, silence the emitter.
            if (m_ArmAimOkScans >= 3 && m_Emitter)
            {
                RDF_RadarEmitterRegistry.SetEmitting(m_Emitter, false);
                m_SilenceApplied = true;
                m_Phase = 1;
                Print("[RDF ESM/ARM Test] emitter silenced — expecting ARM unlock.");
            }
        }
        else
        {
            if (!armOk || !radiating)
                m_ArmAimAfterSilenceFail = m_ArmAimAfterSilenceFail + 1;

            RDF_RadarLockManager lockMgr = sensor.GetLockManager();
            if (lockMgr && lockMgr.GetLockedTrackId() < 0)
                m_UnlockAfterSilence = true;
        }
    }

    protected void FinishAndReport()
    {
        bool passPlots = m_EmitterPlotScans > 0;
        bool passArm = m_SawArmAim && m_ArmAimOkScans > 0;
        bool passSilence = m_SilenceApplied
            && m_ArmAimAfterSilenceFail > 0
            && m_UnlockAfterSilence;
        bool allPass = passPlots && passArm && passSilence && m_ScanCount > 5;

        Print(string.Format(
            "[RDF ESM/ARM Test] %1 scans=%2 emitPlots=%3 armOk=%4 silenceFail=%5 unlock=%6",
            BoolLabel(allPass),
            m_ScanCount.ToString(),
            m_EmitterPlotScans.ToString(),
            m_ArmAimOkScans.ToString(),
            m_ArmAimAfterSilenceFail.ToString(),
            m_UnlockAfterSilence.ToString()));
        Print("[RDF ESM/ARM Test] checks: plots=" + BoolLabel(passPlots)
            + " arm=" + BoolLabel(passArm)
            + " silence=" + BoolLabel(passSilence));

        StopInternal(true);
    }

    protected string BoolLabel(bool v)
    {
        return RDF_RadarTestUtil.BoolLabel(v);
    }
}
