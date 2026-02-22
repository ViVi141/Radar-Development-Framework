// Demo configuration object for LiDAR demos.
// Single source of truth: central params + preset factories. Bootstrap reads from here.
class RDF_LidarDemoConfig
{
    // ---------- Central demo params (single source of truth for bootstrap & defaults) ----------
    protected static bool s_BootstrapEnabled = false;
    protected static bool s_BootstrapAutoCycle = false;
    protected static float s_BootstrapCycleInterval = 10.0;
    protected static int s_DefaultRayCount = 4096;
    protected static float s_DefaultRange = 50.0;
    protected static float s_DefaultUpdateInterval = 1.0;
    protected static int s_DefaultTraceMode = 2;
    protected static bool s_DefaultDrawRays = true;
    protected static bool s_DefaultDrawPoints = true;
    protected static bool s_DefaultDrawOriginAxis = false;
    protected static bool s_DefaultVerbose = false;
    protected static bool s_DefaultRenderWorld = true;
    protected static float s_DefaultMinTickInterval = -1.0;
    protected static bool s_DefaultUseBatchedMesh = true;

    static void SetBootstrapEnabled(bool enabled) { s_BootstrapEnabled = enabled; }
    static bool IsBootstrapEnabled() { return s_BootstrapEnabled; }
    static void SetBootstrapAutoCycle(bool enabled) { s_BootstrapAutoCycle = enabled; }
    static bool IsBootstrapAutoCycle() { return s_BootstrapAutoCycle; }
    static void SetBootstrapCycleInterval(float sec) { s_BootstrapCycleInterval = Math.Max(0.1, sec); }
    static float GetBootstrapCycleInterval() { return s_BootstrapCycleInterval; }
    static void SetDefaultRayCount(int n) { s_DefaultRayCount = Math.Max(1, n); }
    static void SetDefaultRange(float m) { s_DefaultRange = Math.Max(0.1, m); }
    static void SetDefaultUpdateInterval(float sec) { s_DefaultUpdateInterval = Math.Max(0.01, sec); }
    static void SetDefaultTraceMode(int mode) { s_DefaultTraceMode = Math.Clamp(mode, 0, 2); }
    static void SetDefaultDrawRays(bool draw) { s_DefaultDrawRays = draw; }
    static void SetDefaultDrawPoints(bool draw) { s_DefaultDrawPoints = draw; }
    static void SetDefaultDrawOriginAxis(bool draw) { s_DefaultDrawOriginAxis = draw; }
    static void SetDefaultVerbose(bool v) { s_DefaultVerbose = v; }
    static void SetDefaultRenderWorld(bool r) { s_DefaultRenderWorld = r; }
    static void SetDefaultMinTickInterval(float sec)
    {
        if (sec >= 0.0)
            s_DefaultMinTickInterval = Math.Max(0.01, sec);
        else
            s_DefaultMinTickInterval = -1.0;
    }
    static void SetDefaultUseBatchedMesh(bool use) { s_DefaultUseBatchedMesh = use; }

    // Build the config used when bootstrap runs. Uses central params.
    static RDF_LidarDemoConfig GetBootstrapConfig()
    {
        RDF_LidarDemoConfig cfg = CreateWithHUD(s_DefaultRayCount, s_DefaultRange);
        cfg.m_UpdateInterval = s_DefaultUpdateInterval;
        cfg.m_TraceTargetMode = s_DefaultTraceMode;
        cfg.m_DrawRays = s_DefaultDrawRays;
        cfg.m_DrawPoints = s_DefaultDrawPoints;
        cfg.m_DrawOriginAxis = s_DefaultDrawOriginAxis;
        cfg.m_Verbose = s_DefaultVerbose;
        cfg.m_RenderWorld = s_DefaultRenderWorld;
        cfg.m_UseBatchedMesh = s_DefaultUseBatchedMesh;
        if (s_DefaultMinTickInterval > 0.0)
            cfg.m_MinTickInterval = s_DefaultMinTickInterval;
        return cfg;
    }

    // ---------- Instance fields (per-preset overrides) ----------
    bool m_Enable = false;
    ref RDF_LidarSampleStrategy m_SampleStrategy;
    ref RDF_LidarColorStrategy m_ColorStrategy;
    int m_RayCount = -1; // -1 = leave as-is
    float m_MinTickInterval = -1.0; // -1 = leave as-is
    float m_UpdateInterval = -1.0; // scanner update interval override (seconds) or -1 to leave
    bool m_DrawOriginAxis = false; // when true, demo draws scan origin and X/Y/Z axes (debug)
    bool m_Verbose = false; // when true, demo prints hit count and closest distance after each scan
    bool m_RenderWorld = true; // when true: game view + point cloud; when false: point cloud only (solid background)
    bool m_DrawRays = true;     // when false: hide 3D ray visualization
    bool m_DrawPoints = true;   // when false: hide 3D point cloud visualization
    // When true: prefer VisualSettings' batched mesh renderer for performance in large point-clouds
    bool m_UseBatchedMesh = false;
    // When true, show 2D point cloud HUD (RDF_LidarHUD).
    bool m_ShowHUD = false;
    // Scanner max range (m). When > 0, applied to scanner; HUD always syncs from scanner. 0 = leave scanner default (50).
    float m_Range = 0.0;
    // Trace target: 0=terrain only, 1=all (terrain+entities), 2=entities only. Applied when preset is used.
    int m_TraceTargetMode = 1;

