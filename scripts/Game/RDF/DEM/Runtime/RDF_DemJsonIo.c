// Load DemData JSON via FileIO when visible, else via packed ResourceName GUID.
// Workshop / dedicated-server packs register DemData as JSONResourceClass — bare
// "DemData/..." and even "$AddonId:DemData/..." often fail FileIO.FileExists, while
// Resource.Load("{GUID}DemData/...") remains valid (same pattern as SurfaceTable.conf).
class RDF_DemJsonIo
{
    //--------------------------------------------------------------------------------------------
    // relativeOrFsPath: "$profile:...", "DemData/...", or "$AddonId:DemData/..."
    static bool TryLoad(notnull JsonApiStruct doc, string relativeOrFsPath)
    {
        if (relativeOrFsPath.IsEmpty())
            return false;

        if (TryLoadFileIo(doc, relativeOrFsPath))
            return true;

        string key = ToDemDataKey(relativeOrFsPath);
        if (key.IsEmpty())
            return false;

        ResourceName rn = RDF_DemGuidIndex.Lookup(key);
        if (rn.IsEmpty())
            return false;

        if (TryLoadFileIo(doc, rn))
            return true;

        if (TryLoadViaResourceHandle(doc, rn))
            return true;

        return false;
    }

    //--------------------------------------------------------------------------------------------
    static bool Exists(string relativeOrFsPath)
    {
        if (relativeOrFsPath.IsEmpty())
            return false;
        if (FileIO.FileExists(relativeOrFsPath))
            return true;

        string key = ToDemDataKey(relativeOrFsPath);
        if (key.IsEmpty())
            return false;

        ResourceName rn = RDF_DemGuidIndex.Lookup(key);
        if (rn.IsEmpty())
            return false;
        if (FileIO.FileExists(rn))
            return true;

        Resource res = Resource.Load(rn);
        if (res && res.IsValid())
            return true;
        return false;
    }

    //--------------------------------------------------------------------------------------------
    // One-shot diagnostic for the Eden/Cain/Arland surf manifest ResourceName.
    static void LogResourceProbe(string relativeManifestPath)
    {
        ResourceName rn = RDF_DemGuidIndex.Lookup(relativeManifestPath);
        if (rn.IsEmpty())
        {
            Print("[RDF DEM] guid-index miss: " + relativeManifestPath, LogLevel.WARNING);
            return;
        }

        bool fileIo = FileIO.FileExists(rn);
        Resource res = Resource.Load(rn);
        bool valid = false;
        if (res)
            valid = res.IsValid();

        string fileIoStr = "0";
        if (fileIo)
            fileIoStr = "1";
        string validStr = "0";
        if (valid)
            validStr = "1";

        Print("[RDF DEM] resource probe " + relativeManifestPath
            + " fileIo=" + fileIoStr
            + " resourceValid=" + validStr
            + " rn=" + rn,
            LogLevel.WARNING);
    }

    //--------------------------------------------------------------------------------------------
    protected static bool TryLoadFileIo(notnull JsonApiStruct doc, string path)
    {
        if (path.IsEmpty())
            return false;
        if (!FileIO.FileExists(path))
            return false;
        return doc.LoadFromFile(path);
    }

    //--------------------------------------------------------------------------------------------
    // Some builds accept ResourceName in OpenFile even when FileExists is false.
    protected static bool TryLoadViaResourceHandle(notnull JsonApiStruct doc, ResourceName rn)
    {
        if (rn.IsEmpty())
            return false;

        Resource res = Resource.Load(rn);
        if (!res || !res.IsValid())
            return false;

        // Prefer LoadFromFile(ResourceName) — engine may resolve through ResourceManager.
        if (doc.LoadFromFile(rn))
            return true;

        array<string> lines = SCR_FileIOHelper.ReadFileContent(rn, false);
        if (!lines || lines.Count() < 1)
            return false;

        string raw = lines.Get(0);
        int n = lines.Count();
        for (int i = 1; i < n; i++)
            raw = raw + "\n" + lines.Get(i);

        if (raw.IsEmpty())
            return false;

        doc.ExpandFromRAW(raw);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    // Strip "$AddonId:" (or any "$x:") so GuidIndex keys stay "DemData/..." .
    protected static string ToDemDataKey(string path)
    {
        if (path.IsEmpty())
            return string.Empty;

        string s = path;
        s.Replace("\\", "/");

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
