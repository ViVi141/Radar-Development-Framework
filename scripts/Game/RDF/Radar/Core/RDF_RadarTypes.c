// Radar target types for classification.
enum ERDF_RadarTargetType
{
    RDF_RADAR_TARGET_VEHICLE,
    RDF_RADAR_TARGET_PROJECTILE,
    RDF_RADAR_TARGET_RADAR_EMITTER
}

// Single radar detection: position, distance, velocity, entity, type.
class RDF_RadarTarget
{
    IEntity m_Entity;
    vector m_Position;
    float m_Distance;
    vector m_Velocity;
    ERDF_RadarTargetType m_Type;
    float m_Time;
}
