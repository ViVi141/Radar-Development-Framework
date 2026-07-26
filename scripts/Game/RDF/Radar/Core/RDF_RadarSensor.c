// Public product modes for RDF_RadarSensor.ConfigureMode / SetMode.
enum ERDF_RadarSensorMode
{
    RDF_RADAR_MODE_SEARCH,
    RDF_RADAR_MODE_STARE,
    RDF_RADAR_MODE_WLR
}

// Last completed scan geometry (PPI / HUD / fusion consumers).
class RDF_RadarScanContext
{
    vector m_Origin;
    vector m_Forward;
    float m_RangeM;
    float m_WorldTimeS;
    int m_ScanSerial;
    ERDF_RadarSensorMode m_Mode;
    bool m_UsedNetwork;

    void RDF_RadarScanContext()
    {
        m_Origin = "0 0 0";
        m_Forward = "1 0 0";
        m_RangeM = 0.0;
        m_WorldTimeS = 0.0;
        m_ScanSerial = 0;
        m_Mode = ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH;
        m_UsedNetwork = false;
    }
}

// Override OnScanComplete to react after each Sensor.ScanOnce / Tick.
class RDF_RadarScanCompleteHandler
{
    void OnScanComplete(
        RDF_RadarSensor sensor,
        array<ref RDF_RadarTarget> plots,
        RDF_RadarProjectileTracker tracker,
        RDF_RadarScanContext context)
    {
    }
}

// Stable public facade for electromagnetic radar gameplay.
// Mod authors should prefer this over calling Scanner / Ballistics / Registry
// directly. Internally: WorldModel candidates → Channel → Plots → Tracker/WLR.
class RDF_RadarSensor
{
    protected ref RDF_RadarSettings m_Settings;
    protected ref RDF_RadarScanner m_Scanner;
    protected ref RDF_RadarProjectileTracker m_Tracker;
    protected ref RDF_RadarLockManager m_LockManager;
    protected ref array<ref RDF_RadarTarget> m_Plots;
    protected ref RDF_RadarScanContext m_Context;
    protected ref RDF_RadarScanCompleteHandler m_Handler;
    protected ERDF_RadarSensorMode m_Mode;
    protected bool m_Enabled;
    protected bool m_ForceLocalScan;
    // Wall-clock gate for Tick intervals. World time freezes in GM editor /
    // pause, which would otherwise stall scan serial forever.
    protected float m_LastScanWallS;
    protected float m_LastScanTimeS;
    protected int m_ScanSerial;

