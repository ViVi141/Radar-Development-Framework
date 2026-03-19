// Attach to any entity in the world to start the radar demo (creates runner + schedules tick).
// Alternatively enable via SCR_BaseGameMode.SetRadarBootstrapEnabled(true) so radar starts in OnGameStart.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Starts radar demo on init (origin + rays). Attach to any entity; or use SetRadarBootstrapEnabled(true) in game mode.")]
class RDF_RadarBootstrapClass : ScriptComponentClass
{
}

class RDF_RadarBootstrap : ScriptComponent
{
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
        RDF_RadarAutoRunner.SetDemoEnabled(true);
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (RDF_RadarAutoRunner.IsDemoEnabled())
            RDF_RadarAutoRunner.GetInstance().RadarTick();
    }
}
