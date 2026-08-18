// Public product modes for RDF_RadarSensor.ConfigureMode / SetMode.
enum ERDF_RadarSensorMode
{
    RDF_RADAR_MODE_SEARCH,
    RDF_RADAR_MODE_STARE,
    RDF_RADAR_MODE_WLR,
    RDF_RADAR_MODE_ESM,
    // Pulse-Doppler search: MTD bank + rotor sidebands + PRF stagger + coast.
    RDF_RADAR_MODE_PULSE_DOPPLER
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
    // Wall-clock cost of the last ScanOnce (local or network path).
    protected float m_LastScanDurationMs;
    // Dwell scheduler + persistent task state (TODO §9 S1). Keyed by scatterer id
    // so each track's revisit deadline survives across scans.
    protected ref RDF_DwellScheduler m_DwellScheduler;
    protected ref map<int, ref RDF_DwellTask> m_DwellTasks;
    // ECCM decision layer (TODO §9 S3) + last-decided actions for status.
    protected ref RDF_EccmDecisionLayer m_EccmDecision;
    protected bool m_EccmSlb;
    protected bool m_EccmPrf;
    protected bool m_EccmFreq;
    protected bool m_EccmBurn;
    // Adaptive budget governor: server main-thread headroom drives per-tick
    // LOS trace / WLR solve budgets (opt-in via m_EnableAdaptiveBudget).
    protected ref RDF_RadarBudgetGovernor m_BudgetGovernor;
    protected int m_DesignatedScattererId;

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
        m_LastScanDurationMs = 0.0;
        m_DwellScheduler = new RDF_DwellScheduler();
        m_DwellTasks = new map<int, ref RDF_DwellTask>();
        m_EccmDecision = new RDF_EccmDecisionLayer();
        m_BudgetGovernor = new RDF_RadarBudgetGovernor();
        m_BudgetGovernor.Configure(m_Settings);
        m_DesignatedScattererId = 0;
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

    //------------------------------------------------------------------------------------------------
    // Replace the post-CFAR measurement / error model. Affects Tracker and WLR.
    // Pass null to restore the built-in default CRLB synthesizer.
    void SetMeasurementModel(RDF_RadarMeasurementModel model)
    {
        if (!m_Settings)
            return;
        if (model)
            m_Settings.m_MeasurementModel = model;
        else
            m_Settings.m_MeasurementModel = new RDF_RadarDefaultMeasurementModel();
    }

