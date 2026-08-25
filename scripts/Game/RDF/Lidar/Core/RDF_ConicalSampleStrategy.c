// Sample strategy: conical sampling (directions confined to a spherical cap centered on +Z).
class RDF_ConicalSampleStrategy : RDF_LidarSampleStrategy
{
    protected float m_HalfAngleDeg;

    void RDF_ConicalSampleStrategy(float halfAngleDeg = 30.0)
    {
        m_HalfAngleDeg = Math.Clamp(halfAngleDeg, 0.1, 180.0);
    }

    float GetHalfAngleDeg()
    {
        return m_HalfAngleDeg;
    }

    override vector BuildDirection(int index, int count)
    {
        // A single sample should point along the cap axis (+Z) rather than at
        // an arbitrary off-axis direction (the general formula gives z =
        // (1+cosA)/2, r > 0, phi = 0, i.e. a ray tilted toward +X). Degenerate
        // to the axis so count=1 produces the cap centre.
        if (count <= 1)
            return Vector(0, 0, 1);

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
