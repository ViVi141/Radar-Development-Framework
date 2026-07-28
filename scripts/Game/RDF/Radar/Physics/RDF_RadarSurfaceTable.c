// Offline EM parameter table for ERDF_DemSurfaceClass.
// Runtime: profile JSON → packaged .conf → packaged JSON → builtins.
class RDF_RadarSurfaceEntry : JsonApiStruct
{
    int id;
    string name;
    float sigma0_ref_db;
    float gamma_k;
    float clutter_scale;
    float dielectric;
    float roughness;
    float attenuation_db_per_km;

    void RDF_RadarSurfaceEntry()
    {
        RegV("id");
        RegV("name");
        RegV("sigma0_ref_db");
        RegV("gamma_k");
        RegV("clutter_scale");
        RegV("dielectric");
        RegV("roughness");
        RegV("attenuation_db_per_km");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_RadarSurfaceTableDoc : JsonApiStruct
{
    string magic;
    int version;
    string band;
    int sea_state;
    float theta_ref_deg;
    ref array<ref RDF_RadarSurfaceEntry> entries;

    void RDF_RadarSurfaceTableDoc()
    {
        RegV("magic");
        RegV("version");
        RegV("band");
        RegV("sea_state");
        RegV("theta_ref_deg");
        RegV("entries");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_RadarSurfaceParams
{
    int m_Id;
    string m_Name;
    float m_Sigma0RefLin;
    float m_GammaK;
    float m_ClutterScale;
    float m_Dielectric;
    float m_Roughness;
    float m_AttenuationDbPerKm;
}

//------------------------------------------------------------------------------------------------
class RDF_RadarSurfaceTable
{
    static const string MAGIC = "RDF_SURF_TABLE_V1";
    static const string PROFILE_PATH = "$profile:RDF/RadarData/SurfaceTable.json";
    static const string PACKAGED_JSON_PATH = "RadarData/SurfaceTable.json";
    static const ResourceName PACKAGED_CONF =
        "{B4E81F2A7C903D65}RadarData/SurfaceTable.conf";
    static const int CLASS_COUNT = 12;

    protected static bool s_Loaded;
    protected static string s_Source;
    protected static string s_Band;
    protected static float s_ThetaRefRad;
    protected static ref array<ref RDF_RadarSurfaceParams> s_ById;

    //--------------------------------------------------------------------------------------------
    static void EnsureLoaded()
    {
        if (s_Loaded)
            return;
        s_Loaded = true;
        s_ById = new array<ref RDF_RadarSurfaceParams>();
        s_ById.Resize(CLASS_COUNT);
        InstallBuiltins();

        // 1) Profile JSON — local calibration without re-Register
        if (TryLoadJsonFile(PROFILE_PATH))
        {
            s_Source = "profile:" + PROFILE_PATH;
            Print("[RDF SurfaceTable] loaded " + s_Source + " band=" + s_Band, LogLevel.NORMAL);
            return;
        }

        // 2) Packaged Enfusion .conf (workshop preferred)
        if (TryLoadConf(PACKAGED_CONF))
        {
            s_Source = "conf:" + PACKAGED_CONF;
            Print("[RDF SurfaceTable] loaded " + s_Source + " band=" + s_Band, LogLevel.NORMAL);
            return;
        }

        // 3) Packaged JSON fallback
        if (TryLoadJsonFile(PACKAGED_JSON_PATH))
        {
            s_Source = "packaged:" + PACKAGED_JSON_PATH;
            Print("[RDF SurfaceTable] loaded " + s_Source + " band=" + s_Band, LogLevel.NORMAL);
            return;
        }

        s_Source = "builtin";
        Print("[RDF SurfaceTable] using builtins (X-band)", LogLevel.NORMAL);
    }

    //--------------------------------------------------------------------------------------------
    static void Reload()
    {
        s_Loaded = false;
        EnsureLoaded();
    }

    //--------------------------------------------------------------------------------------------
    static string GetSource()
    {
        EnsureLoaded();
        return s_Source;
    }

    //--------------------------------------------------------------------------------------------
    static float GetThetaRefRad()
    {
        EnsureLoaded();
        return s_ThetaRefRad;
    }

    //--------------------------------------------------------------------------------------------
    static RDF_RadarSurfaceParams GetParams(int surfaceClass)
    {
        EnsureLoaded();
        int id = surfaceClass;
        if (id < 0 || id >= CLASS_COUNT)
            id = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        RDF_RadarSurfaceParams p = s_ById.Get(id);
        if (!p)
            return s_ById.Get(ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN);
        return p;
    }

    //--------------------------------------------------------------------------------------------
    static float GetSigma0RefLin(int surfaceClass)
    {
        RDF_RadarSurfaceParams p = GetParams(surfaceClass);
        if (!p)
            return DbToLin(-18.0);
        return p.m_Sigma0RefLin;
    }

    //--------------------------------------------------------------------------------------------
    static float GetGammaK(int surfaceClass)
    {
        RDF_RadarSurfaceParams p = GetParams(surfaceClass);
        if (!p)
            return 1.0;
        return p.m_GammaK;
    }

    //--------------------------------------------------------------------------------------------
    static float GetClutterScale(int surfaceClass)
    {
        RDF_RadarSurfaceParams p = GetParams(surfaceClass);
        if (!p)
            return 1.0;
        return p.m_ClutterScale;
    }

    //--------------------------------------------------------------------------------------------
    static float GetDielectric(int surfaceClass)
    {
        RDF_RadarSurfaceParams p = GetParams(surfaceClass);
        if (!p)
            return 10.0;
        return p.m_Dielectric;
    }

    //--------------------------------------------------------------------------------------------
    static float GetRoughness(int surfaceClass)
    {
        RDF_RadarSurfaceParams p = GetParams(surfaceClass);
        if (!p)
            return 0.08;
        return p.m_Roughness;
    }

    //--------------------------------------------------------------------------------------------
    static float GetAttenuationDbPerKm(int surfaceClass)
    {
        RDF_RadarSurfaceParams p = GetParams(surfaceClass);
        if (!p)
            return 0.0;
        return p.m_AttenuationDbPerKm;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool TryLoadConf(ResourceName path)
    {
        if (path.IsEmpty())
            return false;

        RDF_RadarSurfaceTableConf conf =
            SCR_ConfigHelperT<RDF_RadarSurfaceTableConf>.GetConfigObject(path);
        if (!conf)
            return false;
        if (!conf.m_aEntries || conf.m_aEntries.Count() < 1)
            return false;

        s_Band = conf.m_sBand;
        if (s_Band.IsEmpty())
            s_Band = "X";
        s_ThetaRefRad = conf.m_fThetaRefDeg * 0.017453292519943295;
        if (s_ThetaRefRad <= 0.0)
            s_ThetaRefRad = 0.523598775598;

        int n = conf.m_aEntries.Count();
        for (int i = 0; i < n; i++)
        {
            RDF_RadarSurfaceEntryConf e = conf.m_aEntries.Get(i);
            if (!e)
                continue;
            ApplyEntry(
                e.m_iId,
                e.m_sName,
                e.m_fSigma0RefDb,
                e.m_fGammaK,
                e.m_fClutterScale,
                e.m_fDielectric,
                e.m_fRoughness,
                e.m_fAttenuationDbPerKm);
        }
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool TryLoadJsonFile(string path)
    {
        if (!FileIO.FileExists(path))
            return false;

        RDF_RadarSurfaceTableDoc doc = new RDF_RadarSurfaceTableDoc();
        if (!doc.LoadFromFile(path))
            return false;
        if (doc.magic != MAGIC)
            return false;
        if (!doc.entries || doc.entries.Count() < 1)
            return false;

        s_Band = doc.band;
        if (s_Band.IsEmpty())
            s_Band = "X";
        s_ThetaRefRad = doc.theta_ref_deg * 0.017453292519943295;
        if (s_ThetaRefRad <= 0.0)
            s_ThetaRefRad = 0.523598775598;

        int n = doc.entries.Count();
        for (int i = 0; i < n; i++)
        {
            RDF_RadarSurfaceEntry e = doc.entries.Get(i);
            if (!e)
                continue;
            ApplyEntry(
                e.id,
                e.name,
                e.sigma0_ref_db,
                e.gamma_k,
                e.clutter_scale,
                e.dielectric,
                e.roughness,
                e.attenuation_db_per_km);
        }
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static void ApplyEntry(
        int id,
        string name,
        float sigma0RefDb,
        float gammaK,
        float clutterScale,
        float dielectric,
        float roughness,
        float attenuationDbPerKm)
    {
        if (id < 0 || id >= CLASS_COUNT)
            return;

        RDF_RadarSurfaceParams p = new RDF_RadarSurfaceParams();
        p.m_Id = id;
        p.m_Name = name;
        p.m_Sigma0RefLin = DbToLin(sigma0RefDb);
        p.m_GammaK = gammaK;
        if (p.m_GammaK <= 0.0)
            p.m_GammaK = 1.0;
        p.m_ClutterScale = clutterScale;
        if (p.m_ClutterScale <= 0.0)
            p.m_ClutterScale = 1.0;
        p.m_Dielectric = dielectric;
        if (p.m_Dielectric < 1.0)
            p.m_Dielectric = 1.0;
        p.m_Roughness = roughness;
        if (p.m_Roughness < 0.0)
            p.m_Roughness = 0.0;
        p.m_AttenuationDbPerKm = attenuationDbPerKm;
        if (p.m_AttenuationDbPerKm < 0.0)
            p.m_AttenuationDbPerKm = 0.0;
        s_ById.Set(id, p);
    }

    //--------------------------------------------------------------------------------------------
    protected static void InstallBuiltins()
    {
        s_Band = "X";
        s_ThetaRefRad = 0.523598775598;
        SetBuiltin(0, "unknown", -18.0, 1.0, 1.0, 10.0, 0.08, 0.0);
        SetBuiltin(1, "water", -22.0, 1.4, 1.0, 80.0, 0.02, 0.0);
        SetBuiltin(2, "vegetation", -14.0, 0.8, 1.0, 15.0, 0.3, 0.5);
        SetBuiltin(3, "soil", -18.0, 1.0, 1.0, 10.0, 0.08, 0.0);
        SetBuiltin(4, "sand", -20.0, 1.1, 1.0, 4.0, 0.05, 0.0);
        SetBuiltin(5, "gravel", -16.0, 1.0, 1.0, 6.0, 0.12, 0.0);
        SetBuiltin(6, "asphalt", -12.0, 1.0, 1.0, 5.0, 0.02, 0.0);
        SetBuiltin(7, "hard", -10.0, 1.0, 1.0, 6.0, 0.03, 0.0);
        SetBuiltin(8, "wood", -18.0, 1.0, 1.0, 3.0, 0.1, 0.2);
        SetBuiltin(9, "metal", -5.0, 0.5, 1.0, 1000.0, 0.01, 0.0);
        SetBuiltin(10, "snow_ice", -20.0, 1.0, 1.0, 3.0, 0.04, 0.0);
        SetBuiltin(11, "fabric", -24.0, 1.0, 1.0, 2.0, 0.15, 0.0);
    }

    //--------------------------------------------------------------------------------------------
    protected static void SetBuiltin(
        int id,
        string name,
        float sigma0RefDb,
        float gammaK,
        float clutterScale,
        float dielectric,
        float roughness,
        float attenuationDbPerKm)
    {
        ApplyEntry(
            id,
            name,
            sigma0RefDb,
            gammaK,
            clutterScale,
            dielectric,
            roughness,
            attenuationDbPerKm);
    }

    //--------------------------------------------------------------------------------------------
    protected static float DbToLin(float db)
    {
        return Math.Pow(10.0, db / 10.0);
    }
}
