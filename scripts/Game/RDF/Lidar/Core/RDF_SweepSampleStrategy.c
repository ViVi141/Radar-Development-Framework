// Sample strategy: a rotating conical sweep (radar-style). Directions are in a narrow sector that rotates over time.
class RDF_SweepSampleStrategy : RDF_LidarSampleStrategy
{
    protected float m_HalfAngleDeg;
    protected float m_SweepWidthDeg;
    protected float m_SweepSpeedDegPerSec;

    void RDF_SweepSampleStrategy(float halfAngleDeg = 30.0, float sweepWidthDeg = 20.0, float sweepSpeedDegPerSec = 45.0)
    {
        m_HalfAngleDeg = Math.Clamp(halfAngleDeg, 0.1, 180.0);
        m_SweepWidthDeg = Math.Clamp(sweepWidthDeg, 1.0, 360.0);
        m_SweepSpeedDegPerSec = sweepSpeedDegPerSec;
    }

    override vector BuildDirection(int index, int count)
    {
        World world = GetGame().GetWorld();
        float time = 0.0;
        if (world)
            time = world.GetWorldTime();

        float sweepWidthRad = m_SweepWidthDeg * Math.DEG2RAD;
        float currentAzimuthRad = (time * m_SweepSpeedDegPerSec) * Math.DEG2RAD;
        float frac = 0.5;
        if (count > 1)
            frac = index / (float)(count - 1);
        float phi = currentAzimuthRad - sweepWidthRad * 0.5 + sweepWidthRad * frac;

        float halfRad = m_HalfAngleDeg * Math.DEG2RAD;
        float cosA = Math.Cos(halfRad);
        float z = cosA + (1.0 - cosA) * 0.5;
        z = Math.Clamp(z, -1.0, 1.0);
        float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        return Vector(Math.Cos(phi) * r, Math.Sin(phi) * r, z);
    }
}
