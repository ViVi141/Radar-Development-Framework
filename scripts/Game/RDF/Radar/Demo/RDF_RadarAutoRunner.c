// Optional auto-run manager for radar scans (demo). Same pattern as LiDAR:
// singleton instance + CallLater recurring tick; no Bootstrap component required.
class RDF_RadarAutoRunner
{
    protected static ref RDF_RadarAutoRunner s_Instance;
    protected static bool s_AutoEnabled = true;
    protected static float s_MinTickInterval = 0.2;

    protected ref RDF_RadarSettings m_Config;
    protected ref RDF_RadarScanner m_Scanner;
    protected ref RDF_RadarProjectileTracker m_Tracker;
    protected ref RDF_RadarVisualizer m_Visualizer;
    protected ref RDF_RadarVisualSettings m_VisualSettings;
    protected ref array<ref RDF_RadarTarget> m_LastTargets;
    protected float m_LastScanTime = -1000.0;
    protected bool m_Running = false;

    void RDF_RadarAutoRunner()
    {
        m_Config = new RDF_RadarSettings();
        m_Scanner = new RDF_RadarScanner(m_Config);
        m_Tracker = new RDF_RadarProjectileTracker();
        m_LastTargets = new array<ref RDF_RadarTarget>();
        m_VisualSettings = new RDF_RadarVisualSettings();
        m_VisualSettings.m_DrawRays = true;
        m_VisualSettings.m_DrawPoints = true;
        m_VisualSettings.m_DrawOriginAxis = true;
        m_VisualSettings.m_OriginAxisLength = 2.0;
        m_Visualizer = new RDF_RadarVisualizer(m_VisualSettings);
        GetGame().GetCallqueue().CallLater(StaticTick, s_MinTickInterval, true);
    }

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
        GetInstance().RadarTick();
    }

    static RDF_RadarAutoRunner GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarAutoRunner();
        return s_Instance;
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

    static void StartWithConfig(RDF_RadarSettings config)
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (config)
            inst.m_Config = config;
        else
            inst.m_Config = new RDF_RadarSettings();
        if (!inst.m_Scanner)
            inst.m_Scanner = new RDF_RadarScanner(inst.m_Config);
        else
            inst.m_Scanner.GetSettings().m_Enabled = inst.m_Config.m_Enabled;
        if (!inst.m_Tracker)
            inst.m_Tracker = new RDF_RadarProjectileTracker();
        if (!inst.m_LastTargets)
            inst.m_LastTargets = new array<ref RDF_RadarTarget>();
        inst.m_Running = true;
        s_AutoEnabled = true;
    }

    static void SetDemoConfig(RDF_RadarSettings config)
    {
        if (!config)
            return;
        RDF_RadarAutoRunner inst = GetInstance();
        inst.m_Config = config;
        if (inst.m_Scanner)
            inst.m_Scanner.GetSettings().m_Enabled = config.m_Enabled;
    }

    static RDF_RadarSettings GetDemoConfig()
    {
        return GetInstance().m_Config;
    }

    static array<ref RDF_RadarTarget> GetLastTargets()
    {
        RDF_RadarAutoRunner inst = GetInstance();
        if (!inst.m_LastTargets)
            inst.m_LastTargets = new array<ref RDF_RadarTarget>();
        return inst.m_LastTargets;
    }

    static RDF_RadarProjectileTracker GetTracker()
    {
        return GetInstance().m_Tracker;
    }

    void RadarTick()
    {
        if (!m_Running || !m_Scanner || !m_Config)
            return;

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!subject)
            return;

        float now = world.GetWorldTime() * 0.001;
        if (now - m_LastScanTime >= m_Config.m_UpdateInterval)
        {
            m_LastScanTime = now;
            m_LastTargets.Clear();
            m_Scanner.Scan(subject, m_LastTargets);
            m_Tracker.Update(m_LastTargets, now);
        }

        if (m_Visualizer && m_VisualSettings && (m_VisualSettings.m_DrawRays || m_VisualSettings.m_DrawPoints || m_VisualSettings.m_DrawOriginAxis))
            m_Visualizer.Render(subject, m_LastTargets);
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
