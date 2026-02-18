// Auto-run manager for radar demo scans.
// Mirrors RDF_LidarAutoRunner but drives RDF_RadarScanner + PPI / A-Scope displays.
//
// Typical usage:
//   RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateXBandSearch());
//
// To stop:
//   RDF_RadarAutoRunner.SetDemoEnabled(false);
class RDF_RadarAutoRunner
{
    // ---- singleton ----
    protected static ref RDF_RadarAutoRunner s_Instance;
    protected static bool s_AutoEnabled = false;
    // Minimum tick interval in seconds; keeps the callqueue lean.
    protected static float s_MinTickInterval = 0.5;

    // ---- per-instance state ----
    protected ref RDF_RadarScanner              m_Scanner;
    // Primary display: tall poles at each detection, visible in 3-D world.
    protected ref RDF_RadarWorldMarkerDisplay   m_WorldDisplay;
    // Legacy PPI (kept for backward-compat; disabled by default).
    protected ref RDF_PPIDisplay                m_PPIDisplay;
    protected ref RDF_AScopeDisplay             m_AScopeDisplay;
    protected ref RDF_RadarDemoConfig           m_DemoConfig;
    protected ref RDF_LidarScanCompleteHandler  m_ScanCompleteHandler;
    protected ref RDF_RadarDemoStatsHandler     m_DemoStatsHandler;
    protected ref array<ref RDF_LidarSample>    m_LastSamples;
    protected float                             m_LastScanTime = -1.0;
    protected bool                              m_Running = false;
    // When true the legacy PPI is shown alongside world markers.
    protected bool                              m_ShowPPI    = false;
    // When true the A-Scope is rendered alongside world markers.
    protected bool                              m_ShowAScope = false;

    void RDF_RadarAutoRunner()
    {
        m_Scanner      = new RDF_RadarScanner();
        m_WorldDisplay = new RDF_RadarWorldMarkerDisplay();
        m_PPIDisplay   = new RDF_PPIDisplay();
        m_AScopeDisplay = new RDF_AScopeDisplay();
        m_LastSamples  = new array<ref RDF_LidarSample>();

        GetGame().GetCallqueue().CallLater(StaticTick, s_MinTickInterval, true);
    }

