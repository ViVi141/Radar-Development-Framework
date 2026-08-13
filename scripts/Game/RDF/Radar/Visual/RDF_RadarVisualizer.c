// Radar world-space visualizer: showcase sector fan, lock beam, afterglow,
// range rings, plot markers, WLR fixes. Debug rays remain optional.
// Perf: static geometry cached; rings/arcs use CreateCircle*; fans use CreateTris.
// Shape lifetime: RDF_DebugShapeManager (SCR_DebugShapeManager pattern).
class RDF_RadarVisualizer
{
    protected ref RDF_RadarVisualSettings m_Settings;
    //! Persistent until origin / range / yaw / half-angle change.
    protected ref RDF_DebugShapeManager m_StaticShapes;
    //! Plots, lock, ribbon — rebuilt on full dynamic refresh.
    protected ref RDF_DebugShapeManager m_DynamicShapes;
    //! Afterglow alone so light ticks can fade without wiping plots.
    protected ref RDF_DebugShapeManager m_AfterglowShapes;
    //! WLR markers — separate so light ticks can refresh without wiping plots.
    protected ref RDF_DebugShapeManager m_WlrShapes;
    //! Sweep needle — rebuilt on presentation ticks, not only on new scans.
    protected ref RDF_DebugShapeManager m_SweepShapes;
    protected ref array<ref RDF_RadarAfterglowBlip> m_Afterglow;
    protected int m_LastAfterglowScanSerial;
    protected int m_AfterglowWrite;
    protected bool m_StaticValid;
    protected vector m_CachedOrigin;
    protected float m_CachedRange;
    protected float m_CachedYaw;
    protected float m_CachedHalfAngleDeg;
    protected static const float FALLBACK_RAY_LENGTH = 100.0;
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const int MAX_SECTOR_SEGMENTS = 24;
    protected static const int MAX_RING_SEGMENTS = 48;
    protected static const int MAX_ALERT_SEGMENTS = 16;
    protected static const int MAX_RIBBON_POINTS = 32;

    void RDF_RadarVisualizer(RDF_RadarVisualSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
        {
            m_Settings = new RDF_RadarVisualSettings();
            m_Settings.ApplyShowcaseDefaults();
        }
        m_StaticShapes = new RDF_DebugShapeManager();
        m_DynamicShapes = new RDF_DebugShapeManager();
        m_AfterglowShapes = new RDF_DebugShapeManager();
        m_WlrShapes = new RDF_DebugShapeManager();
        m_SweepShapes = new RDF_DebugShapeManager();
        m_Afterglow = new array<ref RDF_RadarAfterglowBlip>();
        m_LastAfterglowScanSerial = -1;
        m_AfterglowWrite = 0;
        m_StaticValid = false;
        m_CachedOrigin = "0 0 0";
        m_CachedRange = -1.0;
        m_CachedYaw = 0.0;
        m_CachedHalfAngleDeg = -1.0;
    }

    RDF_RadarVisualSettings GetSettings()
    {
        return m_Settings;
    }

    void Reset()
    {
        if (m_StaticShapes)
            m_StaticShapes.Clear();
        if (m_DynamicShapes)
            m_DynamicShapes.Clear();
        if (m_AfterglowShapes)
            m_AfterglowShapes.Clear();
        if (m_WlrShapes)
            m_WlrShapes.Clear();
        if (m_SweepShapes)
            m_SweepShapes.Clear();
        ClearAfterglow();
        m_LastAfterglowScanSerial = -1;
        m_StaticValid = false;
    }

    void ClearAfterglow()
    {
        if (m_Afterglow)
            m_Afterglow.Clear();
        m_AfterglowWrite = 0;
    }

