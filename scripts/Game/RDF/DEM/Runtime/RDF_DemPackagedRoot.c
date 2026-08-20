// Resolve DemData/<world>/ for packaged SURF/HEIGHT.
// Workbench: relative FileIO. Dedicated server / Workshop: GuidIndex ResourceNames
// (JSONResourceClass is often invisible to FileIO even under "$AddonId:").
class RDF_DemPackagedRoot
{
    static const ResourceName ANCHOR_RESOURCE =
        "{B4E81F2A7C903D65}RadarData/SurfaceTable.conf";
    static const string FALLBACK_ADDON_ID = "RadarDevelopmentFramework";

    protected static string s_CachedAddonFs;
    protected static bool s_AddonFsResolved;
    protected static bool s_LoggedProbe;

    //--------------------------------------------------------------------------------------------
    // Logical root "DemData/<world>/" (optionally "$AddonId:DemData/<world>/") when loadable.
    static string FindWorldRoot(string worldKey)
    {
        if (worldKey.IsEmpty())
            return string.Empty;

        string relativeRoot = RDF_DemBakeConstants.PACKAGED_DEM_DATA_DIR + worldKey + "/";
        string relativeManifest = relativeRoot + RDF_DemSurfaceJsonPack.MANIFEST_NAME;

        if (RDF_DemJsonIo.Exists(relativeManifest))
            return relativeRoot;

        string addonFs = GetAddonFileSystemPrefix();
        if (!addonFs.IsEmpty())
        {
            string addonManifest = addonFs + relativeManifest;
            if (RDF_DemJsonIo.Exists(addonManifest))
                return addonFs + relativeRoot;
        }

        array<string> systems = SCR_AddonTool.GetAllAddonFileSystems();
        if (systems)
        {
            int n = systems.Count();
            for (int i = 0; i < n; i++)
            {
                string fs = systems.Get(i);
                if (fs.IsEmpty())
                    continue;
                if (RDF_DemJsonIo.Exists(fs + relativeManifest))
                    return fs + relativeRoot;
            }
        }

        if (!s_LoggedProbe)
        {
            s_LoggedProbe = true;
            RDF_DemJsonIo.LogResourceProbe(relativeManifest);
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
