// LiDAR scanner that samples rays from a subject entity.
// Workbench note: rays that start inside the subject hull can corrupt the physics heap
// (SEH 0xc0000374). Always push Start outside the local AABB along each ray.
class RDF_LidarScanner
{
    protected ref RDF_LidarSettings m_Settings;
    protected ref RDF_LidarSampleStrategy m_SampleStrategy;
    // Reused TraceParam — never assign owned ColliderName = string.Empty in the hot loop.
    protected ref TraceParam m_TraceParam;

    void RDF_LidarScanner(RDF_LidarSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_LidarSettings();

        m_SampleStrategy = new RDF_UniformSampleStrategy();
        m_TraceParam = new TraceParam();
    }

    RDF_LidarSettings GetSettings()
    {
        return m_Settings;
    }

    void SetSampleStrategy(RDF_LidarSampleStrategy strategy)
    {
        if (strategy)
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

        m_Settings.Validate();
        EnsureTraceParam();

        vector origin = GetSubjectOrigin(subject);
        int rays = Math.MaxInt(m_Settings.m_RayCount, 1);
        float range = m_Settings.m_Range;

        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector basisX = worldMat[0];
        vector basisY = worldMat[1];
        vector basisZ = worldMat[2];

        vector mins;
        vector maxs;
        subject.GetBounds(mins, maxs);
        vector halfExt = (maxs - mins) * 0.5;

        outSamples.Reserve(rays);

        for (int i = 0; i < rays; i++)
        {
            vector dirLocal;
            if (m_SampleStrategy)
                dirLocal = m_SampleStrategy.BuildDirection(i, rays);
            else
                dirLocal = BuildUniformDirection(i, rays);

            vector dir = (basisX * dirLocal[0]) + (basisY * dirLocal[1]) + (basisZ * dirLocal[2]);
            float dlen = dir.Length();
            if (dlen > 0.0001)
                dir = dir / dlen;
            else
                dir = basisZ;

            float clearance = ComputeStartClearanceM(dirLocal, halfExt);
            RDF_LidarSample sample = TraceOneRay(
                world, origin, dir, range, clearance, subject, i);
            if (sample)
                outSamples.Insert(sample);
        }
    }

    //! World-space directed rays (lateral probes, confirmation fans, etc.).
    //! starts/ends must be the same length; each pair is one LiDAR sample.
    void ScanDirectedRays(
        notnull array<vector> starts,
        notnull array<vector> ends,
        IEntity exclude,
        notnull array<ref RDF_LidarSample> outSamples)
    {
        outSamples.Clear();
        if (!m_Settings || !m_Settings.m_Enabled)
            return;

        int count = starts.Count();
        if (count <= 0 || ends.Count() != count)
            return;

        World world = GetGame().GetWorld();
        if (!world)
            return;

        m_Settings.Validate();
        EnsureTraceParam();
        outSamples.Reserve(count);

        for (int i = 0; i < count; i++)
        {
            vector start = starts[i];
            vector end = ends[i];
            vector delta = end - start;
            float range = delta.Length();
            if (range < 0.001)
            {
                RDF_LidarSample miss = new RDF_LidarSample();
                miss.m_Index = i;
                miss.m_Hit = false;
                miss.m_Start = start;
                miss.m_End = end;
                miss.m_Dir = Vector(0, 0, 1);
                miss.m_HitPos = end;
                miss.m_Distance = 0;
                outSamples.Insert(miss);
                continue;
            }

            vector dir = delta / range;
            // Directed starts are caller-authored (often already outside hull) — tiny epsilon only.
            RDF_LidarSample sample = TraceOneRay(
                world, start, dir, range, 0.02, exclude, i);
            if (sample)
                outSamples.Insert(sample);
        }
    }

    RDF_LidarSample TraceDirectedRay(vector start, vector end, IEntity exclude)
    {
        ref array<vector> starts = new array<vector>();
        ref array<vector> ends = new array<vector>();
        ref array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
        starts.Insert(start);
        ends.Insert(end);
        ScanDirectedRays(starts, ends, exclude, samples);
        if (samples.Count() <= 0)
            return null;
        return samples[0];
    }

    //------------------------------------------------------------------------------------------------
    protected void EnsureTraceParam()
    {
        if (!m_TraceParam)
            m_TraceParam = new TraceParam();
    }

    //------------------------------------------------------------------------------------------------
    //! Push Start outside the subject local AABB so TraceMove does not begin in solid geometry.
    protected float ComputeStartClearanceM(vector dirLocal, vector halfExt)
    {
        float baseClear = m_Settings.m_StartClearanceM;
        if (baseClear < 0.05)
            baseClear = 0.05;

        float alongHalf =
            Math.AbsFloat(dirLocal[0]) * halfExt[0]
            + Math.AbsFloat(dirLocal[1]) * halfExt[1]
            + Math.AbsFloat(dirLocal[2]) * halfExt[2];

        float clearance = alongHalf + baseClear;
        if (clearance < baseClear)
            clearance = baseClear;
        return clearance;
    }

