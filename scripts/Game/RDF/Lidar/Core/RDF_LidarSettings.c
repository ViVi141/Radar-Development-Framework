// Trace target mode: which geometry the raycast hits.
enum ERDF_TraceTargetMode
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
    ERDF_TraceTargetMode m_TraceTargetMode = ERDF_TraceTargetMode.ALL;
    int m_TraceFlags = TraceFlags.WORLD | TraceFlags.ENTS;
    // When true, add TraceFlags.VISIBILITY so smoke/particles (visibility occluders) block laser rays.
    bool m_TraceSmokeOcclusion = false;
    int m_LayerMask = EPhysicsLayerPresets.Projectile;
    float m_UpdateInterval = 5.0;
    bool m_UseBoundsCenter = true;
    bool m_UseLocalOffset = true;
    //! Extra meters beyond local AABB along each ray before TraceMove Start (avoids in-solid traces).
    float m_StartClearanceM = 0.25;
    //! When false, skip GameMaterial capture (safer / cheaper for navigation consumers).
    bool m_CaptureSurface = true;
    //! Soft upper bound for a single Scan() (Workbench heap safety). 0 = no clamp beyond Max(1).
    int m_MaxRayCount = 4096;

    // Validate and clamp settings to safe defaults/ranges.
    void Validate()
    {
        m_RayCount = Math.MaxInt(m_RayCount, 1);
        if (m_MaxRayCount > 0 && m_RayCount > m_MaxRayCount)
            m_RayCount = m_MaxRayCount;
        m_Range = Math.Clamp(m_Range, 0.1, 100000.0);
        m_UpdateInterval = Math.Max(0.01, m_UpdateInterval);
        if (m_StartClearanceM < 0.05)
            m_StartClearanceM = 0.05;

        switch (m_TraceTargetMode)
        {
            case ERDF_TraceTargetMode.TERRAIN_ONLY:
                m_TraceFlags = TraceFlags.WORLD;
                break;
            case ERDF_TraceTargetMode.ALL:
                m_TraceFlags = TraceFlags.WORLD | TraceFlags.ENTS;
                break;
            case ERDF_TraceTargetMode.ENTITIES_ONLY:
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
