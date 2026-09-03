// Physical detection chain: NLOS scale, DEM clutter, radar equation / SNR.
// Called from RDF_RadarScanner after LOS / candidate gating.
class RDF_RadarPhysicalDetect
{
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const float RAD_TO_DEG = 57.2957795;
    protected static const float LIGHT_SPEED = 299792458.0;

    // Reused per Process() — single-threaded game loop only.
    protected static ref array<float> s_DopplerLines;
    protected static ref array<float> s_DopplerPowers;
    protected static ref array<float> s_PrfList;
    protected static ref RDF_RadarTruthSample s_PathScratch;

    static void Process(
        RDF_RadarTruthSample target,
        vector origin,
        vector scanForward,
        float worldTime,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache,
        float scanRainLossDbPerKm,
        RDF_RadarClutterMap clutterMap,
        RDF_RadarCoarseRdMap coarseRdMap)
    {
        if (!target)
            return;

        target.m_Detected = true;
        target.m_BeamName = "geometry";
        if (!target.m_DemSampleValid)
        {
            target.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
            target.m_DemSampleValid = false;
        }
        target.m_ClutterPowerW = 0.0;
        target.m_ClutterToNoiseDb = -300.0;
        target.m_CfarPowerW = 0.0;
        if (target.m_LosHitFraction <= 0.0 && !target.m_LosBlocked)
            target.m_LosHitFraction = 1.0;
        if (target.m_MultipathFactor <= 0.0)
            target.m_MultipathFactor = 1.0;

        vector toTarget = target.m_Position - origin;
        float distance = toTarget.Length();
        if (distance <= 0.001)
        {
            target.m_Detected = false;
            target.m_MultipathFactor = 0.0;
            return;
        }

        vector los = toTarget / distance;
        float horizontalRange = Math.Sqrt(
            toTarget[0] * toTarget[0] + toTarget[2] * toTarget[2]);
        float elevationRad = Math.Atan2(toTarget[1], Math.Max(0.001, horizontalRange));
        float elevationDeg = elevationRad * RAD_TO_DEG;

        float scanAzimuth = Math.Atan2(scanForward[2], scanForward[0]);
        float targetAzimuth = Math.Atan2(los[2], los[0]);
        float azimuthOffsetRad = RDF_RadarScanGeometry.NormalizeAngleRad(
            targetAzimuth - scanAzimuth);
        float azimuthOffsetDeg = azimuthOffsetRad * RAD_TO_DEG;

        target.m_AzimuthDeg = targetAzimuth * RAD_TO_DEG;
        target.m_ElevationDeg = elevationDeg;
        target.m_RadialSpeedMs = -(
            target.m_Velocity[0] * los[0]
            + target.m_Velocity[1] * los[1]
            + target.m_Velocity[2] * los[2]);

        // Geometry-only mode: keep Detected=true after polar fill. Do not apply
        // NLOS / SNR / CFAR rejects (used by lock-layer and other regressions).
        if (!settings)
            return;
        RDF_RadarHardware hardware = settings.m_Hardware;
        if (!settings.m_EnablePhysicalDetection || !hardware)
        {
            if (target.m_LosBlocked)
                target.m_MultipathFactor = 0.25;
            else
                target.m_MultipathFactor = 1.0;
            return;
        }

        int scanNumberPre = hardware.GetScanNumber(worldTime);
        float scanFreqHz = hardware.GetScanFrequencyHz(scanNumberPre);
        if (scanFreqHz < 1000000.0)
            scanFreqHz = hardware.m_FrequencyHz;
        float scanGainDbi = hardware.GetAntennaGainDbiAtFrequency(scanFreqHz);
        float scanWavelengthM = hardware.GetWavelengthAtFrequencyM(scanFreqHz);
        string scanBand = RDF_RadarSurfaceTable.BandNameFromFrequencyHz(scanFreqHz);

        float minDetectM = settings.GetEffectiveMinDistance();
        if (distance <= minDetectM)
        {
            target.m_Detected = false;
            return;
        }

        bool usedKnifeEdge = false;
        if (target.m_LosBlocked)
        {
            target.m_MultipathFactor = ComputeNlosMultipathFactor(
                origin,
                target.m_Position,
                distance,
                target.m_LosHitFraction,
                world,
                target.m_AglM,
                target.m_DemSampleValid,
                target.m_DemTerrainY,
                scanWavelengthM,
                settings,
                demCache,
                usedKnifeEdge);
            if (settings.m_EnableSitePathLut)
            {
                float siteF = EvalSitePathFactor(origin, target.m_Position, distance, settings);
                if (siteF > target.m_MultipathFactor)
                    target.m_MultipathFactor = siteF;
            }
            if (target.m_MultipathFactor <= 0.0)
            {
                target.m_Detected = false;
                return;
            }
        }
        else
        {
            target.m_MultipathFactor = ComputeLosTwoRayFactor(
                origin,
                target,
                distance,
                scanWavelengthM,
                settings,
                world,
                demCache);
        }

        if (settings.m_EnableAtmosphericRefraction)
        {
            float radarAgl = ResolveRadarAglM(origin, world, settings, demCache);
            float targetAgl = target.m_AglM;
            if (targetAgl < 0.0)
            {
                if (target.m_DemSampleValid)
                    targetAgl = target.m_Position[1] - target.m_DemTerrainY;
                else
                    targetAgl = Math.Max(1.0, target.m_Position[1] - origin[1] + radarAgl);
            }
            float horizonM = RDF_RadarClutterModel.RadioHorizonRangeM(
                radarAgl,
                targetAgl,
                settings.m_EarthRadiusFactor);
            if (settings.m_EnableAtmosphericDuct)
                horizonM = horizonM * settings.m_DuctHorizonScale;
            float horizonFactor = RDF_RadarClutterModel.HorizonSoftFactor(
                distance,
                horizonM);
            target.m_MultipathFactor = target.m_MultipathFactor * horizonFactor;
        }

        RDF_RadarPatternLut.SetEnabled(settings.m_EnablePatternLut);
        string beamName;
        float patternGain = RDF_RadarClutterModel.GetStrongestBeamGain(
            hardware,
            azimuthOffsetDeg,
            elevationDeg,
            beamName);
        // Pattern LUT already embeds the sidelobe floor; only apply the
        // constant-floor clamp when using bare Gaussian azimuth.
        if (settings.m_EnableSidelobeFloor)
        {
            if (!settings.m_EnablePatternLut)
            {
                patternGain = RDF_RadarClutterModel.ApplyTwoWaySidelobeFloor(
                    patternGain,
                    hardware.m_SidelobeLevelDb);
            }
        }
        target.m_BeamName = beamName;
        if (target.m_LosBlocked)
        {
            if (usedKnifeEdge)
                target.m_BeamName = beamName + "/diff";
            else
                target.m_BeamName = beamName + "/nlos";
        }

        // Registry entries carry a cached RCS; only estimate when absent.
        if (target.m_RcsM2 <= 0.0)
        {
            target.m_RcsM2 = RDF_RadarRcsModel.GetEntityRcsM2(
                target.m_Entity,
                target.m_Type);
        }

        bool useEsm = false;
        if (settings.m_EnableEsmReceive)
        {
            if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
                useEsm = true;
        }

        if (useEsm)
        {
            float emitFreq = target.m_EmitFrequencyHz;
            if (emitFreq <= 0.0)
                emitFreq = scanFreqHz;
            float emitPeak = target.m_EmitPeakPowerW;
            if (emitPeak <= 0.0)
                emitPeak = hardware.m_PeakPowerW;
            float emitGain = target.m_EmitAntennaGainDbi;
            float emitStrength = target.m_EmitStrength;
            if (emitStrength <= 0.0)
                emitStrength = 1.0;

            // One-way pattern: use receive beam gain (not squared two-way).
            target.m_ReceivedPowerW = RDF_RadarClutterModel.ReceivedPowerEsmW(
                emitPeak,
                emitGain,
                emitFreq,
                scanGainDbi,
                hardware.m_SystemLossDb,
                distance,
                emitStrength,
                patternGain);
            target.m_ReceivedPowerW = target.m_ReceivedPowerW * target.m_MultipathFactor;
            float polEsm = ResolvePolarizationFactor(
                hardware,
                settings,
                target.m_Type);
            if (polEsm > 0.0)
            {
                if (polEsm < 1.0)
                    target.m_ReceivedPowerW = target.m_ReceivedPowerW * polEsm;
            }
            if (settings.m_EnableAtmosphericLoss)
            {
                float atmDbEsm = settings.m_AtmLossDbPerKmOneWay;
                if (atmDbEsm < 0.0)
                    atmDbEsm = RDF_RadarClutterModel.AtmosphericOneWayDbPerKm(emitFreq);
                // One-way path: use half of the two-way AtmosphericLossLinear factor.
                float latmTwoWay = RDF_RadarClutterModel.AtmosphericLossLinear(
                    distance,
                    atmDbEsm,
                    scanRainLossDbPerKm);
                float latmOneWay = Math.Sqrt(latmTwoWay);
                if (latmOneWay > 1.0)
                    target.m_ReceivedPowerW = target.m_ReceivedPowerW / latmOneWay;
            }

            target.m_DopplerHz = 0.0;
            target.m_MtiGain = 1.0;
            target.m_DopplerBin = -1;
            target.m_PrfIndex = 0;
            float processingGainEsm = hardware.GetProcessingGain();
            target.m_ProcessedPowerW = target.m_ReceivedPowerW * processingGainEsm;
            target.m_CfarPowerW = target.m_ProcessedPowerW;
            target.m_ClutterPowerW = 0.0;
            target.m_ClutterToNoiseDb = -300.0;

            float noisePowerEsm = hardware.GetNoisePowerW() * processingGainEsm;
            noisePowerEsm = noisePowerEsm + settings.m_AdditionalNoisePowerW;
            if (settings.m_EwStack)
            {
                float ewNoiseEsm = settings.m_EwStack.GetAdditionalNoisePowerW(
                    origin,
                    scanForward,
                    hardware);
                noisePowerEsm = noisePowerEsm + ewNoiseEsm * processingGainEsm;
            }
            float snrEsm = target.m_ProcessedPowerW / Math.Max(
                0.000000000000000000000000000001,
                noisePowerEsm);
            target.m_SnrDb = RDF_RadarClutterModel.LinToDb(snrEsm);
            target.m_Detected = target.m_SnrDb >= settings.m_DetectionSnrDb;

            float scanPeriodEsm = hardware.GetScanPeriodS();
            target.m_ScanNumber = 0;
            if (scanPeriodEsm < 1000000.0)
                target.m_ScanNumber = Math.Floor(worldTime / scanPeriodEsm);
            return;
        }

        target.m_ReceivedPowerW = RDF_RadarClutterModel.ReceivedPowerW(
            hardware.GetEffectivePeakPowerW(),
            scanGainDbi,
            scanFreqHz,
            hardware.m_SystemLossDb,
            target.m_RcsM2,
            distance,
            patternGain);
        target.m_ReceivedPowerW = target.m_ReceivedPowerW * target.m_MultipathFactor;
        float polFactor = ResolvePolarizationFactor(
            hardware,
            settings,
            target.m_Type);
        if (polFactor > 0.0)
        {
            if (polFactor < 1.0)
                target.m_ReceivedPowerW = target.m_ReceivedPowerW * polFactor;
        }
        if (settings.m_EnableAtmosphericLoss)
        {
            float atmDb = settings.m_AtmLossDbPerKmOneWay;
            if (atmDb < 0.0)
                atmDb = RDF_RadarClutterModel.AtmosphericOneWayDbPerKm(scanFreqHz);
            float latm = RDF_RadarClutterModel.AtmosphericLossLinear(
                distance,
                atmDb,
                scanRainLossDbPerKm);
            if (latm > 1.0)
                target.m_ReceivedPowerW = target.m_ReceivedPowerW / latm;
        }

        float wavelength = scanWavelengthM;
        float bodyDopplerHz = RDF_RadarClutterModel.DopplerHz(
            target.m_RadialSpeedMs,
            wavelength);
        target.m_DopplerHz = bodyDopplerHz;
        target.m_MtiGain = 1.0;
        target.m_DopplerBin = -1;
        target.m_RotorSidebandUsed = false;

        float activePrfHz = hardware.GetActivePrfHz(scanNumberPre);
        target.m_PrfIndex = hardware.GetActivePrfIndex(scanNumberPre);

        // Optional micro-Doppler spectrum (rotor sidebands). Empty → body line only.
        if (!s_DopplerLines)
            s_DopplerLines = new array<float>();
        if (!s_DopplerPowers)
            s_DopplerPowers = new array<float>();
        s_DopplerLines.Clear();
        s_DopplerPowers.Clear();
        RDF_RadarRcsModel.FillDopplerSpectrum(
            target,
            wavelength,
            bodyDopplerHz,
            s_DopplerLines,
            s_DopplerPowers);

        if (hardware.m_EnableMti)
        {
            if (hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_MTD_BANK)
            {
                int winBin = 0;
                float peakFd = bodyDopplerHz;
                target.m_MtiGain = RDF_RadarClutterModel.MaxMtdSpectrumGain(
                    s_DopplerLines,
                    s_DopplerPowers,
                    activePrfHz,
                    hardware.m_DopplerBinCount,
                    hardware.m_MtiClutterFloor,
                    hardware.m_MtdClutterLeakage,
                    winBin,
                    peakFd);
                target.m_DopplerBin = winBin;
                target.m_DopplerHz = peakFd;
                if (wavelength > 0.0)
                    target.m_RadialSpeedMs = peakFd * wavelength * 0.5;
                if (target.m_MtiGain < 0.000001)
                    target.m_MtiGain = 0.000001;
                if (target.m_RotorTipSpeedMs > 0.0 && winBin != 0)
                    target.m_RotorSidebandUsed = true;
            }
            else
            {
                if (!s_PrfList)
                    s_PrfList = new array<float>();
                s_PrfList.Clear();
                if (hardware.m_MtiStaggerDeblind && hardware.m_PrfSetHz
                    && hardware.m_PrfSetHz.Count() >= 2)
                {
                    for (int pi = 0; pi < hardware.m_PrfSetHz.Count(); pi++)
                        s_PrfList.Insert(hardware.m_PrfSetHz.Get(pi));
                }
                else
                {
                    s_PrfList.Insert(activePrfHz);
                }

                ERDF_MtiMode cancelMode = ERDF_MtiMode.RDF_MTI_TWOPULSE;
                if (hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_THREE_PULSE)
                    cancelMode = ERDF_MtiMode.RDF_MTI_THREE_PULSE;

                float peakFdCancel = bodyDopplerHz;
                target.m_MtiGain = RDF_RadarClutterModel.MaxMtiCancellerSpectrumGain(
                    s_DopplerLines,
                    s_DopplerPowers,
                    s_PrfList,
                    cancelMode,
                    peakFdCancel);
                target.m_DopplerBin = -1;
                target.m_DopplerHz = peakFdCancel;
                if (wavelength > 0.0)
                    target.m_RadialSpeedMs = peakFdCancel * wavelength * 0.5;
                if (target.m_MtiGain < 0.000001)
                    target.m_MtiGain = 0.000001;
                if (target.m_RotorTipSpeedMs > 0.0
                    && Math.AbsFloat(peakFdCancel - bodyDopplerHz) > 1.0)
                {
                    target.m_RotorSidebandUsed = true;
                }
            }
        }

        float processingGain = hardware.GetProcessingGain();
        target.m_ProcessedPowerW =
            target.m_ReceivedPowerW * processingGain * target.m_MtiGain;
        target.m_CfarPowerW = target.m_ProcessedPowerW;

        float clutterPower = 0.0;
        if (settings.m_EnableDemClutter && demCache)
        {
            clutterPower = ComputeDemClutterPower(
                target,
                origin,
                distance,
                azimuthOffsetDeg,
                elevationDeg,
                patternGain,
                hardware,
                processingGain,
                settings,
                demCache,
                scanRainLossDbPerKm,
                scanFreqHz,
                scanGainDbi,
                scanBand);
            if (settings.m_EnableClutterMap && clutterMap)
            {
                clutterPower = clutterMap.UpdateAndGet(
                    distance,
                    target.m_AzimuthDeg,
                    clutterPower);
            }
        }
        if (settings.m_EnableCoarseRd && coarseRdMap)
        {
            int peekBin = target.m_DopplerBin;
            if (peekBin < 0)
                peekBin = 0;
            float rdW = coarseRdMap.Peek(distance, peekBin, 0.0);
            if (rdW > clutterPower)
            {
                float blend = settings.m_RdClutterBlend;
                if (blend < 0.0)
                    blend = 0.0;
                if (blend > 1.0)
                    blend = 1.0;
                clutterPower = clutterPower * (1.0 - blend) + rdW * blend;
            }
        }
        target.m_ClutterPowerW = clutterPower;

        float noisePower = hardware.GetNoisePowerW() * processingGain;
        noisePower = noisePower + settings.m_AdditionalNoisePowerW;
        noisePower = noisePower + clutterPower;
        if (settings.m_EwStack)
        {
            float ewNoiseRf = settings.m_EwStack.GetAdditionalNoisePowerW(
                origin,
                scanForward,
                hardware);
            noisePower = noisePower + ewNoiseRf * processingGain;
        }
        float snrLinear = target.m_ProcessedPowerW / Math.Max(
            0.000000000000000000000000000001,
            noisePower);
        target.m_SnrDb = RDF_RadarClutterModel.LinToDb(snrLinear);
        if (clutterPower > 0.0)
        {
            float clutterFraction = clutterPower / Math.Max(
                0.000000000000000000000000000001,
                noisePower);
            target.m_ClutterToNoiseDb = RDF_RadarClutterModel.LinToDb(clutterFraction);
        }
        target.m_Detected = target.m_SnrDb >= settings.m_DetectionSnrDb;

        target.m_ScanNumber = scanNumberPre;
    }

