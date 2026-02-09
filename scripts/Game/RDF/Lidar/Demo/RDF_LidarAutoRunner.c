// Optional auto-run manager for LiDAR scans (demo).
class RDF_LidarAutoRunner
{
    protected static ref RDF_LidarAutoRunner s_Instance;
    protected static bool s_AutoEnabled = false;
    // Minimum tick interval for the global call queue (seconds). Reduces per-frame overhead.
    protected static float s_MinTickInterval = 0.2;

    protected ref RDF_LidarScanner m_Scanner;
    protected ref RDF_LidarVisualizer m_Visualizer;
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
        if (enabled)
            StartAutoRun();
        else
            StopAutoRun();
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

    // Convenience helper: start the demo using hemisphere-only sampling.
    static void StartHemisphereDemo()
    {
        SetDemoSampleStrategy(new RDF_HemisphereSampleStrategy());
        SetDemoEnabled(true);
    }

    static bool IsRunning()
    {
        return GetInstance().m_Running;
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
    }
}
