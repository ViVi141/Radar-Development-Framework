// Sample strategy: scanline / sector based sampling (suitable for sweep scans).
class RDF_ScanlineSampleStrategy : RDF_LidarSampleStrategy
{
    protected int m_Sectors;

    void RDF_ScanlineSampleStrategy(int sectors = 32)
    {
        m_Sectors = Math.Max(1, sectors);
    }

    override vector BuildDirection(int index, int count)
    {
        int sector = index % m_Sectors;
        int line = index / m_Sectors;
        int lines = Math.Ceil(count / (float)m_Sectors);

        float t = 0.0;
        if (lines > 1)
            t = line / (float)(lines - 1); // 0..1 across lines

        // Map t -> z in [-1, 1]
        float z = Math.Lerp(1.0, -1.0, t);
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = 2.0 * Math.PI * (sector / (float)m_Sectors);
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}