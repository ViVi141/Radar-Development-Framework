// Visual settings for LiDAR rendering (point cloud + showcase overlays).
class RDF_LidarVisualSettings
{
    bool m_DrawPoints = true;
    bool m_DrawRays = true;
    bool m_ShowHitsOnly = false;
    float m_PointSize = 0.08;
    float m_RayAlpha = 0.25;
    bool m_UseDistanceGradient = true;
    int m_RaySegments = 6;
    // Draw scan origin and local X/Y/Z axes for debugging (default off).
    bool m_DrawOriginAxis = false;
    float m_OriginAxisLength = 0.8;
    // When true: render game world + point cloud. When false: render point cloud only (draws a solid background to hide the world).
    bool m_RenderWorld = true;
    // When true: use batched triangle-mesh rendering (fewer Shape calls) to draw points/rays.
    // Recommended for high `m_RayCount` / large point-clouds to reduce draw-call overhead.
    bool m_UseBatchedMesh = false;
    // When true (and using RDF_DefaultColorStrategy): point brightness/alpha scaled by hit surface
    // reflectivity (GameMaterial/BallisticInfo). For full material-based colors use
    // SetColorStrategy(new RDF_LidarMaterialColorStrategy()) instead.
    bool m_UseMaterialEffect = false;

    // Showcase: phosphor afterglow for recent hit points.
    bool m_DrawAfterglow = false;
    float m_AfterglowSec = 2.0;
    float m_AfterglowPointSize = 0.12;
    int m_AfterglowMaxBlips = 128;

    // Showcase: ground range rings (½ and full scan range).
    bool m_DrawRangeRings = false;
    int m_RangeRingSegments = 24;

    // Showcase: translucent sweep / conical sector fan (needs geometry from AutoRunner).
    bool m_DrawSectorSweep = false;
    int m_SectorSweepSegments = 12;
    float m_SectorSweepAlpha = 0.10;
    float m_SectorSweepEdgeAlpha = 0.55;
    float m_SectorHeightM = 2.0;

    //------------------------------------------------------------------------------------------------
    //! Demo / promo defaults: points + afterglow + rings; rays off.
    void ApplyShowcaseDefaults()
    {
        m_DrawRays = false;
        m_DrawPoints = true;
        m_ShowHitsOnly = true;
        m_PointSize = 0.14;
        m_DrawOriginAxis = false;
        m_DrawAfterglow = true;
        m_AfterglowSec = 1.6;
        m_AfterglowPointSize = 0.08;
        m_AfterglowMaxBlips = 96;
        m_DrawRangeRings = true;
        m_RangeRingSegments = 24;
        m_DrawSectorSweep = true;
        m_SectorSweepSegments = 12;
        m_SectorSweepAlpha = 0.10;
        m_SectorSweepEdgeAlpha = 0.55;
        m_SectorHeightM = 2.0;
    }

    //------------------------------------------------------------------------------------------------
    //! Minimal: no showcase overlays (HUD-only / performance).
    void ApplyMinimalDefaults()
    {
        m_DrawRays = false;
        m_DrawPoints = false;
        m_DrawOriginAxis = false;
        m_DrawAfterglow = false;
        m_DrawRangeRings = false;
        m_DrawSectorSweep = false;
    }
}

class RDF_LidarAfterglowBlip
{
    vector m_Pos;
    float m_BirthS;
    float m_R;
    float m_G;
    float m_B;
}
