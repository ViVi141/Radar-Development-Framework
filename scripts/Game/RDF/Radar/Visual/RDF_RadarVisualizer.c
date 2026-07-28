// Radar world-space visualizer: showcase sector fan, lock beam, afterglow,
// range rings, plot markers, WLR fixes. Debug rays remain optional.
class RDF_RadarVisualizer
{
    protected ref RDF_RadarVisualSettings m_Settings;
    protected ref array<ref Shape> m_DebugShapes;
    protected ref array<ref RDF_RadarAfterglowBlip> m_Afterglow;
    protected int m_LastAfterglowScanSerial;
    protected static const float FALLBACK_RAY_LENGTH = 100.0;
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const int MAX_SECTOR_SEGMENTS = 24;
    protected static const int MAX_RING_SEGMENTS = 48;

    void RDF_RadarVisualizer(RDF_RadarVisualSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
        {
            m_Settings = new RDF_RadarVisualSettings();
            m_Settings.ApplyShowcaseDefaults();
        }
        m_DebugShapes = new array<ref Shape>();
        m_Afterglow = new array<ref RDF_RadarAfterglowBlip>();
        m_LastAfterglowScanSerial = -1;
    }

    RDF_RadarVisualSettings GetSettings()
    {
        return m_Settings;
    }

    void Reset()
    {
        if (m_DebugShapes)
            m_DebugShapes.Clear();
        ClearAfterglow();
        m_LastAfterglowScanSerial = -1;
    }

    void ClearAfterglow()
    {
        if (m_Afterglow)
            m_Afterglow.Clear();
    }

    //------------------------------------------------------------------------------------------------
    // Full showcase / debug draw. Call every presentation tick (shapes persist
    // until the next call — do not use ONCE or CallLater gaps will flicker).
    void Render(
        IEntity subject,
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        float rangeM,
        float sectorHalfAngleDeg,
        RDF_RadarLockManager lockMgr,
        RDF_RadarProjectileTracker tracker,
        int scanSerial)
    {
        if (!m_Settings || !m_DebugShapes)
            return;

        bool any = m_Settings.m_DrawRays
            || m_Settings.m_DrawPoints
            || m_Settings.m_DrawOriginAxis
            || m_Settings.m_DrawSectorSweep
            || m_Settings.m_DrawLockBeam
            || m_Settings.m_DrawAfterglow
            || m_Settings.m_DrawRangeRings
            || m_Settings.m_DrawTrackRibbon;
        if (!any)
            return;

        m_DebugShapes.Clear();

        if (origin.LengthSq() < 0.0001 && subject)
            origin = GetSubjectOrigin(subject);

        float flen = forward.Length();
        if (flen < 0.001)
            forward = "0 0 1";
        else
            forward = forward / flen;

        if (rangeM < 10.0)
            rangeM = 10.0;

        float nowS = System.GetTickCount() * 0.001;
        // Persist until the next Render(): CallLater is ~5 Hz, ONCE would flicker.
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER
            | ShapeFlags.TRANSP | ShapeFlags.DOUBLESIDE;

        if (m_Settings.m_DrawOriginAxis && subject)
            DrawOriginAxis(subject, origin);

        if (m_Settings.m_DrawRangeRings)
            DrawRangeRings(origin, rangeM, shapeFlags);

        if (m_Settings.m_DrawSectorSweep)
            DrawSectorSweep(origin, forward, rangeM, sectorHalfAngleDeg, shapeFlags);

        if (m_Settings.m_DrawAfterglow)
        {
            if (scanSerial != m_LastAfterglowScanSerial)
            {
                IngestPlotsForAfterglow(targets, nowS);
                m_LastAfterglowScanSerial = scanSerial;
            }
            DrawAfterglow(nowS, shapeFlags);
        }

        if (targets && m_Settings.m_DrawPoints)
            DrawPlotMarkers(origin, targets, shapeFlags);

        if (targets && m_Settings.m_DrawRays)
            DrawPlotRays(origin, forward, targets, shapeFlags);

        if (m_Settings.m_DrawLockBeam && lockMgr)
            DrawLockBeam(origin, lockMgr, shapeFlags);

        if (m_Settings.m_DrawTrackRibbon && tracker)
            DrawTrackRibbons(tracker, shapeFlags);
    }

