// Demo cycler: cycles through a set of sample strategies for quick visual comparison.
class RDF_LidarDemoCycler
{
    protected static int s_Index = -1;
    protected static ref array<ref RDF_LidarSampleStrategy> s_Strategies;

    // Auto-cycle state
    protected static bool s_AutoCycleEnabled = false;
    protected static float s_CycleInterval = 10.0; // seconds

    static void InitList()
    {
        if (s_Strategies) return;
        s_Strategies = new array<ref RDF_LidarSampleStrategy>();
        s_Strategies.Insert(new RDF_UniformSampleStrategy());
        s_Strategies.Insert(new RDF_HemisphereSampleStrategy());
        s_Strategies.Insert(new RDF_ConicalSampleStrategy(30.0));
        s_Strategies.Insert(new RDF_StratifiedSampleStrategy());
        s_Strategies.Insert(new RDF_ScanlineSampleStrategy(32));
    }

    // Cycle to the next strategy and start demo.
    static void Cycle(int rayCount = 256)
    {
        InitList();
        s_Index = (s_Index + 1) % s_Strategies.Count();
        RDF_LidarSampleStrategy strat = s_Strategies.Get(s_Index);
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = strat;
        cfg.m_RayCount = Math.Clamp(rayCount, 1, 4096);
        RDF_LidarAutoRunner.SetDemoConfig(cfg);
        RDF_LidarAutoRunner.SetDemoEnabled(true);
        Print("[RDF] Demo started: " + strat.ClassName() + " (index=" + s_Index + ")");
    }

    // Start a specific strategy by index
    static void StartIndex(int index, int rayCount = 256)
    {
        InitList();
        if (index < 0) index = -index; // inline abs
        index = index % s_Strategies.Count();
        RDF_LidarSampleStrategy strat = s_Strategies.Get(index);
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = strat;
        cfg.m_RayCount = Math.Clamp(rayCount, 1, 4096);
        RDF_LidarAutoRunner.SetDemoConfig(cfg);
        RDF_LidarAutoRunner.SetDemoEnabled(true);
        s_Index = index;
        Print("[RDF] Demo started: " + strat.ClassName() + " (index=" + s_Index + ")");
    }

    static void Stop()
    {
        RDF_LidarAutoRunner.SetDemoEnabled(false);
    }

    // Auto-cycle helpers
    static void StaticCycleTick()
    {
        if (!s_AutoCycleEnabled) return;
        Cycle();
        // Schedule next tick one-shot to allow interval changes
        GetGame().GetCallqueue().CallLater(StaticCycleTick, s_CycleInterval, false);
    }

    static void StartAutoCycle(float interval = 10.0)
    {
        s_CycleInterval = Math.Max(0.1, interval);
        if (s_AutoCycleEnabled) return;
        s_AutoCycleEnabled = true;
        // schedule first tick after interval
        GetGame().GetCallqueue().CallLater(StaticCycleTick, s_CycleInterval, false);
        Print("[RDF] Auto-cycle started (interval=" + s_CycleInterval + "s)");
    }

    static void StopAutoCycle()
    {
        s_AutoCycleEnabled = false;
        Print("[RDF] Auto-cycle stopped");
    }

    static bool IsAutoCycling()
    {
        return s_AutoCycleEnabled;
    }

    static void SetAutoCycleInterval(float interval)
    {
        float v = Math.Max(0.1, interval);
        s_CycleInterval = v;
        Print("[RDF] Auto-cycle interval set to " + s_CycleInterval + "s");
    }
}