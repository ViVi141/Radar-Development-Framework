// LiDAR point cloud visualizer using debug shapes.
// Showcase: range rings, optional sector fan, phosphor afterglow.
// Shape lifetime via RDF_DebugShapeManager (SCR_DebugShapeManager pattern).
class RDF_LidarVisualizer
{
    // Cap samples drawn per frame to avoid memory overflow with huge ray counts
    protected static const int MAX_DRAW_RAYS = 50000;
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const int MAX_SECTOR_SEGMENTS = 24;
    protected static const int MAX_RING_SEGMENTS = 48;

    protected ref RDF_LidarVisualSettings m_Settings;
    //! Persistent until origin / range / yaw / half-angle change.
    protected ref RDF_DebugShapeManager m_StaticShapes;
    //! Current-frame points / rays / point-cloud-only background.
    protected ref RDF_DebugShapeManager m_DynamicShapes;
    //! Afterglow alone so fade can refresh without wiping points.
    protected ref RDF_DebugShapeManager m_AfterglowShapes;
    protected ref array<ref RDF_LidarSample> m_Samples;
    protected float m_LastRange = 50.0;
    protected ref RDF_LidarColorStrategy m_ColorStrategy;

    protected ref array<ref RDF_LidarAfterglowBlip> m_Afterglow;
    protected int m_LastAfterglowScanSerial;
    protected int m_AfterglowWrite;

    protected bool m_StaticValid;
    protected vector m_CachedOrigin;
    protected float m_CachedRange;
    protected float m_CachedYaw;
    protected float m_CachedHalfAngleDeg;

    //! Injected by AutoRunner when Sweep / Conical strategy is active.
    protected bool m_HasSweepGeometry;
    protected float m_SweepHalfAngleDeg;
    protected float m_SweepWidthDeg;
    protected float m_SweepAzimuthDeg;

