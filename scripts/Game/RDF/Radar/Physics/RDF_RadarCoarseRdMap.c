// Coarse range–Doppler map with amortized cell updates (default OFF).
// Not a training-grade RD cube: deposits target / clutter power into a few
// cells per scan so Pulse-Doppler dwells feel gated without O(R×D) full fills.
class RDF_RadarCoarseRdMap
{
    protected ref array<float> m_Power;
    protected ref array<bool> m_Initialized;
    protected int m_RangeBinCount;
    protected int m_DopplerBinCount;
    protected float m_MaxRangeM;
    protected float m_Alpha;
    protected int m_AmortizeCursor;
    protected int m_CellsPerScan;

    void RDF_RadarCoarseRdMap()
    {
        m_Power = new array<float>();
        m_Initialized = new array<bool>();
        m_RangeBinCount = 0;
        m_DopplerBinCount = 0;
        m_MaxRangeM = 2000.0;
        m_Alpha = 0.2;
        m_AmortizeCursor = 0;
        m_CellsPerScan = 32;
    }

    void Configure(
        int rangeBinCount,
        int dopplerBinCount,
        float maxRangeM,
        float alpha,
        int cellsPerScan)
    {
        if (rangeBinCount < 4)
            rangeBinCount = 4;
        if (dopplerBinCount < 4)
            dopplerBinCount = 4;
        m_MaxRangeM = maxRangeM;
        if (m_MaxRangeM < 1.0)
            m_MaxRangeM = 1.0;
        m_Alpha = alpha;
        if (m_Alpha < 0.01)
            m_Alpha = 0.01;
        if (m_Alpha > 1.0)
            m_Alpha = 1.0;
        m_CellsPerScan = cellsPerScan;
        if (m_CellsPerScan < 1)
            m_CellsPerScan = 1;

        if (rangeBinCount == m_RangeBinCount && dopplerBinCount == m_DopplerBinCount)
            return;

        m_RangeBinCount = rangeBinCount;
        m_DopplerBinCount = dopplerBinCount;
        int total = m_RangeBinCount * m_DopplerBinCount;
        m_Power.Clear();
        m_Initialized.Clear();
        m_AmortizeCursor = 0;
        for (int i = 0; i < total; i++)
        {
            m_Power.Insert(0.0);
            m_Initialized.Insert(false);
        }
    }

    // Deposit processed power into (range, doppler) with EMA.
    void Deposit(float rangeM, int dopplerBin, float powerW)
    {
        int idx = CellIndex(rangeM, dopplerBin);
        if (idx < 0)
            return;
        if (powerW < 0.0)
            powerW = 0.0;

        if (!m_Initialized.Get(idx))
        {
            m_Power.Set(idx, powerW);
            m_Initialized.Set(idx, true);
            return;
        }

        float prev = m_Power.Get(idx);
        float next = prev * (1.0 - m_Alpha) + powerW * m_Alpha;
        m_Power.Set(idx, next);
    }

    float Peek(float rangeM, int dopplerBin, float fallbackW)
    {
        int idx = CellIndex(rangeM, dopplerBin);
        if (idx < 0)
            return fallbackW;
        if (!m_Initialized.Get(idx))
            return fallbackW;
        return m_Power.Get(idx);
    }

    // Decay a ring of cells each scan so stale energy does not stick forever.
    void AmortizeDecay(float decayFactor)
    {
        int total = m_Power.Count();
        if (total <= 0)
            return;
        if (decayFactor < 0.0)
            decayFactor = 0.0;
        if (decayFactor > 1.0)
            decayFactor = 1.0;

        int steps = m_CellsPerScan;
        if (steps > total)
            steps = total;
        for (int i = 0; i < steps; i++)
        {
            if (m_AmortizeCursor >= total)
                m_AmortizeCursor = 0;
            int idx = m_AmortizeCursor;
            m_AmortizeCursor = m_AmortizeCursor + 1;
            if (!m_Initialized.Get(idx))
                continue;
            float next = m_Power.Get(idx) * decayFactor;
            m_Power.Set(idx, next);
        }
    }

    void Clear()
    {
        int total = m_Power.Count();
        for (int i = 0; i < total; i++)
        {
            m_Power.Set(i, 0.0);
            m_Initialized.Set(i, false);
        }
        m_AmortizeCursor = 0;
    }

    int GetRangeBinCount()
    {
        return m_RangeBinCount;
    }

    int GetDopplerBinCount()
    {
        return m_DopplerBinCount;
    }

    protected int CellIndex(float rangeM, int dopplerBin)
    {
        if (m_RangeBinCount <= 0 || m_DopplerBinCount <= 0)
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

        int dBin = dopplerBin;
        if (dBin < 0)
            dBin = 0;
        if (dBin >= m_DopplerBinCount)
            dBin = m_DopplerBinCount - 1;

        return rangeBin * m_DopplerBinCount + dBin;
    }
}