    void RDF_RadarSensor()
    {
        m_Settings = CreateSearchSettings(64);
        m_Scanner = new RDF_RadarScanner(m_Settings);
        m_Tracker = new RDF_RadarProjectileTracker();
        m_Tracker.ConfigureFromSettings(m_Settings);
        m_LockManager = new RDF_RadarLockManager();
        m_Plots = new array<ref RDF_RadarTarget>();
        m_Context = new RDF_RadarScanContext();
        m_Mode = ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH;
        m_Enabled = true;
        m_ForceLocalScan = false;
        m_LastScanWallS = -1000.0;
        m_LastScanTimeS = -1000.0;
        m_ScanSerial = 0;
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

    void SetScanCompleteHandler(RDF_RadarScanCompleteHandler handler)
    {
        m_Handler = handler;
    }

    // Replace full settings (advanced). Prefer ConfigureMode for gameplay presets.
    void Configure(RDF_RadarSettings settings)
    {
        if (!settings)
            return;
        settings.Validate();
        m_Settings = settings;
        m_Scanner = new RDF_RadarScanner(m_Settings);
        if (!m_Tracker)
            m_Tracker = new RDF_RadarProjectileTracker();
        m_Tracker.ConfigureFromSettings(m_Settings);
        // Force the next Tick to run immediately after a reconfigure.
        m_LastScanWallS = -1000.0;
        // Drop stale plots from the previous settings object so consumers
        // (ManualDemo.Probe, HUD) do not read a frozen previous dwell.
        if (m_Plots)
            m_Plots.Clear();
    }

    void SetMode(ERDF_RadarSensorMode mode)
    {
        ConfigureMode(mode, -1);
    }

    // maxTargets < 0 keeps the preset default.
    void ConfigureMode(ERDF_RadarSensorMode mode, int maxTargets)
    {
        m_Mode = mode;
        RDF_RadarSettings settings;
        if (mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR)
        {
            if (maxTargets > 0)
                settings = CreateWlrSettings(maxTargets);
            else
                settings = CreateWlrSettings(128);
        }
        else
        {
            if (mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_STARE)
            {
                if (maxTargets > 0)
                    settings = CreateStareSettings(maxTargets);
                else
                    settings = CreateStareSettings(96);
            }
            else
            {
                if (maxTargets > 0)
                    settings = CreateSearchSettings(maxTargets);
                else
                    settings = CreateSearchSettings(64);
            }
        }
        Configure(settings);
        m_Context.m_Mode = m_Mode;
    }

    static RDF_RadarSettings CreateSearchSettings(int maxTargets)
    {
        RDF_RadarSettings s = new RDF_RadarSettings();
        s.m_Range = 2000.0;
        s.m_UpdateInterval = 0.25;
        s.m_SectorHalfAngleDeg = 45.0;
        s.m_MaxTargets = maxTargets;
        s.m_MaxLosTracesPerScan = 48;
        s.m_IncludeVehicles = true;
        s.m_IncludeProjectiles = true;
        s.m_IncludeRadarEmitters = true;
        s.m_Hardware = RDF_RadarHardware.CreateShorad();
        // SEARCH is a general surveillance preset, not a pulse-Doppler MTI search.
        // Keep MTI off so stationary vehicles remain detectable by default.
        if (s.m_Hardware)
            s.m_Hardware.m_EnableMti = false;
        s.m_EnablePhysicalDetection = true;
        s.m_DetectionSnrDb = 8.0;
        s.m_EnableMechanicalScan = false;
        s.m_EnableWeaponLocate = false;
        s.Validate();
        return s;
    }

    static RDF_RadarSettings CreateStareSettings(int maxTargets)
    {
        RDF_RadarSettings s = CreateSearchSettings(maxTargets);
        s.m_SectorHalfAngleDeg = 90.0;
        s.m_UpdateInterval = 0.2;
        s.m_EnableMechanicalScan = false;
        if (s.m_Hardware)
        {
            s.m_Hardware.m_ScanRpm = 0.0;
            s.m_Hardware.m_AzimuthBeamwidthDeg = 20.0;
            s.m_Hardware.Validate();
        }
        s.Validate();
        return s;
    }

    static RDF_RadarSettings CreateWlrSettings(int maxTargets)
    {
        RDF_RadarSettings s = CreateSearchSettings(maxTargets);
        s.m_Range = 8000.0;
        s.m_SectorHalfAngleDeg = 90.0;
        s.m_UpdateInterval = 0.15;
        s.m_IncludeVehicles = false;
        s.m_IncludeRadarEmitters = false;
        s.m_IncludeProjectiles = true;
        s.m_EnableMechanicalScan = false;
        s.m_EnableBallisticPrediction = true;
        s.m_EnableWeaponLocate = true;
        s.m_EnableDemGroundForWlr = true;
        s.m_WeaponLocateMinHits = 2;
        s.m_TrackConfirmHits = 2;
        s.m_KeepEntityTruth = false;
        if (s.m_Hardware)
        {
            s.m_Hardware.m_ScanRpm = 0.0;
            s.m_Hardware.m_AzimuthBeamwidthDeg = 25.0;
            s.m_Hardware.ClearElevationBeams();
            s.m_Hardware.AddElevationBeam("mortar_low", 15.0, 28.0, 0.0);
            s.m_Hardware.AddElevationBeam("mortar_mid", 35.0, 30.0, 0.0);
            s.m_Hardware.AddElevationBeam("mortar_high", 55.0, 28.0, -0.5);
            s.m_Hardware.Validate();
        }
        s.Validate();
        return s;
    }

    RDF_RadarSettings GetSettings()
    {
        return m_Settings;
    }

    ERDF_RadarSensorMode GetMode()
    {
        return m_Mode;
    }

    // Escape hatch for framework extensions. Prefer ClearDemCache / GetDemStatusShort.
    RDF_RadarScanner GetScanner()
    {
        return m_Scanner;
    }

    // Escape hatch for framework extensions. Prefer ClearTracks / GetTracks.
    RDF_RadarProjectileTracker GetTracker()
    {
        return m_Tracker;
    }

    RDF_RadarLockManager GetLockManager()
    {
        if (!m_LockManager)
            m_LockManager = new RDF_RadarLockManager();
        return m_LockManager;
    }

    // Drop every track. Call when reconfiguring between scenarios / suite steps
    // so consumers do not inherit tracks from a previous settings object.
    void ClearTracks()
    {
        if (m_Tracker)
            m_Tracker.ClearTracks();
    }

    // Drop warm DEM tiles and invalidate cached DEM samples on scatterers.
    // Used when a test phase changes DemTileLoadsPerScan / DemCacheMaxTiles.
    void ClearDemCache()
    {
        if (m_Scanner)
            m_Scanner.ClearDemCache();
        RDF_RadarScattererRegistry.InvalidateDemSamples();
    }

    string GetDemStatusShort()
    {
        if (!m_Scanner)
            return "DEM ?";
        return m_Scanner.GetDemStatusShort();
    }

    // Clear tracks and unlock. Safe entry point for suite step boundaries.
    void ResetSession()
    {
        ClearTracks();
        if (m_LockManager)
            m_LockManager.Unlock();
    }

    // Convenience: current locked target for weapon / fire-control code.
    bool GetLockedTarget(out IEntity entity, out vector worldPos)
    {
        if (!m_LockManager)
            return false;
        return m_LockManager.GetLockedTarget(entity, worldPos);
    }

    array<ref RDF_RadarTarget> GetPlots()
    {
        return m_Plots;
    }

    array<ref RDF_RadarTrack> GetTracks()
    {
        if (!m_Tracker)
            return null;
        return m_Tracker.GetAllTracks();
    }

    RDF_RadarScanContext GetScanContext()
    {
        return m_Context;
    }

    int GetScanSerial()
    {
        return m_ScanSerial;
    }

    int CountDetectedPlots()
    {
        if (!m_Plots)
            return 0;
        int n = 0;
        for (int i = 0; i < m_Plots.Count(); i++)
        {
            RDF_RadarTarget t = m_Plots.Get(i);
            if (t && t.m_Detected)
                n = n + 1;
        }
        return n;
    }

    int CountConfirmedTracks()
    {
        array<ref RDF_RadarTrack> tracks = GetTracks();
        if (!tracks)
            return 0;
        int n = 0;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (tr && tr.m_Confirmed)
                n = n + 1;
        }
        return n;
    }

