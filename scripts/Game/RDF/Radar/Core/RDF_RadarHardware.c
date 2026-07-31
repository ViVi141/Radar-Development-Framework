// Parameterized in-game radar hardware. Named presets are optional examples;
// scanner physics depends only on this contract.
class RDF_RadarElevationBeam
{
    string m_Name = "main";
    float m_BoresightDeg = 4.0;
    float m_BeamwidthDeg = 8.0;
    float m_RelativeGainDb = 0.0;
}

class RDF_RadarHardware
{
    string m_Name = "generic";
    float m_FrequencyHz = 9000000000.0;
    float m_PeakPowerW = 120000.0;
    float m_AntennaGainDbi = 32.0;
    float m_AzimuthBeamwidthDeg = 2.5;
    float m_SystemLossDb = 6.0;
    float m_NoiseFigureDb = 5.0;
    float m_PulseWidthS = 0.0000005;
    float m_BandwidthHz = 4000000.0;
    float m_PrfHz = 4000.0;
    // Optional multi-PRF set (Hz). Empty → use m_PrfHz only. Stagger cycles by scan.
    ref array<float> m_PrfSetHz;
    // If >1 and set empty, build a 2-PRF stagger: m_PrfHz and m_PrfHz*ratio.
    float m_PrfStaggerRatio = 1.0;
    int m_PulsesIntegrated = 32;
    bool m_CoherentIntegration = false;
    float m_ScanRpm = 15.0;
    float m_MtiClutterFloor = 0.0001;
    // Non-zero MTD bins: DEM clutter × this leakage (not the global TwoPulse floor).
    float m_MtdClutterLeakage = 0.000001;
    bool m_EnableMti = true;
    // Default TwoPulse keeps legacy / GBRS demos unchanged.
    ERDF_MtiMode m_MtiMode = ERDF_MtiMode.RDF_MTI_TWOPULSE;
    // DFT / filter-bank length when m_MtiMode == MtdBank (8–32 typical).
    int m_DopplerBinCount = 16;
    ref array<ref RDF_RadarElevationBeam> m_ElevationBeams;

    void RDF_RadarHardware()
    {
        m_ElevationBeams = new array<ref RDF_RadarElevationBeam>();
        m_PrfSetHz = new array<float>();
        AddElevationBeam("main", 4.0, 8.0, 0.0);
    }

    //------------------------------------------------------------------------------------------------
    // Resolve PRF for this dwell / scan. Stagger cycles when m_PrfSetHz has ≥2 entries.
    float GetActivePrfHz(int scanNumber)
    {
        if (!m_PrfSetHz || m_PrfSetHz.Count() == 0)
            return m_PrfHz;

        int count = m_PrfSetHz.Count();
        int index = 0;
        if (count > 1)
        {
            if (scanNumber < 0)
                scanNumber = 0;
            index = scanNumber - (scanNumber / count) * count;
            if (index < 0)
                index = 0;
        }
        float prf = m_PrfSetHz.Get(index);
        if (prf < 1.0)
            prf = m_PrfHz;
        return prf;
    }

    //------------------------------------------------------------------------------------------------
    int GetActivePrfIndex(int scanNumber)
    {
        if (!m_PrfSetHz || m_PrfSetHz.Count() <= 1)
            return 0;
        int count = m_PrfSetHz.Count();
        if (scanNumber < 0)
            scanNumber = 0;
        int index = scanNumber - (scanNumber / count) * count;
        if (index < 0)
            index = 0;
        return index;
    }

    void AddElevationBeam(
        string name,
        float boresightDeg,
        float beamwidthDeg,
        float relativeGainDb)
    {
        RDF_RadarElevationBeam beam = new RDF_RadarElevationBeam();
        beam.m_Name = name;
        beam.m_BoresightDeg = boresightDeg;
        beam.m_BeamwidthDeg = Math.Max(0.1, beamwidthDeg);
        beam.m_RelativeGainDb = relativeGainDb;
        m_ElevationBeams.Insert(beam);
    }

    void ClearElevationBeams()
    {
        m_ElevationBeams.Clear();
    }

    float GetWavelengthM()
    {
        if (m_FrequencyHz <= 0.0)
            return 1.0;
        return RDF_RadarClutterModel.C_LIGHT / m_FrequencyHz;
    }

    float GetScanPeriodS()
    {
        if (m_ScanRpm <= 0.0)
            return 1000000000.0;
        return 60.0 / m_ScanRpm;
    }

    float GetProcessingGain()
    {
        float pulseCompression = m_BandwidthHz * m_PulseWidthS;
        if (pulseCompression < 1.0)
            pulseCompression = 1.0;

        int pulseCount = Math.Max(1, m_PulsesIntegrated);
        float integration = Math.Sqrt(pulseCount);
        if (m_CoherentIntegration)
            integration = pulseCount;
        return pulseCompression * integration;
    }

