// Color strategy interface and default implementation for point and ray colors.
class RDF_LidarColorStrategy
{
    // Backwards compatible: build color using distance/hit
    int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        return ARGBF(1,1,1,1);
    }

    // New: build color from full sample (default forwards to legacy BuildPointColor)
    int BuildPointColorFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        return BuildPointColor(sample.m_Distance, sample.m_Hit, lastRange, settings);
    }

    // New: allow per-sample point size
    float BuildPointSizeFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        return settings.m_PointSize;
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

    // Forward compatibility: implement sample-based overrides
    override int BuildPointColorFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        if (settings.m_UseMaterialEffect && sample && sample.m_Hit && sample.m_Surface)
        {
            float refl = RDF_LidarMaterialReflectivity.GetReflectivityFromGameMaterial(sample.m_Surface, "");
            float brightness = 0.5 + 0.5 * refl;
            float alpha = 0.5 + 0.5 * refl;
            if (alpha > 1.0)
                alpha = 1.0;
            float t = sample.m_Distance / Math.Max(0.1, lastRange);
            t = Math.Clamp(t, 0.0, 1.0);
            float r = Math.Lerp(1.0, 0.0, t) * brightness;
            float g = Math.Lerp(0.0, 1.0, t) * brightness;
            float b = 0.2 * brightness;
            if (r > 1.0) r = 1.0;
            if (g > 1.0) g = 1.0;
            if (b > 1.0) b = 1.0;
            return ARGBF(alpha, r, g, b);
        }
        return BuildPointColor(sample.m_Distance, sample.m_Hit, lastRange, settings);
    }

    override float BuildPointSizeFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        float baseSize = settings.m_PointSize;
        if (settings.m_UseMaterialEffect && sample && sample.m_Hit && sample.m_Surface)
        {
            float refl = RDF_LidarMaterialReflectivity.GetReflectivityFromGameMaterial(sample.m_Surface, "");
            baseSize = baseSize * (0.85 + 0.3 * refl);
        }
        if (settings.m_UseDistanceGradient)
        {
            float t = sample.m_Distance / Math.Max(0.1, lastRange);
            t = Math.Clamp(t, 0.0, 1.0);
            return baseSize * (1.0 + (1.0 - t) * 0.5);
        }
        return baseSize;
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