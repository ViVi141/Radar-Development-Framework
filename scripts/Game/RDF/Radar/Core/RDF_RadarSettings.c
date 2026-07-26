// Core scan settings for radar (entity-first, visibility ray + optional NLOS).
class RDF_RadarSettings
{
    bool m_Enabled = true;
    float m_Range = 2000.0;
    float m_UpdateInterval = 0.2;
    float m_SectorHalfAngleDeg = 45.0;
    vector m_OriginOffset = "0 0 0";
    bool m_UseBoundsCenter = true;
    bool m_UseLocalOffset = true;
    bool m_IncludeVehicles = true;
    bool m_IncludeProjectiles = true;
    bool m_IncludeRadarEmitters = true;
    // Read candidates from the global scatterer table (incremental discovery)
    // instead of searching the world on every scan.
    bool m_UseScattererRegistry = true;
    // Discovery sweep radius = m_Range * this factor.
    float m_ScattererDiscoveryRangeScale = 1.25;
    float m_ScattererDiscoveryIntervalS = 3.0;
    int m_ScattererClassifyPerTick = 24;
    int m_ScattererRefreshPerTick = 96;
    int m_ScattererMaxEntries = 512;
    // Legacy per-scan search path; only used when the registry is disabled.
    bool m_UseSphereQuery = true;
    // Only call GetActiveEntities when sphere query is off, or when sphere
    // returned nothing (fallback). Do NOT merge both every scan — that hitch.
    bool m_SphereQueryAlsoActive = false;
    int m_MaxTargets = 64;
    // Cap TraceMove calls per scan to bound hitch size in dense scenes.
    int m_MaxLosTracesPerScan = 48;
    float m_MinDistance = 0.5;
    ref RDF_RadarHardware m_Hardware;
    bool m_EnablePhysicalDetection = true;
    bool m_EnableMechanicalScan = false;
    float m_DetectionSnrDb = 8.0;
    bool m_KeepUndetected = false;
    bool m_EnableDemClutter = true;
    int m_DemCacheMaxTiles = RDF_DemBakeConstants.RUNTIME_DEM_CACHE_MAX_TILES;
    int m_DemTileLoadsPerScan = RDF_DemBakeConstants.RUNTIME_DEM_LOADS_PER_SCAN;
    float m_DemClutterScale = 1.0;
    // When Trace is blocked, still attempt a weakened ground-bounce path.
    bool m_EnableNlosMultipath = true;
    // |Gamma| for NLOS-only bounce power (~Gamma^2 * geometric path factor).
    float m_NlosReflectionAbs = 0.55;
    // Discard NLOS candidates weaker than this power scale.
    float m_NlosMinFactor = 0.008;
    // Skip bounce model for high-flying targets (AGL).
    float m_NlosMaxTargetAglM = 800.0;
    // Optional receiver-side EW/noise injection after processing.
    float m_AdditionalNoisePowerW = 0.0;
    ref RDF_RadarEwStack m_EwStack;
    bool m_EnableCfarGate = true;
    int m_CfarGuardCells = 2;
    int m_CfarTrainingCells = 8;
    float m_CfarPfa = 0.000001;
    int m_RangeBinCount = 64;
    // Quantize + noise measurements; clear entity refs so plots are model-derived.
    bool m_EnableMeasurementSynthesis = true;
    // Debug only: keep m_Entity / soft type tags (cheats identity into the PPI).
    bool m_KeepEntityTruth = false;
    // Channel fidelity. Ideal = logic-loop validation; Realistic = "合常理" errors.
    bool m_RealisticChannel = false;
    // Measurement synthesis extras (applied when synthesis is on).
    float m_MeasNoiseScale = 1.0;
    float m_MeasRangeBiasM = 0.0;
    float m_MeasAzimuthBiasDeg = 0.0;
    float m_MeasElevationBiasDeg = 0.0;
    float m_MeasDopplerBiasHz = 0.0;
    // Pluggable channel-error model (runs after CFAR, before Tracker/WLR).
    // Null → built-in RDF_RadarMeasurement.Synthesize fallback.
    ref RDF_RadarMeasurementModel m_MeasurementModel;
    // Fill empty CFAR cells with thermal-noise samples (real Pfa behaviour).
    bool m_EnableCfarThermalFill = false;
    // Two-way atmospheric / rain loss on received power.
    bool m_EnableAtmosphericLoss = false;
    // <0 → auto from carrier frequency (clear-air fit).
    float m_AtmLossDbPerKmOneWay = -1.0;
    // Manual rain floor (always applied when atmospheric loss is on).
    float m_RainLossDbPerKmOneWay = 0.0;
    // When true, add rain/fog from TimeAndWeatherManager each scan.
    bool m_EnableWeatherDrivenRainLoss = false;
    // Extra one-way dB/km when GetRainIntensity() == 1.
    float m_RainLossDbPerKmAtFullIntensity = 0.5;
    // Extra one-way dB/km when GetFogAmount() == 1 (weak; fog is not rain).
    float m_FogLossDbPerKmAtFullFog = 0.05;
    // Plot-to-track association gates (meters / degrees).
    float m_TrackGateRangeM = 400.0;
    float m_TrackGateAzimuthDeg = 4.0;
    int m_TrackConfirmHits = 2;
    int m_TrackMaxMisses = 3;
    // Projectile tracks: AirDrag + global wind extrapolation / WLR fixes.
    bool m_EnableBallisticPrediction = true;
    // Prefab prior (82mm HE O832DU). Override per ammo family later if needed.
    float m_ShellAirDrag = 0.000615;
    // Recompute launch/impact for confirmed projectile tracks each scan.
    bool m_EnableWeaponLocate = true;
    // Prefer DEM / live surface for WLR ground intersection (fallback = flat Y).
    bool m_EnableDemGroundForWlr = true;
    int m_WeaponLocateMinHits = 3;

