// LiDAR scan output sample.
class RDF_LidarSample
{
    int m_Index;
    bool m_Hit;
    vector m_Start;
    vector m_End;
    vector m_Dir;
    vector m_HitPos;
    float m_Distance;
    IEntity m_Entity;
    string m_ColliderName;
    GameMaterial m_Surface;
    // Network-derived material hint: when m_Surface is null (client side, the
    // GameMaterial pointer is not serialised), m_Density / m_IsWater carry the
    // ballistics info so RDF_LidarMaterialColorStrategy can still shade by
    // density instead of falling back to grey. -1.0 = unknown.
    float m_Density = -1.0;
    bool m_IsWater = false;
}
