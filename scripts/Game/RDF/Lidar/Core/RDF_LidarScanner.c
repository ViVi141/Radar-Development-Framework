// LiDAR scanner that samples rays from a subject entity.
class RDF_LidarScanner
{
    protected ref RDF_LidarSettings m_Settings;
    // Sampling strategy (injectable). Defaults to uniform distribution.
    protected ref RDF_LidarSampleStrategy m_SampleStrategy;

    void RDF_LidarScanner(RDF_LidarSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_LidarSettings();

        m_SampleStrategy = new RDF_UniformSampleStrategy();
    }

    RDF_LidarSettings GetSettings()
    {
        return m_Settings;
    }

    void SetSampleStrategy(RDF_LidarSampleStrategy strategy)
    {
        m_SampleStrategy = strategy;
    }

    RDF_LidarSampleStrategy GetSampleStrategy()
    {
        return m_SampleStrategy;
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
        {
            world = GetGame().GetWorld();
            if (!world)
                return;
        }

        // Ensure settings are sane before scanning.
        m_Settings.Validate();

        vector origin = GetSubjectOrigin(subject);
        int rays = Math.Max(m_Settings.m_RayCount, 1);
        float range = m_Settings.m_Range;

        // get subject orientation for sample directions (strategies produce local-space directions)
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector basisX = worldMat[0];
        vector basisY = worldMat[1];
        vector basisZ = worldMat[2];

        // Reuse TraceParam instance to reduce per-ray allocations.
        TraceParam param = new TraceParam();
        for (int i = 0; i < rays; i++)
        {
            vector dirLocal;
            if (m_SampleStrategy)
                dirLocal = m_SampleStrategy.BuildDirection(i, rays);
            else
                dirLocal = BuildUniformDirection(i, rays);

            // Transform local direction into world space using subject basis
            vector dir = (basisX * dirLocal[0]) + (basisY * dirLocal[1]) + (basisZ * dirLocal[2]);
            float dlen = dir.Length();
            if (dlen > 0.0)
                dir = dir / dlen;

            // Populate reusable TraceParam
            param.Start = origin;
            param.End = origin + (dir * range);
            param.Flags = m_Settings.m_TraceFlags;
            param.LayerMask = m_Settings.m_LayerMask;
            param.Exclude = subject;
            // Clear previous trace result fields (safety)
            param.TraceEnt = null;
            param.SurfaceProps = null;
            param.ColliderName = string.Empty;

            float hitFraction = world.TraceMove(param, null);
            // Conservative hit validation: rely on TraceEnt or SurfaceProps; ignore ColliderName checks to avoid type incompat.
            bool hit = (param.TraceEnt != null) || (param.SurfaceProps != null);

            vector hitPos = param.End;
            float dist = range;
            IEntity hitEntity = null;
            string hitCollider = string.Empty;
            GameMaterial hitSurface = null;

            if (hit && hitFraction > 0.0)
            {
                float rawDist = hitFraction * range;
                // Treat max-range / far-plane hits as no-hit: avoids sky, terrain void, or
                // engine far-clip plane being falsely reported as a real surface.
                // NOTE: entities located exactly at the scan range boundary will also be
                // filtered out by this guard; increase m_Range slightly if needed.
                if (rawDist >= range * 0.9999)
                {
                    hit = false;
                    // dist / hitPos / entity stay at default "miss" values
                }
                else
                {
                    dist = rawDist;
                    hitPos = origin + (dir * dist);
                    hitEntity = param.TraceEnt;
                    hitCollider = param.ColliderName;
                    hitSurface = param.SurfaceProps;

                    // ENTITIES_ONLY: filter out hits at or below terrain (e.g. underground rocks).
                    if (m_Settings.m_TraceTargetMode == ETraceTargetMode.ENTITIES_ONLY)
                    {
                        float surfaceY = world.GetSurfaceY(hitPos[0], hitPos[2]);
                        if (hitPos[1] <= surfaceY)
                        {
                            hit = false;
                            hitPos = origin + (dir * range);
                            dist = range;
                            hitEntity = null;
                            hitCollider = string.Empty;
                            hitSurface = null;
                        }
                    }
                }
            }
            else
            {
                // No valid hit
                hit = false;
            }

            RDF_LidarSample sample = new RDF_LidarSample();
            sample.m_Index = i;
            sample.m_Hit = hit;
            sample.m_Start = origin;
            sample.m_End = param.End;
            sample.m_Dir = dir;
            sample.m_HitPos = hitPos;
            sample.m_Distance = dist;
            sample.m_Entity = hitEntity;
            sample.m_ColliderName = hitCollider;
            sample.m_Surface = hitSurface;
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
