// Visual settings for LiDAR rendering.
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
}
