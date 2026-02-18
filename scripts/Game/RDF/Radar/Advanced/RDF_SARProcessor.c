// Synthetic Aperture Radar (SAR) processor prototype.
// Accumulates sequential scan snapshots from a moving platform and
// applies a simplified back-projection algorithm to synthesise a higher-
// resolution image than a physical aperture would normally allow.
//
// This is a conceptual approximation for game use.
// Phase history, motion compensation and focusing are greatly simplified.
class RDF_SARProcessor
{
    // Maximum number of scan snapshots retained for aperture synthesis.
    int m_MaxSnapshots = 32;

    protected ref array<ref array<ref RDF_LidarSample>> m_ScanHistory;
    protected ref array<vector> m_PlatformPositions;
    // Holds Shape references to keep SAR visualisation alive each frame.
    protected ref array<ref Shape> m_VisShapes;

    void RDF_SARProcessor()
    {
        m_ScanHistory       = new array<ref array<ref RDF_LidarSample>>();
        m_PlatformPositions = new array<vector>();
        m_VisShapes         = new array<ref Shape>();
    }

    // Add a new scan snapshot with the platform world position at scan time.
    // Old snapshots beyond m_MaxSnapshots are discarded (FIFO).
    void AddScan(array<ref RDF_LidarSample> samples, vector platformPos)
    {
        if (!samples)
            return;

        m_ScanHistory.Insert(samples);
        m_PlatformPositions.Insert(platformPos);

        // Evict oldest snapshot when the buffer is full.
        while (m_ScanHistory.Count() > m_MaxSnapshots)
        {
            m_ScanHistory.RemoveOrdered(0);
            m_PlatformPositions.RemoveOrdered(0);
        }
    }

    // Return the number of snapshots currently buffered.
    int GetSnapshotCount()
    {
        return m_ScanHistory.Count();
    }

    // Clear all buffered scan history.
    void Reset()
    {
        m_ScanHistory.Clear();
        m_PlatformPositions.Clear();
    }

    // Simplified back-projection: for each grid cell in the target area,
    // coherently sums returns across all aperture positions.
    // Outputs world positions and synthesised intensities.
    void GenerateSARImage(
        float areaSize,
        int   resolution,
        vector areaCenter,
        out array<vector> outPositions,
        out array<float>  outIntensities)
    {
        if (!outPositions)
            outPositions  = new array<vector>();
        if (!outIntensities)
            outIntensities = new array<float>();

        outPositions.Clear();
        outIntensities.Clear();

        int snapshots = m_ScanHistory.Count();
        if (snapshots == 0)
        {
            Print("[SAR] No scan history - nothing to process.");
            return;
        }

        Print(string.Format("[SAR] Back-projection: %d snapshots, grid %dx%d",
            snapshots, resolution, resolution));

        float cellSize = areaSize / (float)Math.Max(resolution, 1);

        for (int gx = 0; gx < resolution; gx++)
        {
            for (int gz = 0; gz < resolution; gz++)
            {
                float worldX = areaCenter[0] + (gx - resolution / 2) * cellSize;
                float worldZ = areaCenter[2] + (gz - resolution / 2) * cellSize;
                vector cellPos = Vector(worldX, areaCenter[1], worldZ);

                float intensity = 0.0;

                for (int s = 0; s < snapshots; s++)
                {
                    vector platPos    = m_PlatformPositions[s];
                    float  slantRange = (cellPos - platPos).Length();

                    array<ref RDF_LidarSample> snap = m_ScanHistory[s];
                    foreach (RDF_LidarSample baseS : snap)
                    {
                        RDF_RadarSample rs = RDF_RadarSample.Cast(baseS);
                        if (!rs || !rs.m_Hit)
                            continue;

                        // Match samples whose range is close to the slant range.
                        float rangeError = Math.AbsFloat(rs.m_Distance - slantRange);
                        if (rangeError < cellSize * 0.5)
                            intensity += rs.m_ReceivedPower;
                    }
                }

                if (intensity > 0.0)
                {
                    outPositions.Insert(cellPos);
                    outIntensities.Insert(intensity);
                }
            }
        }

        Print(string.Format("[SAR] Image complete: %d illuminated cells.", outPositions.Count()));
    }

    // Draw the SAR image in world space using the Shape API.
    // Sphere size is scaled by normalised intensity.
    // Call this every frame to refresh the visualisation; old shapes are released
    // at the start of each call so they do not accumulate.
    void VisualiseImage(
        array<vector> positions,
        array<float>  intensities,
        float         maxIntensity,
        float         sphereRadius)
    {
        if (!positions || !intensities)
            return;

        // Release previous frame's shapes before creating new ones.
        m_VisShapes.Clear();

        float invMax = 1.0;
        if (maxIntensity > 0)
            invMax = 1.0 / maxIntensity;

        int shapeFlags = ShapeFlags.NOOUTLINE | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE;
        int count = Math.Min(positions.Count(), intensities.Count());
        for (int i = 0; i < count; i++)
        {
            float norm   = Math.Clamp(intensities[i] * invMax, 0.0, 1.0);
            int   color  = ARGBF(0.78, norm, 1.0 - norm, 0.0);
            float radius = sphereRadius * (0.3 + 0.7 * norm);
            Shape s = Shape.CreateSphere(color, shapeFlags, positions[i], radius);
            if (s)
                m_VisShapes.Insert(s);
        }
    }
}
