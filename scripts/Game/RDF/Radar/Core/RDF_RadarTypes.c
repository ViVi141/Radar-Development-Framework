// Radar target types for classification.
enum ERDF_RadarTargetType
{
    RDF_RADAR_TARGET_VEHICLE,
    RDF_RADAR_TARGET_PROJECTILE,
    RDF_RADAR_TARGET_RADAR_EMITTER,
    RDF_RADAR_TARGET_ANONYMOUS
}

// Coarse-bin CFAR estimator mode (range-window sides).
enum ERDF_CfarMode
{
    RDF_CFAR_CA,
    RDF_CFAR_GO,
    RDF_CFAR_SO
}

// MTI / MTD processing mode. TwoPulse = legacy sin² canceller (default).
// MtdBank = DFT Doppler filter bank; clutter stays in the near-zero bin.
enum ERDF_MtiMode
{
    RDF_MTI_TWOPULSE,
    RDF_MTI_MTD_BANK
}

// Noise-jammer antenna coupling vs victim scan beam.
// BEAM = instantaneous scanForward (stare / track fidelity).
// SEARCH_AVG = beamwidth/360 duty blend (rotating search, playable soft).
// MAINLOBE_ONLY = contribute only when jammer is in the main beam.
enum ERDF_NoiseJamCoupling
{
    RDF_JAM_COUPLE_BEAM,
    RDF_JAM_COUPLE_SEARCH_AVG,
    RDF_JAM_COUPLE_MAINLOBE_ONLY
}

// Single radar detection / plot. With measurement synthesis enabled, kinematics
// are model-derived (quantized + noisy); m_Entity is debug-only when kept.
class RDF_RadarTarget
{
    // Optional debug link to the scatterer; null under measurement synthesis.
    IEntity m_Entity;
    // Stable scatterer-table id; survives measurement synthesis (debug / regression).
    int m_ScattererId;
    vector m_Position;
    float m_Distance;
    vector m_Velocity;
    ERDF_RadarTargetType m_Type;
    float m_Time;
    float m_AzimuthDeg;
    float m_ElevationDeg;
    float m_RadialSpeedMs;
    float m_RcsM2;
    float m_MeanRcsM2;
    int m_SwerlingModel;
    float m_AglM = -1.0;
    float m_DemTerrainY;
    float m_ReceivedPowerW;
    float m_ProcessedPowerW;
    float m_DopplerHz;
    float m_MtiGain;
    // Winning Doppler filter index under MtdBank (-1 when unused / TwoPulse).
    int m_DopplerBin = -1;
    // Active PRF index when stagger / multi-PRF is enabled (0 = primary).
    int m_PrfIndex;
    // Optional rotor / micro-Doppler params (from signature; 0 = none).
    float m_RotorTipSpeedMs;
    int m_BladeCount;
    float m_RotorRcsFraction;
    float m_HubWidthMs;
    int m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
    bool m_DemSampleValid;
    float m_ClutterPowerW;
    float m_ClutterToNoiseDb;
    float m_SnrDb;
    bool m_Detected;
    bool m_IsAnonymous;
    bool m_IsFalsePlot;
    float m_CfarPowerW;
    // True when TraceMove was blocked before the target (terrain/entity occluder).
    bool m_LosBlocked;
    // Trace hit fraction toward the target point (1 = reached end / clear).
    float m_LosHitFraction;
    // Power scale: 1 for direct path; <1 for NLOS ground-bounce approximation.
    float m_MultipathFactor;
    // Emitter RF summary (filled for RADAR_EMITTER plots; used by ESM receive).
    float m_EmitFrequencyHz;
    float m_EmitPeakPowerW;
    float m_EmitAntennaGainDbi;
    float m_EmitStrength = 1.0;
    string m_BeamName;
    int m_ScanNumber;
}
