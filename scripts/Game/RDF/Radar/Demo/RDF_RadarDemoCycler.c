// Demo cycler: cycles through the five built-in radar presets automatically.
// Mirrors RDF_LidarDemoCycler but uses RDF_RadarDemoConfig presets and
// drives RDF_RadarAutoRunner.
//
// Usage:
//   RDF_RadarDemoCycler.StartAutoCycle(15.0);  // switch preset every 15 s
//   RDF_RadarDemoCycler.StopAutoCycle();
//   RDF_RadarDemoCycler.Cycle();               // advance one preset manually
//   RDF_RadarDemoCycler.StartIndex(2, 512);    // jump to preset #2 with 512 rays
class RDF_RadarDemoCycler
{
    protected static int s_Index = -1;
    protected static ref array<ref RDF_RadarDemoConfig> s_Configs;

    protected static bool  s_AutoCycleEnabled = false;
    protected static float s_CycleInterval    = 15.0; // seconds per preset

    // Ray-count used when building presets in the cycler.
    protected static int s_RayCount = 512;

    // ---- preset list init ----
    static void InitList()
    {
        if (s_Configs) return;
        s_Configs = new array<ref RDF_RadarDemoConfig>();
        s_Configs.Insert(RDF_RadarDemoConfig.CreateHelicopterRadar(s_RayCount));
        s_Configs.Insert(RDF_RadarDemoConfig.CreateXBandSearch(s_RayCount));
        s_Configs.Insert(RDF_RadarDemoConfig.CreateAutomotiveRadar(s_RayCount));
        s_Configs.Insert(RDF_RadarDemoConfig.CreateWeatherRadar(s_RayCount));
        s_Configs.Insert(RDF_RadarDemoConfig.CreatePhasedArrayRadar(s_RayCount));
        s_Configs.Insert(RDF_RadarDemoConfig.CreateLBandSurveillance(s_RayCount));
    }

    // Rebuild the list with a different ray count.
    static void RebuildList(int rayCount)
    {
        s_RayCount = Math.Max(1, rayCount);
        s_Configs  = null;
        InitList();
    }

    // ---- manual stepping ----

    // Advance to the next preset and start demo.
    static void Cycle()
    {
        InitList();
        s_Index = (s_Index + 1) % s_Configs.Count();
        RDF_RadarDemoConfig cfg = s_Configs.Get(s_Index);
        if (!cfg) return;
        Print("[RadarDemo] Cycler -> preset " + s_Index.ToString() + " / " + GetPresetName(s_Index));
        RDF_RadarAutoRunner.StartWithConfig(cfg);
        RDF_RadarHUD.SetMode(GetPresetName(s_Index));
        if (cfg.m_RadarRange > 0)
            RDF_RadarHUD.SetDisplayRange(cfg.m_RadarRange);
    }

    // Jump to a specific preset index.
    static void StartIndex(int index, int rayCount = -1)
    {
        if (rayCount > 0)
            RebuildList(rayCount);
        else
            InitList();

        if (index < 0)
            index = -index;
        s_Index = index % s_Configs.Count();

        RDF_RadarDemoConfig cfg = s_Configs.Get(s_Index);
        if (!cfg) return;
        Print("[RadarDemo] Cycler -> preset " + s_Index.ToString() + " / " + GetPresetName(s_Index));
        RDF_RadarAutoRunner.StartWithConfig(cfg);
        RDF_RadarHUD.SetMode(GetPresetName(s_Index));
        if (cfg.m_RadarRange > 0)
            RDF_RadarHUD.SetDisplayRange(cfg.m_RadarRange);
    }

    // Stop the radar demo.
    static void Stop()
    {
        RDF_RadarAutoRunner.SetDemoEnabled(false);
    }

    // Return human-readable name for the preset at the given index.
    static string GetPresetName(int index)
    {
        switch (index)
        {
            case 0: return "Helicopter (X-Band)";
            case 1: return "X-Band Search";
            case 2: return "Automotive FMCW (77 GHz)";
            case 3: return "Weather (S-Band)";
            case 4: return "Phased Array (C-Band)";
            case 5: return "L-Band Surveillance";
        }
        return "Unknown";
    }

    // Return the total number of presets.
    static int GetPresetCount()
    {
        InitList();
        return s_Configs.Count();
    }

    // Return the index of the currently active preset (-1 = none started).
    static int GetCurrentIndex()
    {
        return s_Index;
    }

    // ---- auto-cycle ----
    static void StaticCycleTick()
    {
        if (!s_AutoCycleEnabled) return;
        Cycle();
        GetGame().GetCallqueue().CallLater(StaticCycleTick, s_CycleInterval, false);
    }

    static void StartAutoCycle(float intervalSeconds = 15.0, int rayCount = -1)
    {
        if (rayCount > 0)
            RebuildList(rayCount);
        s_CycleInterval    = Math.Max(0.5, intervalSeconds);
        if (s_AutoCycleEnabled) return;
        s_AutoCycleEnabled = true;
        GetGame().GetCallqueue().CallLater(StaticCycleTick, s_CycleInterval, false);
        Print("[RadarDemo] Auto-cycle started  interval=" + s_CycleInterval.ToString() + "s  presets=" + GetPresetCount().ToString());
    }

    static void StopAutoCycle()
    {
        s_AutoCycleEnabled = false;
        Print("[RadarDemo] Auto-cycle stopped");
    }

    static bool IsAutoCycling()
    {
        return s_AutoCycleEnabled;
    }

    static void SetAutoCycleInterval(float intervalSeconds)
    {
        s_CycleInterval = Math.Max(0.5, intervalSeconds);
        Print("[RadarDemo] Auto-cycle interval -> " + s_CycleInterval.ToString() + "s");
    }
}
