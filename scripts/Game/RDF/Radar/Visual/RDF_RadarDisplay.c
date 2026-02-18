// Radar display helpers for in-world debug visualisation.
// RDF_PPIDisplay   - Plan Position Indicator (top-down range-azimuth sweep).
// RDF_AScopeDisplay - Range vs. amplitude profile view.

// Plan Position Indicator (PPI) display.
// Draws a horizontal top-down radar picture centred on the radar entity,
// with concentric range rings, azimuth spokes, and coloured target blips.
class RDF_PPIDisplay
{
    // Physical radius of the displayed area (m). Targets beyond this are clipped.
    float m_DisplayRadius = 200.0;
    // Colour of the grid rings and spokes.
    int m_GridColor = ARGB(100, 60, 160, 60);
    // Vertical offset above ground so the grid floats at antenna height (m).
    float m_HeightOffset = 2.0;

    // Optional color strategy for blip coloring.
    // When null the built-in SNR gradient is used.
    protected ref RDF_RadarColorStrategy m_ColorStrategy;

    // Holds active Shape references to keep them alive for the current frame.
    protected ref array<ref Shape> m_Shapes;

    void RDF_PPIDisplay()
    {
        m_Shapes = new array<ref Shape>();
    }

    // Inject a colour strategy; pass null to restore the default SNR coloring.
    void SetColorStrategy(RDF_RadarColorStrategy strategy)
    {
        m_ColorStrategy = strategy;
    }

    RDF_RadarColorStrategy GetColorStrategy()
    {
        return m_ColorStrategy;
    }

    // Draw the full PPI picture for one scan frame.
    void DrawPPI(array<ref RDF_LidarSample> samples, IEntity radarEntity)
    {
        if (!radarEntity)
            return;

        m_Shapes.Clear();

        vector centre = radarEntity.GetOrigin();
        centre[1] = centre[1] + m_HeightOffset;

        DrawPPIGrid(centre);

        foreach (RDF_LidarSample baseSample : samples)
        {
            RDF_RadarSample sample = RDF_RadarSample.Cast(baseSample);
            if (!sample || !sample.m_Hit)
                continue;

            DrawPPITarget(sample, centre);
        }
    }

    // Draw concentric range rings and azimuth spokes.
    protected void DrawPPIGrid(vector centre)
    {
        int rings = 5;
        for (int i = 1; i <= rings; i++)
        {
            float r = m_DisplayRadius * (float)i / (float)rings;
            DrawHorizontalCircle(centre, r, m_GridColor);
        }

        // Azimuth spokes every 30 degrees.
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        for (int deg = 0; deg < 360; deg += 30)
        {
            float rad   = (float)deg * Math.DEG2RAD;
            vector spoke = centre + Vector(Math.Cos(rad) * m_DisplayRadius, 0, Math.Sin(rad) * m_DisplayRadius);
            vector lineArr[2];
            lineArr[0] = centre;
            lineArr[1] = spoke;
            Shape s = Shape.CreateLines(m_GridColor, shapeFlags, lineArr, 2);
            if (s)
                m_Shapes.Insert(s);
        }
    }

    // Draw a single target blip projected onto the horizontal plane.
    protected void DrawPPITarget(RDF_RadarSample sample, vector centre)
    {
        vector relPos = sample.m_HitPos - centre;
        relPos[1] = 0; // Flatten to horizontal plane.

        float dist = relPos.Length();
        if (dist > m_DisplayRadius)
            return;

        // Scale blip size to RCS (larger targets -> bigger blip).
        float rcsDBsm  = sample.GetRCSdBsm();
        float blipSize = Math.Clamp(0.3 + rcsDBsm / 40.0, 0.15, 1.5);

        int blipColor = GetTargetColor(sample);

        vector blipPos = centre + relPos;
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        Shape s = Shape.CreateSphere(blipColor, shapeFlags, blipPos, blipSize);
        if (s)
            m_Shapes.Insert(s);
    }

    // Target blip colour: delegates to injected strategy if present,
    // otherwise falls back to built-in SNR gradient.
    protected int GetTargetColor(RDF_RadarSample sample)
    {
        if (m_ColorStrategy)
            return m_ColorStrategy.BuildPointColorFromRadarSample(sample, null);

        if (sample.m_SignalToNoiseRatio > 25.0)
            return ARGB(255, 0, 255, 50);   // Bright green - strong return.
        else if (sample.m_SignalToNoiseRatio > 13.0)
            return ARGB(255, 255, 220, 0);  // Yellow - moderate.
        else
            return ARGB(255, 255, 90, 0);   // Orange - weak, near threshold.
    }