    int CountWlrFixes()
    {
        array<ref RDF_RadarTrack> tracks = GetTracks();
        if (!tracks)
            return 0;
        int n = 0;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr || !tr.m_Confirmed)
                continue;
            if (!tr.IsProjectileTrack())
                continue;
            RDF_RadarWlrFix fix = tr.m_LastWlrFix;
            if (fix && fix.m_LaunchValid)
                n = n + 1;
        }
        return n;
    }

    // Interval-gated tick. Returns true when a scan actually ran.
    // Gate on wall clock so Workbench GM editor / pause (frozen world time)
    // still advances scan serial for HUD and AutoTests.
    bool Tick(IEntity subject, RDF_RadarNetworkAPI networkAPI)
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

        // Tracker / ballistics still prefer simulation time when it advances.
        float worldTimeS = world.GetWorldTime() * 0.001;
        ScanOnce(subject, networkAPI, worldTimeS);
        return true;
    }

    // Force one dwell regardless of interval.
    void ScanOnce(IEntity subject, RDF_RadarNetworkAPI networkAPI, float worldTimeS)
    {
        if (!m_Enabled || !m_Settings || !m_Scanner || !m_Tracker)
            return;
        if (!subject)
            return;
        if (!m_Plots)
            m_Plots = new array<ref RDF_RadarTarget>();
        if (!m_Context)
            m_Context = new RDF_RadarScanContext();

        m_LastScanTimeS = worldTimeS;
        m_ScanSerial = m_ScanSerial + 1;
        m_Plots.Clear();

        bool usedNetwork = false;
        vector trackOrigin = "0 0 0";
        vector forward = "1 0 0";
        float rangeM = m_Settings.m_Range;

        bool canUseNet = false;
        if (!m_ForceLocalScan && networkAPI)
        {
            if (networkAPI.IsNetworkAvailable())
                canUseNet = true;
        }

        if (canUseNet)
        {
            networkAPI.RequestScan();
            if (networkAPI.HasSyncedTargets())
            {
                array<ref RDF_RadarTarget> synced = networkAPI.GetLastTargets();
                if (synced)
                {
                    for (int i = 0; i < synced.Count(); i++)
                    {
                        RDF_RadarTarget t = synced.Get(i);
                        if (t)
                            m_Plots.Insert(t);
                    }
                }
            }
            trackOrigin = networkAPI.GetLastScanOrigin();
            forward = networkAPI.GetLastScanForward();
            float netRange = networkAPI.GetLastScanRange();
            if (netRange > 0.0)
                rangeM = netRange;
            usedNetwork = true;
        }
        else
        {
            m_Scanner.Scan(subject, m_Plots);
            trackOrigin = m_Scanner.GetLastOrigin();
            forward = m_Scanner.GetLastForward();
            rangeM = m_Scanner.GetLastRange();
        }

        m_Tracker.ConfigureFromSettings(m_Settings);
        m_Tracker.UpdateWithOrigin(m_Plots, worldTimeS, trackOrigin);
        m_Tracker.RefreshWeaponLocates(trackOrigin[1]);

        if (m_LockManager)
        {
            m_LockManager.Update(
                m_Tracker.GetAllTracks(),
                trackOrigin,
                forward,
                rangeM,
                worldTimeS);
        }

        m_Context.m_Origin = trackOrigin;
        m_Context.m_Forward = forward;
        m_Context.m_RangeM = rangeM;
        m_Context.m_WorldTimeS = worldTimeS;
        m_Context.m_ScanSerial = m_ScanSerial;
        m_Context.m_Mode = m_Mode;
        m_Context.m_UsedNetwork = usedNetwork;

        if (m_Handler)
            m_Handler.OnScanComplete(this, m_Plots, m_Tracker, m_Context);
    }

    string GetStatusShort()
    {
        string modeName = "SEARCH";
        if (m_Mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_STARE)
            modeName = "STARE";
        else if (m_Mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR)
            modeName = "WLR";

        string dem = GetDemStatusShort();

        string lock = "";
        if (m_LockManager)
            lock = " | " + m_LockManager.GetStatusShort();

        return modeName + " | " + dem
            + " | plots=" + CountDetectedPlots().ToString()
            + " tracks=" + CountConfirmedTracks().ToString()
            + " wlr=" + CountWlrFixes().ToString()
            + lock;
    }
}
