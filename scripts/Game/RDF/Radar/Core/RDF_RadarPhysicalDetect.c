// Physical detection chain: NLOS scale, DEM clutter, radar equation / SNR.
// Called from RDF_RadarScanner after LOS / candidate gating.
class RDF_RadarPhysicalDetect
{
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const float RAD_TO_DEG = 57.2957795;
    protected static const float LIGHT_SPEED = 299792458.0;

    static void Process(
        RDF_RadarTarget target,
        vector origin,
        vector scanForward,
        float worldTime,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache,
        float scanRainLossDbPerKm)
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
                settings,
                demCache);
            if (target.m_MultipathFactor <= 0.0)
            {
                target.m_Detected = false;
                return;
            }
        }
        else
        {
            target.m_MultipathFactor = 1.0;
        }

        string beamName;
        float patternGain = RDF_RadarClutterModel.GetStrongestBeamGain(
            hardware,
            azimuthOffsetDeg,
            elevationDeg,
            beamName);
        target.m_BeamName = beamName;
        if (target.m_LosBlocked)
            target.m_BeamName = beamName + "/nlos";

        // Registry entries carry a cached RCS; only estimate when absent.
        if (target.m_RcsM2 <= 0.0)
        {
            target.m_RcsM2 = RDF_RadarRcsModel.GetEntityRcsM2(
                target.m_Entity,
                target.m_Type);
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
        target.m_DopplerHz = RDF_RadarClutterModel.DopplerHz(
            target.m_RadialSpeedMs,
            wavelength);
        target.m_MtiGain = 1.0;
        if (hardware.m_EnableMti)
        {
            target.m_MtiGain = RDF_RadarClutterModel.MtiTwoPulseGain(
                target.m_DopplerHz,
                hardware.m_PrfHz);
            if (target.m_MtiGain < 0.000001)
                target.m_MtiGain = 0.000001;
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

        float scanPeriod = hardware.GetScanPeriodS();
        target.m_ScanNumber = 0;
        if (scanPeriod < 1000000.0)
            target.m_ScanNumber = Math.Floor(worldTime / scanPeriod);
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
        if (receivedClutter <= 0.0)
            return 0.0;

        float clutterMti = 1.0;
        if (hardware.m_EnableMti)
            clutterMti = hardware.m_MtiClutterFloor;
        if (clutterMti < 0.000001)
            clutterMti = 0.000001;

        return receivedClutter * processingGain * clutterMti;
    }

    // NLOS-only bounce scale using image-method path length and |Gamma|^2.
    // Early Trace blockers further weaken the return (hitFraction small).
    protected static float ComputeNlosMultipathFactor(
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
        if (!settings || !settings.m_EnableNlosMultipath)
            return 0.0;
        if (distance < 1.0)
            return 0.0;

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

    protected static float SampleTerrainY(
        float worldX,
        float worldZ,
        BaseWorld world,
        RDF_RadarSettings settings,
        RDF_DemRuntimeCache demCache)
    {
        RDF_DemRuntimeCellSample demSample;
        if (demCache && settings && settings.m_EnableDemClutter)
        {
            if (demCache.TrySampleAt(worldX, worldZ, demSample))
            {
                if (demSample && demSample.m_Valid)
                    return demSample.m_TerrainY;
            }
        }

        if (world)
            return world.GetSurfaceY(worldX, worldZ);
        return 0.0;
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
