// Bootstrap for LiDAR demo. Reads all params from RDF_LidarDemoConfig (single source of truth).
modded class SCR_BaseGameMode
{
    override void OnGameStart()
    {
        super.OnGameStart();

        RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject(true);

        if (!RDF_LidarDemoConfig.IsBootstrapEnabled())
            return;

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
}
