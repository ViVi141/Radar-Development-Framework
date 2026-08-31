// Coarse range–azimuth clutter intensity surface for cockpit MFD display.
// Independent of PhysicalDetect / ClutterMap: DEM σ⁰ · area / R^4 along
// boresight-relative rays (matches tools/dem/render_range_az_clutter.py).
// Default OFF via Settings; amortized ray budget per scan.
class RDF_RadarRangeAzClutterSurface
{
    protected static const float MIN_GRAZING_RAD = 0.02;
    protected static const float MIN_RANGE_M = 50.0;
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const float RAD_TO_DEG = 57.2957795131;

    protected ref array<float> m_Power;
    protected int m_AzBinCount;
    protected int m_RangeBinCount;
    protected float m_MaxRangeM;
    protected float m_SectorHalfDeg;
    protected float m_DecayPerScan;
    protected int m_CellsPerScan;
    protected int m_AzCursor;
    protected float m_LastScanAzimuthRad;
    protected float m_LastSectorHalfDeg;
    protected int m_StatRaysThisScan;
    protected int m_StatSamplesThisScan;

    void RDF_RadarRangeAzClutterSurface()
    {
        m_Power = new array<float>();
        m_AzBinCount = 0;
        m_RangeBinCount = 0;
        m_MaxRangeM = 2000.0;
        m_SectorHalfDeg = 45.0;
        m_DecayPerScan = 0.92;
        m_CellsPerScan = 96;
        m_AzCursor = 0;
        m_LastScanAzimuthRad = 0.0;
        m_LastSectorHalfDeg = 45.0;
        m_StatRaysThisScan = 0;
        m_StatSamplesThisScan = 0;
    }

    void Configure(
        int azBinCount,
        int rangeBinCount,
        float maxRangeM,
        float sectorHalfDeg,
        int cellsPerScan,
        float decayPerScan)
    {
        if (azBinCount < 8)
            azBinCount = 8;
        if (azBinCount > 128)
            azBinCount = 128;
        if (rangeBinCount < 8)
            rangeBinCount = 8;
        if (rangeBinCount > 128)
            rangeBinCount = 128;
        m_MaxRangeM = maxRangeM;
        if (m_MaxRangeM < 100.0)
            m_MaxRangeM = 100.0;
        m_SectorHalfDeg = sectorHalfDeg;
        if (m_SectorHalfDeg < 1.0)
            m_SectorHalfDeg = 1.0;
        if (m_SectorHalfDeg > 180.0)
            m_SectorHalfDeg = 180.0;
        m_CellsPerScan = cellsPerScan;
        if (m_CellsPerScan < 8)
            m_CellsPerScan = 8;
        m_DecayPerScan = decayPerScan;
        if (m_DecayPerScan < 0.5)
            m_DecayPerScan = 0.5;
        if (m_DecayPerScan > 1.0)
            m_DecayPerScan = 1.0;

        if (azBinCount == m_AzBinCount && rangeBinCount == m_RangeBinCount)
            return;

        m_AzBinCount = azBinCount;
        m_RangeBinCount = rangeBinCount;
        m_AzCursor = 0;
        int total = m_AzBinCount * m_RangeBinCount;
        m_Power.Clear();
        for (int i = 0; i < total; i++)
            m_Power.Insert(0.0);
    }

    int GetAzBinCount()
    {
        return m_AzBinCount;
    }

    int GetRangeBinCount()
    {
        return m_RangeBinCount;
    }

    float GetMaxRangeM()
    {
        return m_MaxRangeM;
    }

    float GetSectorHalfDeg()
    {
        return m_LastSectorHalfDeg;
    }

    float GetScanAzimuthDeg()
    {
        return m_LastScanAzimuthRad * RAD_TO_DEG;
    }

    int GetStatRaysThisScan()
    {
        return m_StatRaysThisScan;
    }

    int GetStatSamplesThisScan()
    {
        return m_StatSamplesThisScan;
    }

    float Peek(int azBin, int rangeBin)
    {
        int idx = CellIndex(azBin, rangeBin);
        if (idx < 0)
            return 0.0;
        return m_Power.Get(idx);
    }

    // Clear+append into dst (caller reuses the array). Returns cell count.
    int CopyPowers(inout array<float> dst)
    {
        if (!dst)
            return 0;
        dst.Clear();
        int n = m_Power.Count();
        for (int i = 0; i < n; i++)
            dst.Insert(m_Power.Get(i));
        return n;
    }

