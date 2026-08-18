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
    // Rotor / micro-Doppler (0 tip speed = fuselage line only).
    float m_RotorTipSpeedMs;
    int m_BladeCount;
    float m_RotorRcsFraction;
    float m_HubWidthMs;
    float m_FanTipSpeedMs;
    int m_FanBladeCount;
    float m_FanRcsFraction;
    // True when loaded from the baked table (not measured this session).
    bool m_Baked;
}

// Signature table. Load / bake order:
//   1. Offline Workbench bake (RDF Bake Radar Signatures) enumerates PLACEABLE
//      Vehicle/Character prefabs (+ projectiles) from ResourceDatabase — not the
//      live world — measures each once, writes SIG_FILE
//   2. Runtime: Resolve() is a table lookup; unknown models (e.g. third-party mods)
//      are measured on first sighting and optionally merged back via MaybeAutoExport
class RDF_RadarSignatureLibrary
{
    static const string SIG_DIR = "$profile:RDF/Signatures";
    static const string SIG_FILE = "$profile:RDF/Signatures/rdf_radar_signatures.csv";
    static const ResourceName SIG_CONF_PACKAGED =
        "{C8A3F15E902B47D1}Signatures/rdf_radar_signatures.conf";
    static const string SIG_MAGIC = "RDF_RADAR_SIG_V2";
    static const string SIG_HEADER = "key,size_x_m,size_y_m,size_z_m,char_length_m,mean_rcs_m2,swerling,type_hint,rotor_tip_ms,blade_count,rotor_rcs_frac,hub_width_ms";
    static const string FIELD_SEP = ",";

    // UH-1 / Mi-8 class defaults when conf omits rotor columns.
    static const float DEFAULT_HELI_TIP_MS = 220.0;
    static const int DEFAULT_HELI_BLADES = 2;
    static const float DEFAULT_HELI_ROTOR_FRAC = 0.35;
    static const float DEFAULT_HELI_HUB_MS = 40.0;
    // Explicit family table (used when tip==0); Mi-8 tip slightly higher / 5 blades.
    static const float MI8_HELI_TIP_MS = 230.0;
    static const int MI8_HELI_BLADES = 5;
    static const float MI8_HELI_ROTOR_FRAC = 0.40;
    static const float MI8_HELI_HUB_MS = 45.0;

    // Newly measured unknown models (runtime) flushed at most this often.
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
    static void EnsureLoadedPublic()
    {
        EnsureLoaded();
    }

