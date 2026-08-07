// 1D Fresnel-ν → linear power factor LUT for knife-edge diffraction.
// Built from the same ITU-R P.526-ish analytic as PhysicalDetect; optional
// profile bake-back from tools/dem/rdf_radar_knife_lut_validate.py.
class RDF_RadarKnifeEdgeLutDoc : JsonApiStruct
{
    string schema;
    float nu_min;
    float nu_max;
    ref array<float> factors;

    void RDF_RadarKnifeEdgeLutDoc()
    {
        RegV("schema");
        RegV("nu_min");
        RegV("nu_max");
        RegV("factors");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_RadarKnifeEdgeLut
{
    static const string SCHEMA = "RDF_KNIFE_LUT_V1";
    static const string PROFILE_PATH = "$profile:RDF/RadarData/KnifeEdgeLut.json";
    static const float NU_MIN = -1.5;
    static const float NU_MAX = 8.0;
    static const int SAMPLE_COUNT = 129;

    protected static bool s_Loaded;
    protected static bool s_Enabled = true;
    protected static float s_NuMin;
    protected static float s_NuMax;
    protected static int s_Count;
    protected static ref array<float> s_Factors;

    //--------------------------------------------------------------------------------------------
    static void SetEnabled(bool enabled)
    {
        s_Enabled = enabled;
    }

    //--------------------------------------------------------------------------------------------
    static bool IsEnabled()
    {
        return s_Enabled;
    }

    //--------------------------------------------------------------------------------------------
    static void EnsureLoaded()
    {
        if (s_Loaded)
            return;
        s_Loaded = true;
        s_NuMin = NU_MIN;
        s_NuMax = NU_MAX;
        s_Count = SAMPLE_COUNT;
        s_Factors = new array<float>();

        if (!TryLoadProfile())
            BuildAnalytic();
    }

    //--------------------------------------------------------------------------------------------
    static float Eval(float nu)
    {
        EnsureLoaded();
        if (!s_Enabled)
            return AnalyticFactor(nu);
        if (!s_Factors || s_Count < 2)
            return AnalyticFactor(nu);

        if (nu <= s_NuMin)
            return s_Factors.Get(0);
        if (nu >= s_NuMax)
            return s_Factors.Get(s_Count - 1);

        float t = (nu - s_NuMin) / (s_NuMax - s_NuMin);
        float idxF = t * (s_Count - 1);
        int i0 = Math.Floor(idxF);
        if (i0 < 0)
            i0 = 0;
        if (i0 > s_Count - 2)
            i0 = s_Count - 2;
        int i1 = i0 + 1;
        float frac = idxF - i0;
        float a = s_Factors.Get(i0);
        float b = s_Factors.Get(i1);
        return a + (b - a) * frac;
    }

    //--------------------------------------------------------------------------------------------
    // Same formula as legacy PhysicalDetect.KnifeEdgeLinearFactor.
    static float AnalyticFactor(float nu)
    {
        if (nu <= -0.78)
            return 1.0;

        float t = nu - 0.1;
        float inner = Math.Sqrt(t * t + 1.0) + t;
        if (inner < 0.001)
            inner = 0.001;
        float lossDb = 6.9 + 20.0 * Math.Log10(inner);
        if (lossDb < 0.0)
            lossDb = 0.0;
        float factor = RDF_RadarClutterModel.DbToLin(-lossDb);
        if (factor > 1.0)
            factor = 1.0;
        if (factor < 0.0)
            factor = 0.0;
        return factor;
    }

    //--------------------------------------------------------------------------------------------
    protected static void BuildAnalytic()
    {
        s_Factors.Clear();
        int n = SAMPLE_COUNT;
        s_Count = n;
        s_NuMin = NU_MIN;
        s_NuMax = NU_MAX;
        float span = s_NuMax - s_NuMin;
        for (int i = 0; i < n; i++)
        {
            float nu = s_NuMin + span * (i * 1.0 / (n - 1));
            s_Factors.Insert(AnalyticFactor(nu));
        }
    }

    //--------------------------------------------------------------------------------------------
    protected static bool TryLoadProfile()
    {
        if (!FileIO.FileExists(PROFILE_PATH))
            return false;

        RDF_RadarKnifeEdgeLutDoc doc = new RDF_RadarKnifeEdgeLutDoc();
        if (!doc.LoadFromFile(PROFILE_PATH))
            return false;
        if (!doc.schema || doc.schema != SCHEMA)
            return false;
        if (!doc.factors)
            return false;
        int n = doc.factors.Count();
        if (n < 5)
            return false;
        if (doc.nu_max <= doc.nu_min)
            return false;

        s_NuMin = doc.nu_min;
        s_NuMax = doc.nu_max;
        s_Count = n;
        s_Factors.Clear();
        for (int i = 0; i < n; i++)
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
