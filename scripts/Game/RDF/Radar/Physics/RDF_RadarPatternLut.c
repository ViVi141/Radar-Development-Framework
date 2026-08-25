// 1D azimuth offset → one-way power pattern LUT (segmented mainlobe + peaks).
// Built from Hardware HPBW/SLL or loaded from profile bake-back.
class RDF_RadarPatternLutDoc : JsonApiStruct
{
    string schema;
    float offset_min_deg;
    float offset_max_deg;
    float hpbw_deg;
    float sidelobe_level_db;
    ref array<float> factors;

    void RDF_RadarPatternLutDoc()
    {
        RegV("schema");
        RegV("offset_min_deg");
        RegV("offset_max_deg");
        RegV("hpbw_deg");
        RegV("sidelobe_level_db");
        RegV("factors");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_RadarPatternLut
{
    static const string SCHEMA = "RDF_PATTERN_LUT_V1";
    static const string PROFILE_PATH = "$profile:RDF/RadarData/PatternLut.json";
    static const float OFFSET_MIN = -180.0;
    static const float OFFSET_MAX = 180.0;
    static const int SAMPLE_COUNT = 721;

    protected static bool s_Loaded;
    protected static bool s_Enabled = true;
    protected static float s_OffMin;
    protected static float s_OffMax;
    protected static int s_Count;
    protected static ref array<float> s_Factors;
    protected static float s_BuiltHpbw = -1.0;
    protected static float s_BuiltSll = 1.0e9;

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
    static void EnsureReady(RDF_RadarHardware hardware)
    {
        if (!hardware)
        {
            EnsureLoadedDefault();
            return;
        }
        float hpbw = hardware.m_AzimuthBeamwidthDeg;
        float sll = hardware.m_SidelobeLevelDb;
        if (s_Loaded)
        {
            if (Math.AbsFloat(hpbw - s_BuiltHpbw) < 0.001)
            {
                if (Math.AbsFloat(sll - s_BuiltSll) < 0.001)
                    return;
            }
        }
        s_Loaded = false;
        if (TryLoadProfile(hpbw, sll))
        {
            s_Loaded = true;
            s_BuiltHpbw = hpbw;
            s_BuiltSll = sll;
            return;
        }
        BuildSegmented(hpbw, sll);
        s_Loaded = true;
        s_BuiltHpbw = hpbw;
        s_BuiltSll = sll;
    }

    //--------------------------------------------------------------------------------------------
    static void EnsureLoadedDefault()
    {
        if (s_Loaded)
            return;
        if (TryLoadProfile(2.5, -25.0))
        {
            s_Loaded = true;
            s_BuiltHpbw = 2.5;
            s_BuiltSll = -25.0;
            return;
        }
        BuildSegmented(2.5, -25.0);
        s_Loaded = true;
        s_BuiltHpbw = 2.5;
        s_BuiltSll = -25.0;
    }

    //--------------------------------------------------------------------------------------------
    // One-way linear gain for azimuth offset (degrees).
    static float EvalOneWay(float offsetDeg)
    {
        EnsureLoadedDefault();
        if (!s_Enabled)
            return RDF_RadarClutterModel.GaussianBeamGain(offsetDeg, 2.5);
        if (!s_Factors || s_Count < 2)
            return 1.0;

        float off = offsetDeg;
        if (off < s_OffMin)
            off = s_OffMin;
        if (off > s_OffMax)
            off = s_OffMax;

        float t = (off - s_OffMin) / (s_OffMax - s_OffMin);
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
    // Segmented one-way model (mirrors Python rdf_radar_pattern_lut).
    static float SegmentedOneWay(
        float offsetDeg,
        float hpbwDeg,
        float sidelobeLevelDb)
    {
        if (hpbwDeg <= 0.000001)
            return 1.0;
        float absOff = offsetDeg;
        if (absOff < 0.0)
            absOff = -absOff;
        float gain = RDF_RadarClutterModel.GaussianBeamGain(absOff, hpbwDeg);
        float floorLin = RDF_RadarClutterModel.DbToLin(sidelobeLevelDb);
        if (floorLin < 0.0)
            floorLin = 0.0;

        // Peak centers as HPBW multiples; levels dB.
        float p0c = 2.2 * hpbwDeg;
        float p1c = 3.8 * hpbwDeg;
        float p2c = 5.5 * hpbwDeg;
        float p0 = RDF_RadarClutterModel.DbToLin(-18.0);
        float p1 = RDF_RadarClutterModel.DbToLin(-22.0);
        float p2 = RDF_RadarClutterModel.DbToLin(-25.0);
        if (p0 < floorLin)
            p0 = floorLin;
        if (p1 < floorLin)
            p1 = floorLin;
        if (p2 < floorLin)
            p2 = floorLin;

        float lobeW = hpbwDeg * 1.25;
        if (lobeW < 1.0)
            lobeW = 1.0;
        gain = MaxLobe(gain, absOff, p0c, p0, lobeW);
        gain = MaxLobe(gain, absOff, p1c, p1, lobeW);
        gain = MaxLobe(gain, absOff, p2c, p2, lobeW);

        if (gain < floorLin)
            gain = floorLin;
        if (gain > 1.0)
            gain = 1.0;
        return gain;
    }

    //--------------------------------------------------------------------------------------------
    protected static float MaxLobe(
        float gain,
        float absOff,
        float center,
        float peakLin,
        float lobeW)
    {
        float x = (absOff - center) / lobeW;
        float lobe = peakLin * Math.Pow(0.5, x * x);
        if (lobe > gain)
            return lobe;
        return gain;
    }

    //--------------------------------------------------------------------------------------------
    protected static void BuildSegmented(float hpbwDeg, float sidelobeLevelDb)
    {
        s_Factors = new array<float>();
        s_OffMin = OFFSET_MIN;
        s_OffMax = OFFSET_MAX;
        s_Count = SAMPLE_COUNT;
        float span = s_OffMax - s_OffMin;
        for (int i = 0; i < s_Count; i++)
        {
            float off = s_OffMin + span * (i * 1.0 / (s_Count - 1));
            s_Factors.Insert(SegmentedOneWay(off, hpbwDeg, sidelobeLevelDb));
        }
    }

    //--------------------------------------------------------------------------------------------
    protected static bool TryLoadProfile(float hpbwDeg, float sidelobeLevelDb)
    {
        if (!FileIO.FileExists(PROFILE_PATH))
            return false;

        RDF_RadarPatternLutDoc doc = new RDF_RadarPatternLutDoc();
        if (!doc.LoadFromFile(PROFILE_PATH))
            return false;
        if (!doc.schema || doc.schema != SCHEMA)
            return false;
        if (!doc.factors)
            return false;
        int n = doc.factors.Count();
        if (n < 5)
            return false;
        if (doc.offset_max_deg <= doc.offset_min_deg)
            return false;
        // Reject a profile baked with different hardware params: previously
        // a stale profile (baked with old HPBW/SLL) was loaded and the new
        // params recorded as s_BuiltHpbw/s_BuiltSll, so the LUT silently
        // mismatched the runtime hardware. Fall through to BuildSegmented.
        if (Math.AbsFloat(doc.hpbw_deg - hpbwDeg) > 0.001)
            return false;
        if (Math.AbsFloat(doc.sidelobe_level_db - sidelobeLevelDb) > 0.01)
            return false;

        s_OffMin = doc.offset_min_deg;
        s_OffMax = doc.offset_max_deg;
        s_Count = n;
        s_Factors = new array<float>();
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
