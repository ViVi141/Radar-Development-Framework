// Rectangular FOV sample strategy (vehicle forward LiDAR style).
// Local +Z is forward; rays fill a horiz x vert frustum on a cols x rows grid.
// Default 120 deg x 25.4 deg @ 120 x 32 (~3840 rays) is a playable in-game density;
// raise cols/rows toward 1200 x 128 only for offline / HUD-only stress cases.
class RDF_RectangularFOVSampleStrategy : RDF_LidarSampleStrategy
{
    protected float m_HorizHalfDeg;
    protected float m_VertHalfDeg;
    protected int m_Cols;
    protected int m_Rows;

    void RDF_RectangularFOVSampleStrategy(
        float horizFOVDeg = 120.0,
        float vertFOVDeg = 25.4,
        int cols = 120,
        int rows = 32)
    {
        m_HorizHalfDeg = Math.Clamp(horizFOVDeg * 0.5, 0.1, 180.0);
        m_VertHalfDeg = Math.Clamp(vertFOVDeg * 0.5, 0.1, 90.0);
        m_Cols = Math.Max(1, cols);
        m_Rows = Math.Max(1, rows);
    }

    float GetHorizFOVDeg()
    {
        return m_HorizHalfDeg * 2.0;
    }

    float GetVertFOVDeg()
    {
        return m_VertHalfDeg * 2.0;
    }

    int GetCols()
    {
        return m_Cols;
    }

    int GetRows()
    {
        return m_Rows;
    }

    int GetGridRayCount()
    {
        return m_Cols * m_Rows;
    }

    override vector BuildDirection(int index, int count)
    {
        int total = m_Cols * m_Rows;
        int idx = index % total;
        int col = idx % m_Cols;
        int row = idx / m_Cols;

        float u = (col + 0.5) / (float)m_Cols;
        float v = (row + 0.5) / (float)m_Rows;

        float phi = (u - 0.5) * 2.0 * m_HorizHalfDeg * Math.DEG2RAD;
        float theta = (v - 0.5) * 2.0 * m_VertHalfDeg * Math.DEG2RAD;

        float cz = Math.Cos(theta);
        float x = Math.Sin(phi) * cz;
        float y = Math.Sin(theta);
        float z = Math.Cos(phi) * cz;

        return Vector(x, y, z);
    }
}
