// Fully automated DEM clutter regression test for in-game radar.
// Usage (Script Debugger): RDF_RadarAutoTest.Start();
class RDF_RadarAutoTestCaseResult
{
    string m_Name;
    int m_ScanSamples;
    int m_TargetSamples;
    int m_DetectedSamples;
    int m_DemValidSamples;
    int m_AnonymousSamples;
    int m_FalsePlotSamples;
    float m_ClutterSumW;
    float m_SnrSumDb;

    float GetAvgClutterW()
    {
        if (m_TargetSamples <= 0)
            return 0.0;
        return m_ClutterSumW / m_TargetSamples;
    }

    float GetAvgSnrDb()
    {
        if (m_TargetSamples <= 0)
            return 0.0;
        return m_SnrSumDb / m_TargetSamples;
    }
}

class RDF_RadarAutoTest
{
    protected static const float CLUTTER_EPS = 1.0e-35;
    protected static ref RDF_RadarAutoTest s_Instance;
    protected static bool s_TickRegistered;

    protected bool m_Running;
    protected int m_PhaseIndex;
    protected float m_PhaseStartWallS;
    protected float m_LastScanProgressWallS;
    protected float m_PhaseDurationS = 4.0;
    protected int m_LastScanSerial = -1;

    protected ref array<ref RDF_RadarAutoTestCaseResult> m_Results;
    protected ref RDF_RadarAutoTestCaseResult m_Current;

    protected ref RDF_RadarSettings m_PreviousConfig;
    protected bool m_PreviousDemoEnabled;
    protected bool m_PreviousHudEnabled;
    protected bool m_PreviousForceLocal;
    protected ref array<IEntity> m_TestEmitterOwners;
    protected ref array<float> m_TestEmitterBaseAngles;

