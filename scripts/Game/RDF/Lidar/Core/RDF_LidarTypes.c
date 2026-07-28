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
}
