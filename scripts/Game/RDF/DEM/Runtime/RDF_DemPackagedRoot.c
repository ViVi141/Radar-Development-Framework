// Resolve DemData/ inside a packed workshop addon for FileIO.
// Relative "DemData/..." works in Workbench (loose project folder) but fails on
// dedicated servers / Steam Workshop packs — those need "$AddonId:DemData/...".
class RDF_DemPackagedRoot
{
    // Same GUID as RDF_RadarSurfaceTable — already proven loadable on DS.
    static const ResourceName ANCHOR_RESOURCE =
        "{B4E81F2A7C903D65}RadarData/SurfaceTable.conf";
    static const string FALLBACK_ADDON_ID = "RadarDevelopmentFramework";

    protected static string s_CachedAddonFs;
    protected static bool s_AddonFsResolved;

    //--------------------------------------------------------------------------------------------
    // Returns "DemData/<worldKey>/" with whatever prefix FileIO can actually see.
    // Empty string when no packaged SURF manifest is visible.
    static string FindWorldRoot(string worldKey)
    {
        if (worldKey.IsEmpty())
            return string.Empty;

        string relativeRoot = RDF_DemBakeConstants.PACKAGED_DEM_DATA_DIR + worldKey + "/";
        string relativeManifest = relativeRoot + RDF_DemSurfaceJsonPack.MANIFEST_NAME;
        if (FileIO.FileExists(relativeManifest))
            return relativeRoot;

        string addonFs = GetAddonFileSystemPrefix();
        if (!addonFs.IsEmpty())
        {
            string addonRoot = addonFs + relativeRoot;
            if (FileIO.FileExists(addonFs + relativeManifest))
                return addonRoot;
        }

        array<string> systems = SCR_AddonTool.GetAllAddonFileSystems();
        if (!systems)
            return string.Empty;

        int n = systems.Count();
        for (int i = 0; i < n; i++)
        {
            string fs = systems.Get(i);
            if (fs.IsEmpty())
                continue;
            if (FileIO.FileExists(fs + relativeManifest))
                return fs + relativeRoot;
        }

        return string.Empty;
    }

    //--------------------------------------------------------------------------------------------
    protected static string GetAddonFileSystemPrefix()
    {
        if (s_AddonFsResolved)
            return s_CachedAddonFs;

        s_AddonFsResolved = true;
        s_CachedAddonFs = string.Empty;

        string addonId = SCR_AddonTool.GetResourceLastAddon(ANCHOR_RESOURCE);
        if (addonId.IsEmpty())
            addonId = FALLBACK_ADDON_ID;

        string prefix = SCR_AddonTool.ToFileSystem(addonId);
        if (prefix.IsEmpty())
            return s_CachedAddonFs;

        s_CachedAddonFs = prefix;
        return s_CachedAddonFs;
    }
}
