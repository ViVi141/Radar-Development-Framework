// Built-in handler: prints scan stats (hit count, closest distance) after each scan. Used when demo verbose mode is on.
class RDF_LidarDemoStatsHandler : RDF_LidarScanCompleteHandler
{
    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        if (!samples) return;
        int hits = RDF_LidarSampleUtils.GetHitCount(samples);
        RDF_LidarSample closest = RDF_LidarSampleUtils.GetClosestHit(samples);
        string line = "[RDF Demo] hits=" + hits.ToString();
        if (closest)
        {
            line = line + " closest=";
            line = line + closest.m_Distance.ToString();
        }
        Print(line);
    }
}
