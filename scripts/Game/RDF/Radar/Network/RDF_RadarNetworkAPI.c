// Network API for radar synchronization and server authority.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Base network API component for Radar synchronization")]
class RDF_RadarNetworkAPIClass : ScriptComponentClass
{
}

class RDF_RadarNetworkAPI : ScriptComponent
{
    bool IsNetworkAvailable()
    {
        return false;
    }

    void SetDemoEnabled(bool enabled)
    {
    }

    void SetDemoConfig(RDF_RadarSettings config)
    {
    }

    void RequestScan()
    {
    }

    bool HasSyncedTargets()
    {
        return false;
    }

    array<ref RDF_RadarTarget> GetLastTargets()
    {
        return null;
    }

    vector GetLastScanOrigin()
    {
        return "0 0 0";
    }

    vector GetLastScanForward()
    {
        return "1 0 0";
    }

    float GetLastScanRange()
    {
        return 0.0;
    }
}