    //------------------------------------------------------------------------------------------------
    // One-way path power factor for ESM / SIGINT DF (not Friis / RCS / CFAR).
    // LOS: optional two-ray. NLOS: max(ground-bounce, knife-edge) + site LUT.
    // Always folds k-Earth horizon and optional one-way atmospheric loss.
    static float EvaluateOneWayPathFactor(
        vector rxPos,
        vector txPos,
        float wavelengthM,
        bool losBlocked,
        float losHitFraction,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        if (!settings)
            return 1.0;

        float distance = vector.Distance(rxPos, txPos);
        if (distance < 1.0)
            distance = 1.0;

        float lambdaM = wavelengthM;
        if (lambdaM < 0.001)
            lambdaM = 0.001;

        float hitFraction = losHitFraction;
        if (hitFraction <= 0.0)
        {
            if (!losBlocked)
                hitFraction = 1.0;
            else
                hitFraction = 0.5;
        }

        float factor = 1.0;
        if (losBlocked)
        {
            bool usedKnifeEdge = false;
            factor = ComputeNlosMultipathFactor(
                rxPos,
                txPos,
                distance,
                hitFraction,
                world,
                -1.0,
                false,
                0.0,
                lambdaM,
                settings,
                demCache,
                usedKnifeEdge);
            if (settings.m_EnableSitePathLut)
            {
                float siteF = EvalSitePathFactor(rxPos, txPos, distance, settings);
                if (siteF > factor)
                    factor = siteF;
            }
        }
        else
        {
            RDF_RadarTruthSample scratch = BorrowPathScratch(txPos, world, settings, demCache);
            factor = ComputeLosTwoRayFactor(
                rxPos,
                scratch,
                distance,
                lambdaM,
                settings,
                world,
                demCache);
        }

        float horizon = EvaluateHorizonFactor(
            rxPos, txPos, world, settings, demCache);
        factor = factor * horizon;

        if (settings.m_EnableAtmosphericLoss)
        {
            float freqHz = LIGHT_SPEED / lambdaM;
            float atmDb = settings.m_AtmLossDbPerKmOneWay;
            if (atmDb < 0.0)
                atmDb = RDF_RadarClutterModel.AtmosphericOneWayDbPerKm(freqHz);
            float latmTwoWay = RDF_RadarClutterModel.AtmosphericLossLinear(
                distance, atmDb, 0.0);
            float latmOneWay = Math.Sqrt(latmTwoWay);
            if (latmOneWay > 1.0)
                factor = factor / latmOneWay;
        }

        if (factor < 0.0)
            factor = 0.0;
        return factor;
    }