    // ---- singleton accessors ----
    static RDF_RadarAutoRunner GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarAutoRunner();
        return s_Instance;
    }

    // ---- tick interval ----
    static void SetMinTickInterval(float interval)
    {
        s_MinTickInterval = Math.Max(0.05, interval);
    }

    static float GetMinTickInterval()
    {
        return s_MinTickInterval;
    }

    // ---- demo on/off ----
    static void SetDemoEnabled(bool enabled)
    {
        s_AutoEnabled = enabled;
        RDF_RadarAutoRunner inst = GetInstance();
        if (enabled)
        {
            if (inst && inst.m_DemoConfig)
                inst.ApplyDemoConfig();
            inst.m_Running = true;
        }
        else
        {
            inst.m_Running = false;
        }
    }

    static bool IsDemoEnabled()
    {
        return s_AutoEnabled;
    }

    static bool IsRunning()
    {
        return GetInstance().m_Running;
    }

    // ---- config ----
    static void SetDemoConfig(RDF_RadarDemoConfig cfg)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (!inst) return;
        inst.m_DemoConfig = cfg;
        if (s_AutoEnabled && cfg)
            inst.ApplyDemoConfig();
    }

    static RDF_RadarDemoConfig GetDemoConfig()
    {
        return GetInstance().m_DemoConfig;
    }

    // Build a new scanner from the current demo config and apply display settings.
    void ApplyDemoConfig()
    {
        if (!m_DemoConfig) return;

        // Rebuild scanner with preset EM params, mode, strategy.
        m_Scanner = m_DemoConfig.BuildRadarScanner();

        // Apply tick interval from config.
        if (m_DemoConfig.m_MinTickInterval > 0.0)
            s_MinTickInterval = m_DemoConfig.m_MinTickInterval;

        // Apply color strategy to PPI display.
        RDF_RadarColorStrategy cs = RDF_RadarColorStrategy.Cast(m_DemoConfig.m_ColorStrategy);
        if (cs)
            m_PPIDisplay.SetColorStrategy(cs);

        // Verbose stats handler.
        if (m_DemoConfig.m_Verbose)
        {
            if (!m_DemoStatsHandler)
                m_DemoStatsHandler = new RDF_RadarDemoStatsHandler();
            m_ScanCompleteHandler = m_DemoStatsHandler;
        }
        else
        {
            if (m_ScanCompleteHandler == m_DemoStatsHandler)
                m_ScanCompleteHandler = null;
        }
    }

    // Single-call API: apply config and start.
    static void StartWithConfig(RDF_RadarDemoConfig cfg)
    {
        if (!cfg) return;
        SetDemoConfig(cfg);
        SetDemoEnabled(true);
    }

    // ---- display helpers ----
    static void SetShowAScope(bool show)
    {
        GetInstance().m_ShowAScope = show;
    }

    static bool GetShowAScope()
    {
        return GetInstance().m_ShowAScope;
    }

    // Toggle the legacy PPI display (off by default; WorldMarker is the primary display).
    static void SetShowPPI(bool show)
    {
        GetInstance().m_ShowPPI = show;
    }

    static bool GetShowPPI()
    {
        return GetInstance().m_ShowPPI;
    }

    // Reconfigure the world-marker pole height (metres).
    static void SetPoleHeight(float heightM)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (inst && inst.m_WorldDisplay)
            inst.m_WorldDisplay.m_PoleHeight = Math.Max(5.0, heightM);
    }

    // Reconfigure PPI display radius (metres).
    static void SetPPIRadius(float radiusM)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (inst && inst.m_PPIDisplay)
            inst.m_PPIDisplay.m_DisplayRadius = Math.Max(10.0, radiusM);
    }

    // Inject a color strategy for PPI blip coloring.
    static void SetDemoColorStrategy(RDF_RadarColorStrategy strategy)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (inst && inst.m_PPIDisplay)
            inst.m_PPIDisplay.SetColorStrategy(strategy);
    }

    // ---- verbose stats ----
    static void SetDemoVerbose(bool verbose)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (!inst) return;
        if (verbose)
        {
            if (!inst.m_DemoStatsHandler)
                inst.m_DemoStatsHandler = new RDF_RadarDemoStatsHandler();
            inst.m_ScanCompleteHandler = inst.m_DemoStatsHandler;
        }
        else
        {
            if (inst.m_ScanCompleteHandler == inst.m_DemoStatsHandler)
                inst.m_ScanCompleteHandler = null;
        }
    }

    // ---- custom scan-complete handler ----
    static void SetScanCompleteHandler(RDF_LidarScanCompleteHandler handler)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (inst)
            inst.m_ScanCompleteHandler = handler;
    }

    static RDF_LidarScanCompleteHandler GetScanCompleteHandler()
    {
        return GetInstance().m_ScanCompleteHandler;
    }

    // ---- last samples (read-only copy) ----
    static array<ref RDF_LidarSample> GetLastSamples()
    {
        RDF_RadarAutoRunner inst = GetInstance();
        array<ref RDF_LidarSample> outSamples = new array<ref RDF_LidarSample>();
        if (!inst || !inst.m_LastSamples)
            return outSamples;
        foreach (RDF_LidarSample s : inst.m_LastSamples)
            outSamples.Insert(s);
        return outSamples;
    }

    // ---- internal tick ----
    static void StaticTick()
    {
        GetInstance().RDF_RadarTick();
    }

    void RDF_RadarTick()
    {
        if (!m_Running)
            return;

        if (!m_Scanner)
            return;

        RDF_RadarSettings settings = m_Scanner.GetRadarSettings();
        if (!settings || !settings.m_Enabled)
            return;

        World world = GetGame().GetWorld();
        if (!world)
            return;

        float now = world.GetWorldTime();
        if (m_LastScanTime >= 0.0 && (now - m_LastScanTime) < s_MinTickInterval)
            return;
        m_LastScanTime = now;

        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!subject)
            return;

        // Run the radar scan.
        m_LastSamples.Clear();
        m_Scanner.Scan(subject, m_LastSamples);

        // Primary display: world-space poles at detected targets + compass rose.
        m_WorldDisplay.DrawWorldMarkers(m_LastSamples, subject);

        // Optional legacy PPI (disabled by default - use SetShowPPI(true) to enable).
        if (m_ShowPPI)
            m_PPIDisplay.DrawPPI(m_LastSamples, subject);

        // Optional A-Scope.
        if (m_ShowAScope)
        {
            // Position the A-scope 3 m above and 5 m to the right of the subject.
            vector origin = subject.GetOrigin();
            origin[0] = origin[0] + 5.0;
            origin[1] = origin[1] + 3.0;
            m_AScopeDisplay.m_Origin = origin;
            m_AScopeDisplay.DrawAScope(m_LastSamples, settings.m_Range, settings.m_DetectionThreshold);
        }

        // Notify scan-complete handler.
        if (m_ScanCompleteHandler)
            m_ScanCompleteHandler.OnScanComplete(m_LastSamples);
    }
}