    //------------------------------------------------------------------------------------------------
    //! One safe TraceMove → RDF_LidarSample. origin is sensor frame; Start is origin+dir*clearance.
    protected RDF_LidarSample TraceOneRay(
        notnull World world, vector origin, vector dir, float range, float clearance,
        IEntity exclude, int index)
    {
        RDF_LidarSample sample = new RDF_LidarSample();
        sample.m_Index = index;
        sample.m_Dir = dir;
        sample.m_Start = origin;
        sample.m_ColliderName = string.Empty;
        sample.m_Surface = null;

        if (range < 0.05)
        {
            sample.m_Hit = false;
            sample.m_End = origin;
            sample.m_HitPos = origin;
            sample.m_Distance = 0;
            return sample;
        }

        if (clearance < 0)
            clearance = 0;
        if (clearance >= range * 0.95)
            clearance = range * 0.05;

        vector start = origin + (dir * clearance);
        vector end = origin + (dir * range);
        float rayLen = range - clearance;
        if (rayLen < 0.05)
            rayLen = 0.05;

        sample.m_End = end;

        TraceParam param = m_TraceParam;
        param.Start = start;
        param.End = end;
        param.LayerMask = m_Settings.m_LayerMask;
        param.Flags = m_Settings.m_TraceFlags;
        param.Exclude = exclude;
        param.TraceEnt = null;
        param.SurfaceProps = null;
        // Do not write ColliderName (owned string) — engine owns that buffer.

        float hitFraction = 1.0;
        bool hit = false;

        if (m_Settings.m_TraceSmokeOcclusion)
        {
            float hitFractionVis = world.TraceMove(param, null);
            if (hitFractionVis >= 0.9999)
            {
                hit = false;
                hitFraction = 1.0;
            }
            else
            {
                int geomFlags = m_Settings.m_TraceFlags & ~TraceFlags.VISIBILITY;
                param.Flags = geomFlags;
                param.TraceEnt = null;
                param.SurfaceProps = null;
                float hitFractionGeom = world.TraceMove(param, null);
                if (hitFractionVis < hitFractionGeom - 0.0001)
                {
                    hit = false;
                    hitFraction = 1.0;
                }
                else
                {
                    hitFraction = hitFractionGeom;
                    hit = (param.TraceEnt != null) || (param.SurfaceProps != null);
                }
            }
        }
        else
        {
            hitFraction = world.TraceMove(param, null);
            hit = (param.TraceEnt != null) || (param.SurfaceProps != null);
        }

        if (hit && hitFraction > 0.0 && hitFraction < 0.9999)
        {
            float alongRay = hitFraction * rayLen;
            float dist = clearance + alongRay;
            if (dist >= range * 0.9999)
            {
                sample.m_Hit = false;
                sample.m_HitPos = end;
                sample.m_Distance = range;
                sample.m_Entity = null;
            }
            else
            {
                vector hitPos = start + (dir * alongRay);
                IEntity hitEntity = param.TraceEnt;
                // SurfaceProps is typed SurfaceProperties but holds GameMaterial at runtime.
                // Use assignment (engine idiom); GameMaterial.Cast() always returns null here.
                GameMaterial hitSurface = null;
                if (m_Settings.m_CaptureSurface)
                    hitSurface = param.SurfaceProps;

                if (m_Settings.m_TraceTargetMode == ERDF_TraceTargetMode.ENTITIES_ONLY)
                {
                    float surfaceY = world.GetSurfaceY(hitPos[0], hitPos[2]);
                    if (hitPos[1] <= surfaceY)
                    {
                        sample.m_Hit = false;
                        sample.m_HitPos = end;
                        sample.m_Distance = range;
                        sample.m_Entity = null;
                        sample.m_Surface = null;
                        return sample;
                    }
                }

                sample.m_Hit = true;
                sample.m_HitPos = hitPos;
                sample.m_Distance = dist;
                sample.m_Entity = hitEntity;
                sample.m_Surface = hitSurface;
            }
        }
        else
        {
            sample.m_Hit = false;
            sample.m_HitPos = end;
            sample.m_Distance = range;
            sample.m_Entity = null;
        }

        return sample;
    }

    protected vector BuildUniformDirection(int index, int count)
    {
        const float GOLDEN_ANGLE = 2.39996323;
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
            origin = worldMat[3]
                + (worldMat[0] * centerLocal[0])
                + (worldMat[1] * centerLocal[1])
                + (worldMat[2] * centerLocal[2]);
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
