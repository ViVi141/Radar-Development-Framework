// Radar demo configuration - extends RDF_LidarDemoConfig with radar-specific
// preset factories for common radar types.
// Use RDF_LidarAutoRunner + ApplyTo() to drive the demo loop, or construct
// an RDF_RadarScanner directly from the settings embedded here.
class RDF_RadarDemoConfig : RDF_LidarDemoConfig
{
    // Radar-specific EM parameters (overlaid onto the base scanner).
    ref RDF_EMWaveParameters m_EMWaveParams;
    // Initial radar operating mode.
    ERadarMode m_RadarMode = ERadarMode.PULSE;
    // When true, Doppler processing is active.
    bool m_EnableDoppler = true;
    // When true, the SNR colour strategy is applied.
    bool m_UseRadarColors = true;
    // Maximum scan range (m). -1 means use a sensible per-preset default.
    float m_RadarRange = -1.0;
    // SNR detection threshold (dB). -1 = use RDF_RadarSettings default (10 dB).
    float m_DetectionThreshold = -1.0;
    // Minimum detection range (m). Returns closer than this are range-gated out.
    // -1 = use RDF_RadarSettings default (10 m).
    float m_MinRange = -1.0;
    // Enable ground/stationary clutter suppression filter.
    bool m_EnableClutterFilter = false;
    // Enable Moving Target Indicator - suppresses zero-Doppler returns.
    bool m_EnableMTI = false;

    void RDF_RadarDemoConfig()
    {
        m_MinTickInterval = 0.5;
        m_Verbose         = false;
    }

    // Build a configured RDF_RadarSettings from this demo config.
    // The caller can pass the result directly to RDF_RadarScanner,
    // or use BuildRadarScanner() to get a fully-configured scanner in one call.
    RDF_RadarSettings BuildRadarSettings()
    {
        RDF_RadarSettings s = new RDF_RadarSettings();

        s.m_Enabled = m_Enable;

        if (m_RayCount > 0)
            s.m_RayCount = m_RayCount;

        if (m_EMWaveParams)
            s.m_EMWaveParams = m_EMWaveParams;
        else
            s.m_EMWaveParams = new RDF_EMWaveParameters();

        s.m_RadarMode               = m_RadarMode;
        s.m_EnableDopplerProcessing = m_EnableDoppler;
        s.m_EnableClutterFilter     = m_EnableClutterFilter;
        s.m_EnableMTI               = m_EnableMTI;

        if (m_DetectionThreshold >= 0)
            s.m_DetectionThreshold = m_DetectionThreshold;

        if (m_MinRange >= 0)
            s.m_MinRange = m_MinRange;

        if (m_UpdateInterval > 0)
            s.m_UpdateInterval = m_UpdateInterval;

        // Apply radar range; fall back to 1000 m when not explicitly set.
        if (m_RadarRange > 0)
            s.m_Range = m_RadarRange;
        else
            s.m_Range = 1000.0;

        s.Validate();
        return s;
    }

    // Build a fully-configured RDF_RadarScanner from this demo config.
    // Applies the sample strategy and radar mode from the config fields so
    // that the returned scanner is ready to call Scan() without further setup.
    RDF_RadarScanner BuildRadarScanner()
    {
        RDF_RadarSettings settings = BuildRadarSettings();
        RDF_RadarScanner scanner = new RDF_RadarScanner(settings);

        // Apply the sample strategy (overrides the scanner's default UniformStrategy).
        if (m_SampleStrategy)
            scanner.SetSampleStrategy(m_SampleStrategy);

        // The mode processor is already instantiated inside the scanner constructor
        // via settings.m_RadarMode, so no extra step needed here.

        return scanner;
    }