    void RDF_RadarSettings()
    {
        m_Hardware = RDF_RadarHardware.CreateShorad();
        m_EwStack = new RDF_RadarEwStack();
        m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();
    }

    void Validate()
    {
        m_Range = Math.Clamp(m_Range, 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, m_UpdateInterval);
        m_SectorHalfAngleDeg = Math.Clamp(m_SectorHalfAngleDeg, 0.0, 180.0);
        m_MaxTargets = Math.Max(1, m_MaxTargets);
        m_MaxLosTracesPerScan = Math.Clamp(m_MaxLosTracesPerScan, 4, 512);
        m_ScattererDiscoveryRangeScale = Math.Clamp(m_ScattererDiscoveryRangeScale, 1.0, 4.0);
        m_ScattererDiscoveryIntervalS = Math.Clamp(m_ScattererDiscoveryIntervalS, 0.25, 60.0);
        m_ScattererClassifyPerTick = Math.Clamp(m_ScattererClassifyPerTick, 1, 256);
        m_ScattererRefreshPerTick = Math.Clamp(m_ScattererRefreshPerTick, 1, 1024);
        m_ScattererMaxEntries = Math.Clamp(m_ScattererMaxEntries, 8, 4096);
        m_MinDistance = Math.Max(0.0, m_MinDistance);
        m_DetectionSnrDb = Math.Clamp(m_DetectionSnrDb, -100.0, 200.0);
        m_DemCacheMaxTiles = Math.Clamp(m_DemCacheMaxTiles, 1, 256);
        m_DemTileLoadsPerScan = Math.Clamp(m_DemTileLoadsPerScan, 0, 64);
        m_DemClutterScale = Math.Clamp(m_DemClutterScale, 0.0, 1000.0);
        m_NlosReflectionAbs = Math.Clamp(m_NlosReflectionAbs, 0.0, 1.0);
        m_NlosMinFactor = Math.Clamp(m_NlosMinFactor, 0.0, 1.0);
        m_NlosMaxTargetAglM = Math.Clamp(m_NlosMaxTargetAglM, 10.0, 20000.0);
        m_AdditionalNoisePowerW = Math.Max(0.0, m_AdditionalNoisePowerW);
        m_CfarGuardCells = Math.Clamp(m_CfarGuardCells, 0, 32);
        m_CfarTrainingCells = Math.Clamp(m_CfarTrainingCells, 2, 128);
        m_CfarPfa = Math.Clamp(m_CfarPfa, 0.00000001, 0.5);
        m_RangeBinCount = Math.Clamp(m_RangeBinCount, 8, 512);
        m_TrackGateRangeM = Math.Clamp(m_TrackGateRangeM, 10.0, 10000.0);
        m_TrackGateAzimuthDeg = Math.Clamp(m_TrackGateAzimuthDeg, 0.1, 90.0);
        m_TrackConfirmHits = Math.Clamp(m_TrackConfirmHits, 1, 16);
        m_TrackMaxMisses = Math.Clamp(m_TrackMaxMisses, 1, 32);
        m_ShellAirDrag = Math.Clamp(m_ShellAirDrag, 0.0, 1.0);
        m_WeaponLocateMinHits = Math.Clamp(m_WeaponLocateMinHits, 2, 32);
        m_MeasNoiseScale = Math.Clamp(m_MeasNoiseScale, 0.0, 10.0);
        m_MeasRangeBiasM = Math.Clamp(m_MeasRangeBiasM, -500.0, 500.0);
        m_MeasAzimuthBiasDeg = Math.Clamp(m_MeasAzimuthBiasDeg, -5.0, 5.0);
        m_MeasElevationBiasDeg = Math.Clamp(m_MeasElevationBiasDeg, -5.0, 5.0);
        m_MeasDopplerBiasHz = Math.Clamp(m_MeasDopplerBiasHz, -500.0, 500.0);
        m_AtmLossDbPerKmOneWay = Math.Clamp(m_AtmLossDbPerKmOneWay, -1.0, 5.0);
        m_RainLossDbPerKmOneWay = Math.Clamp(m_RainLossDbPerKmOneWay, 0.0, 20.0);
        m_RainLossDbPerKmAtFullIntensity = Math.Clamp(
            m_RainLossDbPerKmAtFullIntensity, 0.0, 20.0);
        m_FogLossDbPerKmAtFullFog = Math.Clamp(m_FogLossDbPerKmAtFullFog, 0.0, 5.0);
        if (!m_Hardware)
            m_Hardware = RDF_RadarHardware.CreateShorad();
        if (!m_EwStack)
            m_EwStack = new RDF_RadarEwStack();
        if (!m_MeasurementModel)
            m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();
        m_Hardware.Validate();
    }

