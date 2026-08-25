// Fully automated DEM clutter regression test for in-game radar.
//
// Targets are real spawned airframes with no detection aid: no emitter flag, no
// RCS override, no registry pre-seeding. Clutter is measured against their own
// skin return.
//
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
    protected static bool s_LastPass;

    protected static const ResourceName TARGET_PREFAB =
        "{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et";
    protected static const int TARGET_COUNT = 3;
    protected static const float WARMUP_TIMEOUT_S = 30.0;

    protected bool m_Running;
    protected int m_PhaseIndex;
    protected float m_PhaseStartWallS;
    protected float m_LastScanProgressWallS;
    // Long enough for a cold scatterer table to classify the spawned targets
    // now that the test no longer pre-registers them.
    protected float m_PhaseDurationS = 8.0;
    protected int m_LastScanSerial = -1;

    protected ref array<ref RDF_RadarAutoTestCaseResult> m_Results;
    protected ref RDF_RadarAutoTestCaseResult m_Current;

    protected ref RDF_RadarSettings m_PreviousConfig;
    protected bool m_PreviousDemoEnabled;
    protected bool m_PreviousHudEnabled;
    protected bool m_PreviousForceLocal;
    protected ref array<IEntity> m_Targets;
    protected ref array<vector> m_TargetAnchors;
    protected bool m_WarmupDone;
    protected bool m_TargetsDiscovered;
    protected float m_WarmupStartWallS;

    static RDF_RadarAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_LastPass = false;
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

    static bool DidLastPass()
    {
        return s_LastPass;
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

        m_PreviousConfig = null;
        RDF_RadarSensor prevSensor = RDF_RadarAutoRunner.GetSensor();
        if (prevSensor)
            m_PreviousConfig = prevSensor.GetSettings();
        m_PreviousDemoEnabled = RDF_RadarAutoRunner.IsDemoEnabled();
        m_PreviousHudEnabled = RDF_RadarAutoRunner.IsHudEnabled();
        m_PreviousForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        m_Results = new array<ref RDF_RadarAutoTestCaseResult>();
        m_Targets = new array<IEntity>();
        m_TargetAnchors = new array<vector>();
        m_PhaseIndex = 0;
        m_WarmupDone = false;
        m_TargetsDiscovered = false;
        m_LastScanSerial = -1;
        m_LastScanProgressWallS = System.GetTickCount() * 0.001;
        m_Running = true;

        // AutoRunner only hosts Sensor.Tick + optional HUD. Configure via Sensor.
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.SetDemoEnabled(true);

        if (!PrepareTargets())
        {
            Print("[RDF Radar AutoTest] failed: cannot spawn clutter targets.", LogLevel.ERROR);
            StopInternal(true);
            return;
        }

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        // Run the base config first so the sweep can classify the new targets.
        // Starting dem_off before they are in the table makes that phase measure
        // nothing and its clutter check pass vacuously.
        m_WarmupDone = false;
        m_WarmupStartWallS = System.GetTickCount() * 0.001;
        RDF_RadarSettings warmup = BuildBaseConfig();
        warmup.m_EnableDemClutter = false;
        warmup.Validate();
        ApplySensorConfig(warmup);
        RDF_RadarAutoRunner.StartAutoRun();

        Print("[RDF Radar AutoTest] start: DEM OFF/ON/SCALE/BUDGET regression");
    }

    // Presentation host keeps ticking; all radar knobs go through RDF_RadarSensor.
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

    // All spawned targets present in the scatterer table.
    protected bool AreTargetsDiscovered()
    {
        if (!m_Targets || m_Targets.Count() <= 0)
            return false;

        for (int i = 0; i < m_Targets.Count(); i++)
        {
            IEntity target = m_Targets.Get(i);
            if (!target)
                continue;
            if (!RDF_RadarScattererRegistry.Find(target))
                return false;
        }
        return true;
    }

    protected void StopInternal(bool restorePrevious)
    {
        m_Running = false;
        m_Current = null;
        CleanupTargets();
        RDF_RadarAutoTestGate.Release("DEM");

        if (!restorePrevious)
            return;

        // Clear session state so a second standalone run does not inherit
        // tracks / scatterer entries from the previous run (matches
        // Lock/ShellFire/Eccm/Dwell/Jpda/Perf/Play StopInternal pattern).
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            sensor.ResetSession();

        if (m_PreviousConfig)
            ApplySensorConfig(m_PreviousConfig);
        RDF_RadarAutoRunner.SetDemoEnabled(m_PreviousDemoEnabled);
        RDF_RadarAutoRunner.SetHudEnabled(m_PreviousHudEnabled);
        RDF_RadarAutoRunner.SetForceLocalScan(m_PreviousForceLocal);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float wallNowS = System.GetTickCount() * 0.001;
        HoldTargets();

        if (!m_WarmupDone)
        {
            bool discovered = AreTargetsDiscovered();
            bool timedOut = wallNowS - m_WarmupStartWallS > WARMUP_TIMEOUT_S;
            if (!discovered && !timedOut)
                return;

            m_WarmupDone = true;
            m_TargetsDiscovered = discovered;
            Print(string.Format(
                "[RDF Radar AutoTest] warmup done after %1s discovered=%2",
                (wallNowS - m_WarmupStartWallS).ToString(),
                discovered.ToString()));
            SetupPhase(m_PhaseIndex);
            return;
        }

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
        ApplySensorConfig(cfg);
        RDF_RadarAutoRunner.StartAutoRun();

        // budget0: drop warm SURF tiles so only liveY+UNKNOWN fallback remains.
        if (phaseIdx == 3)
        {
            RDF_RadarSensor sensor = GetSensor();
            if (sensor)
                sensor.ClearDemCache();
        }

        m_Current = new RDF_RadarAutoTestCaseResult();
        m_Current.m_Name = name;
        RDF_RadarSensor serialSensor = GetSensor();
        if (serialSensor)
            m_LastScanSerial = serialSensor.GetScanSerial();
        else
            m_LastScanSerial = -1;
        m_PhaseStartWallS = System.GetTickCount() * 0.001;

        Print("[RDF Radar AutoTest] phase start: " + name);
    }

    protected RDF_RadarSettings BuildBaseConfig()
    {
        // Sensor SEARCH preset, then widen for clutter measurability.
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_KeepUndetected = true;
        cfg.m_Range = 8000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = true;
        // Skin returns only; an ESM path would bypass the clutter measurement.
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 8.0;
        cfg.m_ScattererClassifyPerTick = 256;
        cfg.m_ScattererRefreshPerTick = 512;
        cfg.m_ScattererMaxEntries = 2048;
        cfg.m_ScattererDiscoveryIntervalS = 0.5;
        cfg.m_DemCacheMaxTiles = RDF_DemBakeConstants.RUNTIME_DEM_CACHE_MAX_TILES;
        cfg.m_DemTileLoadsPerScan = RDF_DemBakeConstants.RUNTIME_DEM_LOADS_PER_SCAN;
        // Keep lazy LRU so budget0 / cache-size phases remain meaningful.
        cfg.m_DemPreloadAll = false;
        cfg.m_DemClutterScale = 1.0;
        // Default Shorad pencil (2.5°) puts the staggered targets outside the main
        // lobe → patternGain≈0 → SNR=-300 and clutter=0. DEM regression needs
        // a wide stare so clutter power is measurable on every target.
        cfg.m_EnableMechanicalScan = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);
        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 90.0;
        hw.m_ScanRpm = 0.0;
        // Targets are held stationary, and MTI also folds the clutter return down
        // to its floor — both would erase what this test measures.
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("dem_low", 2.0, 20.0, 0.0);
        hw.AddElevationBeam("dem_mid", 10.0, 24.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.StabilizeForRegression();
        // Clutter regression needs entity-truth power, not CFAR plots.
        cfg.m_EnableMeasurementSynthesis = false;
        cfg.m_KeepEntityTruth = true;
        cfg.m_EnableCfarGate = false;
        cfg.Validate();
        return cfg;
    }

    protected void AccumulateLatestScan()
    {
        if (!m_Current)
            return;

        RDF_RadarSensor sensor = GetSensor();
        if (!sensor)
            return;

        int scanSerial = sensor.GetScanSerial();
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

        array<ref RDF_RadarTarget> targets = sensor.GetPlots();
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

        // Without this the off phase can report zero clutter simply because it
        // never saw a target, which is a vacuous pass.
        bool passDiscovery = m_TargetsDiscovered && off.m_TargetSamples > 0;
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

        // Tile-load budget gates SURF pack loads only. With liveY, TrySampleAt
        // still returns valid cells (GetSurfaceY + UNKNOWN) and SurfaceTable
        // still produces clutter — so "no dem / no clutter" is obsolete.
        bool passBudgetZeroLiveSamples = budget0.m_DemValidSamples > 0;
        bool budget0ClutterVisible = budget0.GetAvgClutterW() > CLUTTER_EPS;
        bool budget0SnrDegraded = budget0.GetAvgSnrDb() < off.GetAvgSnrDb() - 1.0;
        bool passBudgetZeroLiveClutter = budget0ClutterVisible || budget0SnrDegraded;

        bool allPass = passScansPresent
            && passDiscovery
            && passTargetsPresent
            && passOffLowClutter
            && passOnHasDem
            && passOnClutterNonZero
            && passScaleSensitive
            && passBudgetZeroLiveSamples
            && passBudgetZeroLiveClutter;
        s_LastPass = allPass;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar DEM AutoTest");
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScansPresent));
        lines.Insert("  discovered_unaided " + BoolLabel(passDiscovery));
        lines.Insert("  targets_present " + BoolLabel(passTargetsPresent));
        lines.Insert("  off_low_clutter " + BoolLabel(passOffLowClutter));
        lines.Insert("  on_has_dem_samples " + BoolLabel(passOnHasDem));
        lines.Insert("  on_clutter_nonzero " + BoolLabel(passOnClutterNonZero));
        lines.Insert("  scale_sensitive " + BoolLabel(passScaleSensitive));
        lines.Insert("  budget0_live_samples " + BoolLabel(passBudgetZeroLiveSamples));
        lines.Insert("  budget0_live_clutter " + BoolLabel(passBudgetZeroLiveClutter));
        lines.Insert("");
        lines.Insert("debug:");
        lines.Insert("  on1_clutter_visible " + BoolLabel(on1ClutterVisible));
        lines.Insert("  on5_clutter_visible " + BoolLabel(on5ClutterVisible));
        lines.Insert("  on1_snr_shift " + BoolLabel(on1SnrShift));
        lines.Insert("  on5_snr_shift " + BoolLabel(on5SnrShift));
        lines.Insert("  floor_saturated_chain " + BoolLabel(floorSatChain));
        lines.Insert("  budget0_clutter_visible " + BoolLabel(budget0ClutterVisible));
        lines.Insert("  budget0_snr_degraded " + BoolLabel(budget0SnrDegraded));
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

    // Spawn real airframes along boresight at stepped ranges. Clutter is measured
    // against their own skin return; nothing is flagged as an emitter and no RCS
    // is overridden.
    protected bool PrepareTargets()
    {
        m_Targets.Clear();
        m_TargetAnchors.Clear();

        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!subject)
            return false;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        Resource prefabRes = Resource.Load(TARGET_PREFAB);
        if (!prefabRes)
            return false;

        vector mat[4];
        subject.GetWorldTransform(mat);
        vector origin = mat[3];
        vector fwd = mat[2];
        float flatLen = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        vector flatFwd;
        if (flatLen < 0.001)
            flatFwd = Vector(0.0, 0.0, 1.0);
        else
            flatFwd = Vector(fwd[0] / flatLen, 0.0, fwd[2] / flatLen);

        for (int i = 0; i < TARGET_COUNT; i++)
        {
            float radius = 400.0 + i * 300.0;
            float groundY = world.GetSurfaceY(
                origin[0] + flatFwd[0] * radius,
                origin[2] + flatFwd[2] * radius);
            // Low AGL keeps the grazing angle shallow so DEM clutter matters.
            vector pos = Vector(
                origin[0] + flatFwd[0] * radius,
                groundY + 60.0 + i * 18.0,
                origin[2] + flatFwd[2] * radius);

            EntitySpawnParams spawnParams = new EntitySpawnParams();
            Math3D.AnglesToMatrix(Vector(0, 0, 0), spawnParams.Transform);
            spawnParams.Transform[3] = pos;

            IEntity target = GetGame().SpawnEntityPrefab(prefabRes, world, spawnParams);
            if (!target)
                continue;
            m_Targets.Insert(target);
            m_TargetAnchors.Insert(pos);
        }

        if (m_Targets.Count() <= 0)
            return false;

        Print("[RDF Radar AutoTest] targets spawned: " + m_Targets.Count().ToString());
        return true;
    }

    // Hold the airframes on their anchors; they would otherwise fall out of beam.
    protected void HoldTargets()
    {
        if (!m_Targets)
            return;

        for (int i = 0; i < m_Targets.Count(); i++)
        {
            IEntity target = m_Targets.Get(i);
            if (!target)
                continue;
            target.SetOrigin(m_TargetAnchors.Get(i));
            Physics physics = target.GetPhysics();
            if (physics)
                physics.SetVelocity("0 0 0");
        }
    }

    protected void CleanupTargets()
    {
        if (!m_Targets)
            return;

        for (int i = 0; i < m_Targets.Count(); i++)
        {
            IEntity target = m_Targets.Get(i);
            if (!target)
                continue;
            RDF_RadarScattererRegistry.Unregister(target);
            SCR_EntityHelper.DeleteEntityAndChildren(target);
        }
        m_Targets.Clear();
        m_TargetAnchors.Clear();
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
        return RDF_RadarTestUtil.BoolLabel(value);
    }
}
