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

    // Validate and clamp settings to safe defaults/ranges.
    void Validate()
    {
        // Ray count: minimum 1, no upper limit
        m_RayCount = Math.Max(m_RayCount, 1);
        // Range: 0.1 .. 1000.0
        m_Range = Math.Clamp(m_Range, 0.1, 1000.0);
        // Update interval: at least 0.01s
        m_UpdateInterval = Math.Max(0.01, m_UpdateInterval);
    }
}