    static RDF_RadarAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar AutoTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarAutoTest inst = GetInstance();
        if (!inst)
            return false;
        return inst.m_Running;
    }

    protected static void StaticTick()
    {
        RDF_RadarAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar AutoTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("DEM"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar AutoTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("DEM");
            return;
        }

        m_PreviousConfig = RDF_RadarAutoRunner.GetDemoConfig();
        m_PreviousDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PreviousHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PreviousForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        m_Results = new array<ref RDF_RadarAutoTestCaseResult>();
        m_TestEmitterOwners = new array<IEntity>();
        m_TestEmitterBaseAngles = new array<float>();
        m_PhaseIndex = 0;
        m_LastScanSerial = -1;
        m_LastScanProgressWallS = System.GetTickCount() * 0.001;
        m_Running = true;

        // A Workbench character carrying RplComponent makes AutoRunner pick the
        // networked scanner, which ignores the per-phase configs below.
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.SetDemoEnabled(true);

        if (!PrepareSyntheticEmitters())
        {
            Print("[RDF Radar AutoTest] failed: cannot prepare synthetic emitters.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print("[RDF Radar AutoTest] start: DEM OFF/ON/SCALE/BUDGET regression");
        SetupPhase(m_PhaseIndex);
    }

    protected void StopInternal(bool restorePrevious)
    {
        m_Running = false;
        m_Current = null;
        CleanupSyntheticEmitters();
        RDF_RadarAutoTestGate.Release("DEM");

        if (!restorePrevious)
            return;

        if (m_PreviousConfig)
            RDF_RadarAutoRunner.SetDemoConfig(m_PreviousConfig);
        RDF_RadarAutoRunner.SetDemoEnabled(m_PreviousDemoEnabled);
        RDF_RadarAutoRunner.SetHudEnabled(m_PreviousHudEnabled);
        RDF_RadarAutoRunner.SetForceLocalScan(m_PreviousForceLocal);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float wallNowS = System.GetTickCount() * 0.001;
        UpdateSyntheticEmitters(wallNowS);
        AccumulateLatestScan();
        if (wallNowS - m_PhaseStartWallS < m_PhaseDurationS)
            return;

        FinalizeCurrentPhase();
    }

    protected void SetupPhase(int phaseIdx)
    {
        RDF_RadarSettings cfg = BuildBaseConfig();
        string name = "";

        if (phaseIdx == 0)
        {
            name = "dem_off";
            cfg.m_EnableDemClutter = false;
        }
        else if (phaseIdx == 1)
        {
            name = "dem_on_scale1";
            cfg.m_EnableDemClutter = true;
            cfg.m_DemClutterScale = 0.2;
        }
        else if (phaseIdx == 2)
        {
            name = "dem_on_scale5";
            cfg.m_EnableDemClutter = true;
            cfg.m_DemClutterScale = 1.0;
        }
        else
        {
            name = "dem_on_budget0";
            cfg.m_EnableDemClutter = true;
            cfg.m_DemClutterScale = 1.0;
            cfg.m_DemTileLoadsPerScan = 0;
            cfg.m_DemCacheMaxTiles = 1;
        }

        cfg.Validate();
        RDF_RadarAutoRunner.SetDemoConfig(cfg);
        RDF_RadarAutoRunner.StartAutoRun();

        m_Current = new RDF_RadarAutoTestCaseResult();
        m_Current.m_Name = name;
        m_LastScanSerial = RDF_RadarAutoRunner.GetLastScanSerial();
        m_PhaseStartWallS = System.GetTickCount() * 0.001;

        Print("[RDF Radar AutoTest] phase start: " + name);
    }

    protected RDF_RadarSettings BuildBaseConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarDemoConfig.CreateDefault(128);
        cfg.m_KeepUndetected = true;
        cfg.m_Range = 8000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        cfg.m_IncludeRadarEmitters = true;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 8.0;
        cfg.m_DemCacheMaxTiles = RDF_DemBakeConstants.RUNTIME_DEM_CACHE_MAX_TILES;
        cfg.m_DemTileLoadsPerScan = RDF_DemBakeConstants.RUNTIME_DEM_LOADS_PER_SCAN;
        cfg.m_DemClutterScale = 1.0;
        cfg.Validate();
        return cfg;
    }

    protected void AccumulateLatestScan()
    {
        if (!m_Current)
            return;

        int scanSerial = RDF_RadarAutoRunner.GetLastScanSerial();
        if (scanSerial < 0)
            return;
        if (scanSerial == m_LastScanSerial)
        {
            float wallNowS = System.GetTickCount() * 0.001;
            if (wallNowS - m_LastScanProgressWallS > 5.0)
            {
                m_LastScanProgressWallS = wallNowS;
                Print("[RDF Radar AutoTest] waiting for scan progress; ensure Play mode is running.");
            }
            return;
        }
        m_LastScanSerial = scanSerial;
        m_LastScanProgressWallS = System.GetTickCount() * 0.001;
        m_Current.m_ScanSamples = m_Current.m_ScanSamples + 1;

        array<ref RDF_RadarTarget> targets = RDF_RadarAutoRunner.GetLastTargets();
        if (!targets || targets.Count() <= 0)
            return;

        foreach (RDF_RadarTarget t : targets)
        {
            if (!t)
                continue;

            m_Current.m_TargetSamples = m_Current.m_TargetSamples + 1;
            if (t.m_Detected)
                m_Current.m_DetectedSamples = m_Current.m_DetectedSamples + 1;
            if (t.m_DemSampleValid)
                m_Current.m_DemValidSamples = m_Current.m_DemValidSamples + 1;
            if (t.m_IsAnonymous)
                m_Current.m_AnonymousSamples = m_Current.m_AnonymousSamples + 1;
            if (t.m_IsFalsePlot)
                m_Current.m_FalsePlotSamples = m_Current.m_FalsePlotSamples + 1;

            m_Current.m_ClutterSumW = m_Current.m_ClutterSumW + t.m_ClutterPowerW;
            m_Current.m_SnrSumDb = m_Current.m_SnrSumDb + t.m_SnrDb;
        }
    }

    protected void FinalizeCurrentPhase()
    {
        if (!m_Current)
            return;

        m_Results.Insert(m_Current);
        string scansText = m_Current.m_ScanSamples.ToString();
        string targetsText = m_Current.m_TargetSamples.ToString();
        string demValidText = m_Current.m_DemValidSamples.ToString();
        float avgClutter = m_Current.GetAvgClutterW();
        float avgSnr = m_Current.GetAvgSnrDb();
        string avgClutterText = avgClutter.ToString();
        string avgSnrText = avgSnr.ToString();
        Print(string.Format(
            "[RDF Radar AutoTest] phase done: %1 scans=%2 targets=%3 demValid=%4 avgClutter=%5 avgSNR=%6",
            m_Current.m_Name,
            scansText,
            targetsText,
            demValidText,
            avgClutterText,
            avgSnrText));

        m_Current = null;
        m_PhaseIndex = m_PhaseIndex + 1;

        if (m_PhaseIndex < 4)
        {
            SetupPhase(m_PhaseIndex);
            return;
        }

        EvaluateAndReport();
        StopInternal(true);
    }

    protected void EvaluateAndReport()
    {
        RDF_RadarAutoTestCaseResult off = m_Results.Get(0);
        RDF_RadarAutoTestCaseResult on1 = m_Results.Get(1);
        RDF_RadarAutoTestCaseResult on5 = m_Results.Get(2);
        RDF_RadarAutoTestCaseResult budget0 = m_Results.Get(3);

        bool passOffLowClutter = off.GetAvgClutterW() <= 0.000000000000001;
        bool passScansPresent = off.m_ScanSamples > 0
            && on1.m_ScanSamples > 0
            && on5.m_ScanSamples > 0
            && budget0.m_ScanSamples > 0;
        bool passTargetsPresent = on1.m_TargetSamples > 0
            && on5.m_TargetSamples > 0;
        bool passOnHasDem = on1.m_DemValidSamples > 0;
        bool on1ClutterVisible = on1.GetAvgClutterW() > CLUTTER_EPS;
        bool on5ClutterVisible = on5.GetAvgClutterW() > CLUTTER_EPS;
        bool on1SnrShift = on1.GetAvgSnrDb() < off.GetAvgSnrDb() - 1.0;
        bool on5SnrShift = on5.GetAvgSnrDb() < on1.GetAvgSnrDb() - 0.3;
        bool floorSatChain = on1.GetAvgSnrDb() <= -299.0 && on5.GetAvgSnrDb() <= -299.0;

        bool passOnClutterNonZero = on1ClutterVisible || on1SnrShift;
        bool passScaleSensitive = false;
        if (on1ClutterVisible && on5ClutterVisible)
            passScaleSensitive = on5.GetAvgClutterW() > on1.GetAvgClutterW() * 1.1;
        if (!passScaleSensitive)
            passScaleSensitive = on5SnrShift || floorSatChain;

        bool passBudgetZeroNoDem = budget0.m_DemValidSamples == 0;
        bool passBudgetZeroNoClutter = budget0.GetAvgClutterW() <= 0.000000000000001;

        bool allPass = passScansPresent
            && passTargetsPresent
            && passOffLowClutter
            && passOnHasDem
            && passOnClutterNonZero
            && passScaleSensitive
            && passBudgetZeroNoDem
            && passBudgetZeroNoClutter;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar DEM AutoTest");
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScansPresent));
        lines.Insert("  targets_present " + BoolLabel(passTargetsPresent));
        lines.Insert("  off_low_clutter " + BoolLabel(passOffLowClutter));
        lines.Insert("  on_has_dem_samples " + BoolLabel(passOnHasDem));
        lines.Insert("  on_clutter_nonzero " + BoolLabel(passOnClutterNonZero));
        lines.Insert("  scale_sensitive " + BoolLabel(passScaleSensitive));
        lines.Insert("  budget0_no_dem " + BoolLabel(passBudgetZeroNoDem));
        lines.Insert("  budget0_no_clutter " + BoolLabel(passBudgetZeroNoClutter));
        lines.Insert("");
        lines.Insert("debug:");
        lines.Insert("  on1_clutter_visible " + BoolLabel(on1ClutterVisible));
        lines.Insert("  on5_clutter_visible " + BoolLabel(on5ClutterVisible));
        lines.Insert("  on1_snr_shift " + BoolLabel(on1SnrShift));
        lines.Insert("  on5_snr_shift " + BoolLabel(on5SnrShift));
        lines.Insert("  floor_saturated_chain " + BoolLabel(floorSatChain));
        lines.Insert("");
        lines.Insert("phases:");
        AppendCaseLines(lines, off);
        AppendCaseLines(lines, on1);
        AppendCaseLines(lines, on5);
        AppendCaseLines(lines, budget0);

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_dem_autotest_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar AutoTest] " + BoolLabel(allPass) + "  report=" + reportPath);
    }

    protected bool PrepareSyntheticEmitters()
    {
        m_TestEmitterOwners.Clear();
        m_TestEmitterBaseAngles.Clear();

        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!subject)
            return false;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        array<IEntity> active = new array<IEntity>();
        world.GetActiveEntities(active);
        for (int i = 0; i < active.Count(); i++)
        {
            IEntity ent = active.Get(i);
            if (!ent || ent == subject)
                continue;

            m_TestEmitterOwners.Insert(ent);
            float phase = 0.0;
            if (m_TestEmitterOwners.Count() == 1)
                phase = 0.0;
            else if (m_TestEmitterOwners.Count() == 2)
                phase = 2.0943951;
            else
                phase = 4.1887902;
            m_TestEmitterBaseAngles.Insert(phase);

            if (m_TestEmitterOwners.Count() >= 3)
                break;
        }

        if (m_TestEmitterOwners.Count() <= 0)
            return false;

        UpdateSyntheticEmitters(world.GetWorldTime() * 0.001);
        Print("[RDF Radar AutoTest] synthetic emitters prepared: " + m_TestEmitterOwners.Count().ToString());
        return true;
    }

    protected void UpdateSyntheticEmitters(float nowS)
    {
        if (!m_TestEmitterOwners || m_TestEmitterOwners.Count() <= 0)
            return;

        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!subject)
            return;

        vector mat[4];
        subject.GetWorldTransform(mat);
        vector origin = mat[3];
        float radarY = origin[1] + 40.0;

        for (int i = 0; i < m_TestEmitterOwners.Count(); i++)
        {
            IEntity owner = m_TestEmitterOwners.Get(i);
            if (!owner)
                continue;

            float baseAngle = m_TestEmitterBaseAngles.Get(i);
            float angularRate = 0.20 + i * 0.08;
            float angle = baseAngle + nowS * angularRate;
            float radius = 450.0 + i * 350.0;

            vector pos = Vector(
                origin[0] + Math.Cos(angle) * radius,
                radarY + i * 18.0,
                origin[2] + Math.Sin(angle) * radius);
            RDF_RadarEmitterRegistry.Register(owner, pos, true, 1.0);
        }
    }

    protected void CleanupSyntheticEmitters()
    {
        if (!m_TestEmitterOwners)
            return;

        for (int i = 0; i < m_TestEmitterOwners.Count(); i++)
        {
            IEntity owner = m_TestEmitterOwners.Get(i);
            if (!owner)
                continue;
            RDF_RadarEmitterRegistry.SetEmitting(owner, false);
            RDF_RadarEmitterRegistry.Unregister(owner);
        }
        m_TestEmitterOwners.Clear();
        m_TestEmitterBaseAngles.Clear();
    }

    protected void AppendCaseLines(
        notnull array<string> lines,
        RDF_RadarAutoTestCaseResult c)
    {
        if (!c)
            return;

        string scans = c.m_ScanSamples.ToString();
        string targets = c.m_TargetSamples.ToString();
        string detected = c.m_DetectedSamples.ToString();
        string demValid = c.m_DemValidSamples.ToString();
        string anon = c.m_AnonymousSamples.ToString();
        string falsePlots = c.m_FalsePlotSamples.ToString();
        float avgClutter = c.GetAvgClutterW();
        float avgSnr = c.GetAvgSnrDb();
        string avgClutterText = avgClutter.ToString();
        string avgSnrText = avgSnr.ToString();

        string line = "  " + c.m_Name;
        line = line + " scans=" + scans;
        line = line + " targets=" + targets;
        line = line + " detected=" + detected;
        line = line + " demValid=" + demValid;
        line = line + " anon=" + anon;
        line = line + " false=" + falsePlots;
        line = line + " avgClutterW=" + avgClutterText;
        line = line + " avgSNRdB=" + avgSnrText;
        lines.Insert(line);
    }

    protected string BoolLabel(bool value)
    {
        if (value)
            return "PASS";
        return "FAIL";
    }
}
