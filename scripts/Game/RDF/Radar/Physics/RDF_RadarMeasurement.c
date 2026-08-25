// Measurement synthesis: turn forward truth into quantized, noisy radar plots.
// PhysicalDetect writes RDF_RadarTruthSample; this layer emits RDF_RadarTarget
// observations for Tracker / Lock / Fusion (inverse path).
class RDF_RadarMeasurement
{
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const float RAD_TO_DEG = 57.2957795;
    protected static const float SNR_DENOM_FLOOR = 1.0;
    protected static const float CRLB_K = 1.6;

    //------------------------------------------------------------------------------------------------
    // Copy detectability + pre-noise kinematics from truth into a new observation.
    // Entity is never copied onto the inverse DTO (use Sensor debug-truth map).
    static RDF_RadarTarget PublishFromTruth(RDF_RadarTruthSample truth)
    {
        if (!truth)
            return null;

        RDF_RadarTarget plot = new RDF_RadarTarget();
        plot.m_Entity = null;
        plot.m_ScattererId = truth.m_ScattererId;
        plot.m_Position = truth.m_Position;
        plot.m_Distance = truth.m_Distance;
        plot.m_Velocity = truth.m_Velocity;
        plot.m_Type = truth.m_Type;
        plot.m_Time = truth.m_Time;
        plot.m_AzimuthDeg = truth.m_AzimuthDeg;
        plot.m_ElevationDeg = truth.m_ElevationDeg;
        plot.m_RadialSpeedMs = truth.m_RadialSpeedMs;
        plot.m_RcsM2 = truth.m_RcsM2;
        plot.m_MeanRcsM2 = truth.m_MeanRcsM2;
        plot.m_SwerlingModel = truth.m_SwerlingModel;
        plot.m_AglM = truth.m_AglM;
        plot.m_DemTerrainY = truth.m_DemTerrainY;
        plot.m_ReceivedPowerW = truth.m_ReceivedPowerW;
        plot.m_ProcessedPowerW = truth.m_ProcessedPowerW;
        plot.m_DopplerHz = truth.m_DopplerHz;
        plot.m_MtiGain = truth.m_MtiGain;
        plot.m_DopplerBin = truth.m_DopplerBin;
        plot.m_PrfIndex = truth.m_PrfIndex;
        plot.m_RotorTipSpeedMs = truth.m_RotorTipSpeedMs;
        plot.m_BladeCount = truth.m_BladeCount;
        plot.m_RotorRcsFraction = truth.m_RotorRcsFraction;
        plot.m_HubWidthMs = truth.m_HubWidthMs;
        plot.m_RotorSidebandUsed = truth.m_RotorSidebandUsed;
        plot.m_FanTipSpeedMs = truth.m_FanTipSpeedMs;
        plot.m_FanBladeCount = truth.m_FanBladeCount;
        plot.m_FanRcsFraction = truth.m_FanRcsFraction;
        plot.m_NctrClass = truth.m_NctrClass;
        plot.m_NctrConfidence = truth.m_NctrConfidence;
        plot.m_DemSurfaceClass = truth.m_DemSurfaceClass;
        plot.m_DemSampleValid = truth.m_DemSampleValid;
        plot.m_ClutterPowerW = truth.m_ClutterPowerW;
        plot.m_ClutterToNoiseDb = truth.m_ClutterToNoiseDb;
        plot.m_SnrDb = truth.m_SnrDb;
        plot.m_Detected = truth.m_Detected;
        plot.m_IsAnonymous = true;
        plot.m_IsFalsePlot = truth.m_IsFalsePlot;
        plot.m_CfarPowerW = truth.m_CfarPowerW;
        plot.m_LosBlocked = truth.m_LosBlocked;
        plot.m_LosHitFraction = truth.m_LosHitFraction;
        plot.m_MultipathFactor = truth.m_MultipathFactor;
        plot.m_EmitFrequencyHz = truth.m_EmitFrequencyHz;
        plot.m_EmitPeakPowerW = truth.m_EmitPeakPowerW;
        plot.m_EmitAntennaGainDbi = truth.m_EmitAntennaGainDbi;
        plot.m_EmitStrength = truth.m_EmitStrength;
        plot.m_BeamName = truth.m_BeamName;
        plot.m_ScanNumber = truth.m_ScanNumber;

        if (truth.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
        {
            // ESM ARM needs emitter type tag on the observation; still no entity.
            plot.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
            plot.m_IsAnonymous = false;
        }
        else
        {
            plot.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        }
        return plot;
    }

    //------------------------------------------------------------------------------------------------
    // Overwrite observation kinematics with measured values. Truth is never mutated.
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

        float noiseScale = settings.m_MeasNoiseScale;
        if (noiseScale < 0.0)
            noiseScale = 0.0;

        float rangeBinM = hardware.GetRangeBinM();
        float rangeSigma = (rangeBinM / denom) * noiseScale;
        float workRange = trueRange;
        if (settings.m_EnableRangeAmbiguityFold)
        {
            float runamb = hardware.GetUnambiguousRangeM();
            workRange = RDF_RadarClutterModel.FoldRangeAmbiguous(trueRange, runamb);
        }
        int rangeBin = Math.Floor(workRange / rangeBinM);
        if (rangeBin < 0)
            rangeBin = 0;
        // Clamp the upper end: a target at/above the last bin edge would
        // produce rangeBin == binCount and the quantized range would land
        // one bin past the end, breaking downstream bin-index consumers
        // (CFAR / RWR / datalink) that assume rangeBin in [0, binCount-1].
        int rangeBinCount = settings.m_RangeBinCount;
        if (rangeBinCount > 0 && rangeBin >= rangeBinCount)
            rangeBin = rangeBinCount - 1;
        float quantizedRange = (rangeBin + 0.5) * rangeBinM;
        float measuredRange = quantizedRange + settings.m_MeasRangeBiasM;
        // Math.RandomGaussFloat(stdDev, mean): sigma first, then the mean (0.0).
        if (rangeSigma > 0.0)
            measuredRange = measuredRange + Math.RandomGaussFloat(rangeSigma, 0.0);
        if (measuredRange < settings.GetEffectiveMinDistance())
            measuredRange = settings.GetEffectiveMinDistance();
        if (measuredRange > settings.m_Range)
            measuredRange = settings.m_Range;

        float azSigmaDeg = (hardware.m_AzimuthBeamwidthDeg / denom) * noiseScale;
        float measuredAzDeg = target.m_AzimuthDeg + settings.m_MeasAzimuthBiasDeg;
        if (azSigmaDeg > 0.0)
            measuredAzDeg = measuredAzDeg + Math.RandomGaussFloat(azSigmaDeg, 0.0);

        float elBeamDeg = GetStrongestElevationBeamwidthDeg(hardware, target.m_ElevationDeg);
        float elSigmaDeg = (elBeamDeg / denom) * noiseScale;
        float measuredElDeg = target.m_ElevationDeg + settings.m_MeasElevationBiasDeg;
        if (settings.m_EnableAtmosphericRefraction)
        {
            measuredElDeg = measuredElDeg + RDF_RadarClutterModel.RefractionElevationBiasDeg(
                trueRange,
                settings.m_EarthRadiusFactor);
        }
        if (elSigmaDeg > 0.0)
            measuredElDeg = measuredElDeg + Math.RandomGaussFloat(elSigmaDeg, 0.0);
        if (settings.m_EnableMultipathGlint)
        {
            measuredElDeg = measuredElDeg + RDF_RadarNctr.GlintElevationBiasDeg(
                target.m_AglM,
                target.m_DemSurfaceClass,
                target.m_Time,
                trueRange);
        }

        float wavelength = hardware.GetWavelengthM();
        float trueDoppler = target.m_DopplerHz;
        if (trueDoppler == 0.0 && wavelength > 0.0)
            trueDoppler = RDF_RadarClutterModel.DopplerHz(target.m_RadialSpeedMs, wavelength);

        float obsTimeS = EstimateObservationTimeS(hardware, target.m_ScanNumber);
        float dopplerSigma = 0.0;
        if (obsTimeS > 0.000001 && wavelength > 0.0)
            dopplerSigma = (1.0 / (obsTimeS * denom)) * noiseScale;
        float measuredDoppler = trueDoppler + settings.m_MeasDopplerBiasHz;
        if (dopplerSigma > 0.0)
            measuredDoppler = measuredDoppler + Math.RandomGaussFloat(dopplerSigma, 0.0);
        // WLR / weapon-locate needs true radial; skip PRF fold there.
        bool foldDoppler = settings.m_EnableDopplerAmbiguityFold;
        if (foldDoppler)
        {
            if (settings.m_EnableWeaponLocate)
                foldDoppler = false;
        }
        if (foldDoppler)
        {
            float prf = hardware.GetActivePrfHz(target.m_ScanNumber);
            measuredDoppler = RDF_RadarClutterModel.FoldDopplerAmbiguous(
                measuredDoppler,
                prf);
        }
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

        // Inverse DTO: by default never carry the scatterer entity and
        // re-anonymize non-emitter types. When m_KeepEntityTruth is set (debug /
        // PPI type colours / test assertions), preserve both so projectile and
        // vehicle identity survives synthesis (ShellFire WLR depends on the
        // PROJECTILE type staying intact through FilterUpdate).
        if (!settings.m_KeepEntityTruth)
        {
            target.m_Entity = null;
            if (target.m_Type != ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            {
                target.m_IsAnonymous = true;
                target.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
            }
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
    static float EstimateObservationTimeS(RDF_RadarHardware hardware, int scanNumber)
    {
        if (!hardware)
            return 0.001;
        float prf = hardware.GetActivePrfHz(scanNumber);
        if (prf < 1.0)
            prf = 1.0;
        int pulses = hardware.m_PulsesIntegrated;
        if (pulses < 1)
            pulses = 1;
        return pulses / prf;
    }
}
