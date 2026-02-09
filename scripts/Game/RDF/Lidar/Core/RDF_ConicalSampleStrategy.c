// Sample strategy: conical sampling (directions confined to a spherical cap centered on +Z).
class RDF_ConicalSampleStrategy : RDF_LidarSampleStrategy
{
    protected float m_HalfAngleDeg;

    void RDF_ConicalSampleStrategy(float halfAngleDeg = 30.0)
    {
        m_HalfAngleDeg = Math.Clamp(halfAngleDeg, 0.1, 180.0);
    }

    override vector BuildDirection(int index, int count)
    {
        // Uniformly distribute directions over the spherical cap [0, halfAngle]
        float halfRad = m_HalfAngleDeg * Math.DEG2RAD;
        float cosA = Math.Cos(halfRad);
        float t = (index + 0.5) / count; // in (0,1]
        float z = cosA + (1.0 - cosA) * t; // moves from cosA -> 1
        z = Math.Clamp(z, -1.0, 1.0);
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = 2.0 * Math.PI * (index / (float)count);
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}