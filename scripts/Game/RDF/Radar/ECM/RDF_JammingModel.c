// Electronic Counter-Measure (ECM) jamming models.
// Provides noise jamming, deceptive jamming (false targets), and
// active cancellation helpers for radar-vs-EW simulation.
class RDF_JammingModel
{
    void RDF_JammingModel() {}

    // Noise jamming - jamming-to-signal ratio (J/S) in dB.
    // A positive J/S means the jammer is stronger than the target echo.
    //
    // Simplified one-way jammer model (self-screening or stand-off):
    //   J = (Pj * Gj * Gr * lambda^2) / ((4*PI)^2 * Rj^2)
    //   normalised to radar bandwidth Br.
    static float CalculateNoiseJammingJS(
        float jammerPower,      // Pj (W)
        float jammerGain,       // Gj (dimensionless)
        float radarReceiveGain, // Gr (dimensionless)
        float wavelength,       // lambda (m)
        float jammerRange,      // Rj (m) - jammer-to-radar distance
        float radarBandwidth)   // Br (Hz)
    {
        if (jammerRange <= 0 || radarBandwidth <= 0 || wavelength <= 0)
            return -999.0;

        float fourPiSq = Math.Pow(4.0 * Math.PI, 2.0);

        float jammerPower_rx = (jammerPower * jammerGain * radarReceiveGain
                                * wavelength * wavelength)
                               / (fourPiSq * jammerRange * jammerRange
                                  * radarBandwidth);

        if (jammerPower_rx <= 0)
            return -999.0;

        return 10.0 * Math.Log10(jammerPower_rx);
    }

    // Apply noise jamming to a scan result.
    // Raises the effective noise floor by the J/S ratio (dB), suppressing
    // detections whose SNR falls below the raised floor.
    static void ApplyNoiseJamming(
        array<ref RDF_LidarSample> samples,
        float jsRatioDb,
        float detectionThresholdDB)
    {
        if (!samples)
            return;

        foreach (RDF_LidarSample base : samples)
        {
            RDF_RadarSample rs = RDF_RadarSample.Cast(base);
            if (!rs)
                continue;

            rs.m_SignalToNoiseRatio -= jsRatioDb;

            if (!rs.IsDetectable(detectionThresholdDB))
                rs.m_Hit = false;
        }
    }

    // Deceptive jamming - inject synthetic false-target samples.
    // Each false target has randomised range, RCS, and velocity within the
    // supplied bounds, with a configurable SNR to mimic a real detection.
    static void InjectFalseTargets(
        array<ref RDF_LidarSample> samples,
        int   falseTargetCount,
        float radarOriginX,
        float radarOriginY,
        float radarOriginZ,
        float minRange,
        float maxRange,
        float fakeSNR)
    {
        if (!samples || falseTargetCount <= 0)
            return;

        vector radarPos = Vector(radarOriginX, radarOriginY, radarOriginZ);

        for (int i = 0; i < falseTargetCount; i++)
        {
            RDF_RadarSample fake = new RDF_RadarSample();

            float dist    = minRange + Math.RandomFloat(0.0, maxRange - minRange);
            float azimuth = Math.RandomFloat(0.0, 360.0) * Math.DEG2RAD;
            vector dir    = Vector(Math.Cos(azimuth), 0.0, Math.Sin(azimuth));

            fake.m_Index             = -(i + 1); // Negative index flags false targets.
            fake.m_Hit               = true;
            fake.m_Start             = radarPos;
            fake.m_Dir               = dir;
            fake.m_Distance          = dist;
            fake.m_HitPos            = radarPos + dir * dist;
            fake.m_End               = fake.m_HitPos;
            fake.m_SignalToNoiseRatio = fakeSNR;
            fake.m_ReceivedPower     = 0.000001;
            fake.m_RadarCrossSection = Math.RandomFloat(1.0, 20.0);
            fake.m_TargetVelocity    = Math.RandomFloat(-30.0, 30.0);
            fake.m_DopplerFrequency  = 0.0;

            samples.Insert(fake);
        }
    }

    // Chaff RCS contribution.
    // Returns RCS augmentation (m^2) from a chaff cloud.
    //   sigma_chaff ~= N * lambda^2 / (8*PI)  (randomly oriented dipoles)
    // Parameters:
    //   numDipoles - number of individual chaff fibres deployed
    //   wavelength - radar wavelength (m)
    static float CalculateChaffRCS(int numDipoles, float wavelength)
    {
        if (numDipoles <= 0 || wavelength <= 0)
            return 0.0;

        return (float)numDipoles * wavelength * wavelength / (8.0 * Math.PI);
    }
}