    //------------------------------------------------------------------------------------------------
    // k-Earth radio-horizon soft factor only (cheap ranker before full path).
    static float EvaluateHorizonFactor(
        vector rxPos,
        vector txPos,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        if (!settings)
            return 1.0;
        if (!settings.m_EnableAtmosphericRefraction)
            return 1.0;

        float distance = vector.Distance(rxPos, txPos);
        if (distance < 1.0)
            return 1.0;

        float rxAgl = ResolveRadarAglM(rxPos, world, settings, demCache);
        float txTerrainY = SampleTerrainY(txPos[0], txPos[2], world, settings, demCache);
        float txAgl = txPos[1] - txTerrainY;
        if (txAgl < 1.0)
            txAgl = 1.0;

        float horizonM = RDF_RadarClutterModel.RadioHorizonRangeM(
            rxAgl, txAgl, settings.m_EarthRadiusFactor);
        if (settings.m_EnableAtmosphericDuct)
            horizonM = horizonM * settings.m_DuctHorizonScale;
        return RDF_RadarClutterModel.HorizonSoftFactor(distance, horizonM);
    }

    protected static RDF_RadarTruthSample BorrowPathScratch(
        vector txPos,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        if (!s_PathScratch)
            s_PathScratch = new RDF_RadarTruthSample();

        RDF_RadarTruthSample scratch = s_PathScratch;
        scratch.m_Position = txPos;
        scratch.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        scratch.m_AglM = -1.0;
        scratch.m_DemSampleValid = false;
        scratch.m_DemTerrainY = 0.0;
        scratch.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;

        if (demCache)
        {
            RDF_DemRuntimeCellSample demSample;
            if (demCache.TrySampleAt(txPos[0], txPos[2], demSample))
            {
                if (demSample && demSample.m_Valid)
                {
                    scratch.m_DemSampleValid = true;
                    scratch.m_DemTerrainY = demSample.m_TerrainY;
                    scratch.m_DemSurfaceClass = demSample.m_SurfaceClass;
                    scratch.m_AglM = txPos[1] - demSample.m_TerrainY;
                }
            }
        }

        if (!scratch.m_DemSampleValid && world)
        {
            scratch.m_DemTerrainY = SampleTerrainY(
                txPos[0], txPos[2], world, settings, demCache);
            scratch.m_AglM = txPos[1] - scratch.m_DemTerrainY;
        }

        return scratch;
    }

