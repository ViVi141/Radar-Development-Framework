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

// Opt-in gameplay fidelity packs (propagation + measurement). Not a hidden
// "realistic tier": defaults stay lean; call ApplyGameplayFidelity after
// Create*Settings / ConfigureMode. AutoTest still uses StabilizeForRegression.
enum ERDF_RadarFidelityPreset
{
    RDF_FIDELITY_NONE,
    RDF_FIDELITY_SHORAD,
    RDF_FIDELITY_AIRBORNE,
    RDF_FIDELITY_WLR,
    RDF_FIDELITY_ESM
}

// MTI / MTD processing mode. TwoPulse = legacy sin² canceller (default).
// MtdBank = DFT Doppler filter bank; clutter stays in the near-zero bin.
// ThreePulse appended (value 2) so existing saved MtdBank=1 stays valid.
enum ERDF_MtiMode
{
    RDF_MTI_TWOPULSE,
    RDF_MTI_MTD_BANK,
    RDF_MTI_THREE_PULSE
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

// Transmit / receive polarization mode (engineering match tables, not Stokes).
enum ERDF_RadarPolarization
{
    RDF_POL_H,
    RDF_POL_V,
    RDF_POL_CIRCULAR
}

// SURVEIL = authored Pt / rpm. LPI scales peak and scan rate for intercept vs SNR.
enum ERDF_RadarSearchMode
{
    RDF_SEARCH_SURVEIL,
    RDF_SEARCH_LPI
}

// Forward-only truth sample for PhysicalDetect. Never publish to Tracker/Lock.
// Entity and DEM/LOS inputs live here; inverse path must not read this type.
class RDF_RadarTruthSample
{
    IEntity m_Entity;
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
    int m_DopplerBin = -1;
    int m_PrfIndex;
    float m_RotorTipSpeedMs;
    int m_BladeCount;
    float m_RotorRcsFraction;
    float m_HubWidthMs;
    bool m_RotorSidebandUsed;
    float m_FanTipSpeedMs;
    int m_FanBladeCount;
    float m_FanRcsFraction;
    ERDF_RadarNctrClass m_NctrClass;
    float m_NctrConfidence;
    int m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
    bool m_DemSampleValid;
    float m_ClutterPowerW;
    float m_ClutterToNoiseDb;
    float m_SnrDb;
    bool m_Detected;
    bool m_IsAnonymous;
    bool m_IsFalsePlot;
    float m_CfarPowerW;
    bool m_LosBlocked;
    float m_LosHitFraction;
    float m_MultipathFactor;
    float m_EmitFrequencyHz;
    float m_EmitPeakPowerW;
    float m_EmitAntennaGainDbi;
    float m_EmitStrength = 1.0;
    string m_BeamName;
    int m_ScanNumber;
}

// Published observation / plot (inverse input). Kinematics are model-derived
// after measurement synthesis. m_Entity stays null on the inverse path;
// use Sensor debug-truth bypass when AutoTests need the scatterer handle.
class RDF_RadarTarget
{
    // Debug-only when KeepEntityTruth; inverse algorithms must ignore this.
    IEntity m_Entity;
    // Stable scatterer-table id for debug / regression / truth bypass.
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
    // True when MTD win bin is non-zero and rotor tip lines contributed (observability).
    bool m_RotorSidebandUsed;
    float m_FanTipSpeedMs;
    int m_FanBladeCount;
    float m_FanRcsFraction;
    ERDF_RadarNctrClass m_NctrClass;
    float m_NctrConfidence;
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
