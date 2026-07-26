// Measurement synthesis: turn truth geometry into quantized, noisy radar plots.
// Entities supply RCS / LOS / truth kinematics only for the physics chain;
// published range / angles / radial speed come from this model.
class RDF_RadarMeasurement
{
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const float RAD_TO_DEG = 57.2957795;
    protected static const float SNR_DENOM_FLOOR = 1.0;
    protected static const float CRLB_K = 1.6;

    //------------------------------------------------------------------------------------------------
    // Overwrite target kinematics with measured values. Truth entity may be cleared.
    static void Synthesize(
        RDF_RadarTarget target,
        vector radarOrigin,
        RDF_RadarHardware hardware,
        RDF_RadarSettings settings)
    {
        if (!target || !hardware || !settings)
            return;
        if (!settings.m_EnableMeasurementSynthesis)
            return;

        float trueRange = target.m_Distance;
        if (trueRange <= 0.001)
            return;

        float snrLin = RDF_RadarClutterModel.DbToLin(target.m_SnrDb);
        float snrEff = snrLin;
        if (snrEff < SNR_DENOM_FLOOR)
            snrEff = SNR_DENOM_FLOOR;
        float denom = CRLB_K * Math.Sqrt(2.0 * snrEff);
        if (denom < 0.001)
            denom = 0.001;

        float rangeBinM = hardware.GetRangeBinM();
        float rangeSigma = rangeBinM / denom;
        int rangeBin = Math.Floor(trueRange / rangeBinM);
        if (rangeBin < 0)
            rangeBin = 0;
        float quantizedRange = (rangeBin + 0.5) * rangeBinM;
        float measuredRange = quantizedRange + Math.RandomGaussFloat(rangeSigma, 0.0);
        if (measuredRange < settings.m_MinDistance)
            measuredRange = settings.m_MinDistance;
        if (measuredRange > settings.m_Range)
            measuredRange = settings.m_Range;

        float azSigmaDeg = hardware.m_AzimuthBeamwidthDeg / denom;
        float measuredAzDeg = target.m_AzimuthDeg + Math.RandomGaussFloat(azSigmaDeg, 0.0);

        float elBeamDeg = GetStrongestElevationBeamwidthDeg(hardware, target.m_ElevationDeg);
        float elSigmaDeg = elBeamDeg / denom;
        float measuredElDeg = target.m_ElevationDeg + Math.RandomGaussFloat(elSigmaDeg, 0.0);

        float wavelength = hardware.GetWavelengthM();
        float trueDoppler = target.m_DopplerHz;
        if (trueDoppler == 0.0 && wavelength > 0.0)
            trueDoppler = RDF_RadarClutterModel.DopplerHz(target.m_RadialSpeedMs, wavelength);

        float obsTimeS = EstimateObservationTimeS(hardware);
        float dopplerSigma = 0.0;
        if (obsTimeS > 0.000001 && wavelength > 0.0)
            dopplerSigma = 1.0 / (obsTimeS * denom);
        float measuredDoppler = trueDoppler + Math.RandomGaussFloat(dopplerSigma, 0.0);
        float measuredRadial = 0.0;
        if (wavelength > 0.0)
            measuredRadial = measuredDoppler * wavelength * 0.5;

        float azRad = measuredAzDeg * DEG_TO_RAD;
        float elRad = measuredElDeg * DEG_TO_RAD;
        float cosEl = Math.Cos(elRad);
        float sinEl = Math.Sin(elRad);
        float cosAz = Math.Cos(azRad);
        float sinAz = Math.Sin(azRad);
        vector los = Vector(cosAz * cosEl, sinEl, sinAz * cosEl);

        target.m_Distance = measuredRange;
        target.m_AzimuthDeg = measuredAzDeg;
        target.m_ElevationDeg = measuredElDeg;
        target.m_DopplerHz = measuredDoppler;
        target.m_RadialSpeedMs = measuredRadial;
        target.m_Position = radarOrigin + los * measuredRange;
        // Only radial component is observable; Cartesian velocity is reconstructed along LOS.
        target.m_Velocity = los * (-measuredRadial);

        if (!settings.m_KeepEntityTruth)
        {
            target.m_Entity = null;
            target.m_IsAnonymous = true;
            target.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        }
    }

    //------------------------------------------------------------------------------------------------
    static float GetStrongestElevationBeamwidthDeg(
        RDF_RadarHardware hardware,
        float elevationDeg)
    {
        float fallback = 8.0;
        if (!hardware || !hardware.m_ElevationBeams || hardware.m_ElevationBeams.Count() == 0)
            return fallback;

        float bestGain = -1.0;
        float bestWidth = fallback;
        for (int i = 0; i < hardware.m_ElevationBeams.Count(); i++)
        {
            RDF_RadarElevationBeam beam = hardware.m_ElevationBeams.Get(i);
            if (!beam)
                continue;
            float gain = RDF_RadarClutterModel.GaussianBeamGain(
                elevationDeg - beam.m_BoresightDeg,
                beam.m_BeamwidthDeg);
            if (gain > bestGain)
            {
                bestGain = gain;
                bestWidth = beam.m_BeamwidthDeg;
            }
        }
        if (bestWidth < 0.1)
            bestWidth = 0.1;
        return bestWidth;
    }

    //------------------------------------------------------------------------------------------------
    static float EstimateObservationTimeS(RDF_RadarHardware hardware)
    {
        if (!hardware)
            return 0.001;
        float prf = hardware.m_PrfHz;
        if (prf < 1.0)
            prf = 1.0;
        int pulses = hardware.m_PulsesIntegrated;
        if (pulses < 1)
            pulses = 1;
        return pulses / prf;
    }
}
