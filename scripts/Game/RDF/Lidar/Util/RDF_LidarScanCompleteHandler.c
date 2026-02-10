// Handler for "scan complete" callback. Override OnScanComplete in a subclass and set via RDF_LidarAutoRunner.SetScanCompleteHandler().
class RDF_LidarScanCompleteHandler
{
    void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        // Override in subclass to react to each completed scan.
    }
}
