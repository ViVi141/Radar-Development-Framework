// Electromagnetic wave parameters for radar simulation.
// Encapsulates all physical properties of the EM signal used by the radar system.
class RDF_EMWaveParameters
{
    // Carrier frequency (Hz). Default: 10 GHz (X-band).
    float m_CarrierFrequency = 10000000000.0;
    // Wavelength (m) - auto-calculated via CalculateWavelength() or set manually.
    float m_Wavelength = 0.03;
    // Band designation (L/S/C/X/Ku/K/Ka/V/W).
    string m_BandName = "X";

    // Transmit power (W).
    float m_TransmitPower = 1000.0;
    // Pulse width (s) - for pulsed radar modes.
    float m_PulseWidth = 0.000001;
    // Pulse repetition frequency (Hz).
    float m_PRF = 1000.0;

    // Antenna gain (dBi).
    float m_AntennaGain = 30.0;
    // Azimuth beamwidth (degrees).
    float m_BeamwidthAzimuth = 3.0;
    // Elevation beamwidth (degrees).
    float m_BeamwidthElevation = 3.0;

    // Receiver sensitivity (dBm). Minimum detectable signal level.
    float m_ReceiverSensitivity = -90.0;
    // Receiver noise figure (dB).
    float m_NoiseFigure = 5.0;

    void RDF_EMWaveParameters() {}

    // Calculate wavelength from carrier frequency using lambda = c / f.
    void CalculateWavelength()
    {
        const float SPEED_OF_LIGHT = 299792458.0;
        if (m_CarrierFrequency > 0)
            m_Wavelength = SPEED_OF_LIGHT / m_CarrierFrequency;
    }

    // Determine and set band name from carrier frequency.
    void DetermineBand()
    {
        float freqGHz = m_CarrierFrequency / 1000000000.0;
        if (freqGHz < 2.0)
            m_BandName = "L";
        else if (freqGHz < 4.0)
            m_BandName = "S";
        else if (freqGHz < 8.0)
            m_BandName = "C";
        else if (freqGHz < 12.0)
            m_BandName = "X";
        else if (freqGHz < 18.0)
            m_BandName = "Ku";
        else if (freqGHz < 27.0)
            m_BandName = "K";
        else if (freqGHz < 40.0)
            m_BandName = "Ka";
        else if (freqGHz < 75.0)
            m_BandName = "V";
        else
            m_BandName = "W";
    }

    // Validate all parameters and clamp to physically plausible ranges.
    // Returns true if all parameters were already valid.
    bool Validate()
    {
        bool valid = true;

        if (m_CarrierFrequency <= 0)
        {
            m_CarrierFrequency = 10000000000.0;
            valid = false;
        }

        if (m_Wavelength <= 0)
        {
            CalculateWavelength();
            valid = false;
        }

        if (m_TransmitPower <= 0)
        {
            m_TransmitPower = 0.001;
            valid = false;
        }

        m_PulseWidth = Math.Clamp(m_PulseWidth, 0.000000001, 1.0);
        m_PRF = Math.Clamp(m_PRF, 1.0, 1000000.0);
        m_NoiseFigure = Math.Clamp(m_NoiseFigure, 0.0, 30.0);

        DetermineBand();

        return valid;
    }

    // Return a human-readable description for logging/debug.
    string GetDescription()
    {
        return string.Format("EMWave[%s %.1f GHz, Pt=%.0fW, G=%.1fdBi, wl=%.4fm]",
            m_BandName,
            m_CarrierFrequency / 1000000000.0,
            m_TransmitPower,
            m_AntennaGain,
            m_Wavelength);
    }

    // Approximate antenna pattern multiplier (linear) for given off-axis angles (deg).
    // - azOffDeg / elOffDeg: off-axis angles relative to boresight
    // Returns linear gain (not dB) = peakLinearGain * pattern(az,el).
    float GetAntennaGainLinear(float azOffDeg = 0.0, float elOffDeg = 0.0)
    {
        // Convert peak dBi to linear
        float peakLin = RDF_RadarEquation.DBiToLinear(m_AntennaGain);

        // Effective off-axis angle magnitude (deg)
        float offDeg = Math.Sqrt(azOffDeg * azOffDeg + elOffDeg * elOffDeg);

        // Convert beamwidth (FWHM) to sigma for Gaussian approximation
        float bw = Math.Max(m_BeamwidthAzimuth, 0.0001);
        float sigma = bw / (2.0 * Math.Sqrt(2.0 * Math.Log(2.0)));
        if (sigma <= 0.0)
            return peakLin;

        float pattern = Math.Pow(2.718281828, -0.5 * Math.Pow(offDeg / sigma, 2.0));

        // sidelobe floor approx -30 dB
        float floorLin = RDF_RadarEquation.DBiToLinear(-30.0);
        pattern = Math.Max(pattern, floorLin);

        return peakLin * pattern;
    }
}
