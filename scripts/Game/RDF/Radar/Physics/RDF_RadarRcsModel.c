// In-game target RCS defaults, aspect scaling, and Swerling fluctuation.
// Engineering approximations aligned with tools/dem/rdf_radar_channel.py.
class RDF_RadarRcsModel
{
    static float GetDefaultRcsM2(ERDF_RadarTargetType targetType)
    {
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 0.01;
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 10.0;
        return 5.0;
    }

    static int GetDefaultSwerlingModel(ERDF_RadarTargetType targetType)
    {
        // 0 = non-fluctuating; 1/3 scan-to-scan; 2/4 pulse-to-pulse.
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 0;
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 1;
        return 1;
    }

    static float GetEntityRcsM2(
        IEntity entity,
        ERDF_RadarTargetType targetType)
    {
        float fallback = GetDefaultRcsM2(targetType);
        if (!entity)
            return fallback;

        vector mins;
        vector maxs;
        entity.GetBounds(mins, maxs);
        vector size = maxs - mins;
        return EstimateRcsFromExtents(
            Math.AbsFloat(size[0]),
            Math.AbsFloat(size[1]),
            Math.AbsFloat(size[2]),
            targetType);
    }

    //------------------------------------------------------------------------------------------------
    // Optical-region estimate from AABB extents (no entity query).
    static float EstimateRcsFromExtents(
        float sizeX,
        float sizeY,
        float sizeZ,
        ERDF_RadarTargetType targetType)
    {
        float fallback = GetDefaultRcsM2(targetType);
        float width = Math.AbsFloat(sizeX);
        float height = Math.AbsFloat(sizeY);
        float length = Math.AbsFloat(sizeZ);

        float projectedA = width * height;
        float projectedB = length * height;
        float projected = Math.Max(projectedA, projectedB);
        if (projected <= 0.01)
            return fallback;

        float estimate = projected * 0.25;
        return Math.Clamp(estimate, fallback * 0.25, 1000.0);
    }

    //------------------------------------------------------------------------------------------------
    // Nose-on brighter than broadside. yawDeg / losAzimuthDeg are world horizontal.
    static float AspectFactor(float yawDeg, float losAzimuthDeg)
    {
        float rel = losAzimuthDeg - yawDeg;
        while (rel > 180.0)
            rel = rel - 360.0;
        while (rel < -180.0)
            rel = rel + 360.0;
        if (rel < 0.0)
            rel = -rel;

        float rad = rel * 0.0174532925199;
        float c = Math.Cos(rad);
        // 0.35 broadside .. 1.0 nose/tail.
        float factor = 0.35 + 0.65 * Math.AbsFloat(c);
        return factor;
    }

    //------------------------------------------------------------------------------------------------
    // Extent-aware projected-area proxy when size is known.
    static float AspectRcsFromExtents(
        float meanRcsM2,
        float sizeX,
        float sizeY,
        float sizeZ,
        float yawDeg,
        float losAzimuthDeg)
    {
        float fallback = meanRcsM2;
        if (fallback <= 0.0)
            fallback = 1.0;

        float factor = AspectFactor(yawDeg, losAzimuthDeg);
        if (sizeX <= 0.01 && sizeY <= 0.01 && sizeZ <= 0.01)
            return fallback * factor;

        float rel = losAzimuthDeg - yawDeg;
        while (rel > 180.0)
            rel = rel - 360.0;
        while (rel < -180.0)
            rel = rel + 360.0;
        float rad = rel * 0.0174532925199;
        float c = Math.AbsFloat(Math.Cos(rad));
        float s = Math.AbsFloat(Math.Sin(rad));
        // Local X aligns with entity forward in this project (see GetWorldTransform usage).
        float height = sizeY;
        if (height < 0.1)
            height = 0.1;
        float projected = c * sizeX * height + s * sizeZ * height;
        float estimate = projected * 0.25;
        float lo = fallback * 0.2;
        float hi = fallback * 4.0;
        if (estimate < lo)
            estimate = lo;
        if (estimate > hi)
            estimate = hi;
        return estimate;
    }

    //------------------------------------------------------------------------------------------------
    // Swerling sample. Models 1/3 constant within a scan; 2/4 redraw each call.
    static float SampleSwerling(
        float meanRcsM2,
        int model,
        int seed,
        int scanNumber,
        int scattererId)
    {
        if (meanRcsM2 <= 0.0)
            return 0.0;
        if (model <= 0)
            return meanRcsM2;

        // Deterministic u in (0,1) from ids — stable within a scan for I/III.
        float u1 = HashUnit(seed, scattererId, scanNumber, 1);
        float u2 = HashUnit(seed, scattererId, scanNumber, 2);
        if (model == 2 || model == 4)
        {
            // Extra salt so pulse-group draws differ from scan-constant draws.
            u1 = HashUnit(seed, scattererId, scanNumber * 131 + 17, 3);
            u2 = HashUnit(seed, scattererId, scanNumber * 131 + 17, 4);
        }

        bool chi4 = false;
        if (model == 3 || model == 4)
            chi4 = true;

        if (chi4)
            return -0.5 * meanRcsM2 * (Math.Log(u1) + Math.Log(u2));

        return -meanRcsM2 * Math.Log(u1);
    }

    //------------------------------------------------------------------------------------------------
    // Map integers to (eps, 1].
    protected static float HashUnit(int seed, int idA, int idB, int channel)
    {
        int x = seed;
        x = x * 374761393 + idA * 668265263;
        x = x * 2246822519 + idB * 3266489917;
        x = x * 668265263 + channel * 1013904223;
        if (x < 0)
            x = -x;
        int rem = x - (x / 1000000) * 1000000;
        float u = rem / 1000000.0;
        if (u < 0.000001)
            u = 0.000001;
        if (u > 1.0)
            u = 1.0;
        return u;
    }
}