    //------------------------------------------------------------------------------------------------
    // Deterministic / logic-loop profile: no random measurement noise, no thermal
    // CFAR fill, no atmospheric loss. Suite default so regressions stay "过准".
    void ApplyIdealChannel()
    {
        m_RealisticChannel = false;
        m_EnableCfarGate = false;
        m_MeasNoiseScale = 0.0;
        m_MeasRangeBiasM = 0.0;
        m_MeasAzimuthBiasDeg = 0.0;
        m_MeasElevationBiasDeg = 0.0;
        m_MeasDopplerBiasHz = 0.0;
        m_EnableCfarThermalFill = false;
        m_EnableAtmosphericLoss = false;
        m_EnableWeatherDrivenRainLoss = false;
        m_AtmLossDbPerKmOneWay = -1.0;
        m_RainLossDbPerKmOneWay = 0.0;
    }

    //------------------------------------------------------------------------------------------------
    // Gameplay / fidelity profile: louder measurement noise, thermal CFAR fill,
    // clear-air atmosphere + weather-driven rain/fog loss.
    // Tests that enable this must use wider error bands.
    void ApplyRealisticChannel()
    {
        m_RealisticChannel = true;
        m_EnableMeasurementSynthesis = true;
        m_EnableCfarGate = true;
        m_MeasNoiseScale = 3.5;
        m_MeasRangeBiasM = 5.0;
        m_MeasAzimuthBiasDeg = 0.2;
        m_MeasElevationBiasDeg = 0.15;
        m_MeasDopplerBiasHz = 0.0;
        m_EnableCfarThermalFill = true;
        m_EnableAtmosphericLoss = true;
        m_EnableWeatherDrivenRainLoss = true;
        m_AtmLossDbPerKmOneWay = -1.0;
        m_RainLossDbPerKmOneWay = 0.0;
        m_RainLossDbPerKmAtFullIntensity = 0.5;
        m_FogLossDbPerKmAtFullFog = 0.05;
    }

    //------------------------------------------------------------------------------------------------
    // Resolve effective one-way rain(+fog) loss for this scan.
    // Manual floor always applies; weather terms optional.
    float ResolveRainLossDbPerKm(RDF_RadarWeatherSnapshot weather)
    {
        float rainDb = m_RainLossDbPerKmOneWay;
        if (!m_EnableWeatherDrivenRainLoss)
            return rainDb;
        if (!weather || !weather.m_Valid)
            return rainDb;

        rainDb = rainDb
            + weather.m_RainIntensity * m_RainLossDbPerKmAtFullIntensity
            + weather.m_FogAmount * m_FogLossDbPerKmAtFullFog;
        if (rainDb < 0.0)
            rainDb = 0.0;
        if (rainDb > 20.0)
            rainDb = 20.0;
        return rainDb;
    }
}
