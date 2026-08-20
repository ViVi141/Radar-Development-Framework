// DemData IO: profile JSON via FileIO; workshop packs via CONF ResourceName.
// Game/DS has no JSON Resource loader — packaged data must be CONFResourceClass.
class RDF_DemPackIo
{
    static const string SURF_MANIFEST_JSON = "surf_manifest.json";
    static const string SURF_MANIFEST_CONF = "surf_manifest.conf";
    static const string HEIGHT_MANIFEST_JSON = "height_manifest.json";
    static const string HEIGHT_MANIFEST_CONF = "height_manifest.conf";
    static const string CHUNKS_SURF = "surf_chunks/";
    static const string CHUNKS_HEIGHT = "height_chunks/";

    //--------------------------------------------------------------------------------------------
    static string SurfManifestJsonPath(string rootDir)
    {
        return rootDir + SURF_MANIFEST_JSON;
    }

    //--------------------------------------------------------------------------------------------
    static string SurfManifestConfPath(string rootDir)
    {
        return rootDir + SURF_MANIFEST_CONF;
    }

    //--------------------------------------------------------------------------------------------
    static string HeightManifestJsonPath(string rootDir)
    {
        return rootDir + HEIGHT_MANIFEST_JSON;
    }

    //--------------------------------------------------------------------------------------------
    static string HeightManifestConfPath(string rootDir)
    {
        return rootDir + HEIGHT_MANIFEST_CONF;
    }

    //--------------------------------------------------------------------------------------------
    static string SurfRowJsonPath(string rootDir, int tileIz)
    {
        return rootDir + CHUNKS_SURF + "row_" + tileIz.ToString() + ".json";
    }

    //--------------------------------------------------------------------------------------------
    static string SurfRowConfPath(string rootDir, int tileIz)
    {
        return rootDir + CHUNKS_SURF + "row_" + tileIz.ToString() + ".conf";
    }

    //--------------------------------------------------------------------------------------------
    static string HeightRowJsonPath(string rootDir, int tileIz)
    {
        return rootDir + CHUNKS_HEIGHT + "row_" + tileIz.ToString() + ".json";
    }

    //--------------------------------------------------------------------------------------------
    static string HeightRowConfPath(string rootDir, int tileIz)
    {
        return rootDir + CHUNKS_HEIGHT + "row_" + tileIz.ToString() + ".conf";
    }

    //--------------------------------------------------------------------------------------------
    static bool TryLoadJsonDoc(notnull JsonApiStruct doc, string path)
    {
        if (path.IsEmpty())
            return false;
        if (!FileIO.FileExists(path))
            return false;
        return doc.LoadFromFile(path);
    }

    //--------------------------------------------------------------------------------------------
    static ResourceName ResolveConfResource(string relativeOrFsPath)
    {
        // Never map $profile:…/DemData/… onto the workshop GuidIndex — that made DS
        // logs claim "profile SURF" while actually loading packaged CONF.
        if (IsProfileFsPath(relativeOrFsPath))
            return ResourceName.Empty;

        string key = ToDemDataKey(relativeOrFsPath);
        if (key.IsEmpty())
            return ResourceName.Empty;
        return RDF_DemGuidIndex.Lookup(key);
    }

    //--------------------------------------------------------------------------------------------
    static bool ConfExists(string relativeOrFsPath)
    {
        if (relativeOrFsPath.IsEmpty())
            return false;
        if (FileIO.FileExists(relativeOrFsPath))
            return true;
        if (IsProfileFsPath(relativeOrFsPath))
            return false;
        string key = ToDemDataKey(relativeOrFsPath);
        if (key.IsEmpty())
            return false;
        return RDF_DemGuidIndex.Has(key);
    }

    //--------------------------------------------------------------------------------------------
    static RDF_DemSurfManifestConf LoadSurfManifestConf(string relativeOrFsPath)
    {
        ResourceName rn = ResolveConfResource(relativeOrFsPath);
        if (rn.IsEmpty())
        {
            // Workbench loose file: ConfigHelper still wants ResourceName; try Guid only.
            return null;
        }
        return SCR_ConfigHelperT<RDF_DemSurfManifestConf>.GetConfigObject(rn);
    }

    //--------------------------------------------------------------------------------------------
    static RDF_DemHeightManifestConf LoadHeightManifestConf(string relativeOrFsPath)
    {
        ResourceName rn = ResolveConfResource(relativeOrFsPath);
        if (rn.IsEmpty())
            return null;
        return SCR_ConfigHelperT<RDF_DemHeightManifestConf>.GetConfigObject(rn);
    }

    //--------------------------------------------------------------------------------------------
    static RDF_DemChunkRowConf LoadChunkRowConf(string relativeOrFsPath)
    {
        ResourceName rn = ResolveConfResource(relativeOrFsPath);
        if (rn.IsEmpty())
            return null;
        return SCR_ConfigHelperT<RDF_DemChunkRowConf>.GetConfigObject(rn);
    }

    //--------------------------------------------------------------------------------------------
    static void LogConfProbe(string relativeConfPath)
    {
        ResourceName rn = RDF_DemGuidIndex.Lookup(relativeConfPath);
        if (rn.IsEmpty())
        {
            Print("[RDF DEM] guid-index miss: " + relativeConfPath, LogLevel.WARNING);
            return;
        }

        RDF_DemSurfManifestConf surf =
            SCR_ConfigHelperT<RDF_DemSurfManifestConf>.GetConfigObject(rn);
        string ok = "0";
        if (surf)
            ok = "1";
        Print("[RDF DEM] conf probe " + relativeConfPath
            + " configOk=" + ok
            + " rn=" + rn,
            LogLevel.WARNING);
    }

    //--------------------------------------------------------------------------------------------
    protected static bool IsProfileFsPath(string path)
    {
        if (path.IsEmpty())
            return false;
        string s = path;
        s.Replace("\\", "/");
        s.ToLower();
        return s.IndexOf("$profile:") == 0;
    }

    //--------------------------------------------------------------------------------------------
    // Strip addon FS / normalize to GuidIndex keys "DemData/...".
    // Do not call for $profile: paths (use IsProfileFsPath first).
    protected static string ToDemDataKey(string path)
    {
        if (path.IsEmpty())
            return string.Empty;

        string s = path;
        s.Replace("\\", "/");

        if (IsProfileFsPath(s))
            return string.Empty;

        int dem = s.IndexOf("DemData/");
        if (dem >= 0)
            return s.Substring(dem, s.Length() - dem);

        if (s.StartsWith("$"))
        {
            int colon = s.IndexOf(":");
            if (colon >= 0 && colon + 1 < s.Length())
                return s.Substring(colon + 1, s.Length() - colon - 1);
        }

        return s;
    }
}
