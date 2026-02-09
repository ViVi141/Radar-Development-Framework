// Example color strategy: color points by their sample index/angle.
class RDF_IndexColorStrategy : RDF_LidarColorStrategy
{
    override int BuildPointColorFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        // Simple deterministic hue-ish mapping using fractional multiplier
        float t = sample.m_Index * 0.6180339887498948; // golden ratio conjugate
        t = t - Math.Floor(t);
        float sr = Math.Sin(2.0 * Math.PI * t);
        float r;
        if (sr < 0.0)
            r = -sr;
        else
            r = sr;

        float sg = Math.Sin(2.0 * Math.PI * (t + 0.33));
        float g;
        if (sg < 0.0)
            g = -sg;
        else
            g = sg;

        float sb = Math.Sin(2.0 * Math.PI * (t + 0.66));
        float b;
        if (sb < 0.0)
            b = -sb;
        else
            b = sb;
        float alpha = 0.9;
        return ARGBF(alpha, r, g, b);
    }

    override float BuildPointSizeFromSample(RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)
    {
        // Keep default size
        return settings.m_PointSize;
    }
}