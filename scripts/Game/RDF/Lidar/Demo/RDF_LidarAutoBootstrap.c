// Global auto-start for LiDAR demo (opt-in by including this file).
modded class SCR_BaseGameMode
{
    protected static bool s_EnableBootstrap = true;

    override void OnGameStart()
    {
        super.OnGameStart();
        if (s_EnableBootstrap)
            RDF_LidarAutoRunner.SetDemoEnabled(true);
    }

    // Set to true to enable global auto-start.
    static void SetBootstrapEnabled(bool enabled)
    {
        s_EnableBootstrap = enabled;
    }
}
