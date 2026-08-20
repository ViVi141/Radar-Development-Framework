// Resolve DemData/<world>/ for packaged SURF/HEIGHT (.conf ResourceNames).
class RDF_DemPackagedRoot
{
    protected static bool s_LoggedProbe;

    //--------------------------------------------------------------------------------------------
    // Logical root "DemData/<world>/" when packaged surf_manifest.conf is indexed/loadable.
    static string FindWorldRoot(string worldKey)
    {
        if (worldKey.IsEmpty())
            return string.Empty;

        string relativeRoot = RDF_DemBakeConstants.PACKAGED_DEM_DATA_DIR + worldKey + "/";
        string relativeManifest = RDF_DemPackIo.SurfManifestConfPath(relativeRoot);

        if (RDF_DemPackIo.ConfExists(relativeManifest))
            return relativeRoot;

        if (!s_LoggedProbe)
        {
            s_LoggedProbe = true;
            RDF_DemPackIo.LogConfProbe(relativeManifest);
        }

        return string.Empty;
    }
}
