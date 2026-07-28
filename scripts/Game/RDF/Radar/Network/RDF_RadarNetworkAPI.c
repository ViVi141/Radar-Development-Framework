// Network API for radar synchronization and server authority.
// Default methods are intentional no-ops; override in a network component.
// Prefer SetEnabled / SetConfig; SetDemo* names are legacy aliases for AutoRunner.
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

    //! Enable/disable authoritative scan path (intentional no-op in base).
    void SetEnabled(bool enabled)
    {
    }

    //! Apply settings on authority (intentional no-op in base).
    void SetConfig(RDF_RadarSettings config)
    {
    }

    //! Legacy alias used by AutoRunner demos; prefer SetEnabled.
    void SetDemoEnabled(bool enabled)
    {
        SetEnabled(enabled);
    }

    //! Legacy alias used by AutoRunner demos; prefer SetConfig.
    void SetDemoConfig(RDF_RadarSettings config)
    {
        SetConfig(config);
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