    protected static float EvalSitePathFactor(
        vector origin,
        vector targetPos,
        float distance,
        RDF_RadarSettings settings)
    {
        if (!settings)
            return 0.0;
        RDF_RadarSitePathLut.SetEnabled(true);
        if (!RDF_RadarSitePathLut.OriginMatches(origin, settings.m_SitePathMaxOriginDriftM))
            return 0.0;
        float worldAz = RDF_RadarSitePathLut.WorldAzimuthDeg(origin, targetPos);
        return RDF_RadarSitePathLut.Eval(worldAz, distance);
    }

    protected static float ResolvePolarizationFactor(
        RDF_RadarHardware hardware,
        RDF_RadarSettings settings,
        ERDF_RadarTargetType targetType)
    {
        if (!hardware)
            return 1.0;
        float factor = hardware.m_PolarizationFactor;
        if (settings && settings.m_EnablePolarizationMatch)
        {
            float match = RDF_RadarClutterModel.PolarizationMatchFactor(
                hardware.m_PolarizationMode,
                targetType);
            factor = factor * match;
        }
        if (factor < 0.05)
            factor = 0.05;
        if (factor > 1.0)
            factor = 1.0;
        return factor;
    }

    // Average σ⁰ over a few DEM cells spanning the illuminated footprint.
    // Center class remains the atten / truth label; only σ⁰ is mixed.
    protected static float ResolveFootprintSigma0(
        RDF_RadarTruthSample target,
        vector origin,
        float distance,
        int centerSurfaceClass,
        float grazingRad,
        float azWidthM,
        float rangeResolutionM,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache,
        string band)
    {
        float centerSigma0 = RDF_RadarClutterModel.GetSigma0(
            centerSurfaceClass,
            grazingRad,
            band);
        if (!settings || !settings.m_EnableClutterFootprintMix)
            return centerSigma0;
        if (!target || !demCache)
            return centerSigma0;
        if (distance < 1.0)
            return centerSigma0;

        int samples = settings.m_ClutterFootprintSamples;
        if (samples < 3)
            samples = 3;
        if (samples > 9)
            samples = 9;

        float dx = target.m_Position[0] - origin[0];
        float dz = target.m_Position[2] - origin[2];
        float horiz = Math.Sqrt(dx * dx + dz * dz);
        if (horiz < 1.0)
            return centerSigma0;
        float ux = dx / horiz;
        float uz = dz / horiz;
        // Left-handed cross in XZ (perpendicular to radial).
        float cx = -uz;
        float cz = ux;

        float halfAz = 0.5 * azWidthM;
        float halfRg = 0.5 * rangeResolutionM;
        float sum = centerSigma0;
        int count = 1;
        int i;
        for (i = 0; i < samples; i++)
        {
            float t = 0.0;
            if (samples > 1)
                t = (i / (samples - 1.0)) * 2.0 - 1.0;
            // Skip the exact center slot (already counted).
            if (t > -0.001 && t < 0.001)
                continue;
            // Alternate range / cross offsets for odd samples; even stay near center.
            float offR = 0.0;
            float offC = 0.0;
            if ((i % 2) == 0)
                offC = t * halfAz;
            else
                offR = t * halfRg;

            float sx = target.m_Position[0] + ux * offR + cx * offC;
            float sz = target.m_Position[2] + uz * offR + cz * offC;
            RDF_DemRuntimeCellSample demSample;
            if (!demCache.TrySampleAt(sx, sz, demSample))
                continue;
            if (!demSample || !demSample.m_Valid)
                continue;
            float s0 = RDF_RadarClutterModel.GetSigma0(
                demSample.m_SurfaceClass,
                grazingRad,
                band);
            sum = sum + s0;
            count = count + 1;
        }

        return sum / count;
    }

