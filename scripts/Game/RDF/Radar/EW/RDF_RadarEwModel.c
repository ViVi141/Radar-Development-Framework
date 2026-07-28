// Optional receiver-side EW effects. Effects contribute processed noise power;
// the scanner remains independent of concrete jammer types.
class RDF_RadarEwEffect
{
    float GetAdditionalNoisePowerW(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        return 0.0;
    }

    void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
    }
}

class RDF_RadarFalsePlot
{
    float m_RangeM;
    float m_AzimuthDeg;
    float m_ElevationDeg;
    float m_PowerW;
    float m_RangeRateMs;
}

class RDF_RadarNoiseJammerEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    vector m_Position;
    float m_ErpW = 10000.0;
    float m_BandwidthHz = 5000000.0;

    override float GetAdditionalNoisePowerW(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        if (!m_Enabled || !hardware || m_ErpW <= 0.0)
            return 0.0;

        vector delta = m_Position - radarOrigin;
        float rangeM = delta.Length();
        if (rangeM < 1.0)
            rangeM = 1.0;
        vector direction = delta / rangeM;

        float dot = scanForward[0] * direction[0]
            + scanForward[1] * direction[1]
            + scanForward[2] * direction[2];
        float halfBeamRad = hardware.m_AzimuthBeamwidthDeg * 0.5 * 0.01745329;
        float mainThreshold = Math.Cos(halfBeamRad);
        float coupling = RDF_RadarClutterModel.DbToLin(-25.0);
        if (dot >= mainThreshold)
            coupling = 1.0;

        float jammerBandwidth = Math.Max(1.0, m_BandwidthHz);
        float receiverBandwidth = Math.Max(1.0, hardware.m_BandwidthHz);
        float overlap = Math.Min(jammerBandwidth, receiverBandwidth);
        float fluxDensity = m_ErpW / (
            RDF_RadarClutterModel.FOUR_PI
            * rangeM
            * rangeM
            * jammerBandwidth);
        float wavelength = hardware.GetWavelengthM();
        float aperture = RDF_RadarClutterModel.DbToLin(
            hardware.m_AntennaGainDbi)
            * wavelength
            * wavelength
            / RDF_RadarClutterModel.FOUR_PI;
        return fluxDensity * aperture * overlap * coupling;
    }
}

class RDF_RadarEwStack
{
    ref array<ref RDF_RadarEwEffect> m_Effects;

    void RDF_RadarEwStack()
    {
        m_Effects = new array<ref RDF_RadarEwEffect>();
    }

    void Add(RDF_RadarEwEffect effect)
    {
        if (effect)
            m_Effects.Insert(effect);
    }

    void Clear()
    {
        m_Effects.Clear();
    }

    float GetAdditionalNoisePowerW(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware)
    {
        float total = 0.0;
        for (int i = 0; i < m_Effects.Count(); i++)
        {
            RDF_RadarEwEffect effect = m_Effects.Get(i);
            if (effect)
            {
                total = total + effect.GetAdditionalNoisePowerW(
                    radarOrigin,
                    scanForward,
                    hardware);
            }
        }
        return total;
    }

    void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        for (int i = 0; i < m_Effects.Count(); i++)
        {
            RDF_RadarEwEffect effect = m_Effects.Get(i);
            if (!effect)
                continue;
            effect.CollectFalsePlots(
                radarOrigin,
                scanForward,
                hardware,
                worldTimeS,
                outPlots);
        }
    }
}

class RDF_RadarDeceptionJammerEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    ref array<ref RDF_RadarFalsePlot> m_FalsePlots;

    void RDF_RadarDeceptionJammerEffect()
    {
        m_FalsePlots = new array<ref RDF_RadarFalsePlot>();
    }

    void AddFalsePlot(
        float rangeM,
        float azimuthDeg,
        float powerW,
        float rangeRateMs = 0.0,
        float elevationDeg = 0.0)
    {
        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = rangeM;
        p.m_AzimuthDeg = azimuthDeg;
        p.m_ElevationDeg = elevationDeg;
        p.m_PowerW = powerW;
        p.m_RangeRateMs = rangeRateMs;
        m_FalsePlots.Insert(p);
    }

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || !m_FalsePlots)
            return;

        for (int i = 0; i < m_FalsePlots.Count(); i++)
        {
            RDF_RadarFalsePlot src = m_FalsePlots.Get(i);
            if (!src)
                continue;
            RDF_RadarFalsePlot dst = new RDF_RadarFalsePlot();
            dst.m_AzimuthDeg = src.m_AzimuthDeg;
            dst.m_ElevationDeg = src.m_ElevationDeg;
            dst.m_PowerW = src.m_PowerW;
            dst.m_RangeRateMs = src.m_RangeRateMs;
            dst.m_RangeM = src.m_RangeM + src.m_RangeRateMs * worldTimeS;
            if (dst.m_RangeM < 1.0)
                continue;
            outPlots.Insert(dst);
        }
    }
}

