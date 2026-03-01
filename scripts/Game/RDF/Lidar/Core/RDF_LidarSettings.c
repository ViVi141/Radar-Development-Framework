// Trace target mode: which geometry the raycast hits.
enum ETraceTargetMode
{
    TERRAIN_ONLY,  // TraceFlags.WORLD only - ground, water, static world.
    ALL,           // WORLD | ENTS - terrain and entities.
    ENTITIES_ONLY  // TraceFlags.ENTS only - entities (vehicles, characters) but not terrain.
}

// Core scan settings for LiDAR.
class RDF_LidarSettings
{
    bool m_Enabled = true;
    float m_Range = 50.0;
    int m_RayCount = 512;
    vector m_OriginOffset = "0 0 0";
    // Switch: terrain-only, all (terrain+entities), or entities-only. Overrides m_TraceFlags when Validate() runs.
    ETraceTargetMode m_TraceTargetMode = ETraceTargetMode.ALL;
    int m_TraceFlags = TraceFlags.WORLD | TraceFlags.ENTS;
    // When true, add TraceFlags.VISIBILITY so smoke/particles (visibility occluders) block laser rays.
    bool m_TraceSmokeOcclusion = false;
    int m_LayerMask = EPhysicsLayerPresets.Projectile;
    float m_UpdateInterval = 5.0;
    bool m_UseBoundsCenter = true;
    bool m_UseLocalOffset = true;

    // Validate and clamp settings to safe defaults/ranges.
    void Validate()
    {
        // Ray count: minimum 1, no upper limit
        m_RayCount = Math.Max(m_RayCount, 1);
        // Range: 0.1 .. 100000.0 (100 km, aligned with radar for long-range scans)
        m_Range = Math.Clamp(m_Range, 0.1, 100000.0);
        // Update interval: at least 0.01s
        m_UpdateInterval = Math.Max(0.01, m_UpdateInterval);

        // Derive TraceFlags from m_TraceTargetMode (see LESSONS_FROM_ENGINE.md).
        switch (m_TraceTargetMode)
        {
            case ETraceTargetMode.TERRAIN_ONLY:
                m_TraceFlags = TraceFlags.WORLD;
                break;
            case ETraceTargetMode.ALL:
                m_TraceFlags = TraceFlags.WORLD | TraceFlags.ENTS;
                break;
            case ETraceTargetMode.ENTITIES_ONLY:
                m_TraceFlags = TraceFlags.ENTS;
                break;
            default:
                m_TraceFlags = TraceFlags.WORLD | TraceFlags.ENTS;
                break;
        }
        if (m_TraceSmokeOcclusion)
            m_TraceFlags = m_TraceFlags | TraceFlags.VISIBILITY;
    }
}
