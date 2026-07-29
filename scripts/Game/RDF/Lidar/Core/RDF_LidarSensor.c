// Public product modes for RDF_LidarSensor.ConfigureMode / SetMode.
enum ERDF_LidarSensorMode
{
    RDF_LIDAR_MODE_FULL_SPHERE,
    RDF_LIDAR_MODE_FORWARD_CONE,
    RDF_LIDAR_MODE_FORWARD_RECT,
    RDF_LIDAR_MODE_SWEEP,
    RDF_LIDAR_MODE_ENTITIES_NEAR
}

// Last completed scan geometry (HUD / fusion consumers).
class RDF_LidarScanContext
{
    vector m_Origin;
    vector m_Forward;
    float m_RangeM;
    float m_WorldTimeS;
    int m_ScanSerial;
    ERDF_LidarSensorMode m_Mode;
    bool m_UsedNetwork;

    void RDF_LidarScanContext()
    {
        m_Origin = "0 0 0";
        m_Forward = "0 0 1";
        m_RangeM = 0.0;
        m_WorldTimeS = 0.0;
        m_ScanSerial = 0;
        m_Mode = ERDF_LidarSensorMode.RDF_LIDAR_MODE_FULL_SPHERE;
        m_UsedNetwork = false;
    }
}

// Override OnScanComplete to react after each Sensor.ScanOnce / Tick.
class RDF_LidarSensorScanHandler
{
    void OnScanComplete(
        RDF_LidarSensor sensor,
        array<ref RDF_LidarSample> samples,
        RDF_LidarScanContext context)
    {
    }
}

// Stable public facade for LiDAR gameplay.
// Mod authors should prefer this over calling Scanner / strategies directly.
// Internally: Settings + SampleStrategy → Scanner.Scan → samples (+ optional network).
class RDF_LidarSensor
{
    protected ref RDF_LidarSettings m_Settings;
    protected ref RDF_LidarScanner m_Scanner;
    protected ref array<ref RDF_LidarSample> m_Samples;
    protected ref RDF_LidarScanContext m_Context;
    protected ref RDF_LidarSensorScanHandler m_Handler;
    protected ERDF_LidarSensorMode m_Mode;
    protected bool m_Enabled;
    protected bool m_ForceLocalScan;
    protected float m_LastScanWallS;
    protected float m_LastScanTimeS;
    protected int m_ScanSerial;
    protected float m_LastScanDurationMs;

    void RDF_LidarSensor()
    {
        m_Settings = CreateFullSphereSettings(512);
        m_Scanner = new RDF_LidarScanner(m_Settings);
        m_Samples = new array<ref RDF_LidarSample>();
        m_Context = new RDF_LidarScanContext();
        m_Mode = ERDF_LidarSensorMode.RDF_LIDAR_MODE_FULL_SPHERE;
        m_Enabled = true;
        m_ForceLocalScan = false;
        m_LastScanWallS = -1000.0;
        m_LastScanTimeS = -1000.0;
        m_ScanSerial = 0;
        m_LastScanDurationMs = 0.0;
        ApplyStrategyForMode(m_Mode);
    }

    void SetEnabled(bool enabled)
    {
        m_Enabled = enabled;
    }

    bool IsEnabled()
    {
        return m_Enabled;
    }

    void SetForceLocalScan(bool forceLocal)
    {
        m_ForceLocalScan = forceLocal;
    }

    bool IsForceLocalScan()
    {
        return m_ForceLocalScan;
    }

    void SetScanCompleteHandler(RDF_LidarSensorScanHandler handler)
    {
        m_Handler = handler;
    }

    // Replace full settings (advanced). Prefer ConfigureMode for gameplay presets.
    void Configure(RDF_LidarSettings settings)
    {
        if (!settings)
            return;
        settings.Validate();
        m_Settings = settings;
        m_Scanner = new RDF_LidarScanner(m_Settings);
        ApplyStrategyForMode(m_Mode);
        m_LastScanWallS = -1000.0;
        if (m_Samples)
            m_Samples.Clear();
    }

    void SetMode(ERDF_LidarSensorMode mode)
    {
        ConfigureMode(mode, -1);
    }

