// Radar scan output sample - extends RDF_LidarSample with electromagnetic signal data.
// Produced by RDF_RadarScanner.Scan(); geometry fields are populated first,
// then radar-physics fields are filled by ApplyRadarPhysics().
class RDF_RadarSample : RDF_LidarSample
{
    // === Transmit / receive power ===
    // Transmitted signal power (W).
    float m_TransmitPower;
    // Power received back from the target (W).
    float m_ReceivedPower;
    // Signal-to-noise ratio (dB).
    float m_SignalToNoiseRatio;
    // Estimated target radar cross section (m^2).
    float m_RadarCrossSection;

    // === Propagation & losses ===
    // Free-space path loss (dB).
    float m_PathLoss;
    // Atmospheric gas absorption loss (dB).
    float m_AtmosphericLoss;
    // Rain attenuation loss (dB).
    float m_RainAttenuation;

    // === Phase & Doppler ===
    // Phase shift of the received signal (radians).
    float m_PhaseShift;
    // Doppler frequency shift (Hz).
    float m_DopplerFrequency;
    // Radial velocity of the target relative to the radar (m/s). Positive = approaching.
    float m_TargetVelocity;

    // === Reflection characteristics ===
    // Angle of incidence at the target surface (degrees).
    float m_IncidenceAngle;
    // Reflection coefficient (0 = absorber, 1 = perfect reflector).
    float m_ReflectionCoefficient;
    // Material type string obtained from the hit surface.
    string m_MaterialType;

    // === Polarisation ===
    // Polarisation type ("H" / "V" / "C").
    string m_PolarizationType;
    // Polarisation mismatch loss (dB).
    float m_PolarizationLoss;

    // === Timing ===
    // Two-way time of flight (s).
    float m_TimeOfFlight;
    // Relative propagation delay used for range-gating (s).
    float m_DelayTime;

    void RDF_RadarSample()
    {
        m_TransmitPower = 0;
        m_ReceivedPower = 0;
        m_SignalToNoiseRatio = -999.0;
        m_RadarCrossSection = 0;
        m_PathLoss = 0;
        m_AtmosphericLoss = 0;
        m_RainAttenuation = 0;
        m_PhaseShift = 0;
        m_DopplerFrequency = 0;
        m_TargetVelocity = 0;
        m_IncidenceAngle = 0;
        m_ReflectionCoefficient = 1.0;
        m_MaterialType = "";
        m_PolarizationType = "H";
        m_PolarizationLoss = 0;
        m_TimeOfFlight = 0;
        m_DelayTime = 0;
    }

    // Compute two-way time of flight from the stored distance.
    void CalculateTimeOfFlight()
    {
        const float SPEED_OF_LIGHT = 299792458.0;
        if (m_Distance > 0)
            m_TimeOfFlight = (2.0 * m_Distance) / SPEED_OF_LIGHT;
    }

    // Return true if this sample exceeds the detection SNR threshold (dB).
    bool IsDetectable(float thresholdDB)
    {
        if (!m_Hit)
            return false;
        return m_SignalToNoiseRatio >= thresholdDB;
    }

    // Return RCS in dBsm. Returns -999 when RCS is zero.
    float GetRCSdBsm()
    {
        if (m_RadarCrossSection <= 0)
            return -999.0;
        return 10.0 * Math.Log10(m_RadarCrossSection);
    }
}
