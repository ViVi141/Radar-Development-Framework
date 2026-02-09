// Demo helper: start/stop a scanline/sector sampling demo
class RDF_ScanlineDemo
{
    static void Start(int sectors = 32, int rayCount = 256)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_ScanlineSampleStrategy(sectors);
        cfg.m_RayCount = Math.Clamp(rayCount, 1, 4096);
        cfg.m_MinTickInterval = RDF_LidarAutoRunner.GetMinTickInterval();
        cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
        RDF_LidarAutoRunner.SetDemoConfig(cfg);
        RDF_LidarAutoRunner.SetDemoEnabled(true);
    }

    static void Stop()
    {
        RDF_LidarAutoRunner.SetDemoEnabled(false);
    }
}