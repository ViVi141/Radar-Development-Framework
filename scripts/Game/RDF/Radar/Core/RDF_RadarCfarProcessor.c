class RDF_RadarCfarProcessor
{
    protected static const float RAD_TO_DEG = 57.2957795;

    protected ref array<ref array<float>> m_PowerGrid;
    protected ref array<bool> m_RowHits;
    protected int m_CachedAzBins;
    protected int m_CachedRangeBins;

    void RDF_RadarCfarProcessor()
    {
        m_PowerGrid = new array<ref array<float>>();
        m_RowHits = new array<bool>();
        m_CachedAzBins = 0;
        m_CachedRangeBins = 0;
    }

    void ApplyGate(
        notnull array<ref RDF_RadarTarget> outTargets,
        vector origin,
        vector scanForward,
        float halfAngleRad,
        RDF_RadarSettings settings)
    {
        if (!settings || !settings.m_EnableCfarGate)
            return;
        if (outTargets.Count() <= 0)
            return;

        int azBinCount = 24;
        int rangeBinCount = settings.m_RangeBinCount;
        if (rangeBinCount < 8)
            rangeBinCount = 8;

        EnsureGrid(azBinCount, rangeBinCount);

        float rangeM = Math.Max(1.0, settings.m_Range);
        float scanAzimuth = Math.Atan2(scanForward[2], scanForward[0]);
        array<int> targetAzIndices = new array<int>();
        array<int> targetRangeIndices = new array<int>();
        targetAzIndices.Reserve(outTargets.Count());
        targetRangeIndices.Reserve(outTargets.Count());

        for (int i = 0; i < outTargets.Count(); i++)
        {
            RDF_RadarTarget t = outTargets.Get(i);
            int azIndex = -1;
            int rangeIndex = -1;
            if (t)
                GetCellIndices(t, origin, scanAzimuth, halfAngleRad, rangeM, azBinCount, rangeBinCount, azIndex, rangeIndex);
            targetAzIndices.Insert(azIndex);
            targetRangeIndices.Insert(rangeIndex);
            if (!t)
                continue;
            if (azIndex < 0 || rangeIndex < 0)
                continue;
            float power = GetTargetPowerW(t);
            if (power <= 0.0)
                continue;
            float prev = m_PowerGrid.Get(azIndex).Get(rangeIndex);
            m_PowerGrid.Get(azIndex).Set(rangeIndex, prev + power);
        }

        float noiseFloor = EstimateNoiseFloorW(origin, scanForward, settings);
        int flatCount = azBinCount * rangeBinCount;
        while (m_RowHits.Count() < flatCount)
            m_RowHits.Insert(false);

        for (int azHit = 0; azHit < azBinCount; azHit++)
        {
            array<float> powerRow = m_PowerGrid.Get(azHit);
            for (int rb = 0; rb < rangeBinCount; rb++)
            {
                bool hit = RDF_RadarCfarGate.CellDetected(
                    powerRow,
                    rb,
                    noiseFloor,
                    settings.m_CfarGuardCells,
                    settings.m_CfarTrainingCells,
                    settings.m_CfarPfa);
                m_RowHits.Set(azHit * rangeBinCount + rb, hit);
            }
        }

        for (int k = 0; k < outTargets.Count(); k++)
        {
            RDF_RadarTarget target = outTargets.Get(k);
            if (!target)
                continue;
            int azK = targetAzIndices.Get(k);
            int rangeK = targetRangeIndices.Get(k);
            if (azK < 0 || rangeK < 0)
            {
                target.m_Detected = false;
                continue;
            }
            target.m_Detected = m_RowHits.Get(azK * rangeBinCount + rangeK);
        }

        for (int az = 0; az < azBinCount; az++)
        {
            for (int rb2 = 0; rb2 < rangeBinCount; rb2++)
            {
                if (!m_RowHits.Get(az * rangeBinCount + rb2))
                    continue;
                bool occupied = false;
                for (int existing = 0; existing < targetAzIndices.Count(); existing++)
                {
                    if (targetAzIndices.Get(existing) != az)
                        continue;
                    if (targetRangeIndices.Get(existing) == rb2)
                    {
                        occupied = true;
                        break;
                    }
                }
                if (occupied)
                    continue;

                RDF_RadarTarget anon = BuildAnonymousTarget(
                    origin,
                    scanAzimuth,
                    halfAngleRad,
                    rangeM,
                    azBinCount,
                    rangeBinCount,
                    az,
                    rb2,
                    m_PowerGrid.Get(az).Get(rb2),
                    noiseFloor,
                    settings);
                if (anon)
                    outTargets.Insert(anon);
            }
        }
    }

    void RemoveUndetectedTargets(notnull array<ref RDF_RadarTarget> targets, RDF_RadarSettings settings)
    {
        if (settings && settings.m_KeepUndetected)
            return;
        for (int i = targets.Count() - 1; i >= 0; i--)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
            {
                targets.Remove(i);
                continue;
            }
            if (!t.m_Detected)
                targets.Remove(i);
        }
    }

    protected void EnsureGrid(int azBins, int rangeBins)
    {
        if (m_CachedAzBins != azBins || m_CachedRangeBins != rangeBins)
        {
            m_PowerGrid.Clear();
            for (int az = 0; az < azBins; az++)
            {
                array<float> row = new array<float>();
                row.Reserve(rangeBins);
                for (int rb = 0; rb < rangeBins; rb++)
                    row.Insert(0.0);
                m_PowerGrid.Insert(row);
            }
            m_CachedAzBins = azBins;
            m_CachedRangeBins = rangeBins;
            return;
        }

        for (int azClear = 0; azClear < azBins; azClear++)
        {
            array<float> clearRow = m_PowerGrid.Get(azClear);
            for (int rbClear = 0; rbClear < rangeBins; rbClear++)
                clearRow.Set(rbClear, 0.0);
        }
    }

    protected void GetCellIndices(
        RDF_RadarTarget target,
        vector origin,
        float scanAzimuth,
        float halfAngleRad,
        float maxRange,
        int azBins,
        int rangeBins,
        out int outAzIndex,
        out int outRangeIndex)
    {
        outAzIndex = -1;
        outRangeIndex = -1;
        if (!target)
            return;

        vector toTarget = target.m_Position - origin;
        float dist = toTarget.Length();
        if (dist <= 0.001)
            return;
        if (dist > maxRange)
            dist = maxRange;

        float targetAzimuth = Math.Atan2(toTarget[2], toTarget[0]);
        float rel = NormalizeAngleRad(targetAzimuth - scanAzimuth);
        if (halfAngleRad > 0.0001)
        {
            if (rel < -halfAngleRad || rel > halfAngleRad)
                return;
        }

        float azNorm = 0.5;
        if (halfAngleRad > 0.0001)
            azNorm = (rel + halfAngleRad) / (2.0 * halfAngleRad);
        azNorm = Math.Clamp(azNorm, 0.0, 0.999999);
        float rangeNorm = dist / Math.Max(1.0, maxRange);
        rangeNorm = Math.Clamp(rangeNorm, 0.0, 0.999999);

        outAzIndex = Math.Floor(azNorm * azBins);
        outRangeIndex = Math.Floor(rangeNorm * rangeBins);
        if (outAzIndex < 0)
            outAzIndex = 0;
        if (outAzIndex >= azBins)
            outAzIndex = azBins - 1;
        if (outRangeIndex < 0)
            outRangeIndex = 0;
        if (outRangeIndex >= rangeBins)
            outRangeIndex = rangeBins - 1;
    }

    protected float GetTargetPowerW(RDF_RadarTarget target)
    {
        if (!target)
            return 0.0;
        if (target.m_CfarPowerW > 0.0)
            return target.m_CfarPowerW;
        if (target.m_ProcessedPowerW > 0.0)
            return target.m_ProcessedPowerW;
        if (target.m_ReceivedPowerW > 0.0)
            return target.m_ReceivedPowerW;
        return 0.0;
    }

    protected float EstimateNoiseFloorW(
        vector origin,
        vector scanForward,
        RDF_RadarSettings settings)
    {
        if (!settings || !settings.m_Hardware)
            return 0.000000000000000000000000000001;

        RDF_RadarHardware hardware = settings.m_Hardware;
        float processingGain = hardware.GetProcessingGain();
        float noise = hardware.GetNoisePowerW() * processingGain;
        noise = noise + settings.m_AdditionalNoisePowerW;
        if (settings.m_EwStack)
        {
            float ewNoiseRf = settings.m_EwStack.GetAdditionalNoisePowerW(
                origin,
                scanForward,
                hardware);
            noise = noise + ewNoiseRf * processingGain;
        }
        if (noise < 0.000000000000000000000000000001)
            noise = 0.000000000000000000000000000001;
        return noise;
    }

    protected RDF_RadarTarget BuildAnonymousTarget(
        vector origin,
        float scanAzimuth,
        float halfAngleRad,
        float maxRange,
        int azBins,
        int rangeBins,
        int azIndex,
        int rangeIndex,
        float cellPowerW,
        float noiseFloorW,
        RDF_RadarSettings settings)
    {
        float azCenterNorm = (azIndex + 0.5) / azBins;
        float rangeCenterNorm = (rangeIndex + 0.5) / rangeBins;
        float relAz = 0.0;
        if (halfAngleRad > 0.0001)
            relAz = (azCenterNorm * 2.0 - 1.0) * halfAngleRad;
        float azimuth = scanAzimuth + relAz;
        float distance = rangeCenterNorm * maxRange;
        if (distance < settings.m_MinDistance)
            distance = settings.m_MinDistance;
        if (distance > maxRange)
            distance = maxRange;

        vector dir = Vector(Math.Cos(azimuth), 0.0, Math.Sin(azimuth));
        RDF_RadarTarget t = new RDF_RadarTarget();
        t.m_Entity = null;
        t.m_Position = origin + dir * distance;
        t.m_Distance = distance;
        t.m_Velocity = "0 0 0";
        t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        t.m_Time = 0.0;
        t.m_AzimuthDeg = azimuth * RAD_TO_DEG;
        t.m_ElevationDeg = 0.0;
        t.m_RadialSpeedMs = 0.0;
        t.m_RcsM2 = 0.0;
        t.m_ReceivedPowerW = cellPowerW;
        t.m_ProcessedPowerW = cellPowerW;
        t.m_DopplerHz = 0.0;
        t.m_MtiGain = 1.0;
        t.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        t.m_DemSampleValid = false;
        t.m_ClutterPowerW = 0.0;
        t.m_ClutterToNoiseDb = -300.0;
        float snrLinear = cellPowerW / Math.Max(
            0.000000000000000000000000000001,
            noiseFloorW);
        t.m_SnrDb = RDF_RadarClutterModel.LinToDb(snrLinear);
        t.m_Detected = true;
        t.m_IsAnonymous = true;
        t.m_IsFalsePlot = false;
        t.m_CfarPowerW = cellPowerW;
        t.m_LosBlocked = false;
        t.m_LosHitFraction = 1.0;
        t.m_MultipathFactor = 1.0;
        t.m_BeamName = "cfar";
        t.m_ScanNumber = 0;
        return t;
    }

    protected float NormalizeAngleRad(float a)
    {
        while (a > Math.PI)
            a = a - Math.PI2;
        while (a < -Math.PI)
            a = a + Math.PI2;
        return a;
    }
}