    void RDF_LidarDemoConfig() {}

    // ---------- Preset factory API (use these instead of legacy RDF_*Demo.Start) ----------

    static RDF_LidarDemoConfig CreateDefault(int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_UniformSampleStrategy();
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    // Like CreateDefault but with three-color distance gradient (near=green, mid=yellow, far=red).
    static RDF_LidarDemoConfig CreateThreeColor(int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_UniformSampleStrategy();
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_ColorStrategy = new RDF_ThreeColorStrategy(0xFF00FF00, 0xFFFFFF00, 0xFFFF0000);
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    // Like CreateDefault but with origin axis and verbose stats (showcases new features).
    static RDF_LidarDemoConfig CreateDefaultDebug(int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        // debug preset does not auto-enable demo by default — keep behaviour opt-in
        cfg.m_Enable = false;
        cfg.m_SampleStrategy = new RDF_UniformSampleStrategy();
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_DrawOriginAxis = true;
        cfg.m_Verbose = true;
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    // Preset with HUD enabled (2D point cloud display). Scanner and HUD use same range. 3D rays/points off.
    static RDF_LidarDemoConfig CreateWithHUD(int rayCount = 512, float rangeM = 1000.0)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_UniformSampleStrategy();
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_ShowHUD = true;
        cfg.m_DrawRays = false;
        cfg.m_DrawPoints = false;
        cfg.m_UseBatchedMesh = true;
        if (rangeM > 0.0)
            cfg.m_Range = rangeM;
        else
            cfg.m_Range = 1000.0;
        return cfg;
    }

    static RDF_LidarDemoConfig CreateHemisphere(int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_HemisphereSampleStrategy();
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    static RDF_LidarDemoConfig CreateConical(float halfAngleDeg = 30.0, int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_ConicalSampleStrategy(halfAngleDeg);
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    static RDF_LidarDemoConfig CreateStratified(int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_StratifiedSampleStrategy();
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    static RDF_LidarDemoConfig CreateScanline(int sectors = 32, int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_ScanlineSampleStrategy(sectors);
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    static RDF_LidarDemoConfig CreateSweep(float halfAngleDeg = 30.0, float sweepWidthDeg = 20.0, float sweepSpeedDegPerSec = 45.0, int rayCount = 512)
    {
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = new RDF_SweepSampleStrategy(halfAngleDeg, sweepWidthDeg, sweepSpeedDegPerSec);
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
        cfg.m_UseBatchedMesh = true;
        return cfg;
    }

    // Build config from an existing strategy (e.g. for Cycler). Optionally set color strategy.
    static RDF_LidarDemoConfig FromStrategy(RDF_LidarSampleStrategy strategy, int rayCount = 512, RDF_LidarColorStrategy colorStrategy = null)
    {
        if (!strategy) return null;
        RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
        cfg.m_Enable = true;
        cfg.m_SampleStrategy = strategy;
        cfg.m_RayCount = Math.Max(rayCount, 1);
        cfg.m_UseBatchedMesh = true;
        if (colorStrategy)
            cfg.m_ColorStrategy = colorStrategy;
        return cfg;
    }

    // ---------- Apply config to runner (uses RDF_LidarAutoRunner public API only) ----------

    void ApplyTo(RDF_LidarAutoRunner runner)
    {
        if (!runner) return;
        if (m_SampleStrategy)
            RDF_LidarAutoRunner.SetDemoSampleStrategy(m_SampleStrategy);
        if (m_RayCount > 0)
            RDF_LidarAutoRunner.SetDemoRayCount(m_RayCount);
        if (m_Range > 0.0)
            RDF_LidarAutoRunner.SetDemoRange(m_Range);
        if (m_MinTickInterval > 0.0)
            RDF_LidarAutoRunner.SetMinTickInterval(m_MinTickInterval);
        if (m_ColorStrategy)
            RDF_LidarAutoRunner.SetDemoColorStrategy(m_ColorStrategy);
        if (m_UpdateInterval > 0.0)
            RDF_LidarAutoRunner.SetDemoUpdateInterval(Math.Max(0.01, m_UpdateInterval));
        RDF_LidarAutoRunner.SetDemoTraceTargetMode(m_TraceTargetMode);
        RDF_LidarAutoRunner.SetDemoDrawOriginAxis(m_DrawOriginAxis);
        RDF_LidarAutoRunner.SetDemoVerbose(m_Verbose);
        RDF_LidarAutoRunner.SetDemoRenderWorld(m_RenderWorld);
        RDF_LidarAutoRunner.SetDemoDrawRays(m_DrawRays);
        RDF_LidarAutoRunner.SetDemoDrawPoints(m_DrawPoints);
        RDF_LidarAutoRunner.SetDemoUseBatchedMesh(m_UseBatchedMesh);
        // HUD takes precedence over verbose handler
        if (m_ShowHUD)
        {
            RDF_LidarHUD.Show();
            RDF_LidarHUD.SetDisplayRange(RDF_LidarAutoRunner.GetDemoScannerRange());
            RDF_LidarAutoRunner.SetScanCompleteHandler(RDF_LidarHUD.GetInstance());
        }
        else
        {
            RDF_LidarHUD.Hide();
            if (!m_Verbose)
                RDF_LidarAutoRunner.SetScanCompleteHandler(null);
        }
    }
}