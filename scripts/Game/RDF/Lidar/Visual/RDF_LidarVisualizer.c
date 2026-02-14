// LiDAR point cloud visualizer using debug shapes.
class RDF_LidarVisualizer
{
    protected ref RDF_LidarVisualSettings m_Settings;
    protected ref array<ref Shape> m_DebugShapes;
    protected ref array<ref RDF_LidarSample> m_Samples;
    protected float m_LastRange = 50.0;
    // Color strategy (injectable). Defaults to internal color builder.
    protected ref RDF_LidarColorStrategy m_ColorStrategy;

    void RDF_LidarVisualizer(RDF_LidarVisualSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_LidarVisualSettings();

        m_DebugShapes = new array<ref Shape>();
        m_Samples = new array<ref RDF_LidarSample>();

        m_ColorStrategy = new RDF_DefaultColorStrategy();
    }

    RDF_LidarVisualSettings GetSettings()
    {
        return m_Settings;
    }

    void SetColorStrategy(RDF_LidarColorStrategy strategy)
    {
        if (strategy)
            m_ColorStrategy = strategy;
    }

    RDF_LidarColorStrategy GetColorStrategy()
    {
        return m_ColorStrategy;
    }

    // Return the most recent scan samples after Render()
    ref array<ref RDF_LidarSample> GetLastSamples()
    {
        return m_Samples;
    }

    void Render(IEntity subject, RDF_LidarScanner scanner)
    {
        if (!subject || !scanner || !m_Settings)
            return;

        RDF_LidarSettings scanSettings = scanner.GetSettings();
        int rays = 0;
        if (scanSettings)
        {
            m_LastRange = Math.Max(0.1, scanSettings.m_Range);
            rays = Math.Max(scanSettings.m_RayCount, 1);
        }

        // Pre-allocate arrays to reduce per-frame reallocations
        if (m_DebugShapes)
        {
            m_DebugShapes.Clear();
            int segs = Math.Max(1, m_Settings.m_RaySegments);
            int extraPoint = 0;
            if (m_Settings.m_DrawPoints)
                extraPoint = 1;
            int estShapes = 16 + rays * (segs + extraPoint);
            m_DebugShapes.Reserve(estShapes);
        }

        if (m_Samples)
        {
            m_Samples.Clear();
            m_Samples.Reserve(rays);
        }

        scanner.Scan(subject, m_Samples);

        // When "point cloud only": draw a black quad in front of the camera to hide the world (point cloud uses NOZBUFFER so it draws on top).
        if (!m_Settings.m_RenderWorld && m_DebugShapes)
            DrawPointCloudOnlyBackground();

        if (m_Settings.m_DrawOriginAxis && m_Samples.Count() > 0)
            DrawOriginAxis(subject, m_Samples.Get(0).m_Start);

        foreach (RDF_LidarSample sample : m_Samples)
        {
            if (m_Settings.m_ShowHitsOnly && !sample.m_Hit)
                continue;

            if (m_Settings.m_DrawRays)
                DrawRay(sample.m_Start, sample.m_HitPos, sample.m_Distance, sample.m_Hit);

            if (m_Settings.m_DrawPoints)
                DrawPointFromSample(sample);
        }
    }

    // Render using pre-computed samples (for network synchronization)
    void RenderWithSamples(IEntity subject, array<ref RDF_LidarSample> samples)
    {
        if (!subject || !samples || !m_Settings)
            return;

        // Update last range from samples if available (use max positive distance among samples)
        if (samples.Count() > 0)
        {
            float maxDist = 0.0;
            foreach (RDF_LidarSample s : samples)
            {
                if (s && s.m_Distance > maxDist)
                    maxDist = s.m_Distance;
            }
            if (maxDist > 0.0)
                m_LastRange = maxDist;
            else
                m_LastRange = 50.0;
        }

        // Pre-allocate arrays to reduce per-frame reallocations
        if (m_DebugShapes)
        {
            m_DebugShapes.Clear();
            int segs = Math.Max(1, m_Settings.m_RaySegments);
            int extraPoint = 0;
            if (m_Settings.m_DrawPoints)
                extraPoint = 1;
            int estShapes = 16 + samples.Count() * (segs + extraPoint);
            m_DebugShapes.Reserve(estShapes);
        }

        // Use provided samples instead of scanning
        m_Samples.Clear();
        m_Samples.Reserve(samples.Count());
        foreach (RDF_LidarSample sampleIn : samples)
        {
            m_Samples.Insert(sampleIn);
        }

        // Note: always use configured segmentation; do not apply automatic degradation here.

        if (!m_Settings.m_RenderWorld && m_DebugShapes)
            DrawPointCloudOnlyBackground();

        if (m_Settings.m_DrawOriginAxis && m_Samples.Count() > 0)
            DrawOriginAxis(subject, m_Samples.Get(0).m_Start);

        foreach (RDF_LidarSample sample : m_Samples)
        {
            if (m_Settings.m_ShowHitsOnly && !sample.m_Hit)
                continue;

            if (m_Settings.m_DrawRays)
                DrawRay(sample.m_Start, sample.m_HitPos, sample.m_Distance, sample.m_Hit);

            if (m_Settings.m_DrawPoints)
                DrawPointFromSample(sample);
        }
    }

