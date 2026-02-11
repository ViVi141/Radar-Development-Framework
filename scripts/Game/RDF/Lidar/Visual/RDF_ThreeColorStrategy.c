// Three-color distance gradient: near (green) -> mid (yellow) -> far (red).
// Suited for vehicle LiDAR visualization and depth perception.
class RDF_ThreeColorStrategy : RDF_LidarColorStrategy
{
    int m_NearColor;
    int m_MidColor;
    int m_FarColor;
    float m_NearFrac;
    float m_FarFrac;

    void RDF_ThreeColorStrategy(int nearColor = 0xFF00FF00, int midColor = 0xFFFFFF00, int farColor = 0xFFFF0000)
    {
        m_NearColor = nearColor;
        m_MidColor = midColor;
        m_FarColor = farColor;
        m_NearFrac = 0.33;
        m_FarFrac = 0.66;
    }

    override int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        if (lastRange <= 0.0)
            return m_MidColor;

        float t = dist / lastRange;
        if (t <= m_NearFrac)
        {
            float local = t / m_NearFrac;
            return InterpColor(m_NearColor, m_MidColor, local);
        }
        else if (t <= m_FarFrac)
        {
            float local = (t - m_NearFrac) / (m_FarFrac - m_NearFrac);
            return InterpColor(m_MidColor, m_FarColor, local);
        }
        else
        {
            return m_FarColor;
        }
    }

    override int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)
    {
        if (t <= m_NearFrac)
            return InterpColor(m_NearColor, m_MidColor, t / m_NearFrac);
        else if (t <= m_FarFrac)
            return InterpColor(m_MidColor, m_FarColor, (t - m_NearFrac) / (m_FarFrac - m_NearFrac));
        else
            return m_FarColor;
    }

    protected int InterpColor(int a, int b, float t)
    {
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;

        int aa = (a >> 24) & 0xFF;
        int ar = (a >> 16) & 0xFF;
        int ag = (a >> 8) & 0xFF;
        int ab = a & 0xFF;

        int ba = (b >> 24) & 0xFF;
        int br = (b >> 16) & 0xFF;
        int bg = (b >> 8) & 0xFF;
        int bb = b & 0xFF;

        int ra = aa + (int)((ba - aa) * t);
        int rr = ar + (int)((br - ar) * t);
        int rg = ag + (int)((bg - ag) * t);
        int rb = ab + (int)((bb - ab) * t);

        return (ra << 24) | (rr << 16) | (rg << 8) | rb;
    }
}
