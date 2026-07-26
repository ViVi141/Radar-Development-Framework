// Radar target visualizer: draws rays (origin -> target) and points at each target.
// When no targets, draws a forward direction ray so something is always visible.
// Color by type: vehicle=green, projectile=red, radar_emitter=yellow.
class RDF_RadarVisualizer
{
    protected ref RDF_RadarVisualSettings m_Settings;
    protected ref array<ref Shape> m_DebugShapes;
    protected static const float FALLBACK_RAY_LENGTH = 100.0;

    void RDF_RadarVisualizer(RDF_RadarVisualSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_RadarVisualSettings();
        m_DebugShapes = new array<ref Shape>();
    }

    RDF_RadarVisualSettings GetSettings()
    {
        return m_Settings;
    }

    void Reset()
    {
        if (m_DebugShapes)
            m_DebugShapes.Clear();
    }

    void Render(IEntity subject, array<ref RDF_RadarTarget> targets)
    {
        if (!subject || !targets || !m_Settings)
            return;
        if (!m_Settings.m_DrawRays && !m_Settings.m_DrawPoints && !m_Settings.m_DrawOriginAxis)
            return;

        m_DebugShapes.Clear();

        vector origin = GetSubjectOrigin(subject);

        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        // Enfusion forward is mat[2]; mat[0] is right.
        vector forward = worldMat[2];
        float flen = forward.Length();
        if (flen < 0.001)
            forward = "0 0 1";
        else
            forward = forward / flen;

        if (m_Settings.m_DrawOriginAxis && m_Settings.m_OriginAxisLength > 0.0)
        {
            vector axisX = worldMat[0];
            vector axisY = worldMat[1];
            vector axisZ = worldMat[2];
            float len = m_Settings.m_OriginAxisLength;

            vector endX[2];
            endX[0] = origin;
            endX[1] = origin + axisX * len;
            m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 1, 0, 0), ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE, endX, 2));

            vector endY[2];
            endY[0] = origin;
            endY[1] = origin + axisY * len;
            m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 0, 1, 0), ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE, endY, 2));

            vector endZ[2];
            endZ[0] = origin;
            endZ[1] = origin + axisZ * len;
            m_DebugShapes.Insert(Shape.CreateLines(ARGBF(1, 0, 0, 1), ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE, endZ, 2));
        }

        if (targets.Count() == 0 && m_Settings.m_DrawRays)
        {
            vector p[2];
            p[0] = origin;
            p[1] = origin + forward * FALLBACK_RAY_LENGTH;
            int dirColor = ARGBF(0.5, 0.5, 0.5, 1.0);
            m_DebugShapes.Insert(Shape.CreateLines(dirColor, ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE, p, 2));
        }

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;

            vector pos = t.m_Position;
            int color = GetColorForType(t.m_Type);
            float pointSize = m_Settings.m_PointSize;

            if (m_Settings.m_DrawRays)
            {
                vector p[2];
                p[0] = origin;
                p[1] = pos;
                int rayColor = GetRayColorForType(t.m_Type);
                m_DebugShapes.Insert(Shape.CreateLines(rayColor, ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE, p, 2));
            }

            if (m_Settings.m_DrawPoints)
            {
                int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;
                m_DebugShapes.Insert(Shape.CreateSphere(color, shapeFlags, pos, pointSize));
            }
        }
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
        int launchColor = ARGBF(0.95, 1.0, 0.55, 0.1);
        int impactColor = ARGBF(0.95, 0.15, 0.9, 1.0);
        int linkColor = ARGBF(0.55, 0.85, 0.85, 0.85);
        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.ONCE;

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
                m_DebugShapes.Insert(Shape.CreateSphere(launchColor, shapeFlags, fix.m_LaunchPos, markerSize));
            if (fix.m_ImpactValid)
                m_DebugShapes.Insert(Shape.CreateSphere(impactColor, shapeFlags, fix.m_ImpactPos, markerSize));
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

    protected int GetColorForType(ERDF_RadarTargetType type)
    {
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return ARGBF(0.95, 1.0, 0.2, 0.2);
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return ARGBF(0.95, 1.0, 1.0, 0.0);
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
            return ARGBF(0.95, 1.0, 1.0, 1.0);
        return ARGBF(0.95, 0.2, 1.0, 0.2);
    }

    protected int GetRayColorForType(ERDF_RadarTargetType type)
    {
        float a = Math.Clamp(m_Settings.m_RayAlpha, 0.05, 1.0);
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return ARGBF(a, 1.0, 0.2, 0.2);
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return ARGBF(a, 1.0, 1.0, 0.0);
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
            return ARGBF(a, 1.0, 1.0, 1.0);
        return ARGBF(a, 0.2, 1.0, 0.2);
    }
}
