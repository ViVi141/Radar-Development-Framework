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
}
