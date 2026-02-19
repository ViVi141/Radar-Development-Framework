// Radar operating mode enumeration.
enum ERadarMode
{
    PULSE        = 0, // Pulsed radar - range by TOF, limited by PRF.
    CW           = 1, // Continuous wave - velocity only (Doppler), no ranging.
    FMCW         = 2, // Frequency-modulated CW - simultaneous range & velocity.
    PHASED_ARRAY = 3  // Electronically steered phased-array beam.
}

// Radar scanner configuration - extends RDF_LidarSettings with EM-wave and signal-processing settings.
class RDF_RadarSettings : RDF_LidarSettings
{
    // Electromagnetic wave parameters (frequency, power, antenna).
    ref RDF_EMWaveParameters m_EMWaveParams;

    // === Operating mode ===
    ERadarMode m_RadarMode = ERadarMode.PULSE;

    // === Signal processing ===
    // Detection SNR threshold (dB). Samples below this are treated as no-hit.
    float m_DetectionThreshold = 10.0;
    // Hardware system loss (dB): cables, connectors, filter insertion loss, etc.
    // This is the ONLY loss applied to the radar equation L parameter.
    // Do NOT include FSPL here - it is already in the R^4 term of the equation.
    float m_SystemLossDB = 6.0;
    // Enable Doppler frequency-shift computation.
    bool m_EnableDopplerProcessing = true;
    // Moving Target Indicator - suppress returns with |Doppler| < MinTargetVelocity.
    bool m_EnableMTI = false;
    // Moving Target Detection (threshold-based Doppler filter).
    bool m_EnableMTD = false;

    // === Environment model ===
    // Enable ITU-R atmospheric gas attenuation model.
    bool m_EnableAtmosphericModel = false;
    // Ambient temperature (deg C).
    float m_Temperature = 20.0;
    // Relative humidity (%).
    float m_Humidity = 60.0;
    // Rain rate (mm/h). Zero = no rain.
    float m_RainRate = 0.0;

    // === Target signature ===
    // Enable geometry + material RCS estimation.
    bool m_UseRCSModel = true;
    // Apply material reflectivity correction to RCS.
    bool m_UseMaterialReflection = true;

    // === Clutter rejection ===
    // Enable clutter / ground-return suppression (terrain-only returns not shown as target).
    bool m_EnableClutterFilter = true;
    // Minimum radial speed (m/s) for a target to survive MTI / clutter filter.
    float m_MinTargetVelocity = 1.0;

    // === Antenna pattern / sidelobes ===
    // Enable emission of low-power sidelobe rays around main beam (PoC).
    bool m_EnableSidelobes = false;
    // Number of sidelobe samples around main beam (PoC).
    int  m_SidelobeSampleCount = 6;
    // Relative sidelobe power fraction per side ray (total sidelobe power = m_SidelobeFraction * Pt).
    float m_SidelobeFraction = 0.01; // 1% of transmit power total, distributed across samples

    // === CFAR (constant false alarm rate) detection ===
    // Enable CFAR local-noise adaptive detection (post-scan).
    bool  m_EnableCFAR = false;
    // Use Order-Statistic CFAR (OS-CFAR) instead of CA-CFAR when true.
    bool  m_CfarUseOrderStatistic = false;
    // When true, compute CA‑CFAR multiplier from target Pfa automatically.
    bool  m_CfarAutoScale = false;
    // Target probability of false alarm used when m_CfarAutoScale == true.
    float m_CfarTargetPfa = 1e-6;
    // Number of reference cells to use for CFAR (nearest neighbours).
    int   m_CfarWindowSize = 16;
    // Angular guard radius (degrees) to exclude near neighbours from reference set.
    float m_CfarGuardAngleDeg = 2.0;
    // Detection threshold multiplier applied to the mean/reference power.
    float m_CfarMultiplier = 6.0;
    // OS-CFAR rank (1 = largest, windowSize = smallest). Default ~median.
    int   m_CfarOrderRank = 8;

    // === Offline OS‑CFAR lookup table ===
    // When true, the scanner will attempt to load an offline CSV lookup table
    // at startup to fill OS multiplier cache. If the file is missing, runtime
    // estimator remains available as a fallback.
    bool  m_CfarUseOfflineTable = true;
    // Path to CSV file containing precomputed multipliers (window,rank,pfa,multiplier)
    string m_CfarOfflineTablePath = "scripts/Game/RDF/Radar/Data/os_cfar_multipliers.csv";

    // === Blind-speed filtering ===
    // Drop targets whose Doppler aliases to (near) zero due to PRF sampling.
    bool  m_EnableBlindSpeedFilter = false;
    // Tolerance for aliased doppler to be considered a blind speed (Hz).
    float m_BlindSpeedToleranceHz = 1.0;

    // === Range gate ===
    // Minimum detection range (m). Returns closer than this are gated out.
    // Prevents near-field SNR saturation and models the radar blind zone
    // caused by the transmit pulse occupying the receiver.
    // Default 10 m is appropriate for short-pulse demo radars.
    float m_MinRange = 10.0;

    void RDF_RadarSettings()
    {
        // Override LiDAR defaults for typical radar use.
        m_Range = 500.0;
        m_RayCount = 512;
        m_UpdateInterval = 1.0;

        m_EMWaveParams = new RDF_EMWaveParameters();
    }

    // Validate all settings; also validates the nested EM-wave parameters.
    override void Validate()
    {
        // Save desired range before calling base Validate(), which caps at 1000 m.
        float desiredRange = m_Range;

        super.Validate();

        // Radar-specific range limit: up to 100 km (override base 1000 m cap).
        m_Range = Math.Clamp(desiredRange, 0.1, 100000.0);

        m_DetectionThreshold = Math.Clamp(m_DetectionThreshold, -30.0, 60.0);
        m_Temperature = Math.Clamp(m_Temperature, -80.0, 60.0);
        m_Humidity = Math.Clamp(m_Humidity, 0.0, 100.0);
        m_RainRate = Math.Max(m_RainRate, 0.0);
        m_MinTargetVelocity = Math.Max(m_MinTargetVelocity, 0.0);
        m_MinRange = Math.Clamp(m_MinRange, 0.0, m_Range);

        // CFAR / blind-speed parameter clamping (PoC)
        m_CfarWindowSize = Math.Clamp(m_CfarWindowSize, 1, 1024);
        m_CfarGuardAngleDeg = Math.Clamp(m_CfarGuardAngleDeg, 0.0, 180.0);
        m_CfarMultiplier = Math.Max(m_CfarMultiplier, 1.0);
        m_CfarOrderRank = Math.Clamp(m_CfarOrderRank, 1, Math.Max(1, m_CfarWindowSize));
        m_BlindSpeedToleranceHz = Math.Max(m_BlindSpeedToleranceHz, 0.0);

        if (!m_EMWaveParams)
            m_EMWaveParams = new RDF_EMWaveParameters();

        m_EMWaveParams.Validate();
    }
}
