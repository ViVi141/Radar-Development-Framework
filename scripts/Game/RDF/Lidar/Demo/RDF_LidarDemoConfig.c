// Demo configuration object for LiDAR demos.
class RDF_LidarDemoConfig
{
    bool m_Enable = false;
    ref RDF_LidarSampleStrategy m_SampleStrategy;
    ref RDF_LidarColorStrategy m_ColorStrategy;
    int m_RayCount = -1; // -1 = leave as-is
    float m_MinTickInterval = -1.0; // -1 = leave as-is
    float m_UpdateInterval = -1.0; // scanner update interval override (seconds) or -1 to leave

    void RDF_LidarDemoConfig(){}

    void ApplyTo(RDF_LidarAutoRunner runner)
    {
        if (!runner) return;
        if (m_SampleStrategy)
        {
            RDF_LidarAutoRunner.SetDemoSampleStrategy(m_SampleStrategy);
            Print("[RDF Demo] Applied sample strategy: " + m_SampleStrategy.ClassName());
        }

        if (m_RayCount > 0)
        {
            RDF_LidarAutoRunner.SetDemoRayCount(m_RayCount);
            Print("[RDF Demo] Applied ray count: " + m_RayCount);
        }

        if (m_MinTickInterval > 0.0)
        {
            RDF_LidarAutoRunner.SetMinTickInterval(m_MinTickInterval);
            Print("[RDF Demo] Applied min tick interval: " + m_MinTickInterval);
        }

        if (m_ColorStrategy)
        {
            // apply color strategy via AutoRunner public API
            RDF_LidarAutoRunner.SetDemoColorStrategy(m_ColorStrategy);
            Print("[RDF Demo] Applied color strategy: " + m_ColorStrategy.ClassName());
        }

        if (m_UpdateInterval > 0.0)
        {
            // apply update interval via AutoRunner public API
            RDF_LidarAutoRunner.SetDemoUpdateInterval(Math.Max(0.01, m_UpdateInterval));
            Print("[RDF Demo] Applied update interval: " + m_UpdateInterval);
        }
    }
}