    void RDF_LidarVisualizer(RDF_LidarVisualSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
        {
            m_Settings = new RDF_LidarVisualSettings();
            m_Settings.ApplyShowcaseDefaults();
        }

        m_StaticShapes = new RDF_DebugShapeManager();
        m_DynamicShapes = new RDF_DebugShapeManager();
        m_AfterglowShapes = new RDF_DebugShapeManager();
        m_Samples = new array<ref RDF_LidarSample>();
        m_Afterglow = new array<ref RDF_LidarAfterglowBlip>();
        m_ColorStrategy = new RDF_DefaultColorStrategy();
        m_LastAfterglowScanSerial = -1;
        m_AfterglowWrite = 0;
        m_StaticValid = false;
        m_CachedOrigin = "0 0 0";
        m_CachedRange = -1.0;
        m_CachedYaw = 0.0;
        m_CachedHalfAngleDeg = -1.0;
        m_HasSweepGeometry = false;
        m_SweepHalfAngleDeg = 0.0;
        m_SweepWidthDeg = 0.0;
        m_SweepAzimuthDeg = 0.0;
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

    // Inject horizontal fan geometry for Sweep / Conical showcase overlay.
    // Clear with ClearSweepGeometry() when strategy has no sector (Uniform / Hemisphere).
    void SetSweepGeometry(float halfAngleDeg, float sweepWidthDeg, float azimuthDeg)
    {
        m_HasSweepGeometry = true;
        m_SweepHalfAngleDeg = halfAngleDeg;
        m_SweepWidthDeg = sweepWidthDeg;
        m_SweepAzimuthDeg = azimuthDeg;
    }

    void ClearSweepGeometry()
    {
        m_HasSweepGeometry = false;
        m_SweepHalfAngleDeg = 0.0;
        m_SweepWidthDeg = 0.0;
        m_SweepAzimuthDeg = 0.0;
    }

    // Release all cached Shape refs and sample refs.
    void Reset()
    {
        if (m_StaticShapes)
            m_StaticShapes.Clear();
        if (m_DynamicShapes)
            m_DynamicShapes.Clear();
        if (m_AfterglowShapes)
            m_AfterglowShapes.Clear();
        ClearAfterglow();
        m_LastAfterglowScanSerial = -1;
        m_StaticValid = false;
        if (m_Samples)
            m_Samples.Clear();
    }

    void ClearAfterglow()
    {
        if (m_Afterglow)
            m_Afterglow.Clear();
        m_AfterglowWrite = 0;
    }

    // Return a defensive copy of the most recent scan samples after Render().
    ref array<ref RDF_LidarSample> GetLastSamples()
    {
        ref array<ref RDF_LidarSample> outSamples = new array<ref RDF_LidarSample>();
        if (!m_Samples || m_Samples.Count() == 0)
            return outSamples;
        outSamples.Reserve(m_Samples.Count());
        foreach (RDF_LidarSample s : m_Samples)
            outSamples.Insert(s);
        return outSamples;
    }

    protected bool HasAnyVisuals()
    {
        if (!m_Settings)
            return false;
        if (m_Settings.m_DrawRays)
            return true;
        if (m_Settings.m_DrawPoints)
            return true;
        if (m_Settings.m_DrawOriginAxis)
            return true;
        if (m_Settings.m_DrawAfterglow)
            return true;
        if (m_Settings.m_DrawRangeRings)
            return true;
        if (m_Settings.m_DrawSectorSweep && m_HasSweepGeometry)
            return true;
        if (!m_Settings.m_RenderWorld)
            return true;
        return false;
    }

    void Render(IEntity subject, RDF_LidarScanner scanner)
    {
        Render(subject, scanner, -1);
    }

    void Render(IEntity subject, RDF_LidarScanner scanner, int scanSerial)
    {
        if (!subject || !scanner || !m_Settings)
            return;

        RDF_LidarSettings scanSettings = scanner.GetSettings();
        int rays = 0;
        if (scanSettings)
        {
            m_LastRange = Math.Max(0.1, scanSettings.m_Range);
            rays = Math.MaxInt(scanSettings.m_RayCount, 1);
        }

        if (m_Samples)
        {
            m_Samples.Clear();
            m_Samples.Reserve(rays);
        }

        // Always scan — scan results feed the HUD callback regardless of visuals.
        scanner.Scan(subject, m_Samples);

        DrawFromSamples(subject, m_Samples, scanSerial);
    }

    // Render using pre-computed samples (for network synchronization)
    void RenderWithSamples(IEntity subject, array<ref RDF_LidarSample> samples)
    {
        RenderWithSamples(subject, samples, -1);
    }

    void RenderWithSamples(IEntity subject, array<ref RDF_LidarSample> samples, int scanSerial)
    {
        if (!subject || !samples || !m_Settings)
            return;

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

        m_Samples.Clear();
        m_Samples.Reserve(samples.Count());
        foreach (RDF_LidarSample sampleIn : samples)
            m_Samples.Insert(sampleIn);

        DrawFromSamples(subject, m_Samples, scanSerial);
    }

    protected void DrawFromSamples(IEntity subject, array<ref RDF_LidarSample> samples, int scanSerial)
    {
        if (!m_Settings || !samples)
            return;

        if (!m_StaticShapes)
            m_StaticShapes = new RDF_DebugShapeManager();
        if (!m_DynamicShapes)
            m_DynamicShapes = new RDF_DebugShapeManager();
        if (!m_AfterglowShapes)
            m_AfterglowShapes = new RDF_DebugShapeManager();

        bool hasVisuals = HasAnyVisuals();
        if (!hasVisuals)
            return;

        vector origin = "0 0 0";
        vector forward = "0 0 1";
        if (samples.Count() > 0 && samples.Get(0))
            origin = samples.Get(0).m_Start;
        else if (subject)
            origin = subject.GetOrigin();

        if (subject)
        {
            vector worldMat[4];
            subject.GetWorldTransform(worldMat);
            forward = worldMat[2];
            float flen = forward.Length();
            if (flen < 0.001)
                forward = "0 0 1";
            else
                forward = forward / flen;
        }

        float rangeM = m_LastRange;
        if (rangeM < 1.0)
            rangeM = 1.0;

        float sectorHalfDeg = 0.0;
        vector fanForward = forward;
        bool drawFan = m_Settings.m_DrawSectorSweep && m_HasSweepGeometry;
        if (drawFan)
        {
            if (m_SweepWidthDeg > 0.1)
            {
                sectorHalfDeg = m_SweepWidthDeg * 0.5;
                float azRad = m_SweepAzimuthDeg * DEG_TO_RAD;
                fanForward = Vector(Math.Cos(azRad), 0.0, Math.Sin(azRad));
            }
            else
            {
                sectorHalfDeg = m_SweepHalfAngleDeg;
                fanForward = forward;
            }
        }

        ShapeFlags shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.DOUBLESIDE;

        bool anyStatic = m_Settings.m_DrawOriginAxis
            || m_Settings.m_DrawRangeRings
            || drawFan;
        if (anyStatic && IsStaticDirty(origin, fanForward, rangeM, sectorHalfDeg))
            RebuildStaticShapes(subject, origin, fanForward, rangeM, sectorHalfDeg, drawFan, shapeFlags);

        float nowS = System.GetTickCount() * 0.001;
        if (m_Settings.m_DrawAfterglow && scanSerial >= 0 && scanSerial != m_LastAfterglowScanSerial)
        {
            IngestSamplesForAfterglow(samples, nowS);
            m_LastAfterglowScanSerial = scanSerial;
        }

        m_DynamicShapes.Clear();
        if (m_AfterglowShapes)
            m_AfterglowShapes.Clear();

        if (!m_Settings.m_RenderWorld)
            DrawPointCloudOnlyBackground();

        // Afterglow first (same frame), then live points on top so density colors stay readable.
        if (m_Settings.m_DrawAfterglow)
            DrawAfterglow(nowS, shapeFlags);

        int drawLimit = Math.MinInt(samples.Count(), MAX_DRAW_RAYS);
        bool anyDynamic = m_Settings.m_DrawRays || m_Settings.m_DrawPoints;
        if (anyDynamic && drawLimit > 0)
        {
            if (m_Settings.m_UseBatchedMesh)
            {
                DrawBatchedMeshes(subject, samples, drawLimit);
            }
            else
            {
                for (int i = 0; i < drawLimit; i++)
                {
                    RDF_LidarSample sample = samples.Get(i);
                    if (!sample)
                        continue;
                    if (m_Settings.m_ShowHitsOnly && !sample.m_Hit)
                        continue;

                    if (m_Settings.m_DrawRays)
                        DrawRay(sample.m_Start, sample.m_HitPos, sample.m_Distance, sample.m_Hit);

                    if (m_Settings.m_DrawPoints)
                        DrawPointFromSample(sample);
                }
            }
        }
    }

    protected bool IsStaticDirty(vector origin, vector forward, float rangeM, float halfAngleDeg)
    {
        if (!m_StaticValid)
            return true;
        if (vector.DistanceSq(origin, m_CachedOrigin) > 0.25)
            return true;
        if (Math.AbsFloat(rangeM - m_CachedRange) > 0.5)
            return true;
        float yaw = Math.Atan2(forward[2], forward[0]);
        float dyaw = Math.AbsFloat(yaw - m_CachedYaw);
        if (dyaw > 0.02)
            return true;
        if (Math.AbsFloat(halfAngleDeg - m_CachedHalfAngleDeg) > 0.5)
            return true;
        return false;
    }

    protected void RebuildStaticShapes(
        IEntity subject,
        vector origin,
        vector forward,
        float rangeM,
        float halfAngleDeg,
        bool drawFan,
        ShapeFlags shapeFlags)
    {
        m_StaticShapes.Clear();

        if (m_Settings.m_DrawOriginAxis && subject)
            DrawOriginAxis(subject, origin);

        if (m_Settings.m_DrawRangeRings)
            DrawRangeRings(origin, rangeM, shapeFlags);

        if (drawFan)
            DrawSectorSweep(origin, forward, rangeM, halfAngleDeg, shapeFlags);

        m_CachedOrigin = origin;
        m_CachedRange = rangeM;
        m_CachedYaw = Math.Atan2(forward[2], forward[0]);
        m_CachedHalfAngleDeg = halfAngleDeg;
        m_StaticValid = true;
    }

    protected void DrawRangeRings(vector origin, float rangeM, ShapeFlags shapeFlags)
    {
        int segs = m_Settings.m_RangeRingSegments;
        if (segs < 8)
            segs = 8;
        if (segs > MAX_RING_SEGMENTS)
            segs = MAX_RING_SEGMENTS;

        // Cyan theme to distinguish from radar green.
        vector ringOrigin = origin + Vector(0.0, 0.5, 0.0);
        m_StaticShapes.AddCircleXZ(
            ringOrigin, rangeM * 0.5, ARGBF(0.25, 0.35, 0.85, 1.0), segs, shapeFlags);
        m_StaticShapes.AddCircleXZ(
            ringOrigin, rangeM, ARGBF(0.35, 0.4, 0.95, 1.0), segs, shapeFlags);
    }

    protected void DrawSectorSweep(
        vector origin,
        vector forward,
        float rangeM,
        float halfAngleDeg,
        ShapeFlags shapeFlags)
    {
        float halfRad = halfAngleDeg * DEG_TO_RAD;
        if (halfRad < 0.02)
            halfRad = 0.02;
        if (halfRad > 3.14159)
            halfRad = 3.14159;

        float yaw = Math.Atan2(forward[2], forward[0]);
        int segs = m_Settings.m_SectorSweepSegments;
        if (segs < 4)
            segs = 4;
        if (segs > MAX_SECTOR_SEGMENTS)
            segs = MAX_SECTOR_SEGMENTS;

        float height = m_Settings.m_SectorHeightM;
        if (height < 0.5)
            height = 0.5;
        vector upLift = Vector(0.0, height, 0.0);
        vector originHi = origin + upLift * 0.35;

        float edgeA = m_Settings.m_SectorSweepEdgeAlpha;
        float fillA = m_Settings.m_SectorSweepAlpha;
        int edgeColor = ARGBF(edgeA, 0.35, 0.85, 1.0);
        int fillColor = ARGBF(fillA, 0.2, 0.7, 0.95);
        int sweepColor = ARGBF(0.85, 0.55, 0.95, 1.0);

        vector sweepEnd = originHi
            + Vector(Math.Cos(yaw), 0.0, Math.Sin(yaw)) * rangeM
            + upLift * 0.15;
        m_StaticShapes.AddLine(originHi, sweepEnd, sweepColor, shapeFlags);

        float leftYaw = yaw - halfRad;
        float rightYaw = yaw + halfRad;
        vector leftDir = Vector(Math.Cos(leftYaw), 0.0, Math.Sin(leftYaw));
        vector rightDir = Vector(Math.Cos(rightYaw), 0.0, Math.Sin(rightYaw));

        m_StaticShapes.AddLine(originHi, originHi + leftDir * rangeM, edgeColor, shapeFlags);
        m_StaticShapes.AddLine(originHi, originHi + rightDir * rangeM, edgeColor, shapeFlags);

        vector tris[72];
        float step = (2.0 * halfRad) / segs;
        for (int i = 0; i < segs; i++)
        {
            float a0 = leftYaw + step * i;
            float a1 = leftYaw + step * (i + 1);
            vector d0 = Vector(Math.Cos(a0), 0.0, Math.Sin(a0));
            vector d1 = Vector(Math.Cos(a1), 0.0, Math.Sin(a1));
            vector p1 = originHi + d0 * rangeM;
            vector p2 = originHi + d1 * rangeM;
            int base = i * 3;
            tris[base] = originHi;
            tris[base + 1] = p1;
            tris[base + 2] = p2;
        }

        ShapeFlags triFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER
            | ShapeFlags.TRANSP | ShapeFlags.DOUBLESIDE;
        Shape fan = Shape.CreateTris(fillColor, triFlags, tris, segs);
        if (fan)
            m_StaticShapes.Add(fan);

        m_StaticShapes.AddCircleArcXZ(
            originHi, leftYaw, 2.0 * halfRad, rangeM, edgeColor, segs, shapeFlags);
    }

    protected void IngestSamplesForAfterglow(array<ref RDF_LidarSample> samples, float nowS)
    {
        if (!samples || !m_Afterglow)
            return;

        int maxBlips = m_Settings.m_AfterglowMaxBlips;
        if (maxBlips < 16)
            maxBlips = 16;

        int hitCount = 0;
        foreach (RDF_LidarSample sCheck : samples)
        {
            if (sCheck && sCheck.m_Hit)
                hitCount = hitCount + 1;
        }

        int step = 1;
        if (hitCount > maxBlips)
            step = hitCount / maxBlips;
        if (step < 1)
            step = 1;

        int hitIndex = 0;
        foreach (RDF_LidarSample s : samples)
        {
            if (!s || !s.m_Hit)
                continue;
            if (hitIndex % step != 0)
            {
                hitIndex = hitIndex + 1;
                continue;
            }
            hitIndex = hitIndex + 1;

            float r;
            float g;
            float b;
            ExtractRgbFromSample(s, r, g, b);

            RDF_LidarAfterglowBlip blip;
            if (m_Afterglow.Count() < maxBlips)
            {
                blip = new RDF_LidarAfterglowBlip();
                m_Afterglow.Insert(blip);
            }
            else
            {
                if (m_AfterglowWrite >= m_Afterglow.Count())
                    m_AfterglowWrite = 0;
                blip = m_Afterglow.Get(m_AfterglowWrite);
                if (!blip)
                {
                    blip = new RDF_LidarAfterglowBlip();
                    m_Afterglow.Set(m_AfterglowWrite, blip);
                }
                m_AfterglowWrite = m_AfterglowWrite + 1;
            }

            blip.m_Pos = s.m_HitPos;
            blip.m_BirthS = nowS;
            blip.m_R = r;
            blip.m_G = g;
            blip.m_B = b;
        }
    }

    protected void ExtractRgbFromSample(RDF_LidarSample sample, out float r, out float g, out float b)
    {
        // Avoid ARGBF ↔ UnpackInt round-trip (can wash density hues). Match material map directly.
        r = 0.6;
        g = 0.45;
        b = 0.65;
        if (!sample || !sample.m_Hit)
            return;

        float density = -1.0;
        if (sample.m_Surface)
        {
            BallisticInfo bi = sample.m_Surface.GetBallisticInfo();
            if (bi)
                density = bi.GetDensity();
        }

        float t = 0.5;
        if (density >= 0.0)
        {
            t = density / 8.0;
            if (t < 0.0)
                t = 0.0;
            if (t > 1.0)
                t = 1.0;
        }

        r = Math.Lerp(0.2, 1.0, t);
        g = Math.Lerp(0.5, 0.4, t);
        b = Math.Lerp(1.0, 0.3, t);
    }

    protected void DrawAfterglow(float nowS, ShapeFlags shapeFlags)
    {
        if (!m_Afterglow)
            return;
        float life = m_Settings.m_AfterglowSec;
        if (life < 0.2)
            life = 0.2;
        float size = m_Settings.m_AfterglowPointSize;
        if (size < 0.05)
            size = 0.05;

        for (int i = 0; i < m_Afterglow.Count(); i++)
        {
            RDF_LidarAfterglowBlip blip = m_Afterglow.Get(i);
            if (!blip)
                continue;
            float age = nowS - blip.m_BirthS;
            if (age < 0.0)
                age = 0.0;
            if (age > life)
                continue;
            float t = 1.0 - (age / life);
            float alpha = 0.08 + 0.28 * t;
            float rad = size * (0.45 + 0.55 * t);
            int color = ARGBF(alpha, blip.m_R, blip.m_G, blip.m_B);
            m_AfterglowShapes.AddSphere(blip.m_Pos, rad, color, shapeFlags);
        }
    }

    // Draw a large black quad just in front of the local camera to occlude the world (used when m_RenderWorld is false).
    protected void DrawPointCloudOnlyBackground()
    {
        if (!m_DynamicShapes)
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
        vector fwd = mat[2];
        vector right = mat[0];
        vector up = mat[1];
        float dist = 1.0;
        float halfSize = 100.0;
        vector center = pos + (fwd * dist);
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
        int bgFlags = ShapeFlags.NOOUTLINE | ShapeFlags.DOUBLESIDE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        Shape bgShape = Shape.CreateTris(bgColor, bgFlags, tris, 2);
        if (bgShape)
            m_DynamicShapes.Add(bgShape);
    }

    protected void DrawOriginAxis(IEntity subject, vector origin)
    {
        if (!m_StaticShapes || !subject || m_Settings.m_OriginAxisLength <= 0.0)
            return;

        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector axisX = worldMat[0];
        vector axisY = worldMat[1];
        vector axisZ = worldMat[2];
        float len = m_Settings.m_OriginAxisLength;
        ShapeFlags flags = ShapeFlags.NOOUTLINE;

        m_StaticShapes.AddLine(origin, origin + axisX * len, ARGBF(1, 1, 0, 0), flags);
        m_StaticShapes.AddLine(origin, origin + axisY * len, ARGBF(1, 0, 1, 0), flags);
        m_StaticShapes.AddLine(origin, origin + axisZ * len, ARGBF(1, 0, 0, 1), flags);
    }

    protected void DrawBatchedMeshes(IEntity subject, array<ref RDF_LidarSample> samples, int maxSamplesToDraw = -1)
    {
        if (!m_DynamicShapes || !samples || samples.Count() == 0 || !m_Settings)
            return;
        int drawLimit;
        if (maxSamplesToDraw >= 0)
            drawLimit = Math.MinInt(samples.Count(), maxSamplesToDraw);
        else
            drawLimit = samples.Count();
        if (drawLimit <= 0)
            return;

        PlayerController controller = GetGame().GetPlayerController();
        if (!controller)
            return;
        PlayerCamera playerCam = controller.GetPlayerCamera();
        if (!playerCam)
            return;

        vector camMat[4];
        playerCam.GetWorldCameraTransform(camMat);
        vector camRight = camMat[0];
        vector camUp = camMat[1];

        int segs = Math.MaxInt(1, m_Settings.m_RaySegments);
        array<ref array<vector>> trisHits = new array<ref array<vector>>();
        array<ref array<vector>> trisMisses = new array<ref array<vector>>();
        for (int i = 0; i < segs; i++)
        {
            trisHits.Insert(new array<vector>());
            trisMisses.Insert(new array<vector>());
        }

        float defaultPointSize = Math.Max(0.001, m_Settings.m_PointSize);
        float rayHalfWidth = defaultPointSize * 0.2;

        for (int sampleIdx = 0; sampleIdx < drawLimit; sampleIdx++)
        {
            RDF_LidarSample sample = samples.Get(sampleIdx);
            if (m_Settings.m_ShowHitsOnly && !sample.m_Hit)
                continue;

            vector start = sample.m_Start;
            vector end = sample.m_HitPos;
            vector full = end - start;
            float fullLen = full.Length();
            vector dirNorm;
            if (fullLen > 0.00001)
                dirNorm = full / fullLen;
            else
                dirNorm = Vector(0, 0, 1);

            vector side = camRight;
            float dot = side[0] * dirNorm[0] + side[1] * dirNorm[1] + side[2] * dirNorm[2];
            vector perp = side - (dirNorm * dot);
            float plen = perp.Length();
            if (plen <= 1e-5)
                perp = camUp;
            else
                perp = perp / plen;

            if (m_Settings.m_DrawRays && fullLen > 0.0)
            {
                for (int si = 1; si <= segs; si++)
                {
                    float t0 = (si - 1) / (float)segs;
                    float t1 = si / (float)segs;
                    vector p0 = start + full * t0;
                    vector p1 = start + full * t1;

                    vector v0 = p0 - perp * rayHalfWidth;
                    vector v1 = p0 + perp * rayHalfWidth;
                    vector v2 = p1 + perp * rayHalfWidth;
                    vector v3 = p1 - perp * rayHalfWidth;

                    int bucket = Math.ClampInt((int)Math.Floor(t1 * segs), 0, segs - 1);
                    ref array<vector> bucketArr;
                    if (sample.m_Hit)
                        bucketArr = trisHits.Get(bucket);
                    else
                        bucketArr = trisMisses.Get(bucket);
                    bucketArr.Insert(v0);
                    bucketArr.Insert(v1);
                    bucketArr.Insert(v2);
                    bucketArr.Insert(v0);
                    bucketArr.Insert(v2);
                    bucketArr.Insert(v3);
                }
            }
        }

        int triFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.DOUBLESIDE | ShapeFlags.ONCE;

        if (m_Settings.m_DrawPoints)
        {
            for (int pi = 0; pi < drawLimit; pi++)
            {
                RDF_LidarSample spt = samples.Get(pi);
                if (m_Settings.m_ShowHitsOnly && !spt.m_Hit)
                    continue;
                DrawPointFromSample(spt);
            }
        }

        for (int i = 0; i < segs; i++)
        {
            int hitColor = BuildRayColorAtT(((i + 0.5) / (float)segs), true);
            ref array<vector> hitTris = trisHits.Get(i);
            if (hitTris && hitTris.Count() > 0)
                EmitTrisArray(hitColor, triFlags, hitTris);

            int missColor = BuildRayColorAtT(((i + 0.5) / (float)segs), false);
            ref array<vector> missTris = trisMisses.Get(i);
            if (missTris && missTris.Count() > 0)
                EmitTrisArray(missColor, triFlags, missTris);
        }
    }

    protected void EmitTrisArray(int color, int flags, array<vector> src)
    {
        const int CHUNK = 1020;
        int totalVerts = src.Count();
        int offset = 0;
        while (offset < totalVerts)
        {
            int copyCount = Math.MinInt(totalVerts - offset, CHUNK);
            vector trisBuf[CHUNK];
            for (int vi = 0; vi < copyCount; vi++)
                trisBuf[vi] = src.Get(offset + vi);

            int triCountChunk = copyCount / 3;
            Shape s = Shape.CreateTris(color, flags, trisBuf, triCountChunk);
            if (s)
                m_DynamicShapes.Add(s);

            offset += copyCount;
        }
    }

    protected void DrawPointFromSample(RDF_LidarSample sample)
    {
        if (!m_DynamicShapes || !sample)
            return;

        int color = BuildPointColorFromSample(sample);
        float size = BuildPointSizeFromSample(sample);
        // No ONCE: keep density-colored points between scan ticks (held by ShapeManager).
        ShapeFlags shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.DOUBLESIDE;
        m_DynamicShapes.AddSphere(sample.m_HitPos, size, color, shapeFlags);
    }

    protected void DrawPoint(vector pos, float dist, bool hit)
    {
        if (!m_DynamicShapes)
            return;

        int color = BuildPointColor(dist, hit);
        ShapeFlags shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.ONCE;
        m_DynamicShapes.AddSphere(pos, m_Settings.m_PointSize, color, shapeFlags);
    }

    protected void DrawRay(vector start, vector end, float dist, bool hit)
    {
        if (!m_DynamicShapes)
            return;

        int segments = m_Settings.m_RaySegments;
        if (segments < 1)
            segments = 1;

        vector last = start;
        ShapeFlags shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.ONCE;
        for (int i = 1; i <= segments; i++)
        {
            float t = i / (float)segments;
            vector next = start + (end - start) * t;
            int color = BuildRayColorAtT(t, hit);
            m_DynamicShapes.AddLine(last, next, color, shapeFlags);
            last = next;
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

    protected int BuildPointColorFromSample(RDF_LidarSample sample)
    {
        if (m_ColorStrategy)
            return m_ColorStrategy.BuildPointColorFromSample(sample, GetRangeSafe(), m_Settings);

        return BuildPointColor(sample.m_Distance, sample.m_Hit);
    }

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
}