    float GetNoisePowerW()
    {
        // k*T0*B*F, T0=290 K.
        const float BOLTZMANN = 1.380649e-23;
        float bandwidth = Math.Max(1.0, m_BandwidthHz);
        float noiseFactor = RDF_RadarClutterModel.DbToLin(m_NoiseFigureDb);
        return BOLTZMANN * 290.0 * bandwidth * noiseFactor;
    }

    // Range resolution ≈ c/(2B); falls back to c*τ/2 when bandwidth is unset.
    float GetRangeBinM()
    {
        float c = RDF_RadarClutterModel.C_LIGHT;
        if (m_BandwidthHz > 1.0)
        {
            float binFromBw = c / (2.0 * m_BandwidthHz);
            if (binFromBw < 1.0)
                binFromBw = 1.0;
            return binFromBw;
        }
        float binFromPulse = c * m_PulseWidthS * 0.5;
        if (binFromPulse < 1.0)
            binFromPulse = 1.0;
        return binFromPulse;
    }

    void Validate()
    {
        m_FrequencyHz = Math.Max(1000000.0, m_FrequencyHz);
        m_PeakPowerW = Math.Max(0.0, m_PeakPowerW);
        m_AntennaGainDbi = Math.Clamp(m_AntennaGainDbi, -20.0, 80.0);
        m_AzimuthBeamwidthDeg = Math.Clamp(m_AzimuthBeamwidthDeg, 0.1, 360.0);
        m_SystemLossDb = Math.Max(0.0, m_SystemLossDb);
        m_NoiseFigureDb = Math.Max(0.0, m_NoiseFigureDb);
        m_PulseWidthS = Math.Max(0.000000001, m_PulseWidthS);
        m_BandwidthHz = Math.Max(1.0, m_BandwidthHz);
        m_PrfHz = Math.Max(1.0, m_PrfHz);
        m_PrfStaggerRatio = Math.Clamp(m_PrfStaggerRatio, 1.0, 2.0);
        if (!m_PrfSetHz)
            m_PrfSetHz = new array<float>();
        if (m_PrfSetHz.Count() == 0 && m_PrfStaggerRatio > 1.001)
        {
            m_PrfSetHz.Insert(m_PrfHz);
            m_PrfSetHz.Insert(m_PrfHz * m_PrfStaggerRatio);
        }
        for (int pi = 0; pi < m_PrfSetHz.Count(); pi++)
        {
            float p = m_PrfSetHz.Get(pi);
            if (p < 1.0)
                m_PrfSetHz.Set(pi, m_PrfHz);
        }
        m_PulsesIntegrated = Math.Max(1, m_PulsesIntegrated);
        m_ScanRpm = Math.Max(0.0, m_ScanRpm);
        m_MtiClutterFloor = Math.Clamp(m_MtiClutterFloor, 0.000001, 1.0);
        m_MtdClutterLeakage = Math.Clamp(m_MtdClutterLeakage, 0.000000001, 1.0);
        m_DopplerBinCount = Math.Clamp(m_DopplerBinCount, 4, 64);
        if (!m_ElevationBeams)
            m_ElevationBeams = new array<ref RDF_RadarElevationBeam>();
        if (m_ElevationBeams.Count() == 0)
            AddElevationBeam("main", 4.0, 8.0, 0.0);
    }

    static RDF_RadarHardware CreateShorad()
    {
        return new RDF_RadarHardware();
    }

    static RDF_RadarHardware CreateP18Like()
    {
        RDF_RadarHardware hardware = new RDF_RadarHardware();
        hardware.m_Name = "p18_like";
        hardware.m_FrequencyHz = 160000000.0;
        hardware.m_PeakPowerW = 250000.0;
        hardware.m_AntennaGainDbi = 18.0;
        hardware.m_AzimuthBeamwidthDeg = 6.0;
        hardware.m_SystemLossDb = 8.0;
        hardware.m_NoiseFigureDb = 6.0;
        hardware.m_PulseWidthS = 0.000006;
        hardware.m_BandwidthHz = 166666.0;
        hardware.m_PrfHz = 200.0;
        hardware.m_PulsesIntegrated = 8;
        hardware.m_ScanRpm = 6.0;
        hardware.m_MtiClutterFloor = 0.01;
        hardware.ClearElevationBeams();
        hardware.AddElevationBeam("low", 3.0, 12.0, 0.0);
        hardware.AddElevationBeam("high", 12.0, 16.0, -1.5);
        hardware.Validate();
        return hardware;
    }
}
