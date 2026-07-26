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
    bool m_UseSphereQuery = true;
    bool m_SphereQueryAlsoActive = true;
    int m_MaxTargets = 64;
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
    // Plot-to-track association gates (meters / degrees).
    float m_TrackGateRangeM = 400.0;
    float m_TrackGateAzimuthDeg = 4.0;
    int m_TrackConfirmHits = 2;
    int m_TrackMaxMisses = 3;

    void RDF_RadarSettings()
    {
        m_Hardware = RDF_RadarHardware.CreateShorad();
        m_EwStack = new RDF_RadarEwStack();
    }

    void Validate()
    {
        m_Range = Math.Clamp(m_Range, 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, m_UpdateInterval);
        m_SectorHalfAngleDeg = Math.Clamp(m_SectorHalfAngleDeg, 0.0, 180.0);
        m_MaxTargets = Math.Max(1, m_MaxTargets);
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
        if (!m_Hardware)
            m_Hardware = RDF_RadarHardware.CreateShorad();
        if (!m_EwStack)
            m_EwStack = new RDF_RadarEwStack();
        m_Hardware.Validate();
    }
}
