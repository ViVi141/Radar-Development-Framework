// Example sampling strategy: hemisphere-only sampling (z >= 0)
// Place this file under `Core/` and set it via `scanner.SetSampleStrategy(new RDF_HemisphereSampleStrategy());`

class RDF_HemisphereSampleStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        const float GOLDEN_ANGLE = 2.39996323; // radians
        float t = (index + 0.5) / count;      // in (0,1)
        float z = Math.Max(0.0, t);           // constrain to upper hemisphere (z >= 0)
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = GOLDEN_ANGLE * index;
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}