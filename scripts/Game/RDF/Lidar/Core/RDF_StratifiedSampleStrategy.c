// Sample strategy: stratified grid sampling on the sphere (optional jitter per cell can be added).
class RDF_StratifiedSampleStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        // Determine rows and columns to make a near-square grid over the sphere
        int rows = Math.Ceil(Math.Sqrt(count));
        int cols = Math.Ceil(count / (float)rows);
        int r = index / cols;
        int c = index % cols;
        float u = (r + 0.5) / rows; // [0,1]
        float v = (c + 0.5) / cols; // [0,1]

        float z = 1.0 - 2.0 * u;
        float rxy = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
        float phi = 2.0 * Math.PI * v;
        return Vector(Math.Cos(phi) * rxy, Math.Sin(phi) * rxy, z);
    }
}