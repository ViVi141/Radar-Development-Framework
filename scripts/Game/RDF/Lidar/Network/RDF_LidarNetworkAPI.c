// Network API for LiDAR synchronization and server authority.
// Default methods are intentional no-ops; override in a network component.
// Prefer SetEnabled / SetConfig; SetDemo* names are legacy aliases for AutoRunner.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Base network API component for LiDAR synchronization")]
class RDF_LidarNetworkAPIClass : ScriptComponentClass
{
}

class RDF_LidarNetworkAPI : ScriptComponent
{
    //------------------------------------------------------------------------------------------------
    //! \return true if network layer is available
    bool IsNetworkAvailable()
    {
        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! Enable/disable authoritative scan path (intentional no-op in base).
    void SetEnabled(bool enabled)
    {
    }

    //------------------------------------------------------------------------------------------------
    //! Apply scan/config scalars on authority (intentional no-op in base).
    void SetConfig(RDF_LidarDemoConfig config)
    {
    }

    //------------------------------------------------------------------------------------------------
    //! Legacy alias used by AutoRunner demos; prefer SetEnabled.
    void SetDemoEnabled(bool enabled)
    {
        SetEnabled(enabled);
    }

    //------------------------------------------------------------------------------------------------
    //! Legacy alias used by AutoRunner demos; prefer SetConfig.
    void SetDemoConfig(RDF_LidarDemoConfig config)
    {
        SetConfig(config);
    }

    //------------------------------------------------------------------------------------------------
    //! Request scan on server using component owner as subject. No parameters to avoid replication issues.
    void RequestScan()
    {
    }

    //------------------------------------------------------------------------------------------------
    //! \return true if synchronized results are available
    bool HasSyncedSamples()
    {
        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! \return last synchronized samples
    array<ref RDF_LidarSample> GetLastScanResults()
    {
        return null;
    }
}
