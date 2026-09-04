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
    // Polarization mode for match-table lookup (H / V / circular).
    ERDF_RadarPolarization m_PolarizationMode = ERDF_RadarPolarization.RDF_POL_H;
    // Extra polarization trim (linear, ≤1). Multiplies the mode match factor.
    float m_PolarizationFactor = 1.0;
    // One-way peak sidelobe floor (dB relative to boresight). Gaussian mainlobe
    // is floored to this level for off-axis pattern / EW coupling.
    float m_SidelobeLevelDb = -25.0;
    float m_PulseWidthS = 0.0000005;
    // Extra T/R switch / duplexer recovery after the transmit pulse (s).
    // Min range uses uncompressed TX time: Rmin = c·(τ + recovery)/2.
    // Pulse compression shrinks range bins, not this blanking interval.
    float m_ReceiverRecoveryS = 0.0;
    float m_BandwidthHz = 4000000.0;
    float m_PrfHz = 4000.0;
    // Optional multi-PRF set (Hz). Empty → use m_PrfHz only. Stagger cycles by scan.
    ref array<float> m_PrfSetHz;
    // If >1 and set empty, build a 2-PRF stagger: m_PrfHz and m_PrfHz*ratio.
    float m_PrfStaggerRatio = 1.0;
    // Optional intra-band hop set (Hz). Empty + hop active → m_FrequencyHz and
    // m_FrequencyHz * m_HopStaggerRatio. Hop does not re-scale G / beamwidth.
    ref array<float> m_HopSetHz;
    float m_HopStaggerRatio = 1.05;
    // Authoring: hop every scan (ECCM frequency agility also sets hop active).
    bool m_FrequencyHopEnabled = false;
    // When true, GetAntennaGainDbiAtFrequency scales G as f² (same physical
    // aperture). Dual-band factories must leave this off (separate antennas).
    bool m_ScaleApertureWithFrequency = false;
    // Runtime: Sensor ORs authored hop with the last ECCM frequency decision.
    protected bool m_HopActive;
    int m_PulsesIntegrated = 32;
    bool m_CoherentIntegration = false;
    float m_ScanRpm = 15.0;
    ERDF_RadarSearchMode m_SearchMode = ERDF_RadarSearchMode.RDF_SEARCH_SURVEIL;
    float m_LpiPeakPowerScale = 0.15;
    float m_LpiScanRpmScale = 0.5;
    float m_MtiClutterFloor = 0.0001;
    // Non-zero MTD bins: DEM clutter × this leakage (not the global TwoPulse floor).
    float m_MtdClutterLeakage = 0.000001;
    // Clutter radial velocity spread used when deriving leakage (m/s).
    float m_ClutterSigmaVrMs = 0.5;
    // When true, Validate() sets m_MtdClutterLeakage from σ_vr (unless calib applied).
    bool m_DeriveMtdLeakageFromSigmaVr = false;
    // Prefer profile / packaged HwCalib scalars when present (workshop .conf).
    bool m_LoadHwCalibFromProfile = false;
    bool m_HwCalibApplied;
    bool m_EnableMti = true;
    // Default TwoPulse keeps legacy / GBRS demos unchanged.
    ERDF_MtiMode m_MtiMode = ERDF_MtiMode.RDF_MTI_TWOPULSE;
    // Classic canceller: max gain over m_PrfSetHz (de-blind). MTD ignores this.
    bool m_MtiStaggerDeblind = false;
    // DFT / filter-bank length when m_MtiMode == MtdBank (8–32 typical).
    int m_DopplerBinCount = 16;
    ref array<ref RDF_RadarElevationBeam> m_ElevationBeams;

    void RDF_RadarHardware()
    {
        m_ElevationBeams = new array<ref RDF_RadarElevationBeam>();
        m_PrfSetHz = new array<float>();
        m_HopSetHz = new array<float>();
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

    //------------------------------------------------------------------------------------------------
    int GetScanNumber(float worldTimeS)
    {
        float scanPeriod = GetScanPeriodS();
        if (scanPeriod >= 1000000.0)
            return 0;
        int n = Math.Floor(worldTimeS / scanPeriod);
        if (n < 0)
            n = 0;
        return n;
    }

    //------------------------------------------------------------------------------------------------
    void SetHopActive(bool active)
    {
        m_HopActive = active;
    }

    //------------------------------------------------------------------------------------------------
    bool IsHopActive()
    {
        return m_HopActive;
    }

    //------------------------------------------------------------------------------------------------
    // Carrier for this scan. Hop cycles m_HopSetHz (or a 5% pair) without
    // mutating authored G / beamwidth. Matches rdf_radar_channel.scan_frequency_hz.
    float GetScanFrequencyHz(int scanNumber)
    {
        if (!m_HopActive)
            return m_FrequencyHz;

        int count = 0;
        if (m_HopSetHz)
            count = m_HopSetHz.Count();
        if (count <= 0)
        {
            if (m_HopStaggerRatio <= 1.001)
                return m_FrequencyHz;
            int bit = 0;
            if (scanNumber > 0)
                bit = scanNumber - (scanNumber / 2) * 2;
            if (bit == 0)
                return m_FrequencyHz;
            float hopped = m_FrequencyHz * m_HopStaggerRatio;
            if (hopped < 1000000.0)
                hopped = 1000000.0;
            return hopped;
        }

        int index = 0;
        if (count > 1)
        {
            if (scanNumber < 0)
                scanNumber = 0;
            index = scanNumber - (scanNumber / count) * count;
            if (index < 0)
                index = 0;
        }
        float freq = m_HopSetHz.Get(index);
        if (freq < 1000000.0)
            freq = m_FrequencyHz;
        return freq;
    }

    //------------------------------------------------------------------------------------------------
    float GetWavelengthAtFrequencyM(float frequencyHz)
    {
        if (frequencyHz <= 0.0)
            return 1.0;
        return RDF_RadarClutterModel.C_LIGHT / frequencyHz;
    }

    //------------------------------------------------------------------------------------------------
    // Same-aperture G(f) ∝ f². Default off so hop / dual-band antennas stay put.
    float GetAntennaGainDbiAtFrequency(float frequencyHz)
    {
        if (!m_ScaleApertureWithFrequency)
            return m_AntennaGainDbi;
        float f0 = m_FrequencyHz;
        if (f0 < 1.0)
            return m_AntennaGainDbi;
        if (frequencyHz < 1.0)
            return m_AntennaGainDbi;
        float ratio = frequencyHz / f0;
        if (ratio <= 0.0)
            return m_AntennaGainDbi;
        float gain = m_AntennaGainDbi + 20.0 * Math.Log10(ratio);
        return Math.Clamp(gain, -20.0, 80.0);
    }

    //------------------------------------------------------------------------------------------------
    // Authoring helper: retune this aperture once (G += 20 log10(f/f0), HPBW ∝ 1/f).
    // Dual-band factories must not call this — they are separate antennas.
    void ScaleApertureToFrequency(float newFrequencyHz)
    {
        if (newFrequencyHz < 1000000.0)
            return;
        float oldHz = m_FrequencyHz;
        if (oldHz < 1.0)
        {
            m_FrequencyHz = newFrequencyHz;
            return;
        }
        float ratio = newFrequencyHz / oldHz;
        if (ratio <= 0.0)
            return;
        m_AntennaGainDbi = m_AntennaGainDbi + 20.0 * Math.Log10(ratio);
        m_AzimuthBeamwidthDeg = m_AzimuthBeamwidthDeg / ratio;
        m_FrequencyHz = newFrequencyHz;
        Validate();
    }

    //------------------------------------------------------------------------------------------------
    // Engineering band tag from carrier. Matches rdf_radar_channel.band_for_frequency
    // (VHF / UHF / L / S / C / X / Ku / K / Ka).
    string GetBand()
    {
        return RDF_RadarSurfaceTable.BandNameFromFrequencyHz(m_FrequencyHz);
    }

    float GetScanPeriodS()
    {
        float rpm = GetEffectiveScanRpm();
        if (rpm <= 0.0)
            return 1000000000.0;
        return 60.0 / rpm;
    }

    //------------------------------------------------------------------------------------------------
    float GetEffectiveScanRpm()
    {
        float rpm = m_ScanRpm;
        if (rpm <= 0.0)
            return 0.0;
        if (m_SearchMode == ERDF_RadarSearchMode.RDF_SEARCH_LPI)
            rpm = rpm * m_LpiScanRpmScale;
        if (rpm < 0.0)
            rpm = 0.0;
        return rpm;
    }

    //------------------------------------------------------------------------------------------------
    float GetEffectivePeakPowerW()
    {
        float p = m_PeakPowerW;
        if (m_SearchMode == ERDF_RadarSearchMode.RDF_SEARCH_LPI)
            p = p * m_LpiPeakPowerScale;
        if (p < 0.0)
            p = 0.0;
        return p;
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

    //------------------------------------------------------------------------------------------------
    // Transmit blanking interval. Receiver is closed for the pulse plus
    // optional duplexer recovery. Compressed bandwidth does not apply here.
    float GetTxBlankingTimeS()
    {
        float blankS = m_PulseWidthS;
        if (blankS < 0.0)
            blankS = 0.0;
        if (m_ReceiverRecoveryS > 0.0)
            blankS = blankS + m_ReceiverRecoveryS;
        return blankS;
    }

    // Near-range eclipsing / 近程盲区. Rmin = c·τ_blank/2.
    float GetMinDetectableRangeM()
    {
        float blankS = GetTxBlankingTimeS();
        if (blankS <= 0.0)
            return 0.0;
        return RDF_RadarClutterModel.C_LIGHT * blankS * 0.5;
    }

    // Unambiguous range R = c / (2·PRF). Uses primary PRF (not stagger cycle).
    float GetUnambiguousRangeM()
    {
        float prf = m_PrfHz;
        if (prf < 1.0)
            return 1000000000.0;
        return RDF_RadarClutterModel.C_LIGHT / (2.0 * prf);
    }

    //------------------------------------------------------------------------------------------------
    // Unambiguous radial speed |v| < λ·PRF/4.
    float GetUnambiguousVelocityMs()
    {
        float prf = m_PrfHz;
        if (prf < 1.0)
            return 1000000000.0;
        return GetWavelengthM() * prf * 0.25;
    }

    void Validate()
    {
        m_FrequencyHz = Math.Max(1000000.0, m_FrequencyHz);
        m_PeakPowerW = Math.Max(0.0, m_PeakPowerW);
        m_AntennaGainDbi = Math.Clamp(m_AntennaGainDbi, -20.0, 80.0);
        m_AzimuthBeamwidthDeg = Math.Clamp(m_AzimuthBeamwidthDeg, 0.1, 360.0);
        m_SystemLossDb = Math.Max(0.0, m_SystemLossDb);
        m_NoiseFigureDb = Math.Max(0.0, m_NoiseFigureDb);
        if (m_PolarizationMode != ERDF_RadarPolarization.RDF_POL_H)
        {
            if (m_PolarizationMode != ERDF_RadarPolarization.RDF_POL_V)
            {
                if (m_PolarizationMode != ERDF_RadarPolarization.RDF_POL_CIRCULAR)
                    m_PolarizationMode = ERDF_RadarPolarization.RDF_POL_H;
            }
        }
        m_PolarizationFactor = Math.Clamp(m_PolarizationFactor, 0.05, 1.0);
        m_SidelobeLevelDb = Math.Clamp(m_SidelobeLevelDb, -80.0, 0.0);
        m_PulseWidthS = Math.Max(0.000000001, m_PulseWidthS);
        m_ReceiverRecoveryS = Math.Max(0.0, m_ReceiverRecoveryS);
        m_BandwidthHz = Math.Max(1.0, m_BandwidthHz);
        m_PrfHz = Math.Max(1.0, m_PrfHz);
        m_PrfStaggerRatio = Math.Clamp(m_PrfStaggerRatio, 1.0, 2.0);
        m_HopStaggerRatio = Math.Clamp(m_HopStaggerRatio, 1.0, 1.5);
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
        if (!m_HopSetHz)
            m_HopSetHz = new array<float>();
        for (int hi = 0; hi < m_HopSetHz.Count(); hi++)
        {
            float hf = m_HopSetHz.Get(hi);
            if (hf < 1000000.0)
                m_HopSetHz.Set(hi, m_FrequencyHz);
        }
        m_PulsesIntegrated = Math.Max(1, m_PulsesIntegrated);
        m_ScanRpm = Math.Max(0.0, m_ScanRpm);
        m_LpiPeakPowerScale = Math.Clamp(m_LpiPeakPowerScale, 0.01, 1.0);
        m_LpiScanRpmScale = Math.Clamp(m_LpiScanRpmScale, 0.05, 1.0);
        if (m_SearchMode != ERDF_RadarSearchMode.RDF_SEARCH_LPI)
            m_SearchMode = ERDF_RadarSearchMode.RDF_SEARCH_SURVEIL;
        m_MtiClutterFloor = Math.Clamp(m_MtiClutterFloor, 0.000001, 1.0);
        m_ClutterSigmaVrMs = Math.Clamp(m_ClutterSigmaVrMs, 0.05, 8.0);
        m_DopplerBinCount = Math.Clamp(m_DopplerBinCount, 4, 64);

        if (m_LoadHwCalibFromProfile && !m_HwCalibApplied)
            RDF_RadarHwCalib.TryApplyFromProfile(this);

        if (m_DeriveMtdLeakageFromSigmaVr && !m_HwCalibApplied)
        {
            m_MtdClutterLeakage = RDF_RadarHwCalib.SuggestMtdLeakage(
                m_ClutterSigmaVrMs,
                GetWavelengthM(),
                m_PrfHz,
                129);
            int cancellerOrder = 1;
            if (m_MtiMode == ERDF_MtiMode.RDF_MTI_THREE_PULSE)
                cancellerOrder = 2;
            m_MtiClutterFloor = RDF_RadarHwCalib.SuggestMtiClutterFloor(
                m_ClutterSigmaVrMs,
                GetWavelengthM(),
                m_PrfHz,
                cancellerOrder);
        }

        m_MtdClutterLeakage = Math.Clamp(m_MtdClutterLeakage, 0.000000001, 1.0);
        m_MtiClutterFloor = Math.Clamp(m_MtiClutterFloor, 0.000001, 1.0);
        if (!m_ElevationBeams)
            m_ElevationBeams = new array<ref RDF_RadarElevationBeam>();
        if (m_ElevationBeams.Count() == 0)
            AddElevationBeam("main", 4.0, 8.0, 0.0);
    }

    static RDF_RadarHardware CreateShorad()
    {
        return new RDF_RadarHardware();
    }

    // VHF early-warning example. GetBand() resolves to "VHF" from 160 MHz.
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
