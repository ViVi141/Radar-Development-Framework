// Per-model radar signature: geometry extents + mean RCS + fluctuation class.
// Keyed by prefab resource name, so all instances of a model share one entry.
class RDF_RadarSignature
{
    string m_Key;
    float m_SizeX;
    float m_SizeY;
    float m_SizeZ;
    float m_CharacteristicLengthM;
    float m_MeanRcsM2;
    int m_SwerlingModel;
    int m_TypeHint;
    // True when loaded from the baked table (not measured this session).
    bool m_Baked;
}

// Signature table. Load order:
//   1. baked CSV (offline, shipped) — no runtime bounds queries needed
//   2. first sighting of an unknown model (e.g. third-party mod) is measured
//      once via GetBounds, cached, and reused by every later instance
//   3. ExportTable() writes the merged table back out; that file is the bake
class RDF_RadarSignatureLibrary
{
    static const string SIG_DIR = "$profile:RDF/Signatures";
    static const string SIG_FILE = "$profile:RDF/Signatures/rdf_radar_signatures.csv";
    static const string SIG_MAGIC = "RDF_RADAR_SIG_V1";
    static const string FIELD_SEP = ";";

    // Newly measured models are flushed back to the table at most this often,
    // so one session in the editor is enough to produce/extend the bake.
    static const float AUTO_EXPORT_INTERVAL_S = 30.0;

    protected static ref map<string, ref RDF_RadarSignature> s_ByKey;
    protected static bool s_BakedLoadAttempted;
    protected static bool s_Dirty;
    protected static float s_LastExportTime = -1000.0;

    protected static int s_StatBakedLoaded;
    protected static int s_StatMeasured;
    protected static int s_StatHits;
    protected static int s_StatNoKey;

    //------------------------------------------------------------------------------------------------
    // Resolve the signature for an entity, measuring only on first sighting.
    static RDF_RadarSignature Resolve(
        IEntity entity,
        ERDF_RadarTargetType targetType)
    {
        EnsureLoaded();
        if (!entity)
            return null;

        string key = MakeKey(entity);
        if (key == "")
        {
            s_StatNoKey = s_StatNoKey + 1;
            return MeasureSignature(entity, targetType, "");
        }

        RDF_RadarSignature cached = s_ByKey.Get(key);
        if (cached)
        {
            s_StatHits = s_StatHits + 1;
            return cached;
        }

        RDF_RadarSignature measured = MeasureSignature(entity, targetType, key);
        if (measured)
        {
            s_ByKey.Set(key, measured);
            s_Dirty = true;
        }
        return measured;
    }

    //------------------------------------------------------------------------------------------------
    // Persist models measured this session. Called from the registry tick.
    static void MaybeAutoExport(float now)
    {
        if (!s_Dirty)
            return;
        // First dirty tick only arms the timer, so the initial flush is not a near-empty file.
        if (s_LastExportTime < -1.0)
        {
            s_LastExportTime = now;
            return;
        }
        if (now - s_LastExportTime < AUTO_EXPORT_INTERVAL_S)
            return;

        s_LastExportTime = now;
        if (ExportTable())
            s_Dirty = false;
    }

    //------------------------------------------------------------------------------------------------
    // Prefab resource name is stable across instances and mods.
    static string MakeKey(IEntity entity)
    {
        if (!entity)
            return "";

        EntityPrefabData prefabData = entity.GetPrefabData();
        if (!prefabData)
            return "";

        ResourceName prefabName = prefabData.GetPrefabName();
        if (!prefabName.IsEmpty())
        {
            string asString = prefabName;
            return asString;
        }

        BaseContainer container = prefabData.GetPrefab();
        if (!container)
            return "";
        return container.GetClassName();
    }

    //------------------------------------------------------------------------------------------------
    static RDF_RadarSignature Find(string key)
    {
        EnsureLoaded();
        if (key == "")
            return null;
        return s_ByKey.Get(key);
    }

    //------------------------------------------------------------------------------------------------
    static int GetCount()
    {
        EnsureLoaded();
        return s_ByKey.Count();
    }

    //------------------------------------------------------------------------------------------------
    static string GetStatsLine()
    {
        EnsureLoaded();
        string line = "sig=" + s_ByKey.Count().ToString();
        line = line + " baked=" + s_StatBakedLoaded.ToString();
        line = line + " measured=" + s_StatMeasured.ToString();
        line = line + " hit=" + s_StatHits.ToString();
        line = line + " nokey=" + s_StatNoKey.ToString();
        return line;
    }