    // Draw a flat horizontal circle using line segments.
    protected void DrawHorizontalCircle(vector centre, float radius, int color)
    {
        int segments = 36;
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        for (int i = 0; i < segments; i++)
        {
            float a1 = ((float)i       / (float)segments) * 360.0 * Math.DEG2RAD;
            float a2 = ((float)(i + 1) / (float)segments) * 360.0 * Math.DEG2RAD;

            vector p1 = centre + Vector(Math.Cos(a1) * radius, 0, Math.Sin(a1) * radius);
            vector p2 = centre + Vector(Math.Cos(a2) * radius, 0, Math.Sin(a2) * radius);

            vector lineArr[2];
            lineArr[0] = p1;
            lineArr[1] = p2;
            Shape s = Shape.CreateLines(color, shapeFlags, lineArr, 2);
            if (s)
                m_Shapes.Insert(s);
        }
    }
}

// A-Scope display.
// Draws a vertical amplitude-vs-range bar chart in world space for debugging.
// The bars rise from a baseline at the radar position; height = signal power.
class RDF_AScopeDisplay
{
    // Origin of the A-scope bar chart in world space.
    vector m_Origin = "0 0 0";
    // Horizontal axis scale: world metres per metre of real range.
    float m_HorizontalScale = 0.02;
    // Vertical scale: world metres per SNR dB.
    float m_VerticalScale = 0.1;
    // Maximum height of a bar (m).
    float m_MaxBarHeight = 20.0;
    // Colour for bars above the detection threshold.
    int m_DetectedColor = ARGB(200, 0, 220, 80);
    // Colour for bars below the detection threshold.
    int m_SubthresholdColor = ARGB(100, 100, 100, 30);
    // Colour for the baseline.
    int m_BaselineColor = ARGB(150, 60, 60, 60);

    // Holds active Shape references to keep them alive for the current frame.
    protected ref array<ref Shape> m_Shapes;

    void RDF_AScopeDisplay()
    {
        m_Shapes = new array<ref Shape>();
    }

    // Draw the A-scope for the supplied samples.
    // Bins samples by range and draws a bar per bin.
    void DrawAScope(array<ref RDF_LidarSample> samples, float maxRange, float thresholdDB)
    {
        if (!samples || samples.Count() == 0)
            return;

        m_Shapes.Clear();

        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        int binCount = 50;
        float binWidth = maxRange / (float)binCount;

        // Baseline.
        vector baseEnd = m_Origin;
        baseEnd[0] = m_Origin[0] + (float)binCount * m_HorizontalScale * binWidth;
        vector baselineArr[2];
        baselineArr[0] = m_Origin;
        baselineArr[1] = baseEnd;
        Shape baseShape = Shape.CreateLines(m_BaselineColor, shapeFlags, baselineArr, 2);
        if (baseShape)
            m_Shapes.Insert(baseShape);

        // Accumulate max SNR per range bin.
        array<float> binSNR = new array<float>();
        array<bool>  binHit = new array<bool>();
        for (int b = 0; b < binCount; b++)
        {
            binSNR.Insert(-999.0);
            binHit.Insert(false);
        }

        foreach (RDF_LidarSample baseSample : samples)
        {
            RDF_RadarSample sample = RDF_RadarSample.Cast(baseSample);
            if (!sample)
                continue;

            int bin = (int)(sample.m_Distance / binWidth);
            bin = Math.Clamp(bin, 0, binCount - 1);

            if (sample.m_SignalToNoiseRatio > binSNR[bin])
            {
                binSNR[bin] = sample.m_SignalToNoiseRatio;
                binHit[bin] = sample.m_Hit;
            }
        }

        // Draw bars.
        for (int b = 0; b < binCount; b++)
        {
            if (binSNR[b] <= -900.0)
                continue;

            float snr    = binSNR[b];
            float height = Math.Clamp(snr * m_VerticalScale, 0.0, m_MaxBarHeight);

            float xOffset = (float)b * m_HorizontalScale * binWidth;

            vector barBase = m_Origin;
            barBase[0] = barBase[0] + xOffset;

            vector barTop = barBase;
            barTop[1] = barTop[1] + height;

            int barColor;
            if (binHit[b])
                barColor = m_DetectedColor;
            else
                barColor = m_SubthresholdColor;

            vector barArr[2];
            barArr[0] = barBase;
            barArr[1] = barTop;
            Shape barShape = Shape.CreateLines(barColor, shapeFlags, barArr, 2);
            if (barShape)
                m_Shapes.Insert(barShape);
        }
    }
}
