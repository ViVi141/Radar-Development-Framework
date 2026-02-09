// Sampling strategy interface and default uniform implementation.
class RDF_LidarSampleStrategy
{
    // Return a direction vector for the given index in [0, count).
    vector BuildDirection(int index, int count)
    {
        return Vector(0,0,1);
    }
}

class RDF_UniformSampleStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        const float GOLDEN_ANGLE = 2.39996323; // radians
        float t = (index + 0.5) / count;
        float z = 1.0 - 2.0 * t;
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = GOLDEN_ANGLE * index;
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}