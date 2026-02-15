// Unified bootstrap for LiDAR demo: single switch, opt-in. Uses only public API.
modded class SCR_BaseGameMode
{
    // Master switch: when true, demo is started on game start. Default: false.
    // Keep bootstrap disabled by default so production mods depending on this addon are unaffected.
    protected static bool s_BootstrapEnabled = false;
    // When true, start in auto-cycle mode (rotate strategies). When false, start with default preset.
    protected static bool s_BootstrapAutoCycle = false;
    protected static float s_BootstrapAutoCycleInterval = 10.0;

    override void OnGameStart()
    {
        super.OnGameStart();

        // Auto-bind network API if present on the local subject.
        RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject(true);

        if (!s_BootstrapEnabled)
            return;

        if (s_BootstrapAutoCycle)
        {
            RDF_LidarDemoCycler.StartAutoCycle(s_BootstrapAutoCycleInterval);
            RDF_LidarAutoRunner.SetDemoDrawOriginAxis(true);
            RDF_LidarAutoRunner.SetDemoVerbose(true);
        }
        else
            RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefaultDebug());
    }

    static void SetBootstrapEnabled(bool enabled)
    {
        s_BootstrapEnabled = enabled;
    }

    static bool IsBootstrapEnabled()
    {
        return s_BootstrapEnabled;
    }

    static void SetBootstrapAutoCycle(bool enabled)
    {
        s_BootstrapAutoCycle = enabled;
    }

    static void SetBootstrapAutoCycleInterval(float intervalSeconds)
    {
        s_BootstrapAutoCycleInterval = Math.Max(0.1, intervalSeconds);
    }
}