    RDF_RadarMeasurementModel GetMeasurementModel()
    {
        if (!m_Settings)
            return null;
        return m_Settings.m_MeasurementModel;
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
        if (!m_BudgetGovernor)
            m_BudgetGovernor = new RDF_RadarBudgetGovernor();
        m_BudgetGovernor.Reset();
        m_BudgetGovernor.Configure(m_Settings);
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
        else if (mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM)
        {
            if (maxTargets > 0)
                settings = CreateEsmSettings(maxTargets);
            else
                settings = CreateEsmSettings(64);
        }
        else if (mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER)
        {
            if (maxTargets > 0)
                settings = CreatePulseDopplerSettings(maxTargets);
            else
                settings = CreatePulseDopplerSettings(96);
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

        if (m_LockManager)
        {
            if (mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM)
            {
                m_LockManager.SetTypeFilter(false, false, true);
                m_LockManager.SetAutoAcquire(true);
                m_LockManager.SetArmRequireLiveEmitter(true);
            }
            else
            {
                m_LockManager.SetArmRequireLiveEmitter(false);
            }
        }
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
        s.m_EnableEsmReceive = true;
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

    // Pulse-Doppler search: MTD filter bank + optional 2-PRF stagger.
    // Use from GBRS / ConfigureMode(PULSE_DOPPLER) when tangential helis
    // must survive CPA / vr≈0. Default SEARCH stays TwoPulse-compatible
    // with MTI off for stationary vehicles.
    static RDF_RadarSettings CreatePulseDopplerSettings(int maxTargets)
    {
        RDF_RadarSettings s = CreateSearchSettings(maxTargets);
        if (s.m_Hardware)
        {
            s.m_Hardware.m_EnableMti = true;
            s.m_Hardware.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;
            s.m_Hardware.m_DopplerBinCount = 16;
            s.m_Hardware.m_MtiClutterFloor = 0.0001;
            s.m_Hardware.m_MtdClutterLeakage = 0.000001;
            s.m_Hardware.m_ClutterSigmaVrMs = 0.5;
            s.m_Hardware.m_DeriveMtdLeakageFromSigmaVr = true;
            s.m_Hardware.m_LoadHwCalibFromProfile = true;
            s.m_Hardware.m_PrfStaggerRatio = 1.2;
            s.m_Hardware.m_MtiStaggerDeblind = true;
            s.m_Hardware.m_CoherentIntegration = true;
            s.m_Hardware.Validate();
        }
        // Asymmetric map EMA (fast down) + footprint σ⁰ mix sharpen class edges.
        s.EnableClutterSharpen(true, 0.15, 0.45, true);
        s.m_ClutterFootprintSamples = 5;
        // Coarse RD stays opt-in; enable explicitly for Perf / PD experiments.
        s.m_EnableCoarseRd = false;
        s.m_TrackCoastOnMiss = true;
        s.m_TrackCoastOnDopplerNull = true;
        s.m_TrackCoastGateGrowPerMiss = 0.25;
        s.m_TrackCoastMaxSec = 8.0;
        s.m_TrackMaxMisses = 6;
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
        s.m_WeaponLocateMinHits = 5;
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

    static RDF_RadarSettings CreateEsmSettings(int maxTargets)
    {
        RDF_RadarSettings s = new RDF_RadarSettings();
        s.m_Range = 8000.0;
        s.m_UpdateInterval = 0.25;
        s.m_SectorHalfAngleDeg = 90.0;
        s.m_MaxTargets = maxTargets;
        s.m_MaxLosTracesPerScan = 64;
        s.m_IncludeVehicles = false;
        s.m_IncludeProjectiles = false;
        s.m_IncludeRadarEmitters = true;
        s.m_EnableEsmReceive = true;
        s.m_EnableDemClutter = false;
        s.m_EnableWeaponLocate = false;
        s.m_EnableBallisticPrediction = false;
        s.m_EnablePhysicalDetection = true;
        s.m_DetectionSnrDb = 6.0;
        s.m_EnableMechanicalScan = false;
        // Receive-only: do not paint victims on the RWR table.
        s.m_EnableRwrReporting = false;
        // Keep emitter entity links so GetArmAim / LockArmTrackId can resolve RF.
        s.m_KeepEntityTruth = true;
        s.m_Hardware = RDF_RadarHardware.CreateShorad();
        if (s.m_Hardware)
        {
            s.m_Hardware.m_ScanRpm = 0.0;
            s.m_Hardware.m_EnableMti = false;
            s.m_Hardware.m_AzimuthBeamwidthDeg = 30.0;
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

    // Adaptive budget governor (null-safe when disabled).
    RDF_RadarBudgetGovernor GetBudgetGovernor()
    {
        return m_BudgetGovernor;
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

    // Load whole SURF/DEM into RAM (not counted in GetLastScanDurationMs when
    // called before ScanOnce; ScanOnce also preloads before its wall clock).
    // No-op on pure clients (RplMode.Client / !Replication.IsServer).
    void EnsureDemPreloaded()
    {
        if (m_Scanner)
            m_Scanner.EnsureDemPreloaded();
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
        if (m_EccmDecision)
            m_EccmDecision.Reset();
        if (m_DwellTasks)
            m_DwellTasks.Clear();
        ClearDesignation();
    }

    // Force FIRE_CONTROL dwells onto a scatterer even without a lock.
    void DesignateScattererId(int scattererId)
    {
        if (scattererId < 0)
            scattererId = 0;
        m_DesignatedScattererId = scattererId;
    }

    void ClearDesignation()
    {
        m_DesignatedScattererId = 0;
    }

    int GetDesignatedScattererId()
    {
        return m_DesignatedScattererId;
    }

    // Convenience: current locked target for weapon / fire-control code.
    bool GetLockedTarget(out IEntity entity, out vector worldPos)
    {
        if (!m_LockManager)
            return false;
        return m_LockManager.GetLockedTarget(entity, worldPos);
    }

    bool LockArmTrackId(int trackId)
    {
        return GetLockManager().LockArmTrackId(trackId);
    }

    bool GetArmAim(out IEntity entity, out vector worldPos, out bool radiating)
    {
        if (!m_LockManager)
        {
            entity = null;
            worldPos = "0 0 0";
            radiating = false;
            return false;
        }
        return m_LockManager.GetArmAim(entity, worldPos, radiating);
    }

    // Platform RWR: is this entity currently painted / tracked / locked by RDF radars?
    bool HasRwrSearchWarning(IEntity victim)
    {
        return RDF_RadarRwr.HasSearchWarning(victim);
    }

    bool HasRwrTrackWarning(IEntity victim)
    {
        return RDF_RadarRwr.HasTrackWarning(victim);
    }

    bool HasRwrLockWarning(IEntity victim)
    {
        return RDF_RadarRwr.HasLockWarning(victim);
    }

    string GetRwrStatusShort(IEntity victim)
    {
        return RDF_RadarRwr.GetStatusShort(victim);
    }

    void CollectRwrThreats(IEntity victim, notnull array<ref RDF_RadarRwrThreat> outThreats)
    {
        RDF_RadarRwr.CollectForVictim(victim, outThreats);
    }

    array<ref RDF_RadarTarget> GetPlots()
    {
        return m_Plots;
    }

    // Forward-side truth bypass: scatterer id -> entity from the last local Scan.
    IEntity GetDebugTruthEntity(int scattererId)
    {
        if (!m_Scanner)
            return null;
        return m_Scanner.GetDebugTruthEntity(scattererId);
    }

    void CopyDebugTruthMap(notnull map<int, IEntity> outMap)
    {
        if (!m_Scanner)
        {
            outMap.Clear();
            return;
        }
        m_Scanner.CopyDebugTruthMap(outMap);
    }

    // After inverse tracking, optionally attach entity handles for fire-control /
    // AutoTest via the forward debug-truth map (not via plot inheritance).
    protected void RebindTrackEntitiesFromDebugTruth()
    {
        if (!m_Tracker || !m_Scanner)
            return;
        array<ref RDF_RadarTrack> tracks = m_Tracker.GetAllTracks();
        if (!tracks)
            return;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr)
                continue;
            if (tr.m_ScattererId <= 0)
                continue;
            IEntity ent = m_Scanner.GetDebugTruthEntity(tr.m_ScattererId);
            if (!ent)
            {
                RDF_RadarScatterer entry = RDF_RadarScattererRegistry.FindById(tr.m_ScattererId);
                if (entry)
                    ent = entry.m_Entity;
            }
            if (ent)
                tr.m_Entity = ent;
        }
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

    // Milliseconds for the most recent ScanOnce (wall clock).
    float GetLastScanDurationMs()
    {
        return m_LastScanDurationMs;
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

        // Preload before the wall clock so a 10–20s SURF decode is not "scanMs".
        EnsureDemPreloaded();

        int wallStartMs = System.GetTickCount();
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
            ERDF_DwellKind dwellKind = ERDF_DwellKind.RDF_DWELL_SEARCH;
            if (m_Settings.m_EnableDwellScheduler)
                dwellKind = PlanDwells(worldTimeS);
            else
                m_Scanner.ClearDwellScattererIds();
            m_Settings.SelectBandForDwell(dwellKind);
            if (m_Settings.m_Hardware)
            {
                bool hop = m_Settings.m_Hardware.m_FrequencyHopEnabled;
                if (m_EccmFreq)
                    hop = true;
                m_Settings.m_Hardware.SetHopActive(hop);
            }
            m_Scanner.Scan(subject, m_Plots);
            if (m_Settings.m_EnableEccmDecision)
                RunEccmDecision();
            trackOrigin = m_Scanner.GetLastOrigin();
            forward = m_Scanner.GetLastForward();
            rangeM = m_Scanner.GetLastRange();
        }

        m_Tracker.ConfigureFromSettings(m_Settings);
        if (usedNetwork && networkAPI && networkAPI.HasSyncedTargets())
        {
            array<ref RDF_RadarTrack> syncedTracks = networkAPI.GetLastTracks();
            m_Tracker.ApplySyncedTracks(syncedTracks);

            if (m_LockManager)
            {
                int lockState = 0;
                int lockTrackId = -1;
                vector lockAim = "0 0 0";
                if (networkAPI.GetLastLockState(lockState, lockTrackId, lockAim))
                    m_LockManager.ApplySyncedStateInt(lockState, lockTrackId, lockAim);
                else
                    m_LockManager.Unlock();
            }
        }
        else
        {
            m_Tracker.UpdateWithOrigin(m_Plots, worldTimeS, trackOrigin);
            m_Tracker.RefreshWeaponLocates(trackOrigin[1]);
            RebindTrackEntitiesFromDebugTruth();

            if (m_LockManager)
            {
                m_LockManager.Update(
                    m_Tracker.GetAllTracks(),
                    trackOrigin,
                    forward,
                    rangeM,
                    worldTimeS);
            }
        }

        m_Context.m_Origin = trackOrigin;
        m_Context.m_Forward = forward;
        m_Context.m_RangeM = rangeM;
        m_Context.m_WorldTimeS = worldTimeS;
        m_Context.m_ScanSerial = m_ScanSerial;
        m_Context.m_Mode = m_Mode;
        m_Context.m_UsedNetwork = usedNetwork;

        PublishRwrThreats(subject, trackOrigin, worldTimeS);

        int wallEndMs = System.GetTickCount();
        m_LastScanDurationMs = wallEndMs - wallStartMs;
        if (m_LastScanDurationMs < 0.0)
            m_LastScanDurationMs = 0.0;

        // Adaptive budget governor: feed the measured scan cost; it reads the
        // server's real frame time (System.GetFPS) and adjusts per-tick LOS /
        // WLR budgets in m_Settings when enabled.
        if (m_BudgetGovernor && m_BudgetGovernor.IsEnabled())
        {
            m_BudgetGovernor.Update(m_Settings, m_LastScanDurationMs);
            // The scanner and tracker read budgets from settings per scan, so
            // the new values take effect on the next Tick automatically.
            if (m_Tracker)
                m_Tracker.ConfigureFromSettings(m_Settings);
        }

        if (m_Handler)
            m_Handler.OnScanComplete(this, m_Plots, m_Tracker, m_Context);
    }

    // Build + schedule FIRE_CONTROL / TRACK dwells (TODO §9 S1). Persistent task
    // state keeps each track's deadline across scans; the scanner then forces a
    // full update for scheduled scatterer ids. SEARCH stays on the fair cursor.
    // Dead scatterer ids accumulate in m_DwellTasks (ids are monotonic, so no
    // reuse collision); prune later if the table ever grows unbounded.
    // Returns the highest-priority scheduled kind (FC > TRACK > SEARCH) so
    // multi-band can pick one carrier for the whole scan.
    protected ERDF_DwellKind PlanDwells(float worldTimeS)
    {
        ERDF_DwellKind bestKind = ERDF_DwellKind.RDF_DWELL_SEARCH;
        if (!m_Settings || !m_DwellScheduler || !m_DwellTasks || !m_Tracker)
            return bestKind;
        m_DwellScheduler.SetBudgetMs(m_Settings.m_DwellBudgetMs);

        array<ref RDF_DwellTask> tasks = new array<ref RDF_DwellTask>();
        array<ref RDF_RadarTrack> tracks = m_Tracker.GetAllTracks();

        // Fire-control: the locked target gets the highest-rate dwell.
        if (m_LockManager && m_LockManager.HasTarget())
        {
            int lockTrackId = m_LockManager.GetLockedTrackId();
            if (lockTrackId >= 0)
            {
                RDF_RadarTrack lockTrack = m_Tracker.GetTrackById(lockTrackId);
                if (lockTrack && lockTrack.m_ScattererId > 0)
                {
                    RDF_DwellTask fc = GetOrCreateDwellTask(
                        lockTrack.m_ScattererId,
                        ERDF_DwellKind.RDF_DWELL_FIRE_CONTROL);
                    fc.m_PeriodS = m_Settings.m_FireControlDwellPeriodS;
                    fc.m_DwellMs = m_Settings.m_FireControlDwellMs;
                    tasks.Insert(fc);
                }
            }
        }

        // Designated scatterer: FIRE_CONTROL even if not locked.
        if (m_DesignatedScattererId > 0)
        {
            bool alreadyFc = false;
            for (int d = 0; d < tasks.Count(); d++)
            {
                RDF_DwellTask existingFc = tasks.Get(d);
                if (!existingFc)
                    continue;
                if (existingFc.m_TaskId != m_DesignatedScattererId)
                    continue;
                existingFc.m_Kind = ERDF_DwellKind.RDF_DWELL_FIRE_CONTROL;
                existingFc.m_PeriodS = m_Settings.m_FireControlDwellPeriodS;
                existingFc.m_DwellMs = m_Settings.m_FireControlDwellMs;
                alreadyFc = true;
            }
            if (!alreadyFc)
            {
                RDF_DwellTask des = GetOrCreateDwellTask(
                    m_DesignatedScattererId,
                    ERDF_DwellKind.RDF_DWELL_FIRE_CONTROL);
                des.m_PeriodS = m_Settings.m_FireControlDwellPeriodS;
                des.m_DwellMs = m_Settings.m_FireControlDwellMs;
                tasks.Insert(des);
            }
        }

        // Track dwells: confirmed tracks with a scatterer id (skip the locked one).
        int lockedId = -1;
        if (m_LockManager && m_LockManager.HasTarget())
            lockedId = m_LockManager.GetLockedTrackId();
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarTrack tr = tracks.Get(i);
            if (!tr || !tr.m_Confirmed || tr.m_ScattererId <= 0)
                continue;
            if (tr.m_TrackId == lockedId)
                continue;
            if (tr.m_ScattererId == m_DesignatedScattererId)
                continue;
            RDF_DwellTask t = GetOrCreateDwellTask(
                tr.m_ScattererId,
                ERDF_DwellKind.RDF_DWELL_TRACK);
            t.m_PeriodS = m_Settings.m_TrackDwellPeriodS;
            t.m_DwellMs = m_Settings.m_TrackDwellMs;
            tasks.Insert(t);
        }

        array<ref RDF_DwellTask> scheduled = new array<ref RDF_DwellTask>();
        array<ref RDF_DwellTask> late = new array<ref RDF_DwellTask>();
        m_DwellScheduler.Plan(tasks, worldTimeS, scheduled, late);
        // late (deadline miss) is ignored for now; a future slice may coast them.

        array<int> ids = new array<int>();
        int bestPri = RDF_DwellScheduler.ClassPriority(bestKind);
        for (int s = 0; s < scheduled.Count(); s++)
        {
            RDF_DwellTask st = scheduled.Get(s);
            if (!st)
                continue;
            ids.Insert(st.m_TaskId);
            RDF_DwellScheduler.Advance(st, worldTimeS);
            int pri = RDF_DwellScheduler.ClassPriority(st.m_Kind);
            if (pri > bestPri)
            {
                bestPri = pri;
                bestKind = st.m_Kind;
            }
        }
        m_Scanner.SetDwellScattererIds(ids);
        return bestKind;
    }

    protected RDF_DwellTask GetOrCreateDwellTask(int scattererId, ERDF_DwellKind kind)
    {
        RDF_DwellTask existing = null;
        if (m_DwellTasks.Contains(scattererId))
            existing = m_DwellTasks.Get(scattererId);
        if (existing)
        {
            existing.m_Kind = kind;
            return existing;
        }
        RDF_DwellTask created = new RDF_DwellTask();
        created.m_TaskId = scattererId;
        created.m_Kind = kind;
        created.m_DeadlineS = 0.0;
        m_DwellTasks.Set(scattererId, created);
        return created;
    }

    // Read EW observables each scan and decide ECCM responses (TODO §9 S3).
    // SLB is applied immediately. Frequency agility arms hop for the *next*
    // scan (GetScanFrequencyHz). PRF stagger is already config-time; burn-
    // through is still status-only.
    protected void RunEccmDecision()
    {
        if (!m_Settings || !m_EccmDecision || !m_Scanner)
            return;
        m_EccmDecision.Configure(
            m_Settings.m_EccmJnOnDb,
            m_Settings.m_EccmJnHysteresisDb,
            m_Settings.m_EccmSidelobeCouplingOn);

        float jnDb = m_Scanner.GetLastJnDb();
        int falsePlots = m_Scanner.GetLastFalsePlotCount();
        bool locked = false;
        if (m_LockManager)
            locked = m_LockManager.HasTarget();

        float sidelobeCoupling = 0.0;
        if (m_Settings.m_EwStack && m_Settings.m_Hardware)
        {
            float minMain = m_Settings.m_EwStack.GetMinMainlobeFraction(
                m_Scanner.GetLastOrigin(),
                m_Scanner.GetLastForward(),
                m_Settings.m_Hardware);
            sidelobeCoupling = 1.0 - minMain;
        }

        bool slb;
        bool prf;
        bool freq;
        bool burn;
        m_EccmDecision.Decide(
            jnDb,
            sidelobeCoupling,
            falsePlots,
            locked,
            slb,
            prf,
            freq,
            burn);
        m_EccmSlb = slb;
        m_EccmPrf = prf;
        m_EccmFreq = freq;
        m_EccmBurn = burn;

        if (m_Settings.m_EwStack)
            m_Settings.m_EwStack.SetSlbEnabled(slb);
    }

    string GetEccmStatusShort()
    {
        if (!m_EccmSlb && !m_EccmPrf && !m_EccmFreq && !m_EccmBurn)
            return "eccm=0";
        string s = "eccm";
        if (m_EccmSlb)
            s = s + " slb";
        if (m_EccmPrf)
            s = s + " prf";
        if (m_EccmFreq)
            s = s + " freq";
        if (m_EccmBurn)
            s = s + " burn";
        return s;
    }

    // Notify illuminated platforms (RWR). Skipped for ESM receive-only.
    protected void PublishRwrThreats(IEntity subject, vector origin, float worldTimeS)
    {
        if (!m_Settings || !m_Settings.m_EnableRwrReporting)
            return;
        if (m_Mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM)
            return;
        if (!subject)
            return;

        if (m_Plots)
        {
            for (int i = 0; i < m_Plots.Count(); i++)
            {
                RDF_RadarTarget plot = m_Plots.Get(i);
                if (!plot || !plot.m_Detected)
                    continue;
                IEntity victim = ResolveRwrVictim(plot);
                if (!victim)
                    continue;
                if (!VictimCanHearIlluminator(plot.m_Distance))
                    continue;
                RDF_RadarRwr.Report(
                    subject,
                    victim,
                    ERDF_RadarRwrLevel.RDF_RWR_SEARCH,
                    origin,
                    worldTimeS,
                    m_ScanSerial);
            }
        }

        array<ref RDF_RadarTrack> tracks = GetTracks();
        if (tracks)
        {
            for (int j = 0; j < tracks.Count(); j++)
            {
                RDF_RadarTrack tr = tracks.Get(j);
                if (!tr || !tr.m_Entity)
                    continue;
                if (tr.m_HitCount < 1)
                    continue;
                if (!VictimCanHearIlluminator(tr.m_FilteredRangeM))
                    continue;

                ERDF_RadarRwrLevel lvl = ERDF_RadarRwrLevel.RDF_RWR_SEARCH;
                if (tr.m_Confirmed)
                    lvl = ERDF_RadarRwrLevel.RDF_RWR_TRACK;

                RDF_RadarRwr.Report(
                    subject,
                    tr.m_Entity,
                    lvl,
                    origin,
                    worldTimeS,
                    m_ScanSerial);
            }
        }

        if (m_LockManager && m_LockManager.HasTarget())
        {
            IEntity lockedEnt = m_LockManager.GetLockedEntity();
            if (lockedEnt)
            {
                if (!VictimCanHearIlluminator(m_LockManager.GetLockedRangeM()))
                    lockedEnt = null;
            }
            if (lockedEnt)
            {
                ERDF_RadarLockState st = m_LockManager.GetState();
                if (st == ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING
                    || st == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING
                    || st == ERDF_RadarLockState.RDF_RADAR_LOCK_COAST)
                {
                    RDF_RadarRwr.Report(
                        subject,
                        lockedEnt,
                        ERDF_RadarRwrLevel.RDF_RWR_LOCK,
                        origin,
                        worldTimeS,
                        m_ScanSerial);
                }
            }
        }
    }

    // Geometric RWR (default): already detected ⇒ hear. Friis opt-in uses LPI peak.
    protected bool VictimCanHearIlluminator(float rangeM)
    {
        if (!m_Settings)
            return true;
        if (!m_Settings.m_EnableRwrFriis)
            return true;
        RDF_RadarHardware hardware = m_Settings.m_Hardware;
        if (!hardware)
            return true;

        float freqHz = hardware.GetScanFrequencyHz(m_ScanSerial);
        float gt = hardware.GetAntennaGainDbiAtFrequency(freqHz);
        float pr = RDF_RadarClutterModel.ReceivedPowerEsmW(
            hardware.GetEffectivePeakPowerW(),
            gt,
            freqHz,
            m_Settings.m_RwrAntennaGainDbi,
            hardware.m_SystemLossDb,
            rangeM,
            1.0,
            1.0);
        float noise = hardware.GetNoisePowerW() * 4.0;
        if (noise < 1.0e-30)
            noise = 1.0e-30;
        if (pr <= 0.0)
            return false;
        float ratio = pr / noise;
        if (ratio <= 0.0)
            return false;
        float snrDb = 10.0 * Math.Log10(ratio);
        if (snrDb >= m_Settings.m_RwrDetectSnrDb)
            return true;
        return false;
    }

    protected IEntity ResolveRwrVictim(RDF_RadarTarget plot)
    {
        if (!plot)
            return null;
        if (plot.m_Entity)
            return plot.m_Entity;
        if (plot.m_ScattererId <= 0)
            return null;
        RDF_RadarScatterer entry = RDF_RadarScattererRegistry.FindById(plot.m_ScattererId);
        if (!entry)
            return null;
        return entry.m_Entity;
    }

    string GetStatusShort()
    {
        string modeName = "SEARCH";
        if (m_Mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_STARE)
            modeName = "STARE";
        else if (m_Mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR)
            modeName = "WLR";
        else if (m_Mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM)
            modeName = "ESM";
        else if (m_Mode == ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER)
            modeName = "PD";

        string dem = GetDemStatusShort();

        string reuse = "";
        if (m_Scanner)
            reuse = " | " + m_Scanner.GetScanReuseStatsShort();

        string mtd = "";
        if (m_Scanner)
            mtd = " | " + m_Scanner.GetMtdStatsShort();

        string ew = "";
        if (m_Scanner)
            ew = " | " + m_Scanner.GetEwStatsShort();

        string gov = "";
        if (m_BudgetGovernor && m_BudgetGovernor.IsEnabled())
        {
            gov = " | gov los=" + m_BudgetGovernor.GetLosBudget().ToString()
                + " wlr=" + m_BudgetGovernor.GetWlrBudget().ToString();
        }

        string lock = "";
        if (m_LockManager)
            lock = " | " + m_LockManager.GetStatusShort();

        string status = modeName + " | " + dem
            + " | plots=" + CountDetectedPlots().ToString()
            + " tracks=" + CountConfirmedTracks().ToString()
            + " wlr=" + CountWlrFixes().ToString()
            + " scanMs=" + (Math.Round(m_LastScanDurationMs * 10.0) * 0.1).ToString()
            + reuse
            + mtd
            + ew
            + gov
            + lock;

        if (m_Settings && m_Settings.m_EnableMultiBand && m_Settings.m_Hardware)
        {
            string band = m_Settings.m_Hardware.GetBand();
            if (m_Settings.m_Hardware.IsHopActive())
                band = band + "*";
            status = status + " | band=" + band;
        }
        return status;
    }
}
