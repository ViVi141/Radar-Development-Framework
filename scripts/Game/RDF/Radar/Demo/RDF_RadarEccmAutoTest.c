// ECCM decision-layer test (TODO §9 S3).
//
// Injects a sidelobe noise jammer + a deception jammer into the EW stack, enables
// the opt-in ECCM decision layer, then verifies the radar auto-responds:
//   * sidelobe noise jam  -> sidelobe blanking (SLB)
//   * deception false plot -> PRF agility
//
// The decision math (hysteresis, coupling-mode selection) is already covered by
// the offline golden (tools/dem/test_rdf_radar_eccm.py). This test validates the
// in-game integration: observables read -> decision -> SLB wired to the jammers.
//
// Usage (Script Debugger): RDF_RadarEccmAutoTest.Start();
class RDF_RadarEccmAutoTest
{
    protected static ref RDF_RadarEccmAutoTest s_Instance;
    protected static bool s_TickRegistered;
    protected static bool s_LastPass;

    protected static const float DURATION_S = 8.0;
    protected static const float JAMMER_SIDELOBE_RANGE_M = 500.0;

    protected bool m_Running;
    protected float m_StartWallS;
    protected int m_LastScanSerial;
    protected float m_LastProgressWallS;

    protected IEntity m_Subject;
    protected vector m_RadarOrigin;

    protected int m_ScanCount;
    protected int m_SlbScans;
    protected int m_PrfScans;

    protected ref RDF_RadarSettings m_PrevConfig;
    protected bool m_PrevDemoEnabled;
    protected bool m_PrevHudEnabled;
    protected bool m_PrevForceLocal;

