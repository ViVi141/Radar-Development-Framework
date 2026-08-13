// Optional auto-run manager for radar scans (demo). Owns an RDF_RadarSensor
// facade; HUD / debug shapes stay here as presentation.
class RDF_RadarAutoRunner
{
    protected static ref RDF_RadarAutoRunner s_Instance;
    protected static bool s_AutoEnabled = true;
    protected static bool s_HudEnabled = true;
    protected static float s_MinTickInterval = 0.2;
    protected static RDF_RadarNetworkAPI s_NetworkAPI;
    // When true, always run the local sensor scanner (ignore network API).
    protected static bool s_ForceLocalScan;
    // Wall time of the last RadarTick (scan path + HUD/presentation when active).
    protected static float s_LastTickDurationMs;

    protected ref RDF_RadarSensor m_Sensor;
    protected ref RDF_RadarVisualizer m_Visualizer;
    protected ref RDF_RadarVisualSettings m_VisualSettings;
    protected bool m_Running = false;
    // Cached when ResolveLocalSubject succeeds; reused while GM free-cam makes
    // ControlledEntity null so scans do not stall mid-AutoTest.
    protected IEntity m_LastSubject;
    // When set, RadarTick scans from this entity instead of the local player.
    protected static IEntity s_ScanSubjectOverride;

    void RDF_RadarAutoRunner()
    {
        m_Sensor = new RDF_RadarSensor();
        m_Sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
        m_VisualSettings = new RDF_RadarVisualSettings();
        m_VisualSettings.ApplyShowcaseDefaults();
        m_Visualizer = new RDF_RadarVisualizer(m_VisualSettings);
        GetGame().GetCallqueue().CallLater(StaticTick, 200, true);
    }

    static void SetMinTickInterval(float interval)
    {
        s_MinTickInterval = Math.Max(0.01, interval);
    }

    static float GetMinTickInterval()
    {
        return s_MinTickInterval;
    }

    static void SetNetworkAPI(RDF_RadarNetworkAPI networkAPI)
    {
        if (!networkAPI)
        {
            s_NetworkAPI = null;
            return;
        }
        if (networkAPI.IsNetworkAvailable())
            s_NetworkAPI = networkAPI;
        else
            s_NetworkAPI = null;
    }

    static void SetForceLocalScan(bool forceLocal)
    {
        s_ForceLocalScan = forceLocal;
        if (forceLocal)
            s_NetworkAPI = null;
        RDF_RadarAutoRunner inst = GetInstance();
        if (inst && inst.m_Sensor)
            inst.m_Sensor.SetForceLocalScan(forceLocal);
    }

    static bool IsForceLocalScan()
    {
        return s_ForceLocalScan;
    }

    // Force scans to originate from a spawned platform (e.g. blue Mi-8 in HeliDuel).
    // Pass null to restore local-player subject resolution.
    static void SetScanSubjectOverride(IEntity subject)
    {
        s_ScanSubjectOverride = subject;
    }

    static IEntity GetScanSubjectOverride()
    {
        return s_ScanSubjectOverride;
    }

    static bool IsNetworkAPIValid()
    {
        if (!s_NetworkAPI)
            return false;
        if (!s_NetworkAPI.IsNetworkAvailable())
        {
            s_NetworkAPI = null;
            return false;
        }
        return true;
    }

    static void StaticTick()
    {
        GetInstance().RadarTick();
    }

