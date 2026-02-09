// Color strategy interface and default implementation for point and ray colors.
class RDF_LidarColorStrategy
{
    int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        return ARGBF(1,1,1,1);
    }

    int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)
    {
        return ARGBF(1,1,1,1);
    }
}

class RDF_DefaultColorStrategy : RDF_LidarColorStrategy
{
    override int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        if (!settings.m_UseDistanceGradient)
        {
            if (hit)
                return ARGBF(0.9, 1, 0, 0);
            return ARGBF(0.6, 1, 0.5, 0);
        }

        float t = dist / Math.Max(0.1, lastRange);
        t = Math.Clamp(t, 0.0, 1.0);
        float r = Math.Lerp(1.0, 0.0, t);
        float g = Math.Lerp(0.0, 1.0, t);
        float b = 0.0;
        float alpha = 0.9;
        if (!hit)
            alpha = 0.6;
        return ARGBF(alpha, r, g, b);
    }

    override int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)
    {
        t = Math.Clamp(t, 0.0, 1.0);
        float baseAlpha = Math.Clamp(settings.m_RayAlpha, 0.05, 1.0);
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
}