    static RDF_RadarEccmAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarEccmAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        s_LastPass = false;
        GetInstance().StartInternal();
    }

    static void Stop()
    {
        RDF_RadarEccmAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.StopInternal(true);
        Print("[RDF Radar EccmTest] stopped by user.");
    }

    static bool IsRunning()
    {
        RDF_RadarEccmAutoTest inst = GetInstance();
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
        RDF_RadarEccmAutoTest inst = GetInstance();
        if (!inst)
            return;
        inst.OnTick();
    }

    protected void StartInternal()
    {
        if (m_Running)
        {
            Print("[RDF Radar EccmTest] already running.");
            return;
        }
        if (!RDF_RadarAutoTestGate.TryAcquire("Eccm"))
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RDF Radar EccmTest] no world.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Eccm");
            return;
        }

        m_Subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!m_Subject)
        {
            Print("[RDF Radar EccmTest] no local subject.", LogLevel.WARNING);
            RDF_RadarAutoTestGate.Release("Eccm");
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
            Print("[RDF Radar EccmTest] failed to choose radar origin.", LogLevel.ERROR);
            RDF_RadarAutoTestGate.Release("Eccm");
            return;
        }

        m_StartWallS = System.GetTickCount() * 0.001;

        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplyTestRadarConfig();
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();

        m_ScanCount = 0;
        m_SlbScans = 0;
        m_PrfScans = 0;
        RDF_RadarSensor sensor = GetSensor();
        if (sensor)
            m_LastScanSerial = sensor.GetScanSerial();
        else
            m_LastScanSerial = -1;
        m_LastProgressWallS = m_StartWallS;
        m_Running = true;

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
        }

        Print("[RDF Radar EccmTest] started sidelobe noise jam + deception; ECCM decision ON.");
    }

    protected void StopInternal(bool restore)
    {
        m_Running = false;
        RDF_RadarAutoTestGate.Release("Eccm");

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
        return true;
    }

    protected void ApplyTestRadarConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(64);
        cfg.m_Range = 2000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;

        // ECCM decision layer: opt-in.
        cfg.m_EnableEccmDecision = true;
        cfg.m_EccmJnOnDb = 6.0;
        cfg.m_EccmJnHysteresisDb = 2.0;
        cfg.m_EccmSidelobeCouplingOn = 0.3;

        // Sidelobe noise jammer: placed perpendicular to boresight so BEAM-mode
        // coupling lands in a sidelobe -> decision should enable SLB.
        vector mat[4];
        m_Subject.GetWorldTransform(mat);
        vector fwd = mat[2];
        float flatLen = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        vector flatFwd;
        if (flatLen < 0.001)
            flatFwd = Vector(0.0, 0.0, 1.0);
        else
            flatFwd = Vector(fwd[0] / flatLen, 0.0, fwd[2] / flatLen);
        vector right = Vector(flatFwd[2], 0.0, -flatFwd[0]);
        vector jammerPos = Vector(
            m_RadarOrigin[0] + right[0] * JAMMER_SIDELOBE_RANGE_M,
            m_RadarOrigin[1],
            m_RadarOrigin[2] + right[2] * JAMMER_SIDELOBE_RANGE_M);

        RDF_RadarNoiseJammerEffect jammer = new RDF_RadarNoiseJammerEffect();
        jammer.m_Position = jammerPos;
        jammer.m_ErpW = 10000.0;
        jammer.m_BandwidthHz = 5000000.0;
        jammer.ConfigurePhysicsBeam(-25.0);
        cfg.m_EwStack.Clear();
        cfg.m_EwStack.Add(jammer);

        // Deception jammer: one walking false plot -> decision should enable PRF agility.
        RDF_RadarDeceptionJammerEffect deceive = new RDF_RadarDeceptionJammerEffect();
        deceive.AddFalsePlot(1600.0, -18.0, 0.0000000000012, 25.0, 0.0);
        cfg.m_EwStack.Add(deceive);

        cfg.Validate();
        ApplySensorConfig(cfg);
    }

    protected void OnTick()
    {
        if (!m_Running)
            return;

        float nowS = System.GetTickCount() * 0.001;
        AccumulateLatestScan(nowS);

        if (nowS - m_StartWallS < DURATION_S)
            return;

        FinalizeAndReport();
        StopInternal(true);
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
                Print("[RDF Radar EccmTest] waiting for scan; ensure Play mode is running.");
            }
            return;
        }
        m_LastScanSerial = serial;
        m_LastProgressWallS = nowS;
        m_ScanCount = m_ScanCount + 1;

        string status = sensor.GetEccmStatusShort();
        if (status.Contains("slb"))
            m_SlbScans = m_SlbScans + 1;
        if (status.Contains("prf"))
            m_PrfScans = m_PrfScans + 1;
    }

    protected void FinalizeAndReport()
    {
        bool passScans = m_ScanCount > 6;
        bool passSlb = m_SlbScans > 0;
        bool passPrf = m_PrfScans > 0;
        bool allPass = passScans && passSlb && passPrf;
        s_LastPass = allPass;

        array<string> lines = new array<string>();
        lines.Insert("RDF Radar ECCM Test (sidelobe noise + deception -> SLB + PRF)");
        lines.Insert("result " + BoolLabel(allPass));
        lines.Insert("");
        lines.Insert("checks:");
        lines.Insert("  scans_present " + BoolLabel(passScans));
        lines.Insert("  slb_triggered " + BoolLabel(passSlb));
        lines.Insert("  prf_triggered " + BoolLabel(passPrf));
        lines.Insert("");
        lines.Insert("metrics:");
        lines.Insert("  scan_count " + m_ScanCount.ToString());
        lines.Insert("  slb_scans " + m_SlbScans.ToString());
        lines.Insert("  prf_scans " + m_PrfScans.ToString());
        lines.Insert("  radar_origin " + m_RadarOrigin.ToString());

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_eccm_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);

        Print("[RDF Radar EccmTest] " + BoolLabel(allPass) + "  report=" + reportPath);
        Print(string.Format(
            "[RDF Radar EccmTest] scans=%1 slb=%2 prf=%3",
            m_ScanCount.ToString(),
            m_SlbScans.ToString(),
            m_PrfScans.ToString()));
    }

    protected string BoolLabel(bool value)
    {
        return RDF_RadarTestUtil.BoolLabel(value);
    }
}