    // Draw a large black quad just in front of the local camera to occlude the world (used when m_RenderWorld is false).
    protected void DrawPointCloudOnlyBackground()
    {
        if (!m_DebugShapes)
            return;
        PlayerController controller = GetGame().GetPlayerController();
        if (!controller)
            return;
        PlayerCamera playerCam = controller.GetPlayerCamera();
        if (!playerCam)
            return;
        vector mat[4];
        playerCam.GetWorldCameraTransform(mat);
        vector pos = mat[3];
        vector forward = mat[2];
        vector right = mat[0];
        vector up = mat[1];
        float dist = 1.0;
        float halfSize = 100.0;
        vector center = pos + (forward * dist);
        vector p0 = center - (right * halfSize) - (up * halfSize);
        vector p1 = center + (right * halfSize) - (up * halfSize);
        vector p2 = center + (right * halfSize) + (up * halfSize);
        vector p3 = center - (right * halfSize) + (up * halfSize);
        vector tris[6];
        tris[0] = p0;
        tris[1] = p1;
        tris[2] = p2;
        tris[3] = p0;
        tris[4] = p2;
        tris[5] = p3;
        int bgColor = ARGBF(1.0, 0.0, 0.0, 0.0);
        int bgFlags = ShapeFlags.NOOUTLINE | ShapeFlags.DOUBLESIDE;
        Shape bgShape = Shape.CreateTris(bgColor, bgFlags, tris, 2);
        if (bgShape)
            m_DebugShapes.Insert(bgShape);
    }

    protected void DrawOriginAxis(IEntity subject, vector origin)
    {
        if (!m_DebugShapes || !subject || m_Settings.m_OriginAxisLength <= 0.0)
            return;

        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector axisX = worldMat[0];
        vector axisY = worldMat[1];
        vector axisZ = worldMat[2];
        float len = m_Settings.m_OriginAxisLength;

        vector endX[2];
        endX[0] = origin;
        endX[1] = origin + axisX * len;
        m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 1, 0, 0), ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER, endX, 2));

        vector endY[2];
        endY[0] = origin;
        endY[1] = origin + axisY * len;
        m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 0, 1, 0), ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER, endY, 2));

        vector endZ[2];
        endZ[0] = origin;
        endZ[1] = origin + axisZ * len;
        m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 0, 0, 1), ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER, endZ, 2));
    }

    // New: draw point using the full sample so color/size strategies can access metadata
    protected void DrawPointFromSample(RDF_LidarSample sample)
    {
        if (!m_DebugShapes || !sample)
            return;

        int color = BuildPointColorFromSample(sample);
        float size = BuildPointSizeFromSample(sample);
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;
        Shape s = Shape.CreateSphere(color, shapeFlags, sample.m_HitPos, size);
        m_DebugShapes.Insert(s);
    }

    protected void DrawPoint(vector pos, float dist, bool hit)
    {
        if (!m_DebugShapes)
            return;

        int color = BuildPointColor(dist, hit);
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;
        Shape s = Shape.CreateSphere(color, shapeFlags, pos, m_Settings.m_PointSize);
        m_DebugShapes.Insert(s);
    }

    protected void DrawRay(vector start, vector end, float dist, bool hit)
    {
        if (!m_DebugShapes)
            return;

        int segments = m_Settings.m_RaySegments;
        if (segments < 1)
            segments = 1;

        vector last = start;
        for (int i = 1; i <= segments; i++)
        {
            float t = i / (float)segments;
            vector p[2];
            p[0] = last;
            p[1] = start + (end - start) * t;
            last = p[1];

            int color = BuildRayColorAtT(t, hit);
            int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP;
            Shape s = Shape.CreateLines(color, shapeFlags, p, 2);
            m_DebugShapes.Insert(s);
        }
    }

    protected int BuildPointColor(float dist, bool hit)
    {
        if (m_ColorStrategy)
            return m_ColorStrategy.BuildPointColor(dist, hit, GetRangeSafe(), m_Settings);

        if (!m_Settings.m_UseDistanceGradient)
        {
            if (hit)
                return ARGBF(0.9, 1, 0, 0);
            return ARGBF(0.6, 1, 0.5, 0);
        }

        float t = dist / GetRangeSafe();
        t = Math.Clamp(t, 0.0, 1.0);
        float r = Math.Lerp(1.0, 0.0, t);
        float g = Math.Lerp(0.0, 1.0, t);
        float b = 0.0;
        float alpha = 0.9;
        if (!hit)
            alpha = 0.6;
        return ARGBF(alpha, r, g, b);
    }

    // New: access color via the color strategy using a full sample
    protected int BuildPointColorFromSample(RDF_LidarSample sample)
    {
        if (m_ColorStrategy)
            return m_ColorStrategy.BuildPointColorFromSample(sample, GetRangeSafe(), m_Settings);

        return BuildPointColor(sample.m_Distance, sample.m_Hit);
    }

    // New: access point size via the color strategy using a full sample
    protected float BuildPointSizeFromSample(RDF_LidarSample sample)
    {
        if (m_ColorStrategy)
            return m_ColorStrategy.BuildPointSizeFromSample(sample, GetRangeSafe(), m_Settings);

        return m_Settings.m_PointSize;
    }

    protected int BuildRayColorAtT(float t, bool hit)
    {
        if (m_ColorStrategy)
            return m_ColorStrategy.BuildRayColorAtT(t, hit, m_Settings);

        t = Math.Clamp(t, 0.0, 1.0);
        float baseAlpha = Math.Clamp(m_Settings.m_RayAlpha, 0.05, 1.0);
        float alpha = baseAlpha;
        if (hit)
            alpha = baseAlpha + 0.2;
        if (alpha > 1.0)
            alpha = 1.0;

        float r = Math.Lerp(1.0, 0.0, t);
        float g = Math.Lerp(0.0, 1.0, t);
        float b = 0.0;
        return ARGBF(alpha, r, g, b);
    }

    protected float GetRangeSafe()
    {
        return m_LastRange;
    }

    // Export functions removed: export functionality has been removed per project owner request.
    // Use `GetLastSamples()` to obtain `array<ref RDF_LidarSample>` and export externally if needed.

}
