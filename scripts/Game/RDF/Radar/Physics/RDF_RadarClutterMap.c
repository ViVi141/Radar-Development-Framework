// Runtime range–azimuth clutter-map EMA. DEM σ⁰ seeds the cell; dwell updates
// refine it so weak-clutter regions (water / clear air) do not keep a high floor.
class RDF_RadarClutterMap
{
    protected ref array<float> m_PowerEma;
    protected ref array<bool> m_Initialized;
    protected int m_AzBinCount;
    protected int m_RangeBinCount;
    protected float m_Alpha;
    protected float m_MaxRangeM;

    void RDF_RadarClutterMap()
    {
        m_PowerEma = new array<float>();
        m_Initialized = new array<bool>();
        m_AzBinCount = 0;
        m_RangeBinCount = 0;
        m_Alpha = 0.15;
        m_MaxRangeM = 2000.0;
    }

    void Configure(int azBinCount, int rangeBinCount, float alpha, float maxRangeM)
    {
        if (azBinCount < 4)
            azBinCount = 4;
        if (rangeBinCount < 8)
            rangeBinCount = 8;
        m_Alpha = alpha;
        if (m_Alpha < 0.01)
            m_Alpha = 0.01;
        if (m_Alpha > 1.0)
            m_Alpha = 1.0;
        m_MaxRangeM = maxRangeM;
        if (m_MaxRangeM < 1.0)
            m_MaxRangeM = 1.0;

        if (azBinCount == m_AzBinCount && rangeBinCount == m_RangeBinCount)
            return;

        m_AzBinCount = azBinCount;
        m_RangeBinCount = rangeBinCount;
        int total = m_AzBinCount * m_RangeBinCount;
        m_PowerEma.Clear();
        m_Initialized.Clear();
        for (int i = 0; i < total; i++)
        {
            m_PowerEma.Insert(0.0);
            m_Initialized.Insert(false);
        }
    }

    // Blend DEM sample into the cell EMA and return the map estimate for gating.
    float UpdateAndGet(float rangeM, float azimuthDeg, float demClutterW)
    {
        int idx = CellIndex(rangeM, azimuthDeg);
        if (idx < 0)
            return demClutterW;

        if (!m_Initialized.Get(idx))
        {
            m_PowerEma.Set(idx, demClutterW);
            m_Initialized.Set(idx, true);
            return demClutterW;
        }

        float prev = m_PowerEma.Get(idx);
        float next = prev * (1.0 - m_Alpha) + demClutterW * m_Alpha;
        m_PowerEma.Set(idx, next);
        return next;
    }

    float Peek(float rangeM, float azimuthDeg, float fallbackW)
    {
        int idx = CellIndex(rangeM, azimuthDeg);
        if (idx < 0)
            return fallbackW;
        if (!m_Initialized.Get(idx))
            return fallbackW;
        return m_PowerEma.Get(idx);
    }

    void Clear()
    {
        int total = m_PowerEma.Count();
        for (int i = 0; i < total; i++)
        {
            m_PowerEma.Set(i, 0.0);
            m_Initialized.Set(i, false);
        }
    }

    protected int CellIndex(float rangeM, float azimuthDeg)
    {
        if (m_AzBinCount <= 0 || m_RangeBinCount <= 0)
            return -1;
        if (rangeM < 0.0)
            rangeM = 0.0;

        float rangeFrac = rangeM / m_MaxRangeM;
        if (rangeFrac > 0.999)
            rangeFrac = 0.999;
        int rangeBin = Math.Floor(rangeFrac * m_RangeBinCount);
        if (rangeBin < 0)
            rangeBin = 0;
        if (rangeBin >= m_RangeBinCount)
            rangeBin = m_RangeBinCount - 1;

        float az = azimuthDeg;
        while (az < 0.0)
            az = az + 360.0;
        while (az >= 360.0)
            az = az - 360.0;
        int azBin = Math.Floor((az / 360.0) * m_AzBinCount);
        if (azBin < 0)
            azBin = 0;
        if (azBin >= m_AzBinCount)
            azBin = m_AzBinCount - 1;

        return azBin * m_RangeBinCount + rangeBin;
    }
}