    // Fill boresight-relative sector with budgeted DEM rays. Call once per Scan.
    void UpdateSector(
        RDF_DemRuntimeCache demCache,
        vector origin,
        float scanAzimuthRad,
        float sectorHalfDeg,
        string band)
    {
        m_StatRaysThisScan = 0;
        m_StatSamplesThisScan = 0;
        if (!demCache)
            return;
        if (m_AzBinCount < 8 || m_RangeBinCount < 8)
            return;

        m_LastScanAzimuthRad = scanAzimuthRad;
        m_LastSectorHalfDeg = sectorHalfDeg;
        if (m_LastSectorHalfDeg < 1.0)
            m_LastSectorHalfDeg = m_SectorHalfDeg;

        ApplyDecay();

        float halfRad = m_LastSectorHalfDeg * DEG_TO_RAD;
        float sectorRad = halfRad * 2.0;
        float stepM = m_MaxRangeM / m_RangeBinCount;
        if (stepM < 5.0)
            stepM = 5.0;

        int budget = m_CellsPerScan;
        int raysDone = 0;
        while (budget > 0 && raysDone < m_AzBinCount)
        {
            int azBin = m_AzCursor;
            m_AzCursor = m_AzCursor + 1;
            if (m_AzCursor >= m_AzBinCount)
                m_AzCursor = 0;

            float t = 0.0;
            if (m_AzBinCount > 1)
                t = azBin / (m_AzBinCount - 1.0);
            float azOff = -halfRad + t * sectorRad;
            float rayAz = scanAzimuthRad + azOff;
            float dirX = Math.Cos(rayAz);
            float dirZ = Math.Sin(rayAz);

            ClearAzColumn(azBin);
            int used = TraceRay(
                demCache,
                origin,
                dirX,
                dirZ,
                azBin,
                stepM,
                sectorRad,
                band,
                budget);
            budget = budget - used;
            raysDone = raysDone + 1;
            m_StatRaysThisScan = m_StatRaysThisScan + 1;
        }
    }

    protected void ApplyDecay()
    {
        int n = m_Power.Count();
        float decay = m_DecayPerScan;
        for (int i = 0; i < n; i++)
        {
            float p = m_Power.Get(i);
            if (p <= 0.0)
                continue;
            m_Power.Set(i, p * decay);
        }
    }

    protected void ClearAzColumn(int azBin)
    {
        if (azBin < 0 || azBin >= m_AzBinCount)
            return;
        int baseIdx = azBin * m_RangeBinCount;
        for (int ir = 0; ir < m_RangeBinCount; ir++)
            m_Power.Set(baseIdx + ir, 0.0);
    }

    // Returns approximate sample count charged against the scan budget.
    protected int TraceRay(
        RDF_DemRuntimeCache demCache,
        vector origin,
        float dirX,
        float dirZ,
        int azBin,
        float stepM,
        float sectorRad,
        string band,
        int budgetLeft)
    {
        float radarY = origin[1];
        float maxElev = -3.4e38;
        bool blocked = false;
        int samples = 0;
        float r = stepM;
        while (r <= m_MaxRangeM && samples < budgetLeft)
        {
            samples = samples + 1;
            float wx = origin[0] + dirX * r;
            float wz = origin[2] + dirZ * r;
            RDF_DemRuntimeCellSample demSample;
            if (!demCache.TrySampleAt(wx, wz, demSample))
            {
                r = r + stepM;
                continue;
            }
            if (!demSample || !demSample.m_Valid)
            {
                r = r + stepM;
                continue;
            }

            float ty = demSample.m_TerrainY;
            float elev = Math.Atan2(ty - radarY, r);
            if (elev > maxElev + 0.0001)
            {
                maxElev = elev;
                blocked = false;
            }
            else if (elev < maxElev - 0.002)
            {
                blocked = true;
            }
            if (blocked)
            {
                r = r + stepM;
                continue;
            }

            float slopeRad = demSample.m_SlopeDeg * DEG_TO_RAD;
            if (slopeRad < 0.0)
                slopeRad = 0.0;
            float grazing = Math.AbsFloat(elev) + slopeRad * 0.15;
            if (grazing < MIN_GRAZING_RAD)
                grazing = MIN_GRAZING_RAD;

            float s0 = RDF_RadarClutterModel.GetSigma0(
                demSample.m_SurfaceClass,
                grazing,
                band);
            float azWidthM = stepM;
            float widthFromSector = r * (sectorRad / m_AzBinCount);
            if (widthFromSector > azWidthM)
                azWidthM = widthFromSector;
            float area = stepM * azWidthM;
            float rSafe = r;
            if (rSafe < MIN_RANGE_M)
                rSafe = MIN_RANGE_M;
            float pr = (s0 * area) / (rSafe * rSafe * rSafe * rSafe);

            int ir = (r / m_MaxRangeM * m_RangeBinCount);
            if (ir < 0)
                ir = 0;
            if (ir >= m_RangeBinCount)
                ir = m_RangeBinCount - 1;
            int idx = CellIndex(azBin, ir);
            if (idx >= 0)
            {
                float prev = m_Power.Get(idx);
                m_Power.Set(idx, prev + pr);
            }
            m_StatSamplesThisScan = m_StatSamplesThisScan + 1;
            r = r + stepM;
        }
        if (samples < 1)
            samples = 1;
        return samples;
    }

    protected int CellIndex(int azBin, int rangeBin)
    {
        if (azBin < 0 || azBin >= m_AzBinCount)
            return -1;
        if (rangeBin < 0 || rangeBin >= m_RangeBinCount)
            return -1;
        return azBin * m_RangeBinCount + rangeBin;
    }
}
