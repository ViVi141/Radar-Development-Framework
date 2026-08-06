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

    static void Process(
        RDF_RadarTarget target,
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
                hardware.GetWavelengthM(),
                settings,
                demCache,
                usedKnifeEdge);
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
                hardware.GetWavelengthM(),
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
            float horizonFactor = RDF_RadarClutterModel.HorizonSoftFactor(
                distance,
                horizonM);
            target.m_MultipathFactor = target.m_MultipathFactor * horizonFactor;
        }

        string beamName;
        float patternGain = RDF_RadarClutterModel.GetStrongestBeamGain(
            hardware,
            azimuthOffsetDeg,
            elevationDeg,
            beamName);
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
                emitFreq = hardware.m_FrequencyHz;
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
                hardware.m_AntennaGainDbi,
                hardware.m_SystemLossDb,
                distance,
                emitStrength,
                patternGain);
            target.m_ReceivedPowerW = target.m_ReceivedPowerW * target.m_MultipathFactor;
            float polEsm = hardware.m_PolarizationFactor;
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
            hardware.m_PeakPowerW,
            hardware.m_AntennaGainDbi,
            hardware.m_FrequencyHz,
            hardware.m_SystemLossDb,
            target.m_RcsM2,
            distance,
            patternGain);
        target.m_ReceivedPowerW = target.m_ReceivedPowerW * target.m_MultipathFactor;
        float polFactor = hardware.m_PolarizationFactor;
        if (polFactor > 0.0)
        {
            if (polFactor < 1.0)
                target.m_ReceivedPowerW = target.m_ReceivedPowerW * polFactor;
        }
        if (settings.m_EnableAtmosphericLoss)
        {
            float atmDb = settings.m_AtmLossDbPerKmOneWay;
            if (atmDb < 0.0)
                atmDb = RDF_RadarClutterModel.AtmosphericOneWayDbPerKm(hardware.m_FrequencyHz);
            float latm = RDF_RadarClutterModel.AtmosphericLossLinear(
                distance,
                atmDb,
                scanRainLossDbPerKm);
            if (latm > 1.0)
                target.m_ReceivedPowerW = target.m_ReceivedPowerW / latm;
        }

        float wavelength = hardware.GetWavelengthM();
        float bodyDopplerHz = RDF_RadarClutterModel.DopplerHz(
            target.m_RadialSpeedMs,
            wavelength);
        target.m_DopplerHz = bodyDopplerHz;
        target.m_MtiGain = 1.0;
        target.m_DopplerBin = -1;
        target.m_RotorSidebandUsed = false;

        float scanPeriodPre = hardware.GetScanPeriodS();
        int scanNumberPre = 0;
        if (scanPeriodPre < 1000000.0)
            scanNumberPre = Math.Floor(worldTime / scanPeriodPre);
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
                demCache);
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

    protected static float ComputeDemClutterPower(
        RDF_RadarTarget target,
        vector origin,
        float distance,
        float azimuthOffsetDeg,
        float elevationDeg,
        float patternGain,
        RDF_RadarHardware hardware,
        float processingGain,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
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

        float sigma0 = RDF_RadarClutterModel.GetSigma0(
            surfaceClass,
            grazingRad);
        sigma0 = sigma0 * settings.m_DemClutterScale;
        if (sigma0 <= 0.0)
            return 0.0;

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

        float clutterSigmaM2 = sigma0 * areaM2;
        float receivedClutter = RDF_RadarClutterModel.ReceivedPowerW(
            hardware.m_PeakPowerW,
            hardware.m_AntennaGainDbi,
            hardware.m_FrequencyHz,
            hardware.m_SystemLossDb,
            clutterSigmaM2,
            distance,
            patternGain);
        float surfaceAttenDbPerKm =
            RDF_RadarClutterModel.GetSurfaceAttenuationDbPerKm(surfaceClass);
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
        RDF_RadarTarget target,
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

    // NLOS: max(ground-bounce, single knife-edge). usedKnifeEdge true when
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

    // Single knife-edge over DEM/surface samples + Trace hitFraction candidate.
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

        float lambdaM = wavelengthM;
        if (lambdaM < 0.001)
            lambdaM = 0.001;

        float hitU = hitFraction;
        if (hitU < 0.05)
            hitU = 0.05;
        if (hitU > 0.95)
            hitU = 0.95;

        float slackM = settings.m_KnifeEdgeClearanceSlackM;
        float maxHObs = -1.0e9;
        float maxU = hitU;

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
            if (hObs > maxHObs)
            {
                maxHObs = hObs;
                maxU = u;
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
        if (hHit > maxHObs)
        {
            maxHObs = hHit;
            maxU = hitU;
        }

        // No terrain above LOS → no knife-edge path (entity/building block stays
        // on bounce-only). Do not return 1.0 or Trace occlusion is ignored.
        if (maxHObs <= 0.0)
            return 0.0;

        float d1 = maxU * distance;
        float d2 = distance - d1;
        if (d1 < 1.0)
            d1 = 1.0;
        if (d2 < 1.0)
            d2 = 1.0;

        float nu = maxHObs * Math.Sqrt(
            2.0 * distance / (lambdaM * d1 * d2));
        float factor = KnifeEdgeLinearFactor(nu);
        if (factor < settings.m_NlosMinFactor)
            return 0.0;
        return factor;
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
    // nu <= -0.78 → ~1; larger nu (deeper obstacle) attenuates.
    protected static float KnifeEdgeLinearFactor(float nu)
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
