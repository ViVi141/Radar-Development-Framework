// Base datalink API (intentional no-ops). Prefer RDF_RadarDatalinkHub.Get().
[ComponentEditorProps(category: "GameScripted/RDF", description: "Base datalink API for multi-radar track sharing")]
class RDF_RadarDatalinkAPIClass : ScriptComponentClass
{
}

class RDF_RadarDatalinkAPI : ScriptComponent
{
    void PublishTracks(notnull array<ref RDF_RadarDatalinkTrack> tracks)
    {
    }

    void GetTracksInRadius(
        vector origin,
        float radiusM,
        notnull array<ref RDF_RadarDatalinkTrack> outTracks)
    {
    }

    void Clear()
    {
    }

    string GetStatusShort()
    {
        return "DL OFF";
    }

    array<ref RDF_RadarFusedTrack> GetFusedTracks()
    {
        return null;
    }
}