// Range walk-off / RGPO-style deception: false range walks outbound (or inbound).
class RDF_RadarRangeWalkOffEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    float m_BaseRangeM = 1200.0;
    float m_AzimuthDeg = 0.0;
    float m_ElevationDeg = 0.0;
    float m_PowerW = 0.000000000001;
    float m_RangeRateMs = 80.0;
    float m_StartTimeS = 0.0;
    float m_MaxRangeM = 8000.0;
    float m_MinRangeM = 50.0;

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || m_PowerW <= 0.0)
            return;

        float tRel = worldTimeS - m_StartTimeS;
        if (tRel < 0.0)
            tRel = 0.0;
        float rangeM = m_BaseRangeM + m_RangeRateMs * tRel;
        if (rangeM < m_MinRangeM)
            return;
        if (rangeM > m_MaxRangeM)
            return;

        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = rangeM;
        p.m_AzimuthDeg = m_AzimuthDeg;
        p.m_ElevationDeg = m_ElevationDeg;
        p.m_PowerW = m_PowerW;
        p.m_RangeRateMs = m_RangeRateMs;
        outPlots.Insert(p);
    }
}

// Angle scintillation: azimuth jitters around a base bearing each dwell.
class RDF_RadarAngleScintillationEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    float m_RangeM = 1500.0;
    float m_BaseAzimuthDeg = 0.0;
    float m_ElevationDeg = 0.0;
    float m_PowerW = 0.000000000001;
    float m_AzimuthJitterDeg = 3.0;
    float m_RangeRateMs = 0.0;
    float m_PhaseRad = 0.0;
    float m_JitterRateRadPerS = 4.5;

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || m_PowerW <= 0.0)
            return;
        if (m_RangeM < 1.0)
            return;

        float phase = m_PhaseRad + worldTimeS * m_JitterRateRadPerS;
        float jitter = Math.Sin(phase) * m_AzimuthJitterDeg;

        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = m_RangeM;
        p.m_AzimuthDeg = m_BaseAzimuthDeg + jitter;
        p.m_ElevationDeg = m_ElevationDeg;
        p.m_PowerW = m_PowerW;
        p.m_RangeRateMs = m_RangeRateMs;
        outPlots.Insert(p);
    }
}

// Intermittent false plot: only present for a duty-cycle fraction of each period.
class RDF_RadarIntermittentFalsePlotEffect : RDF_RadarEwEffect
{
    bool m_Enabled = true;
    float m_RangeM = 1800.0;
    float m_AzimuthDeg = 10.0;
    float m_ElevationDeg = 0.0;
    float m_PowerW = 0.000000000001;
    float m_RangeRateMs = 0.0;
    float m_PeriodS = 2.0;
    float m_DutyCycle = 0.35;
    float m_PhaseS = 0.0;

    override void CollectFalsePlots(
        vector radarOrigin,
        vector scanForward,
        RDF_RadarHardware hardware,
        float worldTimeS,
        notnull array<ref RDF_RadarFalsePlot> outPlots)
    {
        if (!m_Enabled || m_PowerW <= 0.0)
            return;
        if (m_RangeM < 1.0)
            return;

        float period = m_PeriodS;
        if (period < 0.05)
            period = 0.05;
        float duty = m_DutyCycle;
        if (duty < 0.0)
            duty = 0.0;
        if (duty > 1.0)
            duty = 1.0;

        float phase = worldTimeS + m_PhaseS;
        float frac = phase / period;
        frac = frac - Math.Floor(frac);
        if (frac < 0.0)
            frac = frac + 1.0;
        if (frac > duty)
            return;

        RDF_RadarFalsePlot p = new RDF_RadarFalsePlot();
        p.m_RangeM = m_RangeM;
        p.m_AzimuthDeg = m_AzimuthDeg;
        p.m_ElevationDeg = m_ElevationDeg;
        p.m_PowerW = m_PowerW;
        p.m_RangeRateMs = m_RangeRateMs;
        outPlots.Insert(p);
    }
}
