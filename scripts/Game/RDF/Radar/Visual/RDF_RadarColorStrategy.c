// Radar-specific colour strategy base class.
// Extends RDF_LidarColorStrategy with an extra method that accepts the full
// RDF_RadarSample, enabling colour coding by SNR, RCS, or Doppler velocity.
class RDF_RadarColorStrategy : RDF_LidarColorStrategy
{
    void RDF_RadarColorStrategy() {}

    // Route to the radar sample variant when the sample can be downcast.
    override int BuildPointColorFromSample(
        RDF_LidarSample sample,
        float lastRange,
        RDF_LidarVisualSettings settings)
    {
        RDF_RadarSample rs = RDF_RadarSample.Cast(sample);
        if (rs)
            return BuildPointColorFromRadarSample(rs, settings);

        return BuildPointColor(sample.m_Distance, sample.m_Hit, lastRange, settings);
    }

    // Subclasses override this to colour by radar-specific data.
    int BuildPointColorFromRadarSample(
        RDF_RadarSample sample,
        RDF_LidarVisualSettings settings)
    {
        return ARGBF(1.0, 1.0, 1.0, 1.0);
    }
}

// SNR colour strategy.
// Maps signal-to-noise ratio to a red -> yellow -> green -> cyan spectrum.
//   <  10 dB : red    -> yellow  (weak signal)
//  10-20 dB  : yellow -> green   (moderate)
//  20-30 dB  : green  -> cyan    (strong)
//   > 30 dB  : cyan              (very strong)
// Non-hits are rendered as dark grey.
class RDF_SNRColorStrategy : RDF_RadarColorStrategy
{
    override int BuildPointColorFromRadarSample(
        RDF_RadarSample sample,
        RDF_LidarVisualSettings settings)
    {
        if (!sample.m_Hit)
            return ARGB(64, 40, 40, 40);

        float snr = sample.m_SignalToNoiseRatio;

        if (snr < 10.0)
        {
            float t = Math.Clamp(snr / 10.0, 0.0, 1.0);
            return ARGBF(1.0, 1.0, t, 0.0);
        }
        else if (snr < 20.0)
        {
            float t = Math.Clamp((snr - 10.0) / 10.0, 0.0, 1.0);
            return ARGBF(1.0, 1.0 - t, 1.0, 0.0);
        }
        else if (snr < 30.0)
        {
            float t = Math.Clamp((snr - 20.0) / 10.0, 0.0, 1.0);
            return ARGBF(1.0, 0.0, 1.0, t);
        }

        return ARGB(255, 0, 255, 255);
    }

    override int BuildPointColor(
        float dist,
        bool hit,
        float lastRange,
        RDF_LidarVisualSettings settings)
    {
        if (!hit)
            return ARGB(64, 40, 40, 40);
        return ARGB(255, 0, 200, 0);
    }
}

// RCS colour strategy.
// Maps radar cross section to a blue -> cyan -> yellow -> red spectrum.
//   < -20 dBsm : blue   (very small, e.g. bird)
//  -20..0 dBsm : cyan   (person-sized)
//   0..20 dBsm : yellow (vehicle-sized)
//   > 20 dBsm  : red    (large target)
class RDF_RCSColorStrategy : RDF_RadarColorStrategy
{
    override int BuildPointColorFromRadarSample(
        RDF_RadarSample sample,
        RDF_LidarVisualSettings settings)
    {
        if (!sample.m_Hit)
            return ARGB(64, 20, 20, 30);

        float rcsDBsm = sample.GetRCSdBsm();
        // Map -20..+20 dBsm -> 0..1.
        float norm = Math.Clamp((rcsDBsm + 20.0) / 40.0, 0.0, 1.0);

        if (norm < 0.33)
        {
            float t = norm / 0.33;
            return ARGBF(1.0, 0.0, t, 1.0);
        }
        else if (norm < 0.66)
        {
            float t = (norm - 0.33) / 0.33;
            return ARGBF(1.0, t, 1.0, 1.0 - t);
        }

        float t = (norm - 0.66) / 0.34;
        return ARGBF(1.0, 1.0, 1.0 - t, 0.0);
    }

    override int BuildPointColor(
        float dist,
        bool hit,
        float lastRange,
        RDF_LidarVisualSettings settings)
    {
        if (!hit)
            return ARGB(64, 20, 20, 30);
        return ARGB(255, 0, 255, 128);
    }
}

// Doppler velocity colour strategy.
// Blue = receding, white = stationary, red = approaching.
//   Velocity range: +/-maxVelocity m/s mapped to 0..1.
class RDF_DopplerColorStrategy : RDF_RadarColorStrategy
{
    // Maximum displayed speed (m/s).
    float m_MaxVelocity = 50.0;

    void RDF_DopplerColorStrategy(float maxVelocity = 50.0)
    {
        m_MaxVelocity = Math.Max(maxVelocity, 1.0);
    }

    override int BuildPointColorFromRadarSample(
        RDF_RadarSample sample,
        RDF_LidarVisualSettings settings)
    {
        if (!sample.m_Hit)
            return ARGB(64, 15, 15, 25);

        float vel  = sample.m_TargetVelocity;
        // Normalise to 0..1 where 0 = max recede, 0.5 = zero, 1 = max approach.
        float norm = Math.Clamp((vel + m_MaxVelocity) / (2.0 * m_MaxVelocity), 0.0, 1.0);

        if (norm < 0.5)
        {
            // Blue to white (receding to stationary).
            float t = norm / 0.5;
            return ARGBF(1.0, t, t, 1.0);
        }

        // White to red (stationary to approaching).
        float t = (norm - 0.5) / 0.5;
        return ARGBF(1.0, 1.0, 1.0 - t, 1.0 - t);
    }

    override int BuildPointColor(
        float dist,
        bool hit,
        float lastRange,
        RDF_LidarVisualSettings settings)
    {
        if (!hit)
            return ARGB(64, 15, 15, 25);
        return ARGB(255, 255, 255, 255);
    }
}

// Composite radar colour strategy.
// Blends SNR brightness with Doppler hue.
// Strong approaching target = bright red; strong receding = bright blue.
class RDF_RadarCompositeColorStrategy : RDF_RadarColorStrategy
{
    override int BuildPointColorFromRadarSample(
        RDF_RadarSample sample,
        RDF_LidarVisualSettings settings)
    {
        if (!sample.m_Hit)
            return ARGB(32, 10, 10, 10);

        // Brightness driven by SNR (0 dB -> dim, 30 dB -> full brightness).
        float brightness = Math.Clamp(sample.m_SignalToNoiseRatio / 30.0, 0.1, 1.0);

        // Hue driven by radial velocity: +50 m/s = red, 0 = green, -50 m/s = blue.
        float vel  = Math.Clamp(sample.m_TargetVelocity, -50.0, 50.0);
        float norm = (vel + 50.0) / 100.0; // 0..1

        float r, g, b;
        if (norm < 0.5)
        {
            // Blue to green.
            float t = norm / 0.5;
            r = 0.0;
            g = t;
            b = 1.0 - t;
        }
        else
        {
            // Green to red.
            float t = (norm - 0.5) / 0.5;
            r = t;
            g = 1.0 - t;
            b = 0.0;
        }

        return ARGBF(1.0, r * brightness, g * brightness, b * brightness);
    }

    override int BuildPointColor(
        float dist,
        bool hit,
        float lastRange,
        RDF_LidarVisualSettings settings)
    {
        if (!hit)
            return ARGB(32, 10, 10, 10);
        return ARGB(255, 0, 200, 50);
    }
}