    //------------------------------------------------------------------------------------------------
    // Resolve the signature for an entity, measuring only on first sighting of an unknown model.
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
            Print("[RDF Radar Sig] first-sight measure: " + key, LogLevel.NORMAL);
        }
        return measured;
    }

    //------------------------------------------------------------------------------------------------
    // Offline bake path: measure a temporarily spawned prefab and upsert as baked.
    static RDF_RadarSignature BakeFromSpawned(
        IEntity entity,
        string key,
        ERDF_RadarTargetType targetType)
    {
        EnsureLoaded();
        if (!entity || key == "")
            return null;

        RDF_RadarSignature sig = MeasureSignature(entity, targetType, key);
        if (!sig)
            return null;

        sig.m_Baked = true;
        s_ByKey.Set(key, sig);
        s_Dirty = true;
        return sig;
    }

    //------------------------------------------------------------------------------------------------
    // Persist runtime-discovered unknown models. Skip empty tables so a bad bake
    // cannot wipe a previously good CSV.
    static void MaybeAutoExport(float now)
    {
        if (!s_Dirty)
            return;
        if (s_ByKey.Count() == 0)
        {
            s_Dirty = false;
            return;
        }
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
    // Human-readable model name from an entity's signature key
    // (e.g. "{GUID}Prefabs/.../SP01_Mi8MT_unarmed_transport.et" -> "SP01_Mi8MT_unarmed_transport").
    static string FormatDisplayName(IEntity entity)
    {
        return FormatDisplayNameFromKey(MakeKey(entity));
    }

    //------------------------------------------------------------------------------------------------
    static string FormatDisplayNameFromKey(string key)
    {
        if (key == "")
            return "Unknown";

        string s = key;
        s.Replace("\\", "/");

        int brace = s.IndexOf("}");
        if (brace >= 0)
            s = s.Substring(brace + 1, s.Length() - brace - 1);

        int slash = s.LastIndexOf("/");
        if (slash >= 0)
            s = s.Substring(slash + 1, s.Length() - slash - 1);

        int dot = s.LastIndexOf(".");
        if (dot > 0)
            s = s.Substring(0, dot);

        if (s == "")
            return "Unknown";
        return s;
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
    // One-time bounds measurement for an unknown model (or offline bake spawn).
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

        sig.m_MeanRcsM2 = RDF_RadarRcsModel.EstimateRcsFromExtents(
            sig.m_SizeX,
            sig.m_SizeY,
            sig.m_SizeZ,
            targetType);
        sig.m_SwerlingModel = RDF_RadarRcsModel.GetDefaultSwerlingModel(targetType);
        sig.m_TypeHint = TargetTypeToInt(targetType);
        MaybeApplyHelicopterRotorDefaults(sig);
        MaybeApplyJetFanDefaults(sig);

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

        // 1) Packaged Enfusion .conf (workshop)
        if (LoadBakedConf(SIG_CONF_PACKAGED))
            return true;

        // 2) Profile CSV (dev bake)
        if (LoadBakedCsv(SIG_FILE))
            return true;

        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool LoadBakedConf(ResourceName path)
    {
        if (path.IsEmpty())
            return false;

        RDF_RadarSignatureTableConf conf =
            SCR_ConfigHelperT<RDF_RadarSignatureTableConf>.GetConfigObject(path);
        if (!conf)
            return false;
        if (!conf.m_aEntries || conf.m_aEntries.Count() < 1)
            return false;

        int loaded = 0;
        int n = conf.m_aEntries.Count();
        for (int i = 0; i < n; i++)
        {
            RDF_RadarSignatureEntryConf e = conf.m_aEntries.Get(i);
            if (!e)
                continue;
            if (e.m_sKey == "")
                continue;

            RDF_RadarSignature sig = new RDF_RadarSignature();
            sig.m_Key = e.m_sKey;
            sig.m_SizeX = e.m_fSizeX;
            sig.m_SizeY = e.m_fSizeY;
            sig.m_SizeZ = e.m_fSizeZ;
            sig.m_CharacteristicLengthM = e.m_fCharLengthM;
            sig.m_MeanRcsM2 = e.m_fMeanRcsM2;
            sig.m_SwerlingModel = e.m_iSwerling;
            sig.m_TypeHint = e.m_iTypeHint;
            sig.m_RotorTipSpeedMs = e.m_fRotorTipSpeedMs;
            sig.m_BladeCount = e.m_iBladeCount;
            sig.m_RotorRcsFraction = e.m_fRotorRcsFraction;
            sig.m_HubWidthMs = e.m_fHubWidthMs;
            sig.m_FanTipSpeedMs = e.m_fFanTipSpeedMs;
            sig.m_FanBladeCount = e.m_iFanBladeCount;
            sig.m_FanRcsFraction = e.m_fFanRcsFraction;
            sig.m_Baked = true;
            if (sig.m_CharacteristicLengthM < 0.1)
                sig.m_CharacteristicLengthM = 0.1;
            MaybeApplyHelicopterRotorDefaults(sig);
            MaybeApplyJetFanDefaults(sig);

            s_ByKey.Set(sig.m_Key, sig);
            loaded = loaded + 1;
        }

        s_StatBakedLoaded = loaded;
        Print("[RDF Radar Sig] baked conf loaded: " + loaded.ToString()
            + " from " + path, LogLevel.NORMAL);
        return loaded > 0;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool LoadBakedCsv(string path)
    {
        if (!FileIO.FileExists(path))
            return false;

        array<string> lines = SCR_FileIOHelper.ReadFileContent(path, false);
        if (!lines || lines.Count() < 3)
            return false;
        if (lines.Get(0) != SIG_MAGIC)
        {
            Print("[RDF Radar Sig] magic mismatch: " + path, LogLevel.WARNING);
            return false;
        }
        string header = lines.Get(1);
        if (header != SIG_HEADER)
        {
            Print("[RDF Radar Sig] header mismatch: " + path, LogLevel.WARNING);
            return false;
        }

        int loaded = 0;
        for (int i = 2; i < lines.Count(); i++)
        {
            string row = lines.Get(i);
            if (row == "")
                continue;

            array<string> f = new array<string>();
            if (!ParseCsvRow(row, f))
                continue;
            if (f.Count() < 12)
                continue;

            RDF_RadarSignature sig = new RDF_RadarSignature();
            sig.m_Key = UnquoteField(f.Get(0));
            sig.m_SizeX = f.Get(1).ToFloat();
            sig.m_SizeY = f.Get(2).ToFloat();
            sig.m_SizeZ = f.Get(3).ToFloat();
            sig.m_CharacteristicLengthM = f.Get(4).ToFloat();
            sig.m_MeanRcsM2 = f.Get(5).ToFloat();
            sig.m_SwerlingModel = f.Get(6).ToInt();
            sig.m_TypeHint = f.Get(7).ToInt();
            sig.m_RotorTipSpeedMs = f.Get(8).ToFloat();
            sig.m_BladeCount = f.Get(9).ToInt();
            sig.m_RotorRcsFraction = f.Get(10).ToFloat();
            sig.m_HubWidthMs = f.Get(11).ToFloat();
            if (f.Count() >= 15)
            {
                sig.m_FanTipSpeedMs = f.Get(12).ToFloat();
                sig.m_FanBladeCount = f.Get(13).ToInt();
                sig.m_FanRcsFraction = f.Get(14).ToFloat();
            }
            sig.m_Baked = true;
            if (sig.m_Key == "")
                continue;
            if (sig.m_CharacteristicLengthM < 0.1)
                sig.m_CharacteristicLengthM = 0.1;
            MaybeApplyHelicopterRotorDefaults(sig);
            MaybeApplyJetFanDefaults(sig);

            s_ByKey.Set(sig.m_Key, sig);
            loaded = loaded + 1;
        }

        s_StatBakedLoaded = loaded;
        Print("[RDF Radar Sig] baked table loaded: " + loaded.ToString(), LogLevel.NORMAL);
        return loaded > 0;
    }

    //------------------------------------------------------------------------------------------------
    // Write CSV: magic, header, then quoted-key rows. Refuses empty overwrite.
    static bool ExportTable()
    {
        EnsureLoaded();

        int writable = 0;
        int count = s_ByKey.Count();
        for (int i = 0; i < count; i++)
        {
            RDF_RadarSignature probe = s_ByKey.GetElement(i);
            if (probe && probe.m_Key != "")
                writable = writable + 1;
        }

        if (writable == 0)
        {
            if (FileIO.FileExists(SIG_FILE))
            {
                Print("[RDF Radar Sig] refuse empty export; keeping " + SIG_FILE, LogLevel.WARNING);
                return false;
            }
            Print("[RDF Radar Sig] nothing to export.", LogLevel.WARNING);
            return false;
        }

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory(SIG_DIR);

        array<string> lines = new array<string>();
        lines.Insert(SIG_MAGIC);
        lines.Insert(SIG_HEADER);

        for (int i = 0; i < count; i++)
        {
            RDF_RadarSignature sig = s_ByKey.GetElement(i);
            if (!sig)
                continue;
            if (sig.m_Key == "")
                continue;

            string row = QuoteField(sig.m_Key);
            row = row + FIELD_SEP + sig.m_SizeX.ToString();
            row = row + FIELD_SEP + sig.m_SizeY.ToString();
            row = row + FIELD_SEP + sig.m_SizeZ.ToString();
            row = row + FIELD_SEP + sig.m_CharacteristicLengthM.ToString();
            row = row + FIELD_SEP + sig.m_MeanRcsM2.ToString();
            row = row + FIELD_SEP + sig.m_SwerlingModel.ToString();
            row = row + FIELD_SEP + sig.m_TypeHint.ToString();
            row = row + FIELD_SEP + sig.m_RotorTipSpeedMs.ToString();
            row = row + FIELD_SEP + sig.m_BladeCount.ToString();
            row = row + FIELD_SEP + sig.m_RotorRcsFraction.ToString();
            row = row + FIELD_SEP + sig.m_HubWidthMs.ToString();
            lines.Insert(row);
        }

        bool ok = SCR_FileIOHelper.WriteFileContent(SIG_FILE, lines);
        if (ok)
            Print("[RDF Radar Sig] exported " + (lines.Count() - 2).ToString() + " -> " + SIG_FILE, LogLevel.NORMAL);
        return ok;
    }

    //------------------------------------------------------------------------------------------------
    // Minimal RFC4180-ish row parser: supports "quoted,fields" and "".
    protected static bool ParseCsvRow(string row, notnull array<string> fields)
    {
        fields.Clear();
        if (row == "")
            return false;

        string current = "";
        bool inQuotes = false;
        int len = row.Length();
        for (int i = 0; i < len; i++)
        {
            string ch = row.Get(i);
            if (inQuotes)
            {
                if (ch == "\"")
                {
                    if (i + 1 < len && row.Get(i + 1) == "\"")
                    {
                        current = current + "\"";
                        i = i + 1;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    current = current + ch;
                }
            }
            else
            {
                if (ch == "\"")
                {
                    inQuotes = true;
                }
                else if (ch == FIELD_SEP)
                {
                    fields.Insert(current);
                    current = "";
                }
                else
                {
                    current = current + ch;
                }
            }
        }

        fields.Insert(current);
        return fields.Count() > 0;
    }

    //------------------------------------------------------------------------------------------------
    protected static string QuoteField(string value)
    {
        string escaped = value;
        escaped.Replace("\"", "\"\"");
        return "\"" + escaped + "\"";
    }

    //------------------------------------------------------------------------------------------------
    protected static string UnquoteField(string value)
    {
        if (value == "")
            return "";
        if (value.Length() < 2)
            return value;
        if (value.Get(0) != "\"")
            return value;
        if (value.Get(value.Length() - 1) != "\"")
            return value;

        string inner = value.Substring(1, value.Length() - 2);
        inner.Replace("\"\"", "\"");
        return inner;
    }

    //------------------------------------------------------------------------------------------------
    static void Clear()
    {
        if (s_ByKey)
            s_ByKey.Clear();
        else
            s_ByKey = new map<string, ref RDF_RadarSignature>();
        // Keep loaded state so the next Upsert/Export does not re-read the old file.
        s_BakedLoadAttempted = true;
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

    //------------------------------------------------------------------------------------------------
    // Prefab path under Helicopters/ with no rotor columns → family table / UH-1-class defaults.
    protected static void MaybeApplyHelicopterRotorDefaults(RDF_RadarSignature sig)
    {
        if (!sig)
            return;
        if (sig.m_RotorTipSpeedMs > 0.0)
            return;
        if (sig.m_Key == "")
            return;

        string key = sig.m_Key;
        bool isHeli = false;
        if (key.IndexOf("Helicopters/") >= 0)
            isHeli = true;
        if (!isHeli && key.IndexOf("UH1H") >= 0)
            isHeli = true;
        if (!isHeli && key.IndexOf("Mi8") >= 0)
            isHeli = true;
        if (!isHeli && key.IndexOf("SP01_Mi8") >= 0)
            isHeli = true;
        if (!isHeli && key.IndexOf("Cinematic_Flying_Mi8") >= 0)
            isHeli = true;
        if (!isHeli)
            return;

        // Skip rotor/cockpit parts — only airframe prefabs.
        if (key.IndexOf("VehParts/") >= 0)
            return;
        if (key.IndexOf("rotor_") >= 0)
            return;
        if (key.IndexOf("cockpit") >= 0)
            return;

        // Explicit family values (also mirrored into packaged .conf by patch tool).
        if (key.IndexOf("Mi8") >= 0 || key.IndexOf("SP01_Mi8") >= 0)
        {
            sig.m_RotorTipSpeedMs = MI8_HELI_TIP_MS;
            sig.m_BladeCount = MI8_HELI_BLADES;
            sig.m_RotorRcsFraction = MI8_HELI_ROTOR_FRAC;
            sig.m_HubWidthMs = MI8_HELI_HUB_MS;
            return;
        }

        sig.m_RotorTipSpeedMs = DEFAULT_HELI_TIP_MS;
        sig.m_BladeCount = DEFAULT_HELI_BLADES;
        sig.m_RotorRcsFraction = DEFAULT_HELI_ROTOR_FRAC;
        sig.m_HubWidthMs = DEFAULT_HELI_HUB_MS;
    }

    //------------------------------------------------------------------------------------------------
    // Weak JEM sidebands for fixed-wing / jet prefabs (not helicopters).
    protected static void MaybeApplyJetFanDefaults(RDF_RadarSignature sig)
    {
        if (!sig)
            return;
        if (sig.m_FanTipSpeedMs > 0.0)
            return;
        if (sig.m_RotorTipSpeedMs > 0.0)
            return;
        if (sig.m_Key == "")
            return;

        string key = sig.m_Key;
        bool isJet = false;
        if (key.IndexOf("Aircraft/") >= 0)
            isJet = true;
        if (!isJet && key.IndexOf("Airplanes/") >= 0)
            isJet = true;
        if (!isJet && key.IndexOf("Fighter") >= 0)
            isJet = true;
        if (!isJet && key.IndexOf("/Jet") >= 0)
            isJet = true;
        if (!isJet)
            return;
        if (key.IndexOf("Helicopters/") >= 0)
            return;
        if (key.IndexOf("VehParts/") >= 0)
            return;

        sig.m_FanTipSpeedMs = 320.0;
        sig.m_FanBladeCount = 17;
        sig.m_FanRcsFraction = 0.07;
    }
}