    // rayCount < 0 keeps the preset default.
    void ConfigureMode(ERDF_LidarSensorMode mode, int rayCount)
    {
        m_Mode = mode;
        RDF_LidarSettings settings;
        if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_CONE)
        {
            if (rayCount > 0)
                settings = CreateForwardConeSettings(rayCount);
            else
                settings = CreateForwardConeSettings(384);
        }
        else if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_RECT)
        {
            if (rayCount > 0)
                settings = CreateForwardRectSettings(rayCount);
            else
                settings = CreateForwardRectSettings(-1);
        }
        else if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_SWEEP)
        {
            if (rayCount > 0)
                settings = CreateSweepSettings(rayCount);
            else
                settings = CreateSweepSettings(256);
        }
        else if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_ENTITIES_NEAR)
        {
            if (rayCount > 0)
                settings = CreateEntitiesNearSettings(rayCount);
            else
                settings = CreateEntitiesNearSettings(512);
        }
        else
        {
            if (rayCount > 0)
                settings = CreateFullSphereSettings(rayCount);
            else
                settings = CreateFullSphereSettings(512);
        }
        Configure(settings);
        m_Context.m_Mode = m_Mode;
    }

    static RDF_LidarSettings CreateFullSphereSettings(int rayCount)
    {
        RDF_LidarSettings s = new RDF_LidarSettings();
        s.m_Range = 50.0;
        s.m_RayCount = rayCount;
        s.m_UpdateInterval = 0.5;
        s.m_TraceTargetMode = ERDF_TraceTargetMode.ALL;
        s.Validate();
        return s;
    }

    static RDF_LidarSettings CreateForwardConeSettings(int rayCount)
    {
        RDF_LidarSettings s = new RDF_LidarSettings();
        s.m_Range = 80.0;
        s.m_RayCount = rayCount;
        s.m_UpdateInterval = 0.25;
        s.m_TraceTargetMode = ERDF_TraceTargetMode.ALL;
        s.Validate();
        return s;
    }

    // rayCount < 0 uses grid default (cols*rows); otherwise clamps to grid size.
    static RDF_LidarSettings CreateForwardRectSettings(int rayCount)
    {
        RDF_RectangularFOVSampleStrategy grid =
            new RDF_RectangularFOVSampleStrategy(120.0, 25.4, 120, 32);
        int gridRays = grid.GetGridRayCount();
        RDF_LidarSettings s = new RDF_LidarSettings();
        s.m_Range = 100.0;
        s.m_UpdateInterval = 0.2;
        s.m_TraceTargetMode = ERDF_TraceTargetMode.ALL;
        if (rayCount > 0)
            s.m_RayCount = Math.Min(rayCount, gridRays);
        else
            s.m_RayCount = gridRays;
        s.Validate();
        return s;
    }

    static RDF_LidarSettings CreateSweepSettings(int rayCount)
    {
        RDF_LidarSettings s = new RDF_LidarSettings();
        s.m_Range = 100.0;
        s.m_RayCount = rayCount;
        s.m_UpdateInterval = 0.15;
        s.m_TraceTargetMode = ERDF_TraceTargetMode.ALL;
        s.Validate();
        return s;
    }

    static RDF_LidarSettings CreateEntitiesNearSettings(int rayCount)
    {
        RDF_LidarSettings s = new RDF_LidarSettings();
        s.m_Range = 40.0;
        s.m_RayCount = rayCount;
        s.m_UpdateInterval = 0.15;
        s.m_TraceTargetMode = ERDF_TraceTargetMode.ENTITIES_ONLY;
        s.m_CaptureSurface = false;
        s.Validate();
        return s;
    }

    protected void ApplyStrategyForMode(ERDF_LidarSensorMode mode)
    {
        if (!m_Scanner)
            return;

        if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_CONE)
        {
            m_Scanner.SetSampleStrategy(new RDF_ConicalSampleStrategy(45.0));
        }
        else if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_RECT)
        {
            RDF_RectangularFOVSampleStrategy rect =
                new RDF_RectangularFOVSampleStrategy(120.0, 25.4, 120, 32);
            m_Scanner.SetSampleStrategy(rect);
            if (m_Settings)
            {
                m_Settings.m_RayCount = rect.GetGridRayCount();
                m_Settings.Validate();
            }
        }
        else if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_SWEEP)
        {
            m_Scanner.SetSampleStrategy(new RDF_SweepSampleStrategy(30.0, 20.0, 45.0));
        }
        else if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_ENTITIES_NEAR)
        {
            m_Scanner.SetSampleStrategy(new RDF_ConicalSampleStrategy(35.0));
        }
        else
        {
            m_Scanner.SetSampleStrategy(new RDF_UniformSampleStrategy());
        }
    }

    RDF_LidarSettings GetSettings()
    {
        return m_Settings;
    }

    ERDF_LidarSensorMode GetMode()
    {
        return m_Mode;
    }

    RDF_LidarScanner GetScanner()
    {
        return m_Scanner;
    }

    void SetSampleStrategy(RDF_LidarSampleStrategy strategy)
    {
        if (m_Scanner && strategy)
            m_Scanner.SetSampleStrategy(strategy);
    }

    RDF_LidarSampleStrategy GetSampleStrategy()
    {
        if (!m_Scanner)
            return null;
        return m_Scanner.GetSampleStrategy();
    }

    array<ref RDF_LidarSample> GetSamples()
    {
        return m_Samples;
    }

    // Defensive copy for consumers that mutate the array.
    array<ref RDF_LidarSample> GetSamplesCopy()
    {
        array<ref RDF_LidarSample> copy = new array<ref RDF_LidarSample>();
        if (!m_Samples)
            return copy;
        copy.Reserve(m_Samples.Count());
        for (int i = 0; i < m_Samples.Count(); i++)
        {
            RDF_LidarSample s = m_Samples.Get(i);
            if (s)
                copy.Insert(s);
        }
        return copy;
    }

    RDF_LidarScanContext GetContext()
    {
        return m_Context;
    }

    int GetScanSerial()
    {
        return m_ScanSerial;
    }

    float GetLastScanDurationMs()
    {
        return m_LastScanDurationMs;
    }

    int GetHitCount()
    {
        return RDF_LidarSampleUtils.GetHitCount(m_Samples);
    }

    RDF_LidarSample GetClosestHit()
    {
        return RDF_LidarSampleUtils.GetClosestHit(m_Samples);
    }

    IEntity GetClosestHitEntity()
    {
        RDF_LidarSample hit = GetClosestHit();
        if (!hit)
            return null;
        return hit.m_Entity;
    }

    // Interval-gated tick. Returns true when a scan actually ran.
    bool Tick(IEntity subject, RDF_LidarNetworkAPI networkAPI)
    {
        if (!m_Enabled || !m_Settings || !subject)
            return false;

        BaseWorld world = subject.GetWorld();
        if (!world)
            world = GetGame().GetWorld();
        if (!world)
            return false;

        float wallNowS = System.GetTickCount() * 0.001;
        if (wallNowS - m_LastScanWallS < m_Settings.m_UpdateInterval)
            return false;
        m_LastScanWallS = wallNowS;

        float worldTimeS = world.GetWorldTime() * 0.001;
        ScanOnce(subject, networkAPI, worldTimeS);
        return true;
    }

    // Force one dwell regardless of interval.
    void ScanOnce(IEntity subject, RDF_LidarNetworkAPI networkAPI, float worldTimeS)
    {
        if (!m_Enabled || !m_Settings || !m_Scanner)
            return;
        if (!subject)
            return;
        if (!m_Samples)
            m_Samples = new array<ref RDF_LidarSample>();
        if (!m_Context)
            m_Context = new RDF_LidarScanContext();

        m_LastScanTimeS = worldTimeS;
        m_ScanSerial = m_ScanSerial + 1;
        m_Samples.Clear();

        int wallStartMs = System.GetTickCount();
        bool usedNetwork = false;

        bool canUseNet = false;
        if (!m_ForceLocalScan && networkAPI)
        {
            if (networkAPI.IsNetworkAvailable())
                canUseNet = true;
        }

        if (canUseNet)
        {
            if (RDF_LidarNetworkScanner.Scan(subject, m_Scanner, m_Samples, networkAPI))
                usedNetwork = true;
            else
                m_Scanner.Scan(subject, m_Samples);
        }
        else
        {
            m_Scanner.Scan(subject, m_Samples);
        }

        m_LastScanDurationMs = System.GetTickCount() - wallStartMs;

        vector origin = "0 0 0";
        vector forward = "0 0 1";
        float rangeM = m_Settings.m_Range;
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        origin = worldMat[3];
        forward = worldMat[2];
        if (m_Settings.m_UseBoundsCenter)
        {
            // Prefer scanner origin when available via first sample start.
            if (m_Samples.Count() > 0)
            {
                RDF_LidarSample first = m_Samples.Get(0);
                if (first)
                    origin = first.m_Start;
            }
        }

        m_Context.m_Origin = origin;
        m_Context.m_Forward = forward;
        m_Context.m_RangeM = rangeM;
        m_Context.m_WorldTimeS = worldTimeS;
        m_Context.m_ScanSerial = m_ScanSerial;
        m_Context.m_Mode = m_Mode;
        m_Context.m_UsedNetwork = usedNetwork;

        if (m_Handler)
            m_Handler.OnScanComplete(this, m_Samples, m_Context);
    }

    void ResetSession()
    {
        m_ScanSerial = 0;
        m_LastScanWallS = -1000.0;
        m_LastScanTimeS = -1000.0;
        m_LastScanDurationMs = 0.0;
        if (m_Samples)
            m_Samples.Clear();
        if (m_Context)
        {
            m_Context.m_ScanSerial = 0;
            m_Context.m_UsedNetwork = false;
        }
    }

    string GetStatusShort()
    {
        string modeName = "FULL";
        if (m_Mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_CONE)
            modeName = "CONE";
        else if (m_Mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_RECT)
            modeName = "RECT";
        else if (m_Mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_SWEEP)
            modeName = "SWEEP";
        else if (m_Mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_ENTITIES_NEAR)
            modeName = "ENTS";

        int hits = GetHitCount();
        int rays = 0;
        if (m_Samples)
            rays = m_Samples.Count();
        return string.Format(
            "LiDAR %1 #%2 hits %3/%4 %.1fms",
            modeName,
            m_ScanSerial,
            hits,
            rays,
            m_LastScanDurationMs);
    }
}