    // Backward-compatible overload used by older call sites.
    void Render(IEntity subject, array<ref RDF_RadarTarget> targets)
    {
        vector origin = GetSubjectOrigin(subject);
        vector forward = "0 0 1";
        if (subject)
        {
            vector worldMat[4];
            subject.GetWorldTransform(worldMat);
            forward = worldMat[2];
        }
        Render(subject, targets, origin, forward, 2000.0, 45.0, null, null, -1);
    }

    // Draw cached weapon-locating launch / impact points for projectile tracks.
    void RenderWeaponLocates(RDF_RadarProjectileTracker tracker)
    {
        if (!tracker || !m_Settings)
            return;
        if (!m_Settings.m_DrawWeaponLocate)
            return;
        if (!m_DebugShapes)
            m_DebugShapes = new array<ref Shape>();

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        float markerSize = m_Settings.m_WeaponLocateMarkerSize;
        if (markerSize < 0.2)
            markerSize = 0.2;
        float alertRadiusM = m_Settings.m_WeaponLocateAlertRadiusM;
        if (alertRadiusM < 1.0)
            alertRadiusM = 1.0;
        int launchColor = ARGBF(0.95, 1.0, 0.55, 0.1);
        int impactColor = ARGBF(0.95, 0.15, 0.9, 1.0);
        int linkColor = ARGBF(0.65, 0.85, 0.85, 0.85);
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER
            | ShapeFlags.TRANSP;

        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack track = tracks.Get(i);
            if (!track || !track.m_Confirmed)
                continue;
            RDF_RadarWlrFix fix = track.m_LastWlrFix;
            if (!fix)
                continue;

            if (fix.m_LaunchValid && fix.m_ImpactValid)
            {
                vector link[2];
                link[0] = fix.m_LaunchPos;
                link[1] = fix.m_ImpactPos;
                m_DebugShapes.Insert(Shape.CreateLines(linkColor, shapeFlags, link, 2));
            }
            if (fix.m_LaunchValid)
            {
                m_DebugShapes.Insert(Shape.CreateSphere(
                    launchColor, shapeFlags, fix.m_LaunchPos, markerSize));
                m_DebugShapes.Insert(Shape.CreateSphere(
                    ARGBF(0.35, 1.0, 0.55, 0.1), shapeFlags, fix.m_LaunchPos, markerSize * 2.2));
                DrawGroundAlertRing(fix.m_LaunchPos, alertRadiusM, launchColor, shapeFlags);
            }
            if (fix.m_ImpactValid)
            {
                m_DebugShapes.Insert(Shape.CreateSphere(
                    impactColor, shapeFlags, fix.m_ImpactPos, markerSize));
                m_DebugShapes.Insert(Shape.CreateSphere(
                    ARGBF(0.35, 0.15, 0.9, 1.0), shapeFlags, fix.m_ImpactPos, markerSize * 2.2));
                DrawGroundAlertRing(fix.m_ImpactPos, alertRadiusM, impactColor, shapeFlags);
            }
        }
    }

    protected void DrawGroundAlertRing(
        vector center,
        float radiusM,
        int color,
        int shapeFlags)
    {
        if (radiusM < 1.0)
            return;
        int segs = 24;
        if (m_Settings && m_Settings.m_RangeRingSegments > 8)
            segs = m_Settings.m_RangeRingSegments;
        if (segs > 48)
            segs = 48;

        for (int s = 0; s < segs; s++)
        {
            float a0 = (Math.PI * 2.0 * s) / segs;
            float a1 = (Math.PI * 2.0 * (s + 1)) / segs;
            vector p0 = center;
            vector p1 = center;
            p0[0] = center[0] + Math.Cos(a0) * radiusM;
            p0[2] = center[2] + Math.Sin(a0) * radiusM;
            p1[0] = center[0] + Math.Cos(a1) * radiusM;
            p1[2] = center[2] + Math.Sin(a1) * radiusM;
            vector seg[2];
            seg[0] = p0;
            seg[1] = p1;
            m_DebugShapes.Insert(Shape.CreateLines(color, shapeFlags, seg, 2));
        }
    }

    protected void DrawOriginAxis(IEntity subject, vector origin)
    {
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        float len = m_Settings.m_OriginAxisLength;
        int flags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER;

        vector endX[2];
        endX[0] = origin;
        endX[1] = origin + worldMat[0] * len;
        m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 1, 0, 0), flags, endX, 2));

        vector endY[2];
        endY[0] = origin;
        endY[1] = origin + worldMat[1] * len;
        m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 0, 1, 0), flags, endY, 2));

        vector endZ[2];
        endZ[0] = origin;
        endZ[1] = origin + worldMat[2] * len;
        m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 0, 0, 1), flags, endZ, 2));
    }

    protected void DrawRangeRings(vector origin, float rangeM, int shapeFlags)
    {
        int segs = m_Settings.m_RangeRingSegments;
        if (segs < 8)
            segs = 8;
        if (segs > MAX_RING_SEGMENTS)
            segs = MAX_RING_SEGMENTS;

        DrawHorizontalRing(origin, rangeM * 0.5, segs, ARGBF(0.25, 0.2, 0.9, 0.45), shapeFlags);
        DrawHorizontalRing(origin, rangeM, segs, ARGBF(0.35, 0.25, 1.0, 0.55), shapeFlags);
    }

    protected void DrawHorizontalRing(
        vector origin,
        float radiusM,
        int segs,
        int color,
        int shapeFlags)
    {
        if (radiusM < 1.0)
            return;
        float step = 6.2831853 / segs;
        for (int i = 0; i < segs; i++)
        {
            float a0 = step * i;
            float a1 = step * (i + 1);
            vector p[2];
            p[0] = origin + Vector(Math.Cos(a0) * radiusM, 0.5, Math.Sin(a0) * radiusM);
            p[1] = origin + Vector(Math.Cos(a1) * radiusM, 0.5, Math.Sin(a1) * radiusM);
            m_DebugShapes.Insert(Shape.CreateLines(color, shapeFlags, p, 2));
        }
    }

    protected void DrawSectorSweep(
        vector origin,
        vector forward,
        float rangeM,
        float halfAngleDeg,
        int shapeFlags)
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
        if (height < 1.0)
            height = 1.0;
        vector upLift = Vector(0.0, height, 0.0);
        vector originHi = origin + upLift * 0.35;

        float edgeA = m_Settings.m_SectorSweepEdgeAlpha;
        float fillA = m_Settings.m_SectorSweepAlpha;
        int edgeColor = ARGBF(edgeA, 0.35, 1.0, 0.55);
        int fillColor = ARGBF(fillA, 0.15, 0.85, 0.4);
        int sweepColor = ARGBF(0.85, 0.55, 1.0, 0.75);

        // Leading sweep ray (boresight).
        vector sweep[2];
        sweep[0] = originHi;
        sweep[1] = originHi + Vector(Math.Cos(yaw), 0.0, Math.Sin(yaw)) * rangeM + upLift * 0.15;
        m_DebugShapes.Insert(Shape.CreateLines(sweepColor, shapeFlags, sweep, 2));

        float leftYaw = yaw - halfRad;
        float rightYaw = yaw + halfRad;
        vector leftDir = Vector(Math.Cos(leftYaw), 0.0, Math.Sin(leftYaw));
        vector rightDir = Vector(Math.Cos(rightYaw), 0.0, Math.Sin(rightYaw));

        vector leftEdge[2];
        leftEdge[0] = originHi;
        leftEdge[1] = originHi + leftDir * rangeM;
        m_DebugShapes.Insert(Shape.CreateLines(edgeColor, shapeFlags, leftEdge, 2));

        vector rightEdge[2];
        rightEdge[0] = originHi;
        rightEdge[1] = originHi + rightDir * rangeM;
        m_DebugShapes.Insert(Shape.CreateLines(edgeColor, shapeFlags, rightEdge, 2));

        // Filled wedges (flat fan) + arc.
        float step = (2.0 * halfRad) / segs;
        for (int i = 0; i < segs; i++)
        {
            float a0 = leftYaw + step * i;
            float a1 = leftYaw + step * (i + 1);
            vector d0 = Vector(Math.Cos(a0), 0.0, Math.Sin(a0));
            vector d1 = Vector(Math.Cos(a1), 0.0, Math.Sin(a1));
            vector p0 = originHi;
            vector p1 = originHi + d0 * rangeM;
            vector p2 = originHi + d1 * rangeM;

            vector tris[3];
            tris[0] = p0;
            tris[1] = p1;
            tris[2] = p2;
            Shape fan = Shape.CreateTris(fillColor, shapeFlags, tris, 1);
            if (fan)
                m_DebugShapes.Insert(fan);

            vector arc[2];
            arc[0] = p1;
            arc[1] = p2;
            m_DebugShapes.Insert(Shape.CreateLines(edgeColor, shapeFlags, arc, 2));
        }
    }

    protected void DrawPlotMarkers(
        vector origin,
        array<ref RDF_RadarTarget> targets,
        int shapeFlags)
    {
        float pointSize = m_Settings.m_PointSize;
        if (pointSize < 0.2)
            pointSize = 0.2;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;
            if (!t.m_Detected)
                continue;
            int color = GetColorForType(t.m_Type);
            m_DebugShapes.Insert(Shape.CreateSphere(color, shapeFlags, t.m_Position, pointSize));
        }
    }

    protected void DrawPlotRays(
        vector origin,
        vector forward,
        array<ref RDF_RadarTarget> targets,
        int shapeFlags)
    {
        if (targets.Count() == 0)
        {
            vector p[2];
            p[0] = origin;
            p[1] = origin + forward * FALLBACK_RAY_LENGTH;
            m_DebugShapes.Insert(Shape.CreateLines(
                ARGBF(0.5, 0.5, 0.5, 1.0), shapeFlags, p, 2));
            return;
        }

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t || !t.m_Detected)
                continue;
            vector p[2];
            p[0] = origin;
            p[1] = t.m_Position;
            m_DebugShapes.Insert(Shape.CreateLines(
                GetRayColorForType(t.m_Type), shapeFlags, p, 2));
        }
    }

    protected void DrawLockBeam(
        vector origin,
        RDF_RadarLockManager lockMgr,
        int shapeFlags)
    {
        ERDF_RadarLockState state = lockMgr.GetState();
        if (state != ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING
            && state != ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING
            && state != ERDF_RadarLockState.RDF_RADAR_LOCK_COAST)
        {
            return;
        }

        IEntity ent;
        vector aimPos;
        if (!lockMgr.GetLockedTarget(ent, aimPos))
            return;

        float a = m_Settings.m_LockBeamAlpha;
        int beamColor = ARGBF(a, 1.0, 0.25, 0.15);
        int coreColor = ARGBF(Math.Clamp(a + 0.25, 0.0, 1.0), 1.0, 0.85, 0.35);

        vector core[2];
        core[0] = origin;
        core[1] = aimPos;
        m_DebugShapes.Insert(Shape.CreateLines(coreColor, shapeFlags, core, 2));

        float endR = m_Settings.m_LockBeamEndRadiusM;
        if (endR < 1.0)
            endR = 1.0;
        vector toAim = aimPos - origin;
        float dist = toAim.Length();
        if (dist < 1.0)
            return;
        vector dir = toAim / dist;
        vector up = "0 1 0";
        vector side = up * dir;
        float sideLen = side.Length();
        if (sideLen < 0.001)
        {
            side = Vector(1.0, 0.0, 0.0) * dir;
            sideLen = side.Length();
        }
        if (sideLen < 0.001)
            return;
        side = side * (1.0 / sideLen);
        vector binormal = dir * side;

        int spokes = 8;
        for (int i = 0; i < spokes; i++)
        {
            float ang = (6.2831853 * i) / spokes;
            vector offset = side * (Math.Cos(ang) * endR)
                + binormal * (Math.Sin(ang) * endR);
            vector spoke[2];
            spoke[0] = origin;
            spoke[1] = aimPos + offset;
            m_DebugShapes.Insert(Shape.CreateLines(beamColor, shapeFlags, spoke, 2));
        }

        m_DebugShapes.Insert(Shape.CreateSphere(
            ARGBF(0.9, 1.0, 0.35, 0.15), shapeFlags, aimPos, endR * 0.35));
    }

    protected void IngestPlotsForAfterglow(array<ref RDF_RadarTarget> targets, float nowS)
    {
        if (!targets || !m_Afterglow)
            return;

        int maxBlips = m_Settings.m_AfterglowMaxBlips;
        if (maxBlips < 16)
            maxBlips = 16;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t || !t.m_Detected)
                continue;

            float r;
            float g;
            float b;
            GetRgbForType(t.m_Type, r, g, b);

            RDF_RadarAfterglowBlip blip = new RDF_RadarAfterglowBlip();
            blip.m_Pos = t.m_Position;
            blip.m_BirthS = nowS;
            blip.m_R = r;
            blip.m_G = g;
            blip.m_B = b;
            m_Afterglow.Insert(blip);
        }

        while (m_Afterglow.Count() > maxBlips)
            m_Afterglow.Remove(0);
    }

    protected void DrawAfterglow(float nowS, int shapeFlags)
    {
        if (!m_Afterglow)
            return;
        float life = m_Settings.m_AfterglowSec;
        if (life < 0.2)
            life = 0.2;
        float size = m_Settings.m_AfterglowPointSize;
        if (size < 0.15)
            size = 0.15;

        for (int i = m_Afterglow.Count() - 1; i >= 0; i--)
        {
            RDF_RadarAfterglowBlip blip = m_Afterglow.Get(i);
            if (!blip)
            {
                m_Afterglow.Remove(i);
                continue;
            }
            float age = nowS - blip.m_BirthS;
            if (age < 0.0)
                age = 0.0;
            if (age > life)
            {
                m_Afterglow.Remove(i);
                continue;
            }
            float t = 1.0 - (age / life);
            float alpha = 0.15 + 0.55 * t;
            float rad = size * (0.45 + 0.55 * t);
            int color = ARGBF(alpha, blip.m_R, blip.m_G, blip.m_B);
            m_DebugShapes.Insert(Shape.CreateSphere(color, shapeFlags, blip.m_Pos, rad));
        }
    }

    protected void DrawTrackRibbons(RDF_RadarProjectileTracker tracker, int shapeFlags)
    {
        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        int ribbonColor = ARGBF(0.7, 1.0, 0.55, 0.15);
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack track = tracks.Get(i);
            if (!track || !track.IsProjectileTrack())
                continue;
            if (!track.m_Positions || track.m_Positions.Count() < 2)
                continue;

            for (int p = 1; p < track.m_Positions.Count(); p++)
            {
                vector seg[2];
                seg[0] = track.m_Positions.Get(p - 1);
                seg[1] = track.m_Positions.Get(p);
                m_DebugShapes.Insert(Shape.CreateLines(ribbonColor, shapeFlags, seg, 2));
            }
        }
    }

    protected vector GetSubjectOrigin(IEntity subject)
    {
        if (!subject)
            return "0 0 0";
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        return worldMat[3];
    }

    protected void GetRgbForType(ERDF_RadarTargetType type, out float r, out float g, out float b)
    {
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
        {
            r = 1.0;
            g = 0.35;
            b = 0.1;
            return;
        }
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
        {
            r = 1.0;
            g = 0.3;
            b = 0.95;
            return;
        }
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
        {
            r = 1.0;
            g = 0.95;
            b = 0.55;
            return;
        }
        r = 0.25;
        g = 1.0;
        b = 0.4;
    }

    protected int GetColorForType(ERDF_RadarTargetType type)
    {
        float r;
        float g;
        float b;
        GetRgbForType(type, r, g, b);
        return ARGBF(0.95, r, g, b);
    }

    protected int GetRayColorForType(ERDF_RadarTargetType type)
    {
        float a = Math.Clamp(m_Settings.m_RayAlpha, 0.05, 1.0);
        float r;
        float g;
        float b;
        GetRgbForType(type, r, g, b);
        return ARGBF(a, r, g, b);
    }
}
