// Fixed-site polar (az × range) terrain path-factor LUT.
// Offline bake: tools/dem/rdf_radar_pattern_site_validate.py
// Profile: $profile:RDF/RadarData/SitePathLut.json
class RDF_RadarSitePathLutDoc : JsonApiStruct
{
    string schema;
    string world;
    float origin_x;
    float origin_y;
    float origin_z;
    float max_range_m;
    int az_count;
    int range_count;
    float wavelength_m;
    ref array<float> factors;

    void RDF_RadarSitePathLutDoc()
    {
        RegV("schema");
        RegV("world");
        RegV("origin_x");
        RegV("origin_y");
        RegV("origin_z");
        RegV("max_range_m");
        RegV("az_count");
        RegV("range_count");
        RegV("wavelength_m");
        RegV("factors");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_RadarSitePathLut
{
    static const string SCHEMA = "RDF_SITE_PATH_LUT_V1";
    static const string PROFILE_PATH = "$profile:RDF/RadarData/SitePathLut.json";

    protected static bool s_Loaded;
    protected static bool s_Enabled;
    protected static float s_OriginX;
    protected static float s_OriginY;
    protected static float s_OriginZ;
    protected static float s_MaxRangeM;
    protected static int s_AzCount;
    protected static int s_RangeCount;
    protected static ref array<float> s_Factors;

    //--------------------------------------------------------------------------------------------
    static void SetEnabled(bool enabled)
    {
        s_Enabled = enabled;
    }

    //--------------------------------------------------------------------------------------------
    static bool IsReady()
    {
        if (!s_Enabled)
            return false;
        EnsureLoaded();
        if (!s_Loaded)
            return false;
        if (!s_Factors)
            return false;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    static void EnsureLoaded()
    {
        if (s_Loaded)
            return;
        s_Loaded = true;
        if (!TryLoadProfile())
        {
            s_Factors = null;
            s_AzCount = 0;
            s_RangeCount = 0;
        }
    }

    //--------------------------------------------------------------------------------------------
    // Force reload (e.g. after copying a new bake into profile).
    static void Reload()
    {
        s_Loaded = false;
        s_Factors = null;
        EnsureLoaded();
    }

    //--------------------------------------------------------------------------------------------
    static bool OriginMatches(vector radarOrigin, float maxDriftM)
    {
        if (!IsReady())
            return false;
        float dx = radarOrigin[0] - s_OriginX;
        float dy = radarOrigin[1] - s_OriginY;
        float dz = radarOrigin[2] - s_OriginZ;
        float d2 = dx * dx + dy * dy + dz * dz;
        float lim = maxDriftM;
        if (lim < 0.5)
            lim = 0.5;
        return d2 <= lim * lim;
    }

    //--------------------------------------------------------------------------------------------
    // World azimuth degrees: 0 = +X, 90 = +Z (matches offline bake).
    static float WorldAzimuthDeg(vector fromOrigin, vector toPos)
    {
        float dx = toPos[0] - fromOrigin[0];
        float dz = toPos[2] - fromOrigin[2];
        float az = Math.Atan2(dz, dx) * 57.2957795;
        if (az < 0.0)
            az = az + 360.0;
        return az;
    }

    //--------------------------------------------------------------------------------------------
    static float Eval(float worldAzDeg, float rangeM)
    {
        if (!IsReady())
            return 1.0;
        if (s_AzCount < 2 || s_RangeCount < 2)
            return 1.0;

        float az = worldAzDeg;
        while (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;

        float step = 360.0 / s_AzCount;
        int ia = Math.Round(az / step);
        ia = ia - (ia / s_AzCount) * s_AzCount;
        if (ia < 0)
            ia = ia + s_AzCount;

        float dr = s_MaxRangeM / s_RangeCount;
        if (dr < 0.001)
            return 1.0;
        // Range bin centers at (i+0.5)*dr
        float r = rangeM;
        if (r < 0.0)
            r = 0.0;
        if (r > s_MaxRangeM)
            r = s_MaxRangeM;

        float idxF = (r / dr) - 0.5;
        if (idxF < 0.0)
            idxF = 0.0;
        int ir0 = Math.Floor(idxF);
        if (ir0 < 0)
            ir0 = 0;
        if (ir0 > s_RangeCount - 2)
            ir0 = s_RangeCount - 2;
        int ir1 = ir0 + 1;
        float frac = idxF - ir0;
        if (frac < 0.0)
            frac = 0.0;
        if (frac > 1.0)
            frac = 1.0;

        int i0 = ia * s_RangeCount + ir0;
        int i1 = ia * s_RangeCount + ir1;
        float a = s_Factors.Get(i0);
        float b = s_Factors.Get(i1);
        return a + (b - a) * frac;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool TryLoadProfile()
    {
        if (!FileIO.FileExists(PROFILE_PATH))
            return false;

        RDF_RadarSitePathLutDoc doc = new RDF_RadarSitePathLutDoc();
        if (!doc.LoadFromFile(PROFILE_PATH))
            return false;
        if (!doc.schema || doc.schema != SCHEMA)
            return false;
        if (!doc.factors)
            return false;
        if (doc.az_count < 4 || doc.range_count < 4)
            return false;
        int expect = doc.az_count * doc.range_count;
        if (doc.factors.Count() < expect)
            return false;
        if (doc.max_range_m < 10.0)
            return false;

        s_OriginX = doc.origin_x;
        s_OriginY = doc.origin_y;
        s_OriginZ = doc.origin_z;
        s_MaxRangeM = doc.max_range_m;
        s_AzCount = doc.az_count;
        s_RangeCount = doc.range_count;
        s_Factors = new array<float>();
        for (int i = 0; i < expect; i++)
        {
            float f = doc.factors.Get(i);
            if (f < 0.0)
                f = 0.0;
            if (f > 1.0)
                f = 1.0;
            s_Factors.Insert(f);
        }
        return true;
    }
}