    //------------------------------------------------------------------------------------------------
    // One-time bounds measurement for an unknown model.
    protected static RDF_RadarSignature MeasureSignature(
        IEntity entity,
        ERDF_RadarTargetType targetType,
        string key)
    {
        if (!entity)
            return null;

        RDF_RadarSignature sig = new RDF_RadarSignature();
        sig.m_Key = key;
        sig.m_Baked = false;

        vector mins;
        vector maxs;
        entity.GetBounds(mins, maxs);
        vector size = maxs - mins;
        sig.m_SizeX = Math.AbsFloat(size[0]);
        sig.m_SizeY = Math.AbsFloat(size[1]);
        sig.m_SizeZ = Math.AbsFloat(size[2]);

        float longest = sig.m_SizeX;
        if (sig.m_SizeY > longest)
            longest = sig.m_SizeY;
        if (sig.m_SizeZ > longest)
            longest = sig.m_SizeZ;
        if (longest < 0.1)
            longest = 0.1;
        sig.m_CharacteristicLengthM = longest;

        sig.m_MeanRcsM2 = RDF_RadarRcsModel.GetEntityRcsM2(entity, targetType);
        sig.m_SwerlingModel = RDF_RadarRcsModel.GetDefaultSwerlingModel(targetType);
        sig.m_TypeHint = TargetTypeToInt(targetType);

        s_StatMeasured = s_StatMeasured + 1;
        return sig;
    }

    //------------------------------------------------------------------------------------------------
    protected static void EnsureLoaded()
    {
        if (!s_ByKey)
            s_ByKey = new map<string, ref RDF_RadarSignature>();
        if (s_BakedLoadAttempted)
            return;
        s_BakedLoadAttempted = true;
        LoadBakedTable();
    }

    //------------------------------------------------------------------------------------------------
    static bool LoadBakedTable()
    {
        if (!s_ByKey)
            s_ByKey = new map<string, ref RDF_RadarSignature>();
        if (!FileIO.FileExists(SIG_FILE))
            return false;

        array<string> lines = SCR_FileIOHelper.ReadFileContent(SIG_FILE, false);
        if (!lines || lines.Count() < 2)
            return false;
        if (lines.Get(0) != SIG_MAGIC)
        {
            Print("[RDF Radar Sig] magic mismatch: " + SIG_FILE, LogLevel.WARNING);
            return false;
        }

        int loaded = 0;
        for (int i = 1; i < lines.Count(); i++)
        {
            string row = lines.Get(i);
            if (row == "")
                continue;

            array<string> f = new array<string>();
            row.Split(FIELD_SEP, f, true);
            if (f.Count() < 8)
                continue;

            RDF_RadarSignature sig = new RDF_RadarSignature();
            sig.m_Key = f.Get(0);
            sig.m_SizeX = f.Get(1).ToFloat();
            sig.m_SizeY = f.Get(2).ToFloat();
            sig.m_SizeZ = f.Get(3).ToFloat();
            sig.m_CharacteristicLengthM = f.Get(4).ToFloat();
            sig.m_MeanRcsM2 = f.Get(5).ToFloat();
            sig.m_SwerlingModel = f.Get(6).ToInt();
            sig.m_TypeHint = f.Get(7).ToInt();
            sig.m_Baked = true;
            if (sig.m_Key == "")
                continue;
            if (sig.m_CharacteristicLengthM < 0.1)
                sig.m_CharacteristicLengthM = 0.1;

            s_ByKey.Set(sig.m_Key, sig);
            loaded = loaded + 1;
        }

        s_StatBakedLoaded = loaded;
        Print("[RDF Radar Sig] baked table loaded: " + loaded.ToString(), LogLevel.NORMAL);
        return loaded > 0;
    }

    //------------------------------------------------------------------------------------------------
    // Write the merged table (baked + newly measured). Run once, ship the file.
    static bool ExportTable()
    {
        EnsureLoaded();

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory(SIG_DIR);

        array<string> lines = new array<string>();
        lines.Insert(SIG_MAGIC);

        int count = s_ByKey.Count();
        for (int i = 0; i < count; i++)
        {
            RDF_RadarSignature sig = s_ByKey.GetElement(i);
            if (!sig)
                continue;
            if (sig.m_Key == "")
                continue;

            string row = sig.m_Key;
            row = row + FIELD_SEP + sig.m_SizeX.ToString();
            row = row + FIELD_SEP + sig.m_SizeY.ToString();
            row = row + FIELD_SEP + sig.m_SizeZ.ToString();
            row = row + FIELD_SEP + sig.m_CharacteristicLengthM.ToString();
            row = row + FIELD_SEP + sig.m_MeanRcsM2.ToString();
            row = row + FIELD_SEP + sig.m_SwerlingModel.ToString();
            row = row + FIELD_SEP + sig.m_TypeHint.ToString();
            lines.Insert(row);
        }

        bool ok = SCR_FileIOHelper.WriteFileContent(SIG_FILE, lines);
        if (ok)
            Print("[RDF Radar Sig] exported " + (lines.Count() - 1).ToString() + " -> " + SIG_FILE, LogLevel.NORMAL);
        return ok;
    }

    //------------------------------------------------------------------------------------------------
    static void Clear()
    {
        if (s_ByKey)
            s_ByKey.Clear();
        s_BakedLoadAttempted = false;
        s_Dirty = false;
        s_LastExportTime = -1000.0;
        s_StatBakedLoaded = 0;
        s_StatMeasured = 0;
        s_StatHits = 0;
        s_StatNoKey = 0;
    }

    //------------------------------------------------------------------------------------------------
    static int TargetTypeToInt(ERDF_RadarTargetType type)
    {
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 1;
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 2;
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
            return 3;
        return 0;
    }
}
