// Bootstrap for LiDAR and Radar demo. Reads LiDAR params from RDF_LidarDemoConfig (single source of truth).
modded class SCR_BaseGameMode
{
    protected static bool s_RadarBootstrapEnabled = false;

    override void OnGameStart()
    {
        super.OnGameStart();

        RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject(true);

        if (RDF_LidarDemoConfig.IsBootstrapEnabled())
        {
            if (RDF_LidarDemoConfig.IsBootstrapAutoCycle())
            {
                RDF_LidarDemoCycler.StartAutoCycle(RDF_LidarDemoConfig.GetBootstrapCycleInterval());
                RDF_LidarAutoRunner.SetDemoDrawOriginAxis(true);
                RDF_LidarAutoRunner.SetDemoVerbose(true);
            }
            else
            {
                RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.GetBootstrapConfig());
            }
        }

        if (s_RadarBootstrapEnabled)
        {
            RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
            RDF_RadarAutoRunner.SetDemoEnabled(true);
        }
    }

    static void SetBootstrapEnabled(bool enabled)
    {
        RDF_LidarDemoConfig.SetBootstrapEnabled(enabled);
    }

    static bool IsBootstrapEnabled()
    {
        return RDF_LidarDemoConfig.IsBootstrapEnabled();
    }

    static void SetBootstrapAutoCycle(bool enabled)
    {
        RDF_LidarDemoConfig.SetBootstrapAutoCycle(enabled);
    }

    static void SetBootstrapAutoCycleInterval(float intervalSeconds)
    {
        RDF_LidarDemoConfig.SetBootstrapCycleInterval(intervalSeconds);
    }

    static void SetRadarBootstrapEnabled(bool enabled)
    {
        s_RadarBootstrapEnabled = enabled;
    }

    static bool IsRadarBootstrapEnabled()
    {
        return s_RadarBootstrapEnabled;
    }
}
