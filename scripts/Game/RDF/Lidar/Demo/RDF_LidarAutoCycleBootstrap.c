// Optional bootstrap: auto-start the demo cycler on game start (opt-in).
// Default: disabled to avoid surprising behavior for other mods.
modded class SCR_BaseGameMode
{
    // Default: disabled
    protected static bool s_EnableAutoCycleBootstrap = true;

    override void OnGameStart()
    {
        super.OnGameStart();
        if (s_EnableAutoCycleBootstrap)
        {
            // Start auto-cycle with a sensible default interval (seconds)
            RDF_LidarDemoCycler.StartAutoCycle(10000.0);
        }
    }

    // Call at runtime to enable/disable auto-cycle bootstrap
    static void SetAutoCycleBootstrapEnabled(bool enabled)
    {
        s_EnableAutoCycleBootstrap = enabled;
    }
}