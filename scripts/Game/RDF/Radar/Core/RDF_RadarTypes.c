// Radar target types for classification.
enum ERDF_RadarTargetType
{
    RDF_RADAR_TARGET_VEHICLE,
    RDF_RADAR_TARGET_PROJECTILE,
    RDF_RADAR_TARGET_RADAR_EMITTER
}

// Single radar detection: geometry + physical/video processing fields.
class RDF_RadarTarget
{
    IEntity m_Entity;
    vector m_Position;
    float m_Distance;
    vector m_Velocity;
    ERDF_RadarTargetType m_Type;
    float m_Time;
    float m_AzimuthDeg;
    float m_ElevationDeg;
    float m_RadialSpeedMs;
    float m_RcsM2;
    float m_ReceivedPowerW;
    float m_ProcessedPowerW;
    float m_DopplerHz;
    float m_MtiGain;
    int m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
    bool m_DemSampleValid;
    float m_ClutterPowerW;
    float m_ClutterToNoiseDb;
    float m_SnrDb;
    bool m_Detected;
    // True when TraceMove was blocked before the target (terrain/entity occluder).
    bool m_LosBlocked;
    // Trace hit fraction toward the target point (1 = reached end / clear).
    float m_LosHitFraction;
    // Power scale: 1 for direct path; <1 for NLOS ground-bounce approximation.
    float m_MultipathFactor;
    string m_BeamName;
    int m_ScanNumber;
}
