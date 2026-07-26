// Optional auto-run manager for radar scans (demo). Same pattern as LiDAR:
// singleton instance + CallLater recurring tick; no Bootstrap component required.
class RDF_RadarAutoRunner
{
    protected static ref RDF_RadarAutoRunner s_Instance;
    protected static bool s_AutoEnabled = true;
    protected static bool s_HudEnabled = true;
    protected static float s_MinTickInterval = 0.2;
    protected static RDF_RadarNetworkAPI s_NetworkAPI;

    protected ref RDF_RadarSettings m_Config;
    protected ref RDF_RadarScanner m_Scanner;
    protected ref RDF_RadarProjectileTracker m_Tracker;
    protected ref RDF_RadarVisualizer m_Visualizer;
    protected ref RDF_RadarVisualSettings m_VisualSettings;
    protected ref array<ref RDF_RadarTarget> m_LastTargets;
    protected float m_LastScanTime = -1000.0;
    protected int m_ScanSerial = 0;
    protected bool m_Running = false;

    void RDF_RadarAutoRunner()
    {
        m_Config = new RDF_RadarSettings();
        m_Scanner = new RDF_RadarScanner(m_Config);
        m_Tracker = new RDF_RadarProjectileTracker();
        m_Tracker.ConfigureFromSettings(m_Config);
        m_LastTargets = new array<ref RDF_RadarTarget>();
        m_VisualSettings = new RDF_RadarVisualSettings();
        // Debug shapes are expensive; rays off by default. Enable via visual settings.
        m_VisualSettings.m_DrawRays = false;
        m_VisualSettings.m_DrawPoints = false;
        m_VisualSettings.m_DrawOriginAxis = false;
        m_VisualSettings.m_OriginAxisLength = 2.0;
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

    // Toggle the on-screen PPI HUD. When enabled the runner feeds every scan to it.
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
        RDF_RadarAutoRunner inst = GetInstance();
        if (config)
            inst.m_Config = config;
        else
            inst.m_Config = new RDF_RadarSettings();
        inst.m_Scanner = new RDF_RadarScanner(inst.m_Config);
        if (!inst.m_Tracker)
            inst.m_Tracker = new RDF_RadarProjectileTracker();
        inst.m_Tracker.ConfigureFromSettings(inst.m_Config);
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
        inst.m_Scanner = new RDF_RadarScanner(config);
        if (!inst.m_Tracker)
            inst.m_Tracker = new RDF_RadarProjectileTracker();
        inst.m_Tracker.ConfigureFromSettings(config);
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

    // Monotonic scan counter, increments once per executed scan.
    static int GetLastScanSerial()
    {
        return GetInstance().m_ScanSerial;
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

        RDF_RadarNetworkAPI net = RDF_RadarNetworkAPI.Cast(subject.FindComponent(RDF_RadarNetworkAPI));
        if (net != s_NetworkAPI)
            SetNetworkAPI(net);

        float now = world.GetWorldTime() * 0.001;
        if (now - m_LastScanTime >= m_Config.m_UpdateInterval)
        {
            m_LastScanTime = now;
            m_ScanSerial = m_ScanSerial + 1;
            m_LastTargets.Clear();
            if (IsNetworkAPIValid())
            {
                s_NetworkAPI.RequestScan();
                if (s_NetworkAPI.HasSyncedTargets())
                {
                    array<ref RDF_RadarTarget> syncedTargets = s_NetworkAPI.GetLastTargets();
                    if (syncedTargets)
                    {
                        for (int i = 0; i < syncedTargets.Count(); i++)
                        {
                            RDF_RadarTarget t = syncedTargets.Get(i);
                            if (t)
                                m_LastTargets.Insert(t);
                        }
                    }
                }
            }
            else
            {
                m_Scanner.Scan(subject, m_LastTargets);
            }
            vector trackOrigin = m_Scanner.GetLastOrigin();
            if (IsNetworkAPIValid())
                trackOrigin = s_NetworkAPI.GetLastScanOrigin();
            m_Tracker.UpdateWithOrigin(m_LastTargets, now, trackOrigin);
            m_Tracker.RefreshWeaponLocates(trackOrigin[1]);

            if (s_HudEnabled)
            {
                if (!RDF_RadarHUD.IsVisible())
                    RDF_RadarHUD.Show();
                string modeText = "PPI";
                if (IsNetworkAPIValid())
                    modeText = "PPI | NET";
                else
                    modeText = "PPI | " + m_Scanner.GetDemStatusShort();
                RDF_RadarHUD.SetMode(modeText);
                vector hudOrigin = m_Scanner.GetLastOrigin();
                vector hudForward = m_Scanner.GetLastForward();
                float hudRange = m_Scanner.GetLastRange();
                if (IsNetworkAPIValid())
                {
                    hudOrigin = s_NetworkAPI.GetLastScanOrigin();
                    hudForward = s_NetworkAPI.GetLastScanForward();
                    float netRange = s_NetworkAPI.GetLastScanRange();
                    if (netRange > 0.0)
                        hudRange = netRange;
                }
                RDF_RadarHUD.FeedScan(
                    m_LastTargets,
                    hudOrigin,
                    hudForward,
                    hudRange,
                    m_Tracker);
            }
        }

        if (m_Visualizer && m_VisualSettings)
        {
            bool drawScan = m_VisualSettings.m_DrawRays
                || m_VisualSettings.m_DrawPoints
                || m_VisualSettings.m_DrawOriginAxis;
            bool drawWlr = m_VisualSettings.m_DrawWeaponLocate;
            if (drawScan)
                m_Visualizer.Render(subject, m_LastTargets);
            else
            {
                if (drawWlr)
                    m_Visualizer.Reset();
            }
            if (drawWlr && m_Tracker)
                m_Visualizer.RenderWeaponLocates(m_Tracker);
        }
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
