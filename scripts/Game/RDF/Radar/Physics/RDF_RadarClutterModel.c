// Absolute radar equation helpers + σ⁰ clutter model (matches Python tools).
// Pr = Pt * Gt * Gr * λ² * σ / ((4π)³ * R⁴ * L)
// Surface EM params come from RDF_RadarSurfaceTable (RadarData/SurfaceTable.json).
class RDF_RadarClutterModel
{
    static const float MIN_GRAZING_RAD = 0.00872664625; // 0.5 deg
    static const float MAX_SIGMA0 = 10.0;
    static const float C_LIGHT = 299792458.0;
    static const float FOUR_PI = 12.5663706144;

    //------------------------------------------------------------------------------------------------
    // σ⁰ (linear, m²/m²) at reference grazing for ERDF_DemSurfaceClass.
    static float GetSigma0Ref(int surfaceClass)
    {
        return RDF_RadarSurfaceTable.GetSigma0RefLin(surfaceClass);
    }

    //------------------------------------------------------------------------------------------------
    static float GetSigma0Exponent(int surfaceClass)
    {
        return RDF_RadarSurfaceTable.GetGammaK(surfaceClass);
    }

    //------------------------------------------------------------------------------------------------
    // Constant-gamma: σ⁰(θ) = σ⁰_ref * (sinθ / sinθ_ref)^k * clutter_scale
    static float GetSigma0(int surfaceClass, float grazingRad)
    {
        float theta = grazingRad;
        if (theta < MIN_GRAZING_RAD)
            theta = MIN_GRAZING_RAD;
        if (theta > (Math.PI * 0.5))
            theta = Math.PI * 0.5;

        float refValue = GetSigma0Ref(surfaceClass);
        float exponent = GetSigma0Exponent(surfaceClass);
        float thetaRef = RDF_RadarSurfaceTable.GetThetaRefRad();
        float ratio = Math.Sin(theta) / Math.Sin(thetaRef);
        if (ratio < 0.000001)
            ratio = 0.000001;

        float value = refValue * Math.Pow(ratio, exponent);
        value = value * RDF_RadarSurfaceTable.GetClutterScale(surfaceClass);
        if (value > MAX_SIGMA0)
            value = MAX_SIGMA0;
        return value;
    }

    //------------------------------------------------------------------------------------------------
    static float GetCanopySigma0(float grazingRad)
    {
        return GetSigma0(ERDF_DemSurfaceClass.RDF_DEM_SURF_VEGETATION, grazingRad);
    }

    //------------------------------------------------------------------------------------------------
    // One-way foliage/volume attenuation from SurfaceTable (dB/km).
    static float GetSurfaceAttenuationDbPerKm(int surfaceClass)
    {
        return RDF_RadarSurfaceTable.GetAttenuationDbPerKm(surfaceClass);
    }

    //------------------------------------------------------------------------------------------------
    // DbToLin / LinToDb helpers.
    static float DbToLin(float db)
    {
        return Math.Pow(10.0, db / 10.0);
    }

    //------------------------------------------------------------------------------------------------
    static float LinToDb(float value)
    {
        if (value <= 0.000000000000000000000000000001)
            return -300.0;
        return 10.0 * Math.Log10(value);
    }

    //------------------------------------------------------------------------------------------------
    // One-way Gaussian power pattern, 0.5 at half the full HPBW.
    static float GaussianBeamGain(float offsetDeg, float beamwidthDeg)
    {
        if (beamwidthDeg <= 0.000001)
            return 1.0;
        float halfWidth = beamwidthDeg * 0.5;
        float ratio = offsetDeg / halfWidth;
        // exp(-ln(2) * ratio^2) == 0.5^(ratio^2); Enforce has no Math.Exp.
        return Math.Pow(0.5, ratio * ratio);
    }

    //------------------------------------------------------------------------------------------------
    static float GetStrongestBeamGain(
        RDF_RadarHardware hardware,
        float azimuthOffsetDeg,
        float elevationDeg,
        out string outBeamName)
    {
        outBeamName = "";
        if (!hardware || !hardware.m_ElevationBeams)
            return 0.0;

        float azimuthGain = GaussianBeamGain(
            azimuthOffsetDeg,
            hardware.m_AzimuthBeamwidthDeg);
        float strongest = 0.0;

        for (int i = 0; i < hardware.m_ElevationBeams.Count(); i++)
        {
            RDF_RadarElevationBeam beam = hardware.m_ElevationBeams.Get(i);
            if (!beam)
                continue;
            float elevationGain = GaussianBeamGain(
                elevationDeg - beam.m_BoresightDeg,
                beam.m_BeamwidthDeg);
            float relativeGain = DbToLin(beam.m_RelativeGainDb);
            float oneWay = azimuthGain * elevationGain * relativeGain;
            float twoWay = oneWay * oneWay;
            if (twoWay > strongest)
            {
                strongest = twoWay;
                outBeamName = beam.m_Name;
            }
        }
        return strongest;
    }

