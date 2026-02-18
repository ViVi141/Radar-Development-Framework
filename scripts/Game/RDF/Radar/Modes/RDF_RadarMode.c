// Radar operating mode base class and concrete implementations.
// Each mode applies waveform-specific signal processing to an RDF_RadarSample
// after the base radar scanner has computed power, SNR, and Doppler.
class RDF_RadarMode
{
    void RDF_RadarMode() {}

    // Apply mode-specific processing to a single sample.
    void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        // Base implementation is a no-op; override in subclasses.
    }

    // Human-readable mode name for logging and UI.
    string GetModeName()
    {
        return "Base";
    }
}

// Pulsed radar mode.
// Measures range via time-of-flight. Range is unambiguous only up to
//   R_max = c / (2 * PRF).
// Echoes beyond this distance fold back (range aliasing).
class RDF_PulseRadarMode : RDF_RadarMode
{
    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        if (!sample || !settings || !settings.m_EMWaveParams)
            return;

        const float SPEED_OF_LIGHT = 299792458.0;

        float prf        = settings.m_EMWaveParams.m_PRF;
        float pulseWidth = settings.m_EMWaveParams.m_PulseWidth;

        // Range resolution: dR = c * tau / 2.
        float rangeResolution = (SPEED_OF_LIGHT * pulseWidth) / 2.0;

        // Maximum unambiguous range: R_ua = c / (2 * PRF).
        float maxUnambiguousRange = SPEED_OF_LIGHT / (2.0 * Math.Max(prf, 0.001));

        // Suppress returns beyond the unambiguous range.
        if (sample.m_Distance > maxUnambiguousRange)
        {
            sample.m_Hit = false;
            return;
        }

        // Quantise measured distance to the range resolution cell.
        if (rangeResolution > 0)
        {
            float cells = sample.m_Distance / rangeResolution;
            sample.m_Distance = Math.Floor(cells) * rangeResolution;
        }
    }

    override string GetModeName()
    {
        return "Pulse";
    }
}

// Continuous wave (CW) radar mode.
// Has no inherent range measurement; only Doppler (velocity) is available.
// Stationary targets are suppressed; range is set to zero.
class RDF_CWRadarMode : RDF_RadarMode
{
    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        if (!sample || !settings)
            return;

        // CW requires Doppler processing to be enabled.
        if (!settings.m_EnableDopplerProcessing)
        {
            sample.m_Hit = false;
            return;
        }

        // Suppress non-moving targets.
        if (Math.AbsFloat(sample.m_TargetVelocity) < settings.m_MinTargetVelocity)
        {
            sample.m_Hit = false;
            return;
        }

        // CW cannot determine range - invalidate the distance field.
        sample.m_Distance = 0.0;
    }

    override string GetModeName()
    {
        return "CW";
    }
}

// Frequency-modulated CW (FMCW) radar mode.
// Simultaneous range and velocity via beat-frequency analysis.
// Range resolution: dR = c / (2 * B).
class RDF_FMCWRadarMode : RDF_RadarMode
{
    // Sweep bandwidth (Hz). Determines range resolution.
    float m_SweepBandwidth = 500000000.0; // 500 MHz default.

    void RDF_FMCWRadarMode(float sweepBandwidth = 500000000.0)
    {
        m_SweepBandwidth = Math.Max(sweepBandwidth, 1000000.0);
    }

    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        if (!sample || !settings)
            return;

        const float SPEED_OF_LIGHT = 299792458.0;

        // Range resolution: dR = c / (2 * B).
        float rangeResolution = SPEED_OF_LIGHT / (2.0 * m_SweepBandwidth);

        // Quantise range to resolution cell.
        if (rangeResolution > 0 && sample.m_Distance > 0)
        {
            float cells = sample.m_Distance / rangeResolution;
            sample.m_Distance = Math.Floor(cells) * rangeResolution;
        }
    }

    override string GetModeName()
    {
        return "FMCW";
    }
}

// Phased-array radar mode.
// Electronic beam steering; scan-loss correction applied for multi-beam configs.
class RDF_PhasedArrayMode : RDF_RadarMode
{
    // Number of simultaneous beams (affects SNR via reduced dwell time).
    int m_BeamCount = 1;

    void RDF_PhasedArrayMode(int beamCount = 1)
    {
        m_BeamCount = Math.Max(beamCount, 1);
    }

    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        if (!sample || !settings)
            return;

        // Multiple beams reduce dwell time per beam -> SNR penalty.
        if (m_BeamCount > 1)
        {
            float scanLossDB = 10.0 * Math.Log10((float)m_BeamCount);
            sample.m_SignalToNoiseRatio -= scanLossDB;

            // Re-evaluate detectability after the scan-loss penalty.
            if (!sample.IsDetectable(settings.m_DetectionThreshold))
                sample.m_Hit = false;
        }
    }

    override string GetModeName()
    {
        return "Phased Array";
    }
}
