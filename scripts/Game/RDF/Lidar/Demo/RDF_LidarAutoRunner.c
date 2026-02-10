// Optional auto-run manager for LiDAR scans (demo).
class RDF_LidarAutoRunner
{
    protected static ref RDF_LidarAutoRunner s_Instance;
    protected static bool s_AutoEnabled = false;
    // Minimum tick interval for the global call queue (seconds). Reduces per-frame overhead.
    protected static float s_MinTickInterval = 0.2;

    protected ref RDF_LidarScanner m_Scanner;
    protected ref RDF_LidarVisualizer m_Visualizer;
    protected ref RDF_LidarDemoConfig m_DemoConfig;
    protected ref RDF_LidarScanCompleteHandler m_ScanCompleteHandler;
    protected ref RDF_LidarDemoStatsHandler m_DemoStatsHandler;
    protected float m_LastScanTime = -1.0;
    protected bool m_Running = false;

    void RDF_LidarAutoRunner()
    {
        m_Scanner = new RDF_LidarScanner();
        m_Visualizer = new RDF_LidarVisualizer();
        // recurring tick; scanning is gated by m_Running. Use a non-zero min tick interval to avoid per-frame overhead.
        GetGame().GetCallqueue().CallLater(StaticTick, s_MinTickInterval, true);
    }

    // Configure minimum tick interval at runtime (seconds).
    static void SetMinTickInterval(float interval)
    {
        s_MinTickInterval = Math.Max(0.01, interval);
    }

    static float GetMinTickInterval()
    {
        return s_MinTickInterval;
    }

    static void StaticTick()
    {
        GetInstance().RDF_LidarTick();
    }

    static RDF_LidarAutoRunner GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_LidarAutoRunner();
        return s_Instance;
    }

    static void StartAutoRun()
    {
        GetInstance().m_Running = true;
    }

    static void StopAutoRun()
    {
        GetInstance().m_Running = false;
    }

    // Single code switch to enable/disable the demo auto-run.
    static void SetDemoEnabled(bool enabled)
    {
        s_AutoEnabled = enabled;
        RDF_LidarAutoRunner inst = GetInstance();
        if (enabled)
        {
            // Apply configured demo options if present
            if (inst && inst.m_DemoConfig)
                inst.ApplyDemoConfig();

            StartAutoRun();
        }
        else
        {
            StopAutoRun();
        }
    }

    static bool IsDemoEnabled()
    {
        return s_AutoEnabled;
    }

    // Set a custom sampling strategy to be used by the demo auto-runner.
    static void SetDemoSampleStrategy(RDF_LidarSampleStrategy strategy)
    {
        if (!strategy) return;
        RDF_LidarAutoRunner inst = GetInstance();
        if (inst && inst.m_Scanner)
            inst.m_Scanner.SetSampleStrategy(strategy);
    }

    // Set the demo scanner ray count safely (clamped)
    static void SetDemoRayCount(int rays)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner) return;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s) return;
        s.m_RayCount = Math.Clamp(rays, 1, 4096);
    }

    // Set the demo visual color strategy (applies to visualizer if present)
    static void SetDemoColorStrategy(RDF_LidarColorStrategy strategy)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        inst.m_Visualizer.SetColorStrategy(strategy);
    }

    // Draw scan origin and X/Y/Z axes in the demo (uses RDF_LidarVisualSettings.m_DrawOriginAxis).
    static void SetDemoDrawOriginAxis(bool draw)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_DrawOriginAxis = draw;
    }

    // When true, installs built-in handler that prints hit count and closest distance after each scan (uses RDF_LidarSampleUtils).
    static void SetDemoVerbose(bool verbose)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst) return;
        if (verbose)
        {
            if (!inst.m_DemoStatsHandler)
                inst.m_DemoStatsHandler = new RDF_LidarDemoStatsHandler();
            inst.m_ScanCompleteHandler = inst.m_DemoStatsHandler;
        }
        else
        {
            if (inst.m_ScanCompleteHandler == inst.m_DemoStatsHandler)
                inst.m_ScanCompleteHandler = null;
        }
    }

    // Set the demo scanner update interval safely (seconds)
    static void SetDemoUpdateInterval(float interval)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner) return;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s) return;
        s.m_UpdateInterval = Math.Max(0.01, interval);
    }

    static void SetDemoConfig(RDF_LidarDemoConfig cfg)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst) return;
        inst.m_DemoConfig = cfg;
    }

    static RDF_LidarDemoConfig GetDemoConfig()
    {
        return GetInstance().m_DemoConfig;
    }

    // Apply current demo config to the running demo (called on SetDemoEnabled(true)).
    void ApplyDemoConfig()
    {
        if (!m_DemoConfig) return;
        m_DemoConfig.ApplyTo(this);
    }

    // Single API entry: apply config and turn demo on. Use RDF_LidarDemoConfig.Create*() for presets.
    static void StartWithConfig(RDF_LidarDemoConfig cfg)
    {
        if (!cfg) return;
        SetDemoConfig(cfg);
        SetDemoEnabled(true);
    }

    static bool IsRunning()
    {
        return GetInstance().m_Running;
    }

    // Set handler called after each scan (e.g. threat detection, export). Pass null to clear.
    static void SetScanCompleteHandler(RDF_LidarScanCompleteHandler handler)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (inst)
            inst.m_ScanCompleteHandler = handler;
    }

    static RDF_LidarScanCompleteHandler GetScanCompleteHandler()
    {
        return GetInstance().m_ScanCompleteHandler;
    }

    void RDF_LidarTick()
    {
        if (!m_Running)
            return;

        if (!m_Scanner || !m_Visualizer)
            return;

        RDF_LidarSettings settings = m_Scanner.GetSettings();
        if (!settings || !settings.m_Enabled)
            return;

        World world = GetGame().GetWorld();
        if (!world)
            return;

        float now = world.GetWorldTime();
        if (m_LastScanTime >= 0.0 && (now - m_LastScanTime) < settings.m_UpdateInterval)
            return;
        m_LastScanTime = now;

        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        m_Visualizer.Render(subject, m_Scanner);

        if (m_ScanCompleteHandler)
        {
            ref array<ref RDF_LidarSample> samples = m_Visualizer.GetLastSamples();
            if (samples)
                m_ScanCompleteHandler.OnScanComplete(samples);
        }
    }
}