    //------------------------------------------------------------------------------------------------
    static float ReceivedPowerW(
        float peakPowerW,
        float antennaGainDbi,
        float frequencyHz,
        float systemLossDb,
        float sigmaM2,
        float rangeM,
        float patternTwoWay)
    {
        if (rangeM < 1.0)
            rangeM = 1.0;
        if (sigmaM2 <= 0.0)
            return 0.0;
        if (patternTwoWay <= 0.0)
            return 0.0;
        if (frequencyHz <= 0.0)
            return 0.0;

        float wavelength = C_LIGHT / frequencyHz;
        float gainLin = DbToLin(antennaGainDbi);
        float lossLin = DbToLin(systemLossDb);
        float denom = FOUR_PI * FOUR_PI * FOUR_PI * lossLin;
        float radarConst = peakPowerW * gainLin * gainLin * wavelength * wavelength / denom;
        return radarConst * patternTwoWay * sigmaM2 / (rangeM * rangeM * rangeM * rangeM);
    }

    //------------------------------------------------------------------------------------------------
    // One-way ESM / RWR receive (Friis): Pr = Pt Gt Gr λ² / ((4π)² R² L) * strength * pattern.
    // patternOneWay is the receiver antenna power pattern (not two-way radar).
    static float ReceivedPowerEsmW(
        float emitPeakPowerW,
        float emitAntennaGainDbi,
        float emitFrequencyHz,
        float receiveAntennaGainDbi,
        float systemLossDb,
        float rangeM,
        float emitStrength,
        float patternOneWay)
    {
        if (rangeM < 1.0)
            rangeM = 1.0;
        if (emitPeakPowerW <= 0.0)
            return 0.0;
        if (emitFrequencyHz <= 0.0)
            return 0.0;
        if (patternOneWay <= 0.0)
            return 0.0;
        if (emitStrength <= 0.0)
            return 0.0;

        float wavelength = C_LIGHT / emitFrequencyHz;
        float gt = DbToLin(emitAntennaGainDbi);
        float gr = DbToLin(receiveAntennaGainDbi);
        float lossLin = DbToLin(systemLossDb);
        float denom = FOUR_PI * FOUR_PI * lossLin;
        float friis =
            emitPeakPowerW * gt * gr * wavelength * wavelength / denom;
        return friis * emitStrength * patternOneWay
            / (rangeM * rangeM);
    }

    //------------------------------------------------------------------------------------------------
    // Clear-air one-way loss near sea level (engineering fit, dB/km).
    static float AtmosphericOneWayDbPerKm(float frequencyHz)
    {
        float fGhz = frequencyHz / 1000000000.0;
        if (fGhz < 1.0)
            return 0.003;
        if (fGhz < 4.0)
            return 0.007;
        if (fGhz < 8.0)
            return 0.012;
        if (fGhz < 12.0)
            return 0.02;
        return 0.04;
    }

    //------------------------------------------------------------------------------------------------
    // Two-way atmospheric+rain loss as a linear factor (>=1) to divide received power by.
    static float AtmosphericLossLinear(
        float rangeM,
        float atmDbPerKmOneWay,
        float rainDbPerKmOneWay)
    {
        if (rangeM < 0.0)
            rangeM = 0.0;
        float alpha = atmDbPerKmOneWay + rainDbPerKmOneWay;
        if (alpha <= 0.0)
            return 1.0;
        float lossDb = 2.0 * alpha * (rangeM / 1000.0);
        float factor = DbToLin(lossDb);
        if (factor < 1.0)
            return 1.0;
        return factor;
    }

    //------------------------------------------------------------------------------------------------
    // Two-pulse MTI power transfer, peak-normalized to 1 at fd = PRF/2.
    static float MtiTwoPulseGain(float dopplerHz, float prfHz)
    {
        if (prfHz <= 0.0)
            return 1.0;
        float phase = Math.PI * dopplerHz / prfHz;
        float s = Math.Sin(phase);
        return s * s;
    }

    //------------------------------------------------------------------------------------------------
    static float DopplerHz(float radialSpeedMs, float wavelengthM)
    {
        if (wavelengthM <= 0.0)
            return 0.0;
        return 2.0 * radialSpeedMs / wavelengthM;
    }
}
