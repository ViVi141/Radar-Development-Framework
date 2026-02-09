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
        if (scanSettings)
            m_LastRange = Math.Max(0.1, scanSettings.m_Range);

        // Clear previous debug shapes references before rendering.
        if (m_DebugShapes)
            m_DebugShapes.Clear();

        scanner.Scan(subject, m_Samples);

        foreach (RDF_LidarSample sample : m_Samples)
        {
            if (m_Settings.m_ShowHitsOnly && !sample.m_Hit)
                continue;

            if (m_Settings.m_DrawRays)
                DrawRay(sample.m_Start, sample.m_HitPos, sample.m_Distance, sample.m_Hit);

            if (m_Settings.m_DrawPoints)
                DrawPoint(sample.m_HitPos, sample.m_Distance, sample.m_Hit);


        }
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