    //------------------------------------------------------------------------------------------------
    //! fullDynamic=true: rebuild plots/lock/ribbon/afterglow.
    //! fullDynamic=false: only refresh afterglow fade (keeps last plot markers).
    void Render(
        IEntity subject,
        array<ref RDF_RadarTarget> targets,
        vector origin,
        vector forward,
        float rangeM,
        float sectorHalfAngleDeg,
        RDF_RadarLockManager lockMgr,
        RDF_RadarProjectileTracker tracker,
        int scanSerial,
        bool fullDynamic)
    {
        if (!m_Settings)
            return;
        if (!m_StaticShapes)
            m_StaticShapes = new RDF_DebugShapeManager();
        if (!m_DynamicShapes)
            m_DynamicShapes = new RDF_DebugShapeManager();
        if (!m_AfterglowShapes)
            m_AfterglowShapes = new RDF_DebugShapeManager();
        if (!m_SweepShapes)
            m_SweepShapes = new RDF_DebugShapeManager();

        bool anyStatic = m_Settings.m_DrawOriginAxis
            || m_Settings.m_DrawSectorSweep
            || m_Settings.m_DrawRangeRings;
        bool anyDynamic = m_Settings.m_DrawRays
            || m_Settings.m_DrawPoints
            || m_Settings.m_DrawLockBeam
            || m_Settings.m_DrawAfterglow
            || m_Settings.m_DrawTrackRibbon;
        bool anySweep = m_Settings.m_DrawSectorSweep && m_Settings.m_AnimateSweepNeedle;
        if (!anyStatic && !anyDynamic && !anySweep)
            return;

        if (origin.LengthSq() < 0.0001 && subject)
            origin = GetSubjectOrigin(subject);

        float flen = forward.Length();
        if (flen < 0.001)
            forward = "0 0 1";
        else
            forward = forward / flen;

        vector boresight = ResolveBoresight(subject, forward);

        if (rangeM < 10.0)
            rangeM = 10.0;
        float visRange = ResolveVisualRange(rangeM);

        float nowS = System.GetTickCount() * 0.001;
        ShapeFlags shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.DOUBLESIDE;

        if (anyStatic && IsStaticDirty(origin, boresight, visRange, sectorHalfAngleDeg))
            RebuildStaticShapes(subject, origin, boresight, visRange, sectorHalfAngleDeg, shapeFlags);

        if (anySweep)
        {
            m_SweepShapes.Clear();
            DrawSweepNeedle(origin, boresight, visRange, sectorHalfAngleDeg, nowS, shapeFlags);
        }

        if (!anyDynamic)
            return;

        if (m_Settings.m_DrawAfterglow && scanSerial != m_LastAfterglowScanSerial)
        {
            IngestPlotsForAfterglow(targets, nowS);
            m_LastAfterglowScanSerial = scanSerial;
        }

        if (fullDynamic)
        {
            m_DynamicShapes.Clear();

            if (targets && m_Settings.m_DrawPoints)
                DrawPlotMarkers(targets, origin, visRange, shapeFlags);

            if (targets && m_Settings.m_DrawRays)
                DrawPlotRays(origin, forward, targets, shapeFlags);

            if (m_Settings.m_DrawLockBeam && lockMgr)
                DrawLockBeam(origin, lockMgr, shapeFlags);

            if (m_Settings.m_DrawTrackRibbon && tracker)
                DrawTrackRibbons(tracker, shapeFlags);
        }

        if (m_Settings.m_DrawAfterglow)
        {
            m_AfterglowShapes.Clear();
            DrawAfterglow(nowS, shapeFlags);
        }
    }

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
        Render(
            subject, targets, origin, forward, rangeM, sectorHalfAngleDeg,
            lockMgr, tracker, scanSerial, true);
    }

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
        Render(subject, targets, origin, forward, 2000.0, 45.0, null, null, -1, true);
    }

    void RenderWeaponLocates(RDF_RadarProjectileTracker tracker)
    {
        if (!tracker || !m_Settings)
            return;
        if (!m_Settings.m_DrawWeaponLocate)
            return;
        if (!m_WlrShapes)
            m_WlrShapes = new RDF_DebugShapeManager();

        m_WlrShapes.Clear();

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        float markerSize = m_Settings.m_WeaponLocateMarkerSize;
        if (markerSize < 0.2)
            markerSize = 0.2;
        float alertRadiusM = m_Settings.m_WeaponLocateAlertRadiusM;
        if (alertRadiusM < 1.0)
            alertRadiusM = 1.0;
        int alertSegs = m_Settings.m_WeaponLocateAlertSegments;
        if (alertSegs < 8)
            alertSegs = 8;
        if (alertSegs > MAX_ALERT_SEGMENTS)
            alertSegs = MAX_ALERT_SEGMENTS;

        int launchColor = ARGBF(0.95, 1.0, 0.55, 0.1);
        int impactColor = ARGBF(0.95, 0.15, 0.9, 1.0);
        int linkColor = ARGBF(0.65, 0.85, 0.85, 0.85);
        ShapeFlags shapeFlags = ShapeFlags.NOOUTLINE;

        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack track = tracks.Get(i);
            if (!track || !track.m_Confirmed)
                continue;
            RDF_RadarWlrFix fix = track.m_LastWlrFix;
            if (!fix)
                continue;

            if (fix.m_LaunchValid && fix.m_ImpactValid)
                m_WlrShapes.AddLine(fix.m_LaunchPos, fix.m_ImpactPos, linkColor, shapeFlags);

            if (fix.m_LaunchValid)
            {
                m_WlrShapes.AddSphere(fix.m_LaunchPos, markerSize, launchColor, shapeFlags);
                m_WlrShapes.AddSphere(
                    fix.m_LaunchPos, markerSize * 2.2, ARGBF(0.35, 1.0, 0.55, 0.1), shapeFlags);
                DrawGroundAlertRing(fix.m_LaunchPos, alertRadiusM, alertSegs, launchColor, shapeFlags);
            }
            if (fix.m_ImpactValid)
            {
                m_WlrShapes.AddSphere(fix.m_ImpactPos, markerSize, impactColor, shapeFlags);
                m_WlrShapes.AddSphere(
                    fix.m_ImpactPos, markerSize * 2.2, ARGBF(0.35, 0.15, 0.9, 1.0), shapeFlags);
                DrawGroundAlertRing(fix.m_ImpactPos, alertRadiusM, alertSegs, impactColor, shapeFlags);
            }
        }
    }

    protected bool IsStaticDirty(
        vector origin,
        vector forward,
        float rangeM,
        float halfAngleDeg)
    {
        if (!m_StaticValid)
            return true;
        vector d = origin - m_CachedOrigin;
        if (d.LengthSq() > 0.25)
            return true;
        if (Math.AbsFloat(rangeM - m_CachedRange) > 0.5)
            return true;
        float yaw = Math.Atan2(forward[2], forward[0]);
        float dyaw = RDF_RadarScanGeometry.NormalizeAngleRad(yaw - m_CachedYaw);
        if (Math.AbsFloat(dyaw) > 0.02)
            return true;
        if (Math.AbsFloat(halfAngleDeg - m_CachedHalfAngleDeg) > 0.1)
            return true;
        return false;
    }

    protected void RebuildStaticShapes(
        IEntity subject,
        vector origin,
        vector forward,
        float rangeM,
        float halfAngleDeg,
        ShapeFlags shapeFlags)
    {
        m_StaticShapes.Clear();

        if (m_Settings.m_DrawOriginAxis && subject)
            DrawOriginAxis(subject, origin);

        if (m_Settings.m_DrawRangeRings)
            DrawRangeRings(origin, rangeM, shapeFlags);

        if (m_Settings.m_DrawSectorSweep)
            DrawSectorSweep(origin, forward, rangeM, halfAngleDeg, shapeFlags);

        m_CachedOrigin = origin;
        m_CachedRange = rangeM;
        m_CachedYaw = Math.Atan2(forward[2], forward[0]);
        m_CachedHalfAngleDeg = halfAngleDeg;
        m_StaticValid = true;
    }

    //------------------------------------------------------------------------------------------------
    //! Ground alert ring via official CreateCircle (horizontal XZ).
    protected void DrawGroundAlertRing(
        vector center,
        float radiusM,
        int segs,
        int color,
        ShapeFlags shapeFlags)
    {
        if (radiusM < 1.0)
            return;
        if (segs < 8)
            segs = 8;
        if (segs > MAX_ALERT_SEGMENTS)
            segs = MAX_ALERT_SEGMENTS;

        m_WlrShapes.AddCircleXZ(center, radiusM, color, segs, shapeFlags);
    }

    protected void DrawOriginAxis(IEntity subject, vector origin)
    {
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        float len = m_Settings.m_OriginAxisLength;
        ShapeFlags flags = ShapeFlags.NOOUTLINE;

        m_StaticShapes.AddLine(origin, origin + worldMat[0] * len, ARGBF(1, 1, 0, 0), flags);
        m_StaticShapes.AddLine(origin, origin + worldMat[1] * len, ARGBF(1, 0, 1, 0), flags);
        m_StaticShapes.AddLine(origin, origin + worldMat[2] * len, ARGBF(1, 0, 0, 1), flags);
    }

    protected void DrawRangeRings(vector origin, float rangeM, ShapeFlags shapeFlags)
    {
        int segs = m_Settings.m_RangeRingSegments;
        if (segs < 8)
            segs = 8;
        if (segs > MAX_RING_SEGMENTS)
            segs = MAX_RING_SEGMENTS;

        vector ringOrigin = origin + Vector(0.0, 0.5, 0.0);
        m_StaticShapes.AddCircleXZ(
            ringOrigin, rangeM * 0.5, ARGBF(0.25, 0.2, 0.9, 0.45), segs, shapeFlags);
        m_StaticShapes.AddCircleXZ(
            ringOrigin, rangeM, ARGBF(0.35, 0.25, 1.0, 0.55), segs, shapeFlags);
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

        float lift = m_Settings.m_SectorHeightM;
        if (lift < 0.5)
            lift = 0.5;
        float farTilt = m_Settings.m_SectorFarTiltM;
        if (farTilt < 0.0)
            farTilt = 0.0;
        vector originHi = origin + Vector(0.0, lift, 0.0);

        float edgeA = m_Settings.m_SectorSweepEdgeAlpha;
        float fillA = m_Settings.m_SectorSweepAlpha;
        int edgeColor = ARGBF(edgeA, 0.35, 1.0, 0.55);
        int fillColor = ARGBF(fillA, 0.15, 0.85, 0.4);

        if (!m_Settings.m_AnimateSweepNeedle)
        {
            int sweepColor = ARGBF(0.85, 0.55, 1.0, 0.75);
            vector sweepEnd = originHi
                + Vector(Math.Cos(yaw), 0.0, Math.Sin(yaw)) * rangeM
                + Vector(0.0, farTilt, 0.0);
            m_StaticShapes.AddLine(originHi, sweepEnd, sweepColor, shapeFlags);
        }

        float leftYaw = yaw - halfRad;
        float rightYaw = yaw + halfRad;
        vector leftDir = Vector(Math.Cos(leftYaw), 0.0, Math.Sin(leftYaw));
        vector rightDir = Vector(Math.Cos(rightYaw), 0.0, Math.Sin(rightYaw));

        m_StaticShapes.AddLine(originHi, originHi + leftDir * rangeM, edgeColor, shapeFlags);
        m_StaticShapes.AddLine(originHi, originHi + rightDir * rangeM, edgeColor, shapeFlags);

        // Batched fan: one CreateTris for all wedges + official CreateCircleArc.
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

    protected void DrawSweepNeedle(
        vector origin,
        vector boresight,
        float rangeM,
        float halfAngleDeg,
        float nowS,
        ShapeFlags shapeFlags)
    {
        float halfRad = halfAngleDeg * DEG_TO_RAD;
        if (halfRad < 0.02)
            halfRad = 0.02;
        if (halfRad > 3.14159)
            halfRad = 3.14159;

        float period = m_Settings.m_SweepNeedlePeriodS;
        if (period < 0.4)
            period = 0.4;
        float phase = Math.Repeat(nowS, period);
        float u = phase / period;

        float yaw = Math.Atan2(boresight[2], boresight[0]);
        float needleYaw = yaw - halfRad + (2.0 * halfRad * u);

        float lift = m_Settings.m_SectorHeightM;
        if (lift < 0.5)
            lift = 0.5;
        vector originHi = origin + Vector(0.0, lift + 0.4, 0.0);
        vector needleDir = Vector(Math.Cos(needleYaw), 0.0, Math.Sin(needleYaw));
        vector needleEnd = originHi + needleDir * rangeM;

        m_SweepShapes.AddLine(originHi, needleEnd, ARGBF(0.92, 0.55, 1.0, 0.75), shapeFlags);
        m_SweepShapes.AddLine(
            originHi,
            originHi + needleDir * (rangeM * 0.22),
            ARGBF(1.0, 0.85, 1.0, 0.95),
            shapeFlags);
    }

    protected void DrawPlotMarkers(
        array<ref RDF_RadarTarget> targets,
        vector origin,
        float visRange,
        ShapeFlags shapeFlags)
    {
        float pointSize = m_Settings.m_PointSize;
        if (pointSize < 0.2)
            pointSize = 0.2;
        float projSize = m_Settings.m_ProjectilePointSize;
        if (projSize < pointSize)
            projSize = pointSize;

        float visSq = visRange * visRange * 1.35;
        if (visSq < 100.0)
            visSq = 100.0;

        array<IEntity> drawn = null;
        if (m_Settings.m_CollapsePlotsByEntity)
            drawn = new array<IEntity>();

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;
            if (!t.m_Detected)
                continue;
            if (m_Settings.m_HideFalsePlots)
            {
                if (t.m_IsFalsePlot)
                    continue;
            }

            vector pos = t.m_Position;
            IEntity ent = t.m_Entity;
            if (ent)
                pos = ent.GetOrigin();

            vector delta = pos - origin;
            delta[1] = 0.0;
            if (delta.LengthSq() > visSq)
                continue;

            if (drawn)
            {
                if (ent)
                {
                    if (drawn.Find(ent) >= 0)
                        continue;
                    drawn.Insert(ent);
                }
            }

            float size = pointSize;
            if (t.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
                size = projSize;
            int color = GetColorForType(t.m_Type);
            m_DynamicShapes.AddSphere(pos, size, color, shapeFlags);
        }
    }

    protected void DrawPlotRays(
        vector origin,
        vector forward,
        array<ref RDF_RadarTarget> targets,
        ShapeFlags shapeFlags)
    {
        if (targets.Count() == 0)
        {
            m_DynamicShapes.AddLine(
                origin, origin + forward * FALLBACK_RAY_LENGTH,
                ARGBF(0.5, 0.5, 0.5, 1.0), shapeFlags);
            return;
        }

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t || !t.m_Detected)
                continue;
            m_DynamicShapes.AddLine(
                origin, t.m_Position, GetRayColorForType(t.m_Type), shapeFlags);
        }
    }

    protected void DrawLockBeam(
        vector origin,
        RDF_RadarLockManager lockMgr,
        ShapeFlags shapeFlags)
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

        m_DynamicShapes.AddLine(origin, aimPos, coreColor, shapeFlags);

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
            m_DynamicShapes.AddLine(origin, aimPos + offset, beamColor, shapeFlags);
        }

        float aimRad = endR * 0.22;
        if (aimRad > 1.4)
            aimRad = 1.4;
        if (aimRad < 0.6)
            aimRad = 0.6;
        m_DynamicShapes.AddSphere(
            aimPos, aimRad, ARGBF(0.9, 1.0, 0.35, 0.15), shapeFlags);
    }

    protected void IngestPlotsForAfterglow(array<ref RDF_RadarTarget> targets, float nowS)
    {
        if (!targets || !m_Afterglow)
            return;

        int maxBlips = m_Settings.m_AfterglowMaxBlips;
        if (maxBlips < 16)
            maxBlips = 16;

        array<IEntity> drawn = null;
        if (m_Settings.m_CollapsePlotsByEntity)
            drawn = new array<IEntity>();

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t || !t.m_Detected)
                continue;
            if (m_Settings.m_HideFalsePlots)
            {
                if (t.m_IsFalsePlot)
                    continue;
            }

            IEntity ent = t.m_Entity;
            if (drawn)
            {
                if (ent)
                {
                    if (drawn.Find(ent) >= 0)
                        continue;
                    drawn.Insert(ent);
                }
            }

            float r;
            float g;
            float b;
            GetRgbForType(t.m_Type, r, g, b);

            RDF_RadarAfterglowBlip blip;
            if (m_Afterglow.Count() < maxBlips)
            {
                blip = new RDF_RadarAfterglowBlip();
                m_Afterglow.Insert(blip);
            }
            else
            {
                if (m_AfterglowWrite >= m_Afterglow.Count())
                    m_AfterglowWrite = 0;
                blip = m_Afterglow.Get(m_AfterglowWrite);
                if (!blip)
                {
                    blip = new RDF_RadarAfterglowBlip();
                    m_Afterglow.Set(m_AfterglowWrite, blip);
                }
                m_AfterglowWrite = m_AfterglowWrite + 1;
            }

            vector blipPos = t.m_Position;
            if (m_Settings.m_CollapsePlotsByEntity && t.m_Entity)
                blipPos = t.m_Entity.GetOrigin();
            blip.m_Pos = blipPos;
            blip.m_BirthS = nowS;
            blip.m_R = r;
            blip.m_G = g;
            blip.m_B = b;
        }
    }

    protected void DrawAfterglow(float nowS, ShapeFlags shapeFlags)
    {
        if (!m_Afterglow)
            return;
        float life = m_Settings.m_AfterglowSec;
        if (life < 0.2)
            life = 0.2;
        float size = m_Settings.m_AfterglowPointSize;
        if (size < 0.15)
            size = 0.15;

        for (int i = 0; i < m_Afterglow.Count(); i++)
        {
            RDF_RadarAfterglowBlip blip = m_Afterglow.Get(i);
            if (!blip)
                continue;
            float age = nowS - blip.m_BirthS;
            if (age < 0.0)
                age = 0.0;
            if (age > life)
                continue;
            float t = 1.0 - (age / life);
            float alpha = 0.15 + 0.55 * t;
            float rad = size * (0.45 + 0.55 * t);
            int color = ARGBF(alpha, blip.m_R, blip.m_G, blip.m_B);
            m_AfterglowShapes.AddSphere(blip.m_Pos, rad, color, shapeFlags);
        }
    }

    protected void DrawTrackRibbons(RDF_RadarProjectileTracker tracker, ShapeFlags shapeFlags)
    {
        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        if (!tracks)
            return;

        int ribbonColor = ARGBF(0.7, 1.0, 0.55, 0.15);
        int stride = m_Settings.m_TrackRibbonStride;
        if (stride < 1)
            stride = 1;

        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack track = tracks.Get(i);
            if (!track || !track.IsProjectileTrack())
                continue;
            if (!track.m_Positions || track.m_Positions.Count() < 2)
                continue;

            array<vector> pts = new array<vector>();
            int n = track.m_Positions.Count();
            for (int p = 0; p < n && pts.Count() < MAX_RIBBON_POINTS; p = p + stride)
            {
                pts.Insert(track.m_Positions.Get(p));
            }
            if (pts.Count() < 2)
                continue;

            vector last = track.m_Positions.Get(n - 1);
            if (vector.DistanceSq(pts.Get(pts.Count() - 1), last) > 0.01
                && pts.Count() < MAX_RIBBON_POINTS)
            {
                pts.Insert(last);
            }
            m_DynamicShapes.AddPolyLine(pts, ribbonColor, shapeFlags);
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

    protected vector ResolveBoresight(IEntity subject, vector forward)
    {
        if (!m_Settings.m_LockSectorToBoresight)
            return forward;
        if (!subject)
            return forward;
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector fwd = worldMat[2];
        float len = Math.Sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        if (len < 0.001)
            return forward;
        return Vector(fwd[0] / len, 0.0, fwd[2] / len);
    }

    protected float ResolveVisualRange(float rangeM)
    {
        float cap = m_Settings.m_SectorVisualRangeM;
        if (cap < 10.0)
            return rangeM;
        if (cap < rangeM)
            return cap;
        return rangeM;
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