    protected static float ComputeDemClutterPower(
        RDF_RadarTruthSample target,
        vector origin,
        float distance,
        float azimuthOffsetDeg,
        float elevationDeg,
        float patternGain,
        RDF_RadarHardware hardware,
        float processingGain,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache,
        float scanRainLossDbPerKm,
        float scanFrequencyHz,
        float scanGainDbi,
        string scanBand)
    {
        if (!target || !hardware || !demCache || !settings)
            return 0.0;
        if (distance <= 0.001)
            return 0.0;
        if (settings.m_DemClutterScale <= 0.0)
            return 0.0;

        float terrainY = 0.0;
        int surfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        if (target.m_DemSampleValid)
        {
            terrainY = target.m_DemTerrainY;
            surfaceClass = target.m_DemSurfaceClass;
        }
        else
        {
            RDF_DemRuntimeCellSample demSample;
            if (!demCache.TrySampleAt(target.m_Position[0], target.m_Position[2], demSample))
                return 0.0;
            if (!demSample || !demSample.m_Valid)
                return 0.0;
            terrainY = demSample.m_TerrainY;
            surfaceClass = demSample.m_SurfaceClass;
            target.m_DemSampleValid = true;
            target.m_DemSurfaceClass = surfaceClass;
            target.m_DemTerrainY = terrainY;
        }

        float radarAboveGround = origin[1] - terrainY;
        if (radarAboveGround < 0.0)
            radarAboveGround = -radarAboveGround;
        float grazingRad = Math.Atan2(radarAboveGround, Math.Max(1.0, distance));

        float cellSizeM = demCache.GetCellSizeM();

        float azWidthM = distance * (hardware.m_AzimuthBeamwidthDeg * DEG_TO_RAD);
        azWidthM = Math.AbsFloat(azWidthM);
        if (azWidthM < cellSizeM)
            azWidthM = cellSizeM;

        float rangeResolutionM = EstimateRangeResolutionM(hardware);
        float areaM2 = rangeResolutionM * azWidthM;
        float minArea = cellSizeM * cellSizeM;
        if (areaM2 < minArea)
            areaM2 = minArea;

        string band = scanBand;
        float sigma0 = ResolveFootprintSigma0(
            target,
            origin,
            distance,
            surfaceClass,
            grazingRad,
            azWidthM,
            rangeResolutionM,
            settings,
            demCache,
            band);
        sigma0 = sigma0 * settings.m_DemClutterScale;
        if (settings.m_EnableRainSeaClutterDamp)
        {
            if (surfaceClass == ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER)
                sigma0 = sigma0 * RDF_RadarNctr.RainWaterClutterScale(settings.m_LastRainIntensity);
        }
        if (sigma0 <= 0.0)
            return 0.0;

        float clutterSigmaM2 = sigma0 * areaM2;
        float receivedClutter = RDF_RadarClutterModel.ReceivedPowerW(
            hardware.GetEffectivePeakPowerW(),
            scanGainDbi,
            scanFrequencyHz,
            hardware.m_SystemLossDb,
            clutterSigmaM2,
            distance,
            patternGain);
        if (settings.m_EnablePolarizationMatch)
        {
            float polClutter = RDF_RadarClutterModel.PolarizationClutterFactor(
                hardware.m_PolarizationMode,
                scanRainLossDbPerKm);
            if (polClutter < 1.0)
                receivedClutter = receivedClutter * polClutter;
        }
        float surfaceAttenDbPerKm =
            RDF_RadarClutterModel.GetSurfaceAttenuationDbPerKm(surfaceClass, band);
        if (surfaceAttenDbPerKm > 0.0)
        {
            float surfaceLossLin = RDF_RadarClutterModel.AtmosphericLossLinear(
                distance,
                surfaceAttenDbPerKm,
                0.0);
            if (surfaceLossLin > 1.0)
                receivedClutter = receivedClutter / surfaceLossLin;
        }
        if (receivedClutter <= 0.0)
            return 0.0;

        float clutterMti = 1.0;
        if (hardware.m_EnableMti)
        {
            if (hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_MTD_BANK)
            {
                int binIndex = target.m_DopplerBin;
                if (binIndex < 0)
                    binIndex = 0;
                clutterMti = RDF_RadarClutterModel.MtdClutterBinGain(
                    binIndex,
                    hardware.m_DopplerBinCount,
                    hardware.m_MtiClutterFloor,
                    hardware.m_MtdClutterLeakage);
            }
            else
            {
                clutterMti = hardware.m_MtiClutterFloor;
            }
        }
        if (clutterMti < 0.000001)
            clutterMti = 0.000001;

        return receivedClutter * processingGain * clutterMti;
    }

