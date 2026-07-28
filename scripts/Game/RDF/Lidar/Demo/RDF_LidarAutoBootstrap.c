// Bootstrap for LiDAR, Radar demo, and RDF DEM tile bake.
// LiDAR params: RDF_LidarDemoConfig. DEM bake: create $profile:RDF/BakeDemFull.flag.
// Default silent: no LiDAR/Radar start and no network bind unless explicitly enabled.
modded class SCR_BaseGameMode
{
    protected static bool s_RadarBootstrapEnabled = false;

    override void OnGameStart()
    {
        super.OnGameStart();

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

        if (RDF_DemTileBake.IsBakeRequested())
        {
            GetGame().GetCallqueue().CallLater(
                RdfTryStartDemBake, RDF_DemBakeConstants.START_DELAY_MS, false);
        }
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        super.EOnFrame(owner, timeSlice);
        RDF_DemTileBake.OnFrame(timeSlice);
    }

    protected void RdfTryStartDemBake()
    {
        RDF_DemTileBake.TryStartBake();
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

    // Convenience: write BakeDemFull.flag then call this, or just create the flag and restart Play.
    static void RequestDemBake()
    {
        RDF_DemTileBake.WriteBakeRequest();
    }
}
