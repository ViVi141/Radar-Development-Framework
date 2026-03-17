// LiDAR color strategy: point color by surface density (g/cm³), alpha by distance.
// Density from GameMaterial.GetBallisticInfo().GetDensity(); typical: water ~1, wood ~0.6, concrete ~2.5, steel ~7.87.
// Low density = blue/cyan, high density = red/white; transparency decreases with distance.
class RDF_LidarMaterialColorStrategy : RDF_LidarColorStrategy
{
    // Density range for color map (g/cm³). Below min = blue, above max = red.
    static const float DENSITY_MIN = 0.0;
    static const float DENSITY_MAX = 8.0;

    void RDF_LidarMaterialColorStrategy() {}

    override int BuildPointColorFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        if (!sample || !sample.m_Hit)
        {
            return ARGBF(0.4, 0.5, 0.5, 0.5);
        }

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
            float span = DENSITY_MAX - DENSITY_MIN;
            if (span > 0.0)
            {
                t = (density - DENSITY_MIN) / span;
                if (t < 0.0)
                    t = 0.0;
                if (t > 1.0)
                    t = 1.0;
            }
        }

        float r = Math.Lerp(0.2, 1.0, t);
        float g = Math.Lerp(0.5, 0.4, t);
        float b = Math.Lerp(1.0, 0.3, t);
        if (r > 1.0) r = 1.0;
        if (g > 1.0) g = 1.0;
        if (b > 1.0) b = 1.0;
        if (r < 0.0) r = 0.0;
        if (g < 0.0) g = 0.0;
        if (b < 0.0) b = 0.0;

        float dist = sample.m_Distance;
        float range = Math.Max(0.1, lastRange);
        float distNorm = dist / range;
        if (distNorm > 1.0)
            distNorm = 1.0;
        if (distNorm < 0.0)
            distNorm = 0.0;
        float alpha = 1.0 - 0.7 * distNorm;
        if (alpha < 0.2)
            alpha = 0.2;
        if (alpha > 1.0)
            alpha = 1.0;

        return ARGBF(alpha, r, g, b);
    }

    override float BuildPointSizeFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        float baseSize = settings.m_PointSize;
        if (!sample || !sample.m_Hit || !sample.m_Surface)
            return baseSize;

        BallisticInfo bi = sample.m_Surface.GetBallisticInfo();
        if (!bi)
            return baseSize;
        float density = bi.GetDensity();
        float span = DENSITY_MAX - DENSITY_MIN;
        float t = 0.5;
        if (span > 0.0 && density >= 0.0)
        {
            t = (density - DENSITY_MIN) / span;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
        }
        float sizeMul = 0.85 + 0.3 * t;
        return baseSize * sizeMul;
    }

    override int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        if (!hit)
            return ARGBF(0.4, 0.5, 0.5, 0.5);

        float t = dist / Math.Max(0.1, lastRange);
        t = Math.Clamp(t, 0.0, 1.0);
        float r = Math.Lerp(0.8, 0.4, t);
        float g = Math.Lerp(0.8, 0.5, t);
        float b = Math.Lerp(0.8, 0.5, t);
        return ARGBF(0.85, r, g, b);
    }

    override int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)
    {
        t = Math.Clamp(t, 0.0, 1.0);
        float alpha = 0.2 + 0.15 * (1.0 - t);
        if (hit)
            alpha = alpha + 0.15;
        if (alpha > 1.0)
            alpha = 1.0;
        float r = Math.Lerp(0.6, 0.3, t);
        float g = Math.Lerp(0.6, 0.4, t);
        float b = 0.4;
        return ARGBF(alpha, r, g, b);
    }
}
