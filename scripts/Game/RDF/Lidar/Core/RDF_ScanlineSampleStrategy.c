// Sample strategy: scanline / sector based sampling (suitable for sweep scans).
class RDF_ScanlineSampleStrategy : RDF_LidarSampleStrategy
{
    protected int m_Sectors;

    void RDF_ScanlineSampleStrategy(int sectors = 32)
    {
        m_Sectors = Math.MaxInt(1, sectors);
    }

    override vector BuildDirection(int index, int count)
    {
        // Cap the sector count to the sample count. When count <= m_Sectors
        // every ray fell in a single line (lines == 1), t stayed 0 and ALL
        // rays pointed straight up (z = 1.0). Capping m_Sectors to count
        // guarantees at least 2 lines so t sweeps the full range.
        int sectors = m_Sectors;
        if (sectors > count)
            sectors = Math.MaxInt(1, count);
        int sector = index % sectors;
        int line = index / sectors;
        int lines = Math.MaxInt(1, Math.Ceil(count / (float)sectors));

        float t = 0.0;
        if (lines > 1)
            t = line / (float)(lines - 1); // 0..1 across lines
        else
            // Single line: distribute the (few) rays across the vertical
            // sweep instead of stacking them all at z = 1.0.
            t = (sector + 0.5) / sectors;

        // Map t -> z in [-1, 1]
        float z = Math.Lerp(1.0, -1.0, t);
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = 2.0 * Math.PI * (sector / (float)sectors);
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}