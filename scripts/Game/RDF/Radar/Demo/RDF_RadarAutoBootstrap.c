// Unified bootstrap for radar demo.
// Chains onto SCR_BaseGameMode.OnGameStart() - kept disabled by default so
// production mods that depend on this addon are unaffected.
//
// To enable from another script at startup:
//   SCR_BaseGameMode.SetRadarBootstrapEnabled(true);
//   SCR_BaseGameMode.SetRadarBootstrapAutoCycle(true);   // optional
//
// Or flip s_RadarBootstrapEnabled = true here to always start on game start
// (useful when developing / testing standalone).
modded class SCR_BaseGameMode
{
    // Master switch: when true the radar demo starts automatically on game start.
    protected static bool  s_RadarBootstrapEnabled      = true;
    // When true the cycler rotates through all five presets.
    protected static bool  s_RadarBootstrapAutoCycle    = false;
    protected static float s_RadarBootstrapCycleInterval = 15.0;
    // When true each scan prints stats to the log.
    protected static bool  s_RadarBootstrapVerbose      = true;
    // When true the A-Scope is shown alongside the world markers.
    protected static bool  s_RadarBootstrapShowAScope   = false;
    // When true the on-screen radar HUD panel is displayed.
    protected static bool  s_RadarBootstrapShowHUD      = true;

    override void OnGameStart()
    {
        super.OnGameStart();

        if (!s_RadarBootstrapEnabled)
            return;

        if (s_RadarBootstrapAutoCycle)
        {
            RDF_RadarDemoCycler.StartAutoCycle(s_RadarBootstrapCycleInterval);
        }
        else
        {
            // Default single preset: helicopter radar (forward sector, MTI, 8 km).
            RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateHelicopterRadar());
        }

        if (s_RadarBootstrapVerbose)
            RDF_RadarAutoRunner.SetDemoVerbose(true);

        if (s_RadarBootstrapShowAScope)
            RDF_RadarAutoRunner.SetShowAScope(true);

        // Load on-screen HUD panel and wire it as the scan-complete handler.
        if (s_RadarBootstrapShowHUD)
        {
            RDF_RadarHUD.Show();
            RDF_RadarAutoRunner.SetScanCompleteHandler(RDF_RadarHUD.GetInstance());
            if (!s_RadarBootstrapAutoCycle)
            {
                RDF_RadarHUD.SetDisplayRange(8000.0);   // heli preset 8 km
                RDF_RadarHUD.SetMode("Heli Radar");
            }
        }

        Print("[RDF] Radar bootstrap started  (autoCycle=" + s_RadarBootstrapAutoCycle.ToString() + "  hud=" + s_RadarBootstrapShowHUD.ToString() + ")");
    }

    // ---- configuration switches ----

    // Master on/off for the radar demo bootstrap.
    static void SetRadarBootstrapEnabled(bool enabled)
    {
        s_RadarBootstrapEnabled = enabled;
    }

    static bool IsRadarBootstrapEnabled()
    {
        return s_RadarBootstrapEnabled;
    }

    // When enabled, the cycler rotates through all five presets automatically.
    static void SetRadarBootstrapAutoCycle(bool enabled)
    {
        s_RadarBootstrapAutoCycle = enabled;
    }

    // Seconds between preset switches in auto-cycle mode (minimum 0.5).
    static void SetRadarBootstrapCycleInterval(float seconds)
    {
        s_RadarBootstrapCycleInterval = Math.Max(0.5, seconds);
    }

    // When true each scan prints a short stats summary to the log.
    static void SetRadarBootstrapVerbose(bool verbose)
    {
        s_RadarBootstrapVerbose = verbose;
    }

    // When true the A-Scope range-amplitude chart is shown alongside world markers.
    static void SetRadarBootstrapShowAScope(bool show)
    {
        s_RadarBootstrapShowAScope = show;
    }

    // Toggle the on-screen radar HUD panel.
    static void SetRadarBootstrapShowHUD(bool show)
    {
        s_RadarBootstrapShowHUD = show;
    }
}