    static RDF_RadarAutoRunner GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarAutoRunner();
        return s_Instance;
    }

    static RDF_RadarSensor GetSensor()
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (!inst.m_Sensor)
            inst.m_Sensor = new RDF_RadarSensor();
        return inst.m_Sensor;
    }

    static void StartAutoRun()
    {
        GetInstance().m_Running = true;
    }

    static void StopAutoRun()
    {
        RDF_RadarAutoRunner inst = GetInstance();
        inst.m_Running = false;
        if (inst.m_Visualizer)
            inst.m_Visualizer.Reset();
        RDF_RadarHUD.Hide();
    }

    static void SetDemoEnabled(bool enabled)
    {
        s_AutoEnabled = enabled;
        RDF_RadarAutoRunner inst = GetInstance();
        if (enabled)
            StartAutoRun();
        else
            StopAutoRun();
    }

    static bool IsDemoEnabled()
    {
        return s_AutoEnabled;
    }

    static bool IsRunning()
    {
        return GetInstance().m_Running;
    }

    static float GetLastTickDurationMs()
    {
        return s_LastTickDurationMs;
    }

    static void SetHudEnabled(bool enabled)
    {
        s_HudEnabled = enabled;
        if (enabled)
            RDF_RadarHUD.Show();
        else
            RDF_RadarHUD.Hide();
    }

    static bool IsHudEnabled()
    {
        return s_HudEnabled;
    }

    static void StartWithConfig(RDF_RadarSettings config)
    {
        RDF_RadarSensor sensor = GetSensor();
        if (config)
            sensor.Configure(config);
        else
            sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
        sensor.SetForceLocalScan(s_ForceLocalScan);
        GetInstance().m_Running = true;
        s_AutoEnabled = true;
    }

    static void SetDemoConfig(RDF_RadarSettings config)
    {
        if (!config)
            return;
        RDF_RadarSensor sensor = GetSensor();
        sensor.Configure(config);
        sensor.SetForceLocalScan(s_ForceLocalScan);
    }

    static void SetMode(ERDF_RadarSensorMode mode)
    {
        GetSensor().SetMode(mode);
    }

    static RDF_RadarSettings GetDemoConfig()
    {
        return GetSensor().GetSettings();
    }

    static array<ref RDF_RadarTarget> GetLastTargets()
    {
        return GetSensor().GetPlots();
    }

    static RDF_RadarProjectileTracker GetTracker()
    {
        return GetSensor().GetTracker();
    }

    static int GetLastScanSerial()
    {
        return GetSensor().GetScanSerial();
    }

    void RadarTick()
    {
        if (!m_Running)
            return;
        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();

        int wall0 = System.GetTickCount();

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            FinishTickTiming(wall0);
            return;
        }

        IEntity subject = ResolveTickSubject();
        if (!subject)
        {
            FinishTickTiming(wall0);
            return;
        }

        RDF_RadarNetworkAPI net = RDF_RadarNetworkAPI.Cast(
            subject.FindComponent(RDF_RadarNetworkAPI));
        if (!s_ForceLocalScan)
        {
            if (net != s_NetworkAPI)
                SetNetworkAPI(net);
        }
        else
        {
            s_NetworkAPI = null;
        }

        m_Sensor.SetForceLocalScan(s_ForceLocalScan);
        bool scanned = m_Sensor.Tick(subject, s_NetworkAPI);
        if (!scanned)
        {
            // No new scan: keep plot/lock shapes; only fade afterglow + WLR.
            RenderPresentation(subject, false);
            FinishTickTiming(wall0);
            return;
        }

        if (s_HudEnabled)
        {
            if (!RDF_RadarHUD.IsVisible())
                RDF_RadarHUD.Show();
            RDF_RadarScanContext ctx = m_Sensor.GetScanContext();
            string modeText = "PPI";
            if (ctx && ctx.m_UsedNetwork)
                modeText = "PPI | NET";
            else
            {
                modeText = "PPI | " + m_Sensor.GetDemStatusShort();
                RDF_RadarScanner scanner = m_Sensor.GetScanner();
                if (scanner)
                    modeText = modeText + " | " + scanner.GetScanReuseStatsShort();
            }
            RDF_RadarHUD.SetMode(modeText);
            float hudRange = m_Sensor.GetSettings().m_Range;
            vector hudOrigin = "0 0 0";
            vector hudForward = "1 0 0";
            if (ctx)
            {
                hudOrigin = ctx.m_Origin;
                hudForward = ctx.m_Forward;
                if (ctx.m_RangeM > 0.0)
                    hudRange = ctx.m_RangeM;
            }
            RDF_RadarSettings radarCfg = m_Sensor.GetSettings();
            if (radarCfg)
            {
                RDF_RadarHUD.SetWlrAlertsEnabled(radarCfg.m_EnableWlrHudAlerts);
                if (radarCfg.m_EnableWlrHudAlerts)
                    RDF_RadarHUD.SetWlrAlertRadiusM(radarCfg.m_WlrHudAlertRadiusM);
                if (m_VisualSettings)
                    m_VisualSettings.m_WeaponLocateAlertRadiusM = radarCfg.m_WlrHudAlertRadiusM;
            }
            RDF_RadarHUD.FeedScan(
                m_Sensor.GetPlots(),
                hudOrigin,
                hudForward,
                hudRange,
                m_Sensor.GetTracker());
        }

        RenderPresentation(subject, true);
        FinishTickTiming(wall0);
    }

    protected void FinishTickTiming(int wall0)
    {
        float ms = System.GetTickCount() - wall0;
        if (ms < 0.0)
            ms = 0.0;
        s_LastTickDurationMs = ms;
    }

    protected IEntity ResolveTickSubject()
    {
        IEntity subject = s_ScanSubjectOverride;
        if (!subject)
            subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (subject)
            m_LastSubject = subject;
        else
            subject = m_LastSubject;
        return subject;
    }

    static void RefreshPresentation(bool fullDynamic)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (!inst.m_Running)
            return;
        IEntity subject = inst.ResolveTickSubject();
        if (!subject)
            return;
        inst.RenderPresentation(subject, fullDynamic);
    }

    protected void RenderPresentation(IEntity subject, bool fullDynamic)
    {
        if (!m_Visualizer || !m_VisualSettings || !m_Sensor)
            return;

        bool drawWorld = m_VisualSettings.m_DrawRays
            || m_VisualSettings.m_DrawPoints
            || m_VisualSettings.m_DrawOriginAxis
            || m_VisualSettings.m_DrawSectorSweep
            || m_VisualSettings.m_DrawLockBeam
            || m_VisualSettings.m_DrawAfterglow
            || m_VisualSettings.m_DrawRangeRings
            || m_VisualSettings.m_DrawTrackRibbon
            || m_VisualSettings.m_AnimateSweepNeedle;
        bool drawWlr = m_VisualSettings.m_DrawWeaponLocate;

        if (drawWorld)
        {
            RDF_RadarScanContext ctx = m_Sensor.GetScanContext();
            vector origin = "0 0 0";
            vector forward = "0 0 1";
            float rangeM = 2000.0;
            float halfAngle = 45.0;
            if (ctx)
            {
                origin = ctx.m_Origin;
                forward = ctx.m_Forward;
                if (ctx.m_RangeM > 0.0)
                    rangeM = ctx.m_RangeM;
            }
            RDF_RadarSettings cfg = m_Sensor.GetSettings();
            if (cfg)
            {
                if (cfg.m_Range > 0.0)
                    rangeM = cfg.m_Range;
                halfAngle = cfg.m_SectorHalfAngleDeg;
            }
            m_Visualizer.Render(
                subject,
                m_Sensor.GetPlots(),
                origin,
                forward,
                rangeM,
                halfAngle,
                m_Sensor.GetLockManager(),
                m_Sensor.GetTracker(),
                m_Sensor.GetScanSerial(),
                fullDynamic);
        }
        else
        {
            if (drawWlr)
                m_Visualizer.Reset();
        }

        if (drawWlr && m_Sensor.GetTracker())
            m_Visualizer.RenderWeaponLocates(m_Sensor.GetTracker());
    }

    //------------------------------------------------------------------------------------------------
    static void SetShowcaseVisuals(bool enabled)
    {
        RDF_RadarVisualSettings vs = GetVisualSettings();
        if (!vs)
            return;
        if (enabled)
            vs.ApplyShowcaseDefaults();
        else
            vs.ApplyMinimalDefaults();
    }

    static RDF_RadarVisualizer GetVisualizer()
    {
        return GetInstance().m_Visualizer;
    }

    static RDF_RadarVisualSettings GetVisualSettings()
    {
        return GetInstance().m_VisualSettings;
    }
}
