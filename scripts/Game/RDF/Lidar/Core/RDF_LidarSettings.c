// Core scan settings for LiDAR.
class RDF_LidarSettings
{
    bool m_Enabled = true;
    float m_Range = 50.0;
    int m_RayCount = 512;
    vector m_OriginOffset = "0 0 0";
    int m_TraceFlags = TraceFlags.WORLD | TraceFlags.ENTS;
    int m_LayerMask = EPhysicsLayerPresets.Projectile;
    float m_UpdateInterval = 5.0;
    bool m_UseBoundsCenter = true;
    bool m_UseLocalOffset = true;
}