    //------------------------------------------------------------------------------------------------
    // Radar height AGL from DEM / live surface under origin.
    protected static float ResolveRadarAglM(
        vector origin,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        float terrainY = SampleTerrainY(origin[0], origin[2], world, settings, demCache);
        float agl = origin[1] - terrainY;
        if (agl < 1.0)
            agl = 1.0;
        return agl;
    }

    //------------------------------------------------------------------------------------------------
    // Clear-LOS two-ray multipath (direct + specular). Skipped for projectiles.
    // Uses SurfaceTable dielectric → Fresnel Γ when available; else fixed coeff.
    protected static float ComputeLosTwoRayFactor(
        vector origin,
        RDF_RadarTruthSample target,
        float distance,
        float wavelengthM,
        RDF_RadarSettings settings,
        BaseWorld world,
        RDF_DemRuntimeCache demCache)
    {
        if (!settings || !target)
            return 1.0;
        if (!settings.m_EnableLosTwoRayMultipath)
            return 1.0;
        if (target.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 1.0;

        float radarAgl = ResolveRadarAglM(origin, world, settings, demCache);
        float targetAgl = target.m_AglM;
        if (targetAgl < 0.0)
        {
            if (target.m_DemSampleValid)
                targetAgl = target.m_Position[1] - target.m_DemTerrainY;
            else
                targetAgl = Math.Max(1.0, target.m_Position[1] - origin[1] + radarAgl);
        }
        if (targetAgl < 1.0)
            targetAgl = 1.0;

        float gamma = settings.m_LosTwoRayReflectionCoeff;
        if (settings.m_LosTwoRayUseSurfaceDielectric)
        {
            if (target.m_DemSampleValid)
            {
                int surf = target.m_DemSurfaceClass;
                if (surf != ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN)
                {
                    float eps = RDF_RadarSurfaceTable.GetDielectric(surf);
                    float roughness = RDF_RadarSurfaceTable.GetRoughness(surf);
                    float grazing = Math.Atan2(radarAgl + targetAgl, Math.Max(1.0, distance));
                    float fresnel = RDF_RadarClutterModel.FresnelReflectionCoeffH(eps, grazing);
                    float absG = fresnel;
                    if (absG < 0.0)
                        absG = -absG;
                    absG = RDF_RadarClutterModel.RoughnessDampReflectionAbs(
                        absG, roughness, grazing);
                    if (fresnel < 0.0)
                        gamma = -absG;
                    else
                        gamma = absG;
                }
            }
        }

        return RDF_RadarClutterModel.TwoRayMultipathFactor(
            wavelengthM,
            distance,
            radarAgl,
            targetAgl,
            gamma,
            settings.m_LosTwoRayMaxTargetAglM,
            settings.m_LosTwoRayMinFactor,
            settings.m_LosTwoRayMaxFactor);
    }

    // NLOS: max(ground-bounce, knife-edge). usedKnifeEdge true when
    // diffraction path wins (or is the only surviving path).
    protected static float ComputeNlosMultipathFactor(
        vector origin,
        vector targetPos,
        float distance,
        float hitFraction,
        BaseWorld world,
        float targetAglM,
        bool haveTargetTerrain,
        float targetTerrainYCached,
        float wavelengthM,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache,
        out bool usedKnifeEdge)
    {
        usedKnifeEdge = false;
        if (!settings || !settings.m_EnableNlosMultipath)
            return 0.0;
        if (distance < 1.0)
            return 0.0;

        float bounce = ComputeNlosBounceFactor(
            origin,
            targetPos,
            distance,
            hitFraction,
            world,
            targetAglM,
            haveTargetTerrain,
            targetTerrainYCached,
            settings,
            demCache);

        float knife = 0.0;
        if (settings.m_EnableKnifeEdgeDiffraction)
        {
            knife = ComputeKnifeEdgeFactor(
                origin,
                targetPos,
                distance,
                hitFraction,
                wavelengthM,
                world,
                settings,
                demCache);
        }

        float factor = bounce;
        if (knife > factor)
        {
            factor = knife;
            usedKnifeEdge = true;
        }
        return factor;
    }

    // Image-method ground bounce × |Gamma|^2; early Trace hits weaken further.
    protected static float ComputeNlosBounceFactor(
        vector origin,
        vector targetPos,
        float distance,
        float hitFraction,
        BaseWorld world,
        float targetAglM,
        bool haveTargetTerrain,
        float targetTerrainYCached,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        float originTerrainY = SampleTerrainY(origin[0], origin[2], world, settings, demCache);
        float targetTerrainY = targetTerrainYCached;
        if (!haveTargetTerrain)
            targetTerrainY = SampleTerrainY(targetPos[0], targetPos[2], world, settings, demCache);

        float hr = origin[1] - originTerrainY;
        float ht = targetPos[1] - targetTerrainY;
        if (targetAglM >= 0.0)
            ht = targetAglM;
        if (hr < 0.5)
            hr = 0.5;
        if (ht < 0.5)
            ht = 0.5;
        if (ht > settings.m_NlosMaxTargetAglM)
            return 0.0;

        float sumH = hr + ht;
        float bounceRange = Math.Sqrt(distance * distance + sumH * sumH);
        if (bounceRange < 1.0)
            return 0.0;

        float ratio = distance / bounceRange;
        float geom = ratio * ratio * ratio * ratio;
        float gamma = settings.m_NlosReflectionAbs;
        float factor = gamma * gamma * geom;

        float depthScale = hitFraction;
        if (depthScale < 0.2)
            depthScale = 0.2;
        if (depthScale > 1.0)
            depthScale = 1.0;
        factor = factor * depthScale;

        if (factor < settings.m_NlosMinFactor)
            return 0.0;
        if (factor > 1.0)
            factor = 1.0;
        return factor;
    }

    // Knife-edge over DEM/surface samples + Trace hitFraction candidate.
    // Default: up to two dominant edges (Deygout-lite cascade). Optional
    // legacy single worst-edge via settings.m_EnableDualKnifeEdge = false.
    protected static float ComputeKnifeEdgeFactor(
        vector origin,
        vector targetPos,
        float distance,
        float hitFraction,
        float wavelengthM,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        if (!settings)
            return 0.0;
        if (distance < 1.0)
            return 0.0;

        RDF_RadarKnifeEdgeLut.SetEnabled(settings.m_EnableKnifeEdgeLut);

        float lambdaM = wavelengthM;
        if (lambdaM < 0.001)
            lambdaM = 0.001;

        float hitU = hitFraction;
        if (hitU < 0.05)
            hitU = 0.05;
        if (hitU > 0.95)
            hitU = 0.95;

        float slackM = settings.m_KnifeEdgeClearanceSlackM;
        float bestH = -1.0e9;
        float bestU = hitU;

        int samples = settings.m_KnifeEdgeMaxSamples;
        if (samples < 2)
            samples = 2;

        int i;
        for (i = 1; i <= samples; i++)
        {
            float u = i / (samples + 1.0);
            float hObs = ObstacleHeightAboveLos(
                origin,
                targetPos,
                u,
                slackM,
                world,
                settings,
                demCache);
            if (hObs > bestH)
            {
                bestH = hObs;
                bestU = u;
            }
        }

        float hHit = ObstacleHeightAboveLos(
            origin,
            targetPos,
            hitU,
            slackM,
            world,
            settings,
            demCache);
        if (hHit > bestH)
        {
            bestH = hHit;
            bestU = hitU;
        }

        // No terrain above LOS → no knife-edge path (entity/building block stays
        // on bounce-only). Do not return 1.0 or Trace occlusion is ignored.
        if (bestH <= 0.0)
            return 0.0;

        // Second dominant edge: tallest sample with |Δu| >= min separation.
        float minSep = settings.m_KnifeEdgeMinUSeparation;
        float secondH = -1.0e9;
        float secondU = bestU;
        for (i = 1; i <= samples; i++)
        {
            float u2 = i / (samples + 1.0);
            float h2 = ObstacleHeightAboveLos(
                origin,
                targetPos,
                u2,
                slackM,
                world,
                settings,
                demCache);
            float du = u2 - bestU;
            if (du < 0.0)
                du = -du;
            if (du < minSep)
                continue;
            if (h2 > secondH)
            {
                secondH = h2;
                secondU = u2;
            }
        }
        float duHit = hitU - bestU;
        if (duHit < 0.0)
            duHit = -duHit;
        if (duHit >= minSep && hHit > secondH)
        {
            secondH = hHit;
            secondU = hitU;
        }

        float factor = 0.0;
        if (!settings.m_EnableDualKnifeEdge || secondH <= 0.0)
        {
            factor = KnifeEdgeFactorOneEdge(bestH, bestU, distance, lambdaM);
        }
        else
        {
            factor = KnifeEdgeFactorTwoEdges(
                bestH, bestU, secondH, secondU, distance, lambdaM);
        }

        if (factor < settings.m_NlosMinFactor)
            return 0.0;
        return factor;
    }

    protected static float KnifeEdgeFactorOneEdge(
        float hObs,
        float uEdge,
        float distance,
        float lambdaM)
    {
        float u = uEdge;
        if (u < 0.05)
            u = 0.05;
        if (u > 0.95)
            u = 0.95;
        float d1 = u * distance;
        float d2 = distance - d1;
        if (d1 < 1.0)
            d1 = 1.0;
        if (d2 < 1.0)
            d2 = 1.0;
        float nu = hObs * Math.Sqrt(2.0 * distance / (lambdaM * d1 * d2));
        return KnifeEdgeLinearFactor(nu);
    }

    // Dual-dominant cascade (Deygout-lite):
    // full-path factor on the stronger edge × local segment factor on the
    // runner-up. Heights are above the radar→target LOS (engineering approx).
    protected static float KnifeEdgeFactorTwoEdges(
        float hA,
        float uA,
        float hB,
        float uB,
        float distance,
        float lambdaM)
    {
        float u1;
        float h1;
        float u2;
        float h2;
        if (uA <= uB)
        {
            u1 = uA;
            h1 = hA;
            u2 = uB;
            h2 = hB;
        }
        else
        {
            u1 = uB;
            h1 = hB;
            u2 = uA;
            h2 = hA;
        }

        if (u1 < 0.05)
            u1 = 0.05;
        if (u2 > 0.95)
            u2 = 0.95;
        if (u2 <= u1 + 0.02)
            return KnifeEdgeFactorOneEdge(hA, uA, distance, lambdaM);

        float f1 = KnifeEdgeFactorOneEdge(h1, u1, distance, lambdaM);
        float f2 = KnifeEdgeFactorOneEdge(h2, u2, distance, lambdaM);
        float fMain = f1;
        bool mainIsFirst = true;
        if (f2 > f1)
        {
            fMain = f2;
            mainIsFirst = false;
        }

        float dTxE1 = u1 * distance;
        float dE1E2 = (u2 - u1) * distance;
        float dE2Rx = (1.0 - u2) * distance;
        if (dTxE1 < 1.0)
            dTxE1 = 1.0;
        if (dE1E2 < 1.0)
            dE1E2 = 1.0;
        if (dE2Rx < 1.0)
            dE2Rx = 1.0;

        float nuSec;
        if (mainIsFirst)
        {
            float span2 = dE1E2 + dE2Rx;
            nuSec = h2 * Math.Sqrt(2.0 * span2 / (lambdaM * dE1E2 * dE2Rx));
        }
        else
        {
            float span1 = dTxE1 + dE1E2;
            nuSec = h1 * Math.Sqrt(2.0 * span1 / (lambdaM * dTxE1 * dE1E2));
        }

        float fSec = KnifeEdgeLinearFactor(nuSec);
        // Keep secondary from wiping the main-edge path (min ~ -6 dB extra).
        if (fSec < 0.25)
            fSec = 0.25;
        return fMain * fSec;
    }

    protected static float ObstacleHeightAboveLos(
        vector origin,
        vector targetPos,
        float u,
        float slackM,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        float x = origin[0] + (targetPos[0] - origin[0]) * u;
        float yLos = origin[1] + (targetPos[1] - origin[1]) * u;
        float z = origin[2] + (targetPos[2] - origin[2]) * u;
        float terrainY;
        float columnTopY;
        SampleTerrainAndColumnTop(
            x, z, world, settings, demCache, terrainY, columnTopY);
        if (settings && settings.m_EnableDemSpanOcclusion)
        {
            if (columnTopY > terrainY)
                terrainY = columnTopY;
        }
        // Slack reduces obstacle height (less false block from DEM noise).
        return terrainY - slackM - yLos;
    }

    // Column top from an already-sampled DEM cell (avoids a second TrySampleAt).
    protected static float ColumnTopFromSample(RDF_DemRuntimeCellSample demSample)
    {
        if (!demSample || !demSample.m_Valid)
            return 0.0;

        float top = demSample.m_TerrainY;
        int n = demSample.m_NSpans;
        if (n <= 0 || !demSample.m_SpanHi || !demSample.m_SpanLo)
            return top;

        int hiCount = demSample.m_SpanHi.Count();
        int loCount = demSample.m_SpanLo.Count();
        int count = n;
        if (count > hiCount)
            count = hiCount;
        if (count > loCount)
            count = loCount;
        for (int i = 0; i < count; i++)
        {
            float hi = demSample.m_SpanHi.Get(i);
            float lo = demSample.m_SpanLo.Get(i);
            if (hi <= lo)
                continue;
            if (hi > top)
                top = hi;
        }
        return top;
    }

    // Column top from DEM spans (V3/CSV). SURF packs typically nSpans<=1 stub.
    protected static bool TrySampleColumnTopY(
        float worldX,
        float worldZ,
        RDF_DemRuntimeCache demCache,
        out float outTopY)
    {
        outTopY = 0.0;
        if (!demCache)
            return false;

        RDF_DemRuntimeCellSample demSample;
        if (!demCache.TrySampleAt(worldX, worldZ, demSample))
            return false;
        if (!demSample || !demSample.m_Valid)
            return false;

        outTopY = ColumnTopFromSample(demSample);
        return true;
    }

    // ITU-R P.526-ish approximate diffraction loss → linear power factor.
    // Default path: RDF_RadarKnifeEdgeLut (ν grid / optional profile bake-back).
    protected static float KnifeEdgeLinearFactor(float nu)
    {
        return RDF_RadarKnifeEdgeLut.Eval(nu);
    }

    // One DEM sample → terrainY + columnTop. Reads DEM when clutter OR span is on.
    protected static void SampleTerrainAndColumnTop(
        float worldX,
        float worldZ,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache,
        out float outTerrainY,
        out float outColumnTopY)
    {
        outTerrainY = 0.0;
        outColumnTopY = 0.0;

        bool wantDem = false;
        if (demCache && settings)
        {
            if (settings.m_EnableDemClutter)
                wantDem = true;
            else if (settings.m_EnableDemSpanOcclusion)
                wantDem = true;
        }

        if (wantDem)
        {
            RDF_DemRuntimeCellSample demSample;
            if (demCache.TrySampleAt(worldX, worldZ, demSample))
            {
                if (demSample && demSample.m_Valid)
                {
                    outTerrainY = demSample.m_TerrainY;
                    outColumnTopY = ColumnTopFromSample(demSample);
                    return;
                }
            }
        }

        if (world)
            outTerrainY = world.GetSurfaceY(worldX, worldZ);
        outColumnTopY = outTerrainY;
    }

    protected static float SampleTerrainY(
        float worldX,
        float worldZ,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        float terrainY;
        float columnTopY;
        SampleTerrainAndColumnTop(
            worldX, worldZ, world, settings, demCache, terrainY, columnTopY);
        return terrainY;
    }

    protected static float EstimateRangeResolutionM(RDF_RadarHardware hardware)
    {
        if (!hardware)
            return 1.0;

        float byBandwidth = 0.0;
        if (hardware.m_BandwidthHz > 0.0)
            byBandwidth = LIGHT_SPEED / (2.0 * hardware.m_BandwidthHz);

        float byPulseWidth = 0.0;
        if (hardware.m_PulseWidthS > 0.0)
            byPulseWidth = LIGHT_SPEED * hardware.m_PulseWidthS * 0.5;

        if (byBandwidth > 0.0)
            return Math.Max(0.5, byBandwidth);
        if (byPulseWidth > 0.0)
            return Math.Max(0.5, byPulseWidth);
        return 1.0;
    }
}