    // X-band pulse search radar - typical ground/airspace surveillance.
    //   f = 10 GHz, Pt = 50 W (demo scale), G = 30 dBi.
    //   Clutter filter ON: suppresses stationary terrain returns.
    //   Detection threshold 30 dB: ignores low-SNR ground clutter.
    static RDF_RadarDemoConfig CreateXBandSearch(int rayCount = 512)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        cfg.m_Enable               = true;
        cfg.m_RayCount             = rayCount;
        cfg.m_MinTickInterval      = 0.5;
        cfg.m_SampleStrategy       = new RDF_SweepSampleStrategy(60.0, 30.0, 90.0);
        cfg.m_ColorStrategy        = new RDF_SNRColorStrategy();
        cfg.m_EnableClutterFilter  = true;   // suppress static ground returns
        cfg.m_EnableDoppler        = true;
        cfg.m_DetectionThreshold   = 30.0;   // 30 dB - filters weak clutter

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency  = 10000000000.0; // 10 GHz
        em.m_TransmitPower     = 5.0;            // 5 W demo scale: R=30m building -> ~97 dB
        em.m_AntennaGain       = 30.0;
        em.m_PulseWidth        = 0.000001;
        em.m_PRF               = 1000.0;
        em.CalculateWavelength();

        cfg.m_EMWaveParams   = em;
        cfg.m_RadarMode      = ERadarMode.PULSE;
        cfg.m_UpdateInterval = 0.5;
        cfg.m_RadarRange     = 3000.0;  // 3 km - typical ground search
        cfg.m_MinRange       = 30.0;    // 30 m blind zone (filters near-field building clutter)
        return cfg;
    }

    // Helicopter radar - X-band forward sector, clutter filter only (no MTI) so stationary vehicles are visible.
    //   f = 10 GHz, forward-looking sector +/-60 deg H, +/-15 deg V, 8 km range.
    //   MTI off: ground vehicles and static objects remain visible; clutter filter still suppresses terrain-only returns.
    static RDF_RadarDemoConfig CreateHelicopterRadar(int rayCount = 512)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        cfg.m_Enable               = true;
        cfg.m_RayCount             = rayCount;
        cfg.m_MinTickInterval      = 0.4;
        cfg.m_SampleStrategy       = new RDF_SweepSampleStrategy(60.0, 30.0, 90.0);  // forward sector
        cfg.m_ColorStrategy        = new RDF_DopplerColorStrategy(80.0);             // approaching=red, receding=blue
        cfg.m_EnableClutterFilter  = true;   // suppress terrain-only (no entity) returns
        cfg.m_EnableDoppler         = true;
        cfg.m_EnableMTI             = false;  // OFF: keep stationary vehicles and buildings visible
        cfg.m_DetectionThreshold    = 5.0;    // low: accuracy first, entities use 0 dB internally

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency  = 10000000000.0;  // 10 GHz X-band
        em.m_TransmitPower     = 500.0;           // 500 W typical heli radar
        em.m_AntennaGain       = 28.0;
        em.m_PulseWidth        = 0.000002;       // 2 us
        em.m_PRF               = 2000.0;
        em.CalculateWavelength();

        cfg.m_EMWaveParams   = em;
        cfg.m_RadarMode      = ERadarMode.PULSE;
        cfg.m_UpdateInterval = 0.4;
        cfg.m_RadarRange     = 8000.0;   // 8 km - tactical helicopter search
        cfg.m_MinRange       = 15.0;     // blind zone only for terrain; entities always shown
        return cfg;
    }

    // Ka-band FMCW automotive radar - short range, high update rate.
    //   f = 77 GHz, Pt = 100 mW, G = 25 dBi, conical FOV +/-15 deg.
    static RDF_RadarDemoConfig CreateAutomotiveRadar(int rayCount = 256)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        cfg.m_Enable          = true;
        cfg.m_RayCount        = rayCount;
        cfg.m_MinTickInterval = 0.1;
        cfg.m_SampleStrategy  = new RDF_ConicalSampleStrategy(15.0);
        cfg.m_ColorStrategy   = new RDF_DopplerColorStrategy(30.0);

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency  = 77000000000.0; // 77 GHz
        em.m_TransmitPower     = 0.1;
        em.m_AntennaGain       = 25.0;
        em.CalculateWavelength();

        cfg.m_EMWaveParams          = em;
        cfg.m_RadarMode             = ERadarMode.FMCW;
        cfg.m_UpdateInterval        = 0.1;
        cfg.m_RadarRange            = 150.0;  // 150 m - automotive short range
        cfg.m_MinRange              = 2.0;    // 2 m - FMCW can detect very close
        cfg.m_DetectionThreshold    = 15.0;   // moderate threshold for near-range
        cfg.m_EnableClutterFilter   = true;   // suppress road surface return
        return cfg;
    }

    // S-band weather/meteorological radar - wide beam, hemisphere coverage.
    //   f = 3 GHz, Pt = 50 kW, G = 40 dBi.
    static RDF_RadarDemoConfig CreateWeatherRadar(int rayCount = 1024)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        cfg.m_Enable          = true;
        cfg.m_RayCount        = rayCount;
        cfg.m_MinTickInterval = 2.0;
        cfg.m_SampleStrategy  = new RDF_HemisphereSampleStrategy();
        cfg.m_ColorStrategy   = new RDF_RCSColorStrategy();

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency  = 3000000000.0; // 3 GHz
        em.m_TransmitPower     = 50000.0;
        em.m_AntennaGain       = 40.0;
        em.CalculateWavelength();

        cfg.m_EMWaveParams          = em;
        cfg.m_RadarMode             = ERadarMode.PULSE;
        cfg.m_EnableDoppler         = true;
        cfg.m_UpdateInterval        = 2.0;
        cfg.m_RadarRange            = 5000.0;  // 5 km - wide area weather coverage
        cfg.m_MinRange              = 50.0;    // 50 m blind zone for weather radar
        cfg.m_DetectionThreshold    = 10.0;    // low threshold to see precipitation
        return cfg;
    }

    // C-band electronically-scanned phased-array radar.
    //   f = 5.6 GHz, Pt = 10 kW, G = 35 dBi, wide sector sweep.
    static RDF_RadarDemoConfig CreatePhasedArrayRadar(int rayCount = 2048)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        cfg.m_Enable          = true;
        cfg.m_RayCount        = rayCount;
        cfg.m_MinTickInterval = 0.2;
        cfg.m_SampleStrategy  = new RDF_SweepSampleStrategy(60.0, 30.0, 120.0);
        cfg.m_ColorStrategy   = new RDF_RadarCompositeColorStrategy();

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency  = 5600000000.0; // 5.6 GHz
        em.m_TransmitPower     = 10000.0;
        em.m_AntennaGain       = 35.0;
        em.CalculateWavelength();

        cfg.m_EMWaveParams          = em;
        cfg.m_RadarMode             = ERadarMode.PHASED_ARRAY;
        cfg.m_EnableDoppler         = true;
        cfg.m_EnableMTI             = true;    // MTI: highlight only moving targets
        cfg.m_UpdateInterval        = 0.2;
        cfg.m_RadarRange            = 2000.0;  // 2 km - multi-target tracking
        cfg.m_MinRange              = 15.0;
        cfg.m_DetectionThreshold    = 20.0;
        return cfg;
    }

    // L-band long-range surveillance radar.
    //   f = 1.3 GHz, Pt = 500 kW, G = 33 dBi.
    static RDF_RadarDemoConfig CreateLBandSurveillance(int rayCount = 512)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        cfg.m_Enable          = true;
        cfg.m_RayCount        = rayCount;
        cfg.m_MinTickInterval = 1.0;
        cfg.m_SampleStrategy  = new RDF_UniformSampleStrategy();
        cfg.m_ColorStrategy   = new RDF_SNRColorStrategy();

        RDF_EMWaveParameters em = new RDF_EMWaveParameters();
        em.m_CarrierFrequency  = 1300000000.0; // 1.3 GHz
        em.m_TransmitPower     = 500000.0;
        em.m_AntennaGain       = 33.0;
        em.m_PRF               = 300.0;
        em.CalculateWavelength();

        cfg.m_EMWaveParams          = em;
        cfg.m_RadarMode             = ERadarMode.PULSE;
        cfg.m_EnableDoppler         = true;
        cfg.m_EnableClutterFilter   = true;    // L-band benefits from clutter rejection
        cfg.m_UpdateInterval        = 1.0;
        cfg.m_RadarRange            = 5000.0;  // 5 km - long range surveillance
        cfg.m_MinRange              = 100.0;   // 100 m blind zone for long-range pulse
        cfg.m_DetectionThreshold    = 25.0;    // strict threshold at long range
        return cfg;
    }
}
