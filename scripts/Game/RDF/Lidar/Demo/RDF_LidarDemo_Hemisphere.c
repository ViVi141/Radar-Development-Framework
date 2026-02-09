// Small demo helper that uses hemisphere-only sampling and toggles the demo.
// Usage:
//   RDF_HemisphereDemo.Start(); // Starts hemisphere demo
//   RDF_HemisphereDemo.Stop();  // Stops demo

class RDF_HemisphereDemo
{
    static void Start()
    {
        RDF_LidarAutoRunner.SetDemoSampleStrategy(new RDF_HemisphereSampleStrategy());
        RDF_LidarAutoRunner.SetDemoEnabled(true);
    }

    static void Stop()
    {
        RDF_LidarAutoRunner.SetDemoEnabled(false);
    }
}