// Example sampling strategy: hemisphere-only sampling (z >= 0)
// Place this file under `Core/` and set it via `scanner.SetSampleStrategy(new RDF_HemisphereSampleStrategy());`

class RDF_HemisphereSampleStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        const float GOLDEN_ANGLE = 2.39996323; // radians
        // Map t to z as z = 1 - t so the pole (z=1, r=0) is hit by the FIRST
        // index only, not the last batch. With z = t the high-index rays all
        // had z near 1 and r near 0, collapsing ~10% of samples onto a tiny
        // solid angle around the zenith. Reversing keeps the same area-
        // uniform distribution (z uniform in [0,1] => dA = dz dφ = const)
        // but spreads the bulk of the samples across the horizon band.
        float t = (index + 0.5) / count;      // in (0,1), always > 0
        float z = 1.0 - t;                    // upper hemisphere: z in (0,1)
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = GOLDEN_ANGLE * index;
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}