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
    static const int BAND_VHF = 0;
    static const int BAND_L = 1;
    static const int BAND_S = 2;
    static const int BAND_C = 3;
    static const int BAND_X = 4;
    static const int BAND_COUNT = 5;

    protected static bool s_Loaded;
    protected static string s_Source;
    protected static string s_Band;
    protected static int s_SeaState;
    protected static float s_ThetaRefRad;
    protected static ref array<ref RDF_RadarSurfaceParams> s_ById;
    // Per-band σ⁰_ref (linear), CLASS_COUNT slots each. Overlay JSON/conf
    // replaces only the tagged band; other bands keep builtins.
    protected static ref array<float> s_BandSigma0RefLin;
    // Water σ⁰ dB offset vs sea_state=3 (matches tools/dem _SEA_STATE_WATER_DB).
    protected static const float SEA_STATE_WATER_DB_0 = -8.0;
    protected static const float SEA_STATE_WATER_DB_1 = -4.0;
    protected static const float SEA_STATE_WATER_DB_2 = -1.0;
    protected static const float SEA_STATE_WATER_DB_3 = 0.0;
    protected static const float SEA_STATE_WATER_DB_4 = 2.0;
    protected static const float SEA_STATE_WATER_DB_5 = 4.0;
    protected static const float SEA_STATE_WATER_DB_6 = 6.0;

    //--------------------------------------------------------------------------------------------
    static void EnsureLoaded()
    {
        if (s_Loaded)
            return;
        s_Loaded = true;
        s_ById = new array<ref RDF_RadarSurfaceParams>();
        s_ById.Resize(CLASS_COUNT);
        EnsureBandTables();
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
        Print("[RDF SurfaceTable] using builtins (X overlay, VHF/L/S/C/X σ⁰)", LogLevel.NORMAL);
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
    static string GetLoadedBand()
    {
        EnsureLoaded();
        return s_Band;
    }

    //--------------------------------------------------------------------------------------------
    // Matches tools/dem/rdf_radar_channel.band_for_frequency.
    static string BandNameFromFrequencyHz(float frequencyHz)
    {
        float fGhz = frequencyHz / 1000000000.0;
        if (fGhz < 0.3)
            return "VHF";
        if (fGhz < 2.0)
            return "L";
        if (fGhz < 4.0)
            return "S";
        if (fGhz < 8.0)
            return "C";
        return "X";
    }

    //--------------------------------------------------------------------------------------------
    static int BandIndexFromName(string band)
    {
        if (band.IsEmpty())
            return -1;
        string key = band;
        // Enforce ToUpper mutates in place and returns length (int).
        key.ToUpper();
        if (key == "VHF")
            return BAND_VHF;
        if (key == "L")
            return BAND_L;
        if (key == "S")
            return BAND_S;
        if (key == "C")
            return BAND_C;
        if (key == "X")
            return BAND_X;
        return -1;
    }

    //--------------------------------------------------------------------------------------------
    static int BandIndexFromFrequencyHz(float frequencyHz)
    {
        return BandIndexFromName(BandNameFromFrequencyHz(frequencyHz));
    }

    //--------------------------------------------------------------------------------------------
    // Foliage / volume attenuation vs X-band authorship (0.5 dB/km veg).
    static float GetAttenuationScaleForBand(string band)
    {
        int idx = BandIndexFromName(band);
        if (idx == BAND_VHF)
            return 0.20;
        if (idx == BAND_L)
            return 0.40;
        if (idx == BAND_S)
            return 0.55;
        if (idx == BAND_C)
            return 0.75;
        return 1.0;
    }

    //--------------------------------------------------------------------------------------------
    static int GetSeaState()
    {
        EnsureLoaded();
        return s_SeaState;
    }

    //--------------------------------------------------------------------------------------------
    static float GetThetaRefRad()
    {
        EnsureLoaded();
        return s_ThetaRefRad;
    }

    //--------------------------------------------------------------------------------------------
    // Linear multiplier for water σ⁰ at the loaded sea_state (ss3 → 1.0).
    static float GetSeaStateWaterScale()
    {
        EnsureLoaded();
        float db = SeaStateWaterOffsetDb(s_SeaState);
        return DbToLin(db);
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
        EnsureLoaded();
        return GetSigma0RefLin(surfaceClass, s_Band);
    }

    //--------------------------------------------------------------------------------------------
    static float GetSigma0RefLin(int surfaceClass, string band)
    {
        EnsureLoaded();
        int classId = surfaceClass;
        if (classId < 0 || classId >= CLASS_COUNT)
            classId = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;

        int bandIdx = BandIndexFromName(band);
        if (bandIdx < 0)
            bandIdx = BandIndexFromName(s_Band);
        if (bandIdx < 0)
            bandIdx = BAND_X;

        float refLin = 0.0;
        int slot = bandIdx * CLASS_COUNT + classId;
        if (s_BandSigma0RefLin && slot < s_BandSigma0RefLin.Count())
            refLin = s_BandSigma0RefLin.Get(slot);
        if (refLin <= 0.0)
            refLin = DbToLin(-18.0);

        // Table water entries are authored at sea_state=3 reference; scale live.
        if (classId == ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER)
            refLin = refLin * GetSeaStateWaterScale();
        return refLin;
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
    static float GetAttenuationDbPerKm(int surfaceClass, string band)
    {
        float baseAtt = GetAttenuationDbPerKm(surfaceClass);
        if (band.IsEmpty())
            return baseAtt;
        return baseAtt * GetAttenuationScaleForBand(band);
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
        s_Band.ToUpper();
        s_SeaState = ClampSeaState(conf.m_iSeaState);
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
        s_Band.ToUpper();
        s_SeaState = ClampSeaState(doc.sea_state);
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

        int overlayBand = BandIndexFromName(s_Band);
        if (overlayBand < 0)
            overlayBand = BAND_X;
        SetBandSigma0Db(overlayBand, id, sigma0RefDb);
    }

    //--------------------------------------------------------------------------------------------
    protected static int ClampSeaState(int seaState)
    {
        if (seaState < 0)
            return 0;
        if (seaState > 6)
            return 6;
        return seaState;
    }

    //--------------------------------------------------------------------------------------------
    protected static float SeaStateWaterOffsetDb(int seaState)
    {
        int ss = ClampSeaState(seaState);
        if (ss == 0)
            return SEA_STATE_WATER_DB_0;
        if (ss == 1)
            return SEA_STATE_WATER_DB_1;
        if (ss == 2)
            return SEA_STATE_WATER_DB_2;
        if (ss == 3)
            return SEA_STATE_WATER_DB_3;
        if (ss == 4)
            return SEA_STATE_WATER_DB_4;
        if (ss == 5)
            return SEA_STATE_WATER_DB_5;
        return SEA_STATE_WATER_DB_6;
    }

    //--------------------------------------------------------------------------------------------
    protected static void EnsureBandTables()
    {
        if (!s_BandSigma0RefLin)
            s_BandSigma0RefLin = new array<float>();
        int n = BAND_COUNT * CLASS_COUNT;
        if (s_BandSigma0RefLin.Count() != n)
            s_BandSigma0RefLin.Resize(n);
    }

    //--------------------------------------------------------------------------------------------
    protected static void SetBandSigma0Db(int bandIdx, int classId, float db)
    {
        if (bandIdx < 0 || bandIdx >= BAND_COUNT)
            return;
        if (classId < 0 || classId >= CLASS_COUNT)
            return;
        EnsureBandTables();
        s_BandSigma0RefLin.Set(bandIdx * CLASS_COUNT + classId, DbToLin(db));
    }

    //--------------------------------------------------------------------------------------------
    // σ⁰_ref dB at 30° grazing. Matches tools/dem/rdf_radar_materials._BAND_SIGMA0_DB.
    protected static void InstallBandSigma0Row(
        int bandIdx,
        float unknownDb,
        float waterDb,
        float vegetationDb,
        float soilDb,
        float sandDb,
        float gravelDb,
        float asphaltDb,
        float hardDb,
        float woodDb,
        float metalDb,
        float snowIceDb,
        float fabricDb)
    {
        SetBandSigma0Db(bandIdx, 0, unknownDb);
        SetBandSigma0Db(bandIdx, 1, waterDb);
        SetBandSigma0Db(bandIdx, 2, vegetationDb);
        SetBandSigma0Db(bandIdx, 3, soilDb);
        SetBandSigma0Db(bandIdx, 4, sandDb);
        SetBandSigma0Db(bandIdx, 5, gravelDb);
        SetBandSigma0Db(bandIdx, 6, asphaltDb);
        SetBandSigma0Db(bandIdx, 7, hardDb);
        SetBandSigma0Db(bandIdx, 8, woodDb);
        SetBandSigma0Db(bandIdx, 9, metalDb);
        SetBandSigma0Db(bandIdx, 10, snowIceDb);
        SetBandSigma0Db(bandIdx, 11, fabricDb);
    }

    //--------------------------------------------------------------------------------------------
    protected static void InstallAllBandSigma0()
    {
        InstallBandSigma0Row(
            BAND_VHF, -24.0, -30.0, -10.0, -24.0, -26.0, -22.0, -18.0, -16.0, -14.0, -8.0, -20.0, -28.0);
        InstallBandSigma0Row(
            BAND_L, -22.0, -28.0, -12.0, -22.0, -24.0, -20.0, -16.0, -14.0, -22.0, -7.0, -18.0, -28.0);
        InstallBandSigma0Row(
            BAND_S, -20.0, -26.0, -16.0, -20.0, -22.0, -18.0, -14.0, -12.0, -20.0, -6.0, -22.0, -26.0);
        InstallBandSigma0Row(
            BAND_C, -19.0, -24.0, -15.0, -19.0, -21.0, -17.0, -13.0, -11.0, -19.0, -5.5, -21.0, -25.0);
        InstallBandSigma0Row(
            BAND_X, -18.0, -22.0, -14.0, -18.0, -20.0, -16.0, -12.0, -10.0, -18.0, -5.0, -20.0, -24.0);
    }

    //--------------------------------------------------------------------------------------------
    protected static void InstallBuiltins()
    {
        s_Band = "X";
        s_SeaState = 3;
        s_ThetaRefRad = 0.523598775598;
        InstallAllBandSigma0();
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
