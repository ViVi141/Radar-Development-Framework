// LiDAR scanner that samples rays from a subject entity.
class RDF_LidarScanner
{
    protected ref RDF_LidarSettings m_Settings;

    void RDF_LidarScanner(RDF_LidarSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_LidarSettings();
    }

    RDF_LidarSettings GetSettings()
    {
        return m_Settings;
    }

    void Scan(IEntity subject, array<ref RDF_LidarSample> outSamples)
    {
        if (!subject || !m_Settings || !m_Settings.m_Enabled)
            return;

        if (!outSamples)
            return;

        outSamples.Clear();

        World world = subject.GetWorld();
        if (!world)
            return;

        vector origin = GetSubjectOrigin(subject);
        int rays = Math.Max(1, m_Settings.m_RayCount);

        for (int i = 0; i < rays; i++)
        {
            vector dir = BuildUniformDirection(i, rays);

            TraceParam param = new TraceParam();
            param.Start = origin;
            param.End = origin + (dir * m_Settings.m_Range);
            param.Flags = m_Settings.m_TraceFlags;
            param.LayerMask = m_Settings.m_LayerMask;
            param.Exclude = subject;

            float hitFraction = world.TraceMove(param, null);
            bool hit = (param.TraceEnt != null) || (param.SurfaceProps != null) || (param.ColliderName != string.Empty);

            vector hitPos = param.End;
            float dist = m_Settings.m_Range;
            if (hit && hitFraction > 0.0)
            {
                dist = hitFraction * m_Settings.m_Range;
                hitPos = origin + (dir * dist);
            }

            RDF_LidarSample sample = new RDF_LidarSample();
            sample.m_Index = i;
            sample.m_Hit = hit;
            sample.m_Start = origin;
            sample.m_End = param.End;
            sample.m_Dir = dir;
            sample.m_HitPos = hitPos;
            sample.m_Distance = dist;
            sample.m_Entity = param.TraceEnt;
            sample.m_ColliderName = param.ColliderName;
            sample.m_Surface = param.SurfaceProps;
            outSamples.Insert(sample);
        }
    }

    protected vector BuildUniformDirection(int index, int count)
    {
        const float GOLDEN_ANGLE = 2.39996323; // radians
        float t = (index + 0.5) / count;
        float z = 1.0 - 2.0 * t;
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = GOLDEN_ANGLE * index;
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }

    protected vector GetSubjectOrigin(IEntity subject)
    {
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector origin = worldMat[3];

        if (m_Settings.m_UseBoundsCenter)
        {
            vector mins, maxs;
            subject.GetBounds(mins, maxs);
            vector centerLocal = (mins + maxs) * 0.5;
            vector centerWorld = worldMat[3]
                + (worldMat[0] * centerLocal[0])
                + (worldMat[1] * centerLocal[1])
                + (worldMat[2] * centerLocal[2]);
            origin = centerWorld;
        }

        if (m_Settings.m_UseLocalOffset)
        {
            origin = origin
                + (worldMat[0] * m_Settings.m_OriginOffset[0])
                + (worldMat[1] * m_Settings.m_OriginOffset[1])
                + (worldMat[2] * m_Settings.m_OriginOffset[2]);
        }
        else
        {
            origin = origin + m_Settings.m_OriginOffset;
        }

        return origin;
    }
}
