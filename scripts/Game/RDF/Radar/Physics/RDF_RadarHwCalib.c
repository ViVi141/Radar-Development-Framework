// Offline HW calib bake-back (MTD leakage / floor / bins).
// Profile JSON from tools/dem/rdf_radar_hw_calibrate.py → in-game Hardware.
class RDF_RadarHwCalibDoc : JsonApiStruct
{
    string schema;
    string preset;
    float prf_hz;
    float mti_clutter_floor;
    float mtd_clutter_leakage;
    int doppler_bin_count;
    string mti_mode;
    float clutter_sigma_vr_m_s;
    float prf_stagger_ratio;

    void RDF_RadarHwCalibDoc()
    {
        RegV("schema");
        RegV("preset");
        RegV("prf_hz");
        RegV("mti_clutter_floor");
        RegV("mtd_clutter_leakage");
        RegV("doppler_bin_count");
        RegV("mti_mode");
        RegV("clutter_sigma_vr_m_s");
        RegV("prf_stagger_ratio");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_RadarHwCalib
{
    static const string SCHEMA = "RDF_HW_CALIB_V1";
    static const string PROFILE_PATH = "$profile:RDF/RadarData/HwCalib.json";

    //------------------------------------------------------------------------------------------------
    // Suggest non-zero-bin leakage from Gaussian clutter σ_vr (matches Python).
    static float SuggestMtdLeakage(
        float sigmaVrMs,
        float wavelengthM,
        float prfHz,
        int sampleCount)
    {
        if (wavelengthM <= 0.0 || prfHz <= 0.0)
            return 0.000001;
        if (sampleCount < 17)
            sampleCount = 17;
        if (sampleCount > 257)
            sampleCount = 257;

        float sigmaFd = 2.0 * Math.Max(0.000001, sigmaVrMs) / wavelengthM;
        float fdMax = 0.5 * prfHz;
        float dcPower = 0.0;
        float sidePower = 0.0;
        float peak = 0.0;
        // Enforce has no Math.Exp: exp(-0.5 x^2) = 0.5^(x^2 / (2 ln 2)).
        float invTwoLn2 = 1.0 / 1.3862943611198906;

        // First pass: peak of Gaussian PSD on [-fdMax, fdMax].
        for (int i = 0; i < sampleCount; i++)
        {
            float t = i * 1.0 / (sampleCount - 1);
            float fd = -fdMax + t * (2.0 * fdMax);
            float x = fd / sigmaFd;
            float p = Math.Pow(0.5, (x * x) * invTwoLn2);
            if (p > peak)
                peak = p;
        }
        if (peak < 0.0000001)
            peak = 1.0;

        for (int j = 0; j < sampleCount; j++)
        {
            float t2 = j * 1.0 / (sampleCount - 1);
            float fd2 = -fdMax + t2 * (2.0 * fdMax);
            float x2 = fd2 / sigmaFd;
            float p2 = Math.Pow(0.5, (x2 * x2) * invTwoLn2) / peak;
            float fdNorm = fd2 / prfHz;
            if (fdNorm < 0.0)
                fdNorm = -fdNorm;
            if (fdNorm < 0.08)
                dcPower = dcPower + p2;
            else
                sidePower = sidePower + p2;
        }

        float total = dcPower + sidePower;
        if (total <= 0.000000000000000000000000000001)
            return 0.000001;

        float leakage = sidePower / total;
        float hi = 0.05;
        if (sigmaVrMs >= 2.0)
            hi = 0.25;
        else if (sigmaVrMs >= 1.0)
            hi = 0.10;
        else if (sigmaVrMs >= 0.5)
            hi = 0.05;

        if (leakage < 0.000000001)
            leakage = 0.000000001;
        if (leakage > hi)
            leakage = hi;
        return leakage;
    }

    //------------------------------------------------------------------------------------------------
    // Classic MTI clutter residue ≈ (σ_f/PRF)^{2N}; N=1 two-pulse, N=2 three-pulse.
    static float SuggestMtiClutterFloor(
        float sigmaVrMs,
        float wavelengthM,
        float prfHz,
        int cancellerOrder)
    {
        if (wavelengthM <= 0.0 || prfHz <= 0.0)
            return 0.0001;
        if (sigmaVrMs < 0.05)
            sigmaVrMs = 0.05;
        int order = cancellerOrder;
        if (order < 1)
            order = 1;
        if (order > 2)
            order = 2;
        float sigmaFd = 2.0 * sigmaVrMs / wavelengthM;
        float x = sigmaFd / prfHz;
        if (x < 0.0)
            x = -x;
        float residue = Math.Pow(x, 2 * order);
        if (residue < 0.000001)
            residue = 0.000001;
        if (residue > 0.5)
            residue = 0.5;
        return residue;
    }

    //------------------------------------------------------------------------------------------------
    // Load profile calib JSON and apply scalar fields onto hardware.
    static bool TryApplyFromProfile(notnull RDF_RadarHardware hardware)
    {
        if (!FileIO.FileExists(PROFILE_PATH))
            return false;

        RDF_RadarHwCalibDoc doc = new RDF_RadarHwCalibDoc();
        if (!doc.LoadFromFile(PROFILE_PATH))
            return false;
        if (!doc.schema || doc.schema != SCHEMA)
            return false;

        ApplyDoc(doc, hardware);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static void ApplyDoc(notnull RDF_RadarHwCalibDoc doc, notnull RDF_RadarHardware hardware)
    {
        if (doc.prf_hz > 1.0)
            hardware.m_PrfHz = doc.prf_hz;
        if (doc.mti_clutter_floor > 0.0)
            hardware.m_MtiClutterFloor = doc.mti_clutter_floor;
        if (doc.mtd_clutter_leakage > 0.0)
            hardware.m_MtdClutterLeakage = doc.mtd_clutter_leakage;
        if (doc.doppler_bin_count >= 4)
            hardware.m_DopplerBinCount = doc.doppler_bin_count;
        if (doc.prf_stagger_ratio > 1.001)
            hardware.m_PrfStaggerRatio = doc.prf_stagger_ratio;
        if (doc.clutter_sigma_vr_m_s > 0.0)
            hardware.m_ClutterSigmaVrMs = doc.clutter_sigma_vr_m_s;

        if (doc.mti_mode)
        {
            string mode = doc.mti_mode;
            mode.ToLower();
            if (mode.Contains("mtd"))
                hardware.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;
            else if (mode.Contains("three"))
                hardware.m_MtiMode = ERDF_MtiMode.RDF_MTI_THREE_PULSE;
            else
                hardware.m_MtiMode = ERDF_MtiMode.RDF_MTI_TWOPULSE;
        }

        hardware.m_HwCalibApplied = true;
        // Caller (Hardware.Validate) finishes clamps; do not re-enter Validate here.
    }
}
