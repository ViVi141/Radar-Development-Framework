// Example sampling strategy: hemisphere-only sampling (z >= 0)
// Place this file under `Core/` and set it via `scanner.SetSampleStrategy(new RDF_HemisphereSampleStrategy());`

class RDF_HemisphereSampleStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        const float GOLDEN_ANGLE = 2.39996323; // radians
        // t is always in (0.5/count , 1) — strictly positive — so z is already >= 0
        // without any clamping, placing all directions in the upper hemisphere.
        float t = (index + 0.5) / count;      // in (0,1), always > 0
        float z = t;                           // upper hemisphere: z in (0,1)
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = GOLDEN_ANGLE * index;
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}