// Optional auto-run manager for LiDAR scans (demo).
class RDF_LidarAutoRunner
{
    protected static ref RDF_LidarAutoRunner s_Instance;
    protected static bool s_AutoEnabled = false;
    // Minimum tick interval for the global call queue (seconds). Reduces per-frame overhead.
    protected static float s_MinTickInterval = 0.2;
    // Network API for synchronization (optional, for multiplayer)
    // Use a plain reference (no strong ref) to avoid Enforce strong-ref restriction on static fields.
    protected static RDF_LidarNetworkAPI s_NetworkAPI;
    protected ref RDF_LidarScanner m_Scanner;
    protected ref RDF_LidarVisualizer m_Visualizer;
    protected ref RDF_LidarDemoConfig m_DemoConfig;
    protected ref RDF_LidarScanCompleteHandler m_ScanCompleteHandler;
    protected ref RDF_LidarDemoStatsHandler m_DemoStatsHandler;
    // Wall-clock seconds of the last demo scan start (cadence gate). Wall clock
    // matches RDF_LidarSensor.Tick / radar side: world time freezes in GM
    // editor / pause, which would otherwise stall the demo scan serial.
    protected float m_LastScanWallS = -1.0;
    protected bool m_Running = false;
    protected bool m_WriteLiveCSV = false;
    protected int m_ScanSerial = 0;
    protected bool m_HudAfterglowTickScheduled = false;
    protected static const int HUD_AFTERGLOW_TICK_MS = 100;

    void RDF_LidarAutoRunner()
    {
        m_Scanner = new RDF_LidarScanner();
        m_Visualizer = new RDF_LidarVisualizer();
        // Demo default: material-based coloring (metal/vegetation/water etc.). Configs can override via SetDemoColorStrategy.
        m_Visualizer.SetColorStrategy(new RDF_LidarMaterialColorStrategy());
        RDF_LidarVisualSettings vs = m_Visualizer.GetSettings();
        if (vs)
            vs.ApplyShowcaseDefaults();
        // recurring tick; scanning is gated by m_Running. Use a non-zero min tick interval to avoid per-frame overhead.
        GetGame().GetCallqueue().CallLater(StaticTick, (int)(s_MinTickInterval * 1000.0), true);
    }

    // Configure minimum tick interval at runtime (seconds).
    static void SetMinTickInterval(float interval)
    {
        s_MinTickInterval = Math.Max(0.01, interval);
    }

    static float GetMinTickInterval()
    {
        return s_MinTickInterval;
    }

    // Set network API for multiplayer synchronization (weak reference).
    // AutoRunner validates the API before use and clears stale references.
    static void SetNetworkAPI(RDF_LidarNetworkAPI networkAPI)
    {
        if (!networkAPI)
        {
            s_NetworkAPI = null;
            return;
        }
        // Only bind if network layer appears available
        if (networkAPI.IsNetworkAvailable())
            s_NetworkAPI = networkAPI;
        else
            s_NetworkAPI = null;
    }

    // Internal helper: validate and clear stale network API
    static bool IsNetworkAPIValid()
    {
        if (!s_NetworkAPI)
            return false;
        if (!s_NetworkAPI.IsNetworkAvailable())
        {
            s_NetworkAPI = null; // clear stale ref
            return false;
        }
        return true;
    }

    // Get current network API (returns null if invalid)
    static RDF_LidarNetworkAPI GetNetworkAPI()
    {
        if (!IsNetworkAPIValid())
            return null;
        return s_NetworkAPI;
    }

    static void StaticTick()
    {
        GetInstance().RDF_LidarTick();
    }

    static RDF_LidarAutoRunner GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_LidarAutoRunner();
        return s_Instance;
    }

    static void StartAutoRun()
    {
        // Lazy bind: only when LiDAR demo actually starts (not on every game start).
        RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject(true);
        GetInstance().m_Running = true;
    }

    static void StopAutoRun()
    {
        RDF_LidarAutoRunner inst = GetInstance();
        inst.m_Running = false;
        inst.StopHudAfterglowTick();
        // Release stale Shape refs and IEntity-holding sample refs immediately.
        // Without this, the last frame's shapes (ONCE flag shapes still occupy memory
        // as ref-counted objects) and sample IEntity refs survive until the next Render().
        if (inst.m_Visualizer)
            inst.m_Visualizer.Reset();
    }

    // Single code switch to enable/disable the demo auto-run.
    static void SetDemoEnabled(bool enabled, bool sync = true)
    {
        s_AutoEnabled = enabled;

        // Sync to network API if requested
        if (sync && IsNetworkAPIValid())
            s_NetworkAPI.SetEnabled(enabled);

        RDF_LidarAutoRunner inst = GetInstance();
        if (enabled)
        {
            // Apply configured demo options if present
            if (inst && inst.m_DemoConfig)
                inst.ApplyDemoConfig();

            StartAutoRun();
        }
        else
        {
            StopAutoRun();
        }
    }

    static bool IsDemoEnabled()
    {
        return s_AutoEnabled;
    }

    // Set a custom sampling strategy to be used by the demo auto-runner.
    static void SetDemoSampleStrategy(RDF_LidarSampleStrategy strategy)
    {
        if (!strategy) return;
        RDF_LidarAutoRunner inst = GetInstance();
        if (inst && inst.m_Scanner)
            inst.m_Scanner.SetSampleStrategy(strategy);
    }

    // Set the demo scanner ray count (minimum 1, no upper limit)
    static void SetDemoRayCount(int rays)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner) return;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s) return;
        s.m_RayCount = Math.MaxInt(rays, 1);
    }

    // Set the demo visual color strategy (applies to visualizer if present)
    static void SetDemoColorStrategy(RDF_LidarColorStrategy strategy)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        inst.m_Visualizer.SetColorStrategy(strategy);
    }

    // Draw scan origin and X/Y/Z axes in the demo (uses RDF_LidarVisualSettings.m_DrawOriginAxis).
    static void SetDemoDrawOriginAxis(bool draw)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_DrawOriginAxis = draw;
    }

    static void SetDemoDrawRays(bool draw)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_DrawRays = draw;
    }

    static void SetDemoDrawPoints(bool draw)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_DrawPoints = draw;
    }

    // When true: only draw rays/points that hit (sample.m_Hit == true). Skips missed rays.
    static void SetDemoShowHitsOnly(bool showHitsOnly)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_ShowHitsOnly = showHitsOnly;
    }

    // When true: render game view + point cloud. When false: render point cloud only (solid background + disable scene render).
    static void SetDemoRenderWorld(bool renderWorld)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer)
            return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_RenderWorld = renderWorld;
        PlayerController controller = GetGame().GetPlayerController();
        if (controller)
            controller.SetCharacterCameraRenderActive(renderWorld);
    }

    static bool GetDemoRenderWorld()
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer)
            return true;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (!vs)
            return true;
        return vs.m_RenderWorld;
    }

    // When true: prefer the VisualSettings' batched mesh renderer for large point-cloud performance.
    static void SetDemoUseBatchedMesh(bool use)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_UseBatchedMesh = use;
    }

    static bool GetDemoUseBatchedMesh()
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer) return false;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (!vs) return false;
        return vs.m_UseBatchedMesh;
    }

    // When true: ApplyShowcaseDefaults then keep current draw rays/points overrides.
    // When false: ApplyMinimalDefaults (no afterglow / rings / sector).
    static void SetDemoUseShowcase(bool use)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer)
            return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (!vs)
            return;
        if (use)
            vs.ApplyShowcaseDefaults();
        else
            vs.ApplyMinimalDefaults();
    }

    static void SetDemoDrawAfterglow(bool draw)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer)
            return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_DrawAfterglow = draw;
    }

    static void SetDemoDrawRangeRings(bool draw)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer)
            return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_DrawRangeRings = draw;
    }

    static void SetDemoDrawSectorSweep(bool draw)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Visualizer)
            return;
        RDF_LidarVisualSettings vs = inst.m_Visualizer.GetSettings();
        if (vs)
            vs.m_DrawSectorSweep = draw;
    }

    protected void EnsureHudAfterglowTick()
    {
        if (m_HudAfterglowTickScheduled)
            return;
        if (!RDF_LidarHUD.IsVisible())
            return;
        GetGame().GetCallqueue().CallLater(StaticHudAfterglowTick, HUD_AFTERGLOW_TICK_MS, true);
        m_HudAfterglowTickScheduled = true;
    }

    protected void StopHudAfterglowTick()
    {
        if (!m_HudAfterglowTickScheduled)
            return;
        GetGame().GetCallqueue().Remove(StaticHudAfterglowTick);
        m_HudAfterglowTickScheduled = false;
    }

    static void StaticHudAfterglowTick()
    {
        if (!RDF_LidarHUD.IsVisible())
        {
            RDF_LidarAutoRunner inst = GetInstance();
            if (inst)
                inst.StopHudAfterglowTick();
            return;
        }
        RDF_LidarHUD.TickAfterglow();
    }

    protected void UpdateSweepGeometryFromStrategy(IEntity subject)
    {
        if (!m_Visualizer || !m_Scanner)
            return;
        RDF_LidarSampleStrategy strategy = m_Scanner.GetSampleStrategy();
        if (!strategy)
        {
            m_Visualizer.ClearSweepGeometry();
            return;
        }

        RDF_SweepSampleStrategy sweep = RDF_SweepSampleStrategy.Cast(strategy);
        if (sweep)
        {
            float yawDeg = 0.0;
            if (subject)
            {
                float azRad = sweep.GetCurrentAzimuthDeg() * Math.DEG2RAD;
                float halfRad = sweep.GetHalfAngleDeg() * Math.DEG2RAD;
                float cosA = Math.Cos(halfRad);
                float elevFrac = 0.5;
                float z = cosA + (1.0 - cosA) * elevFrac;
                float r = Math.Sqrt(Math.Max(0.0, 1.0 - z * z));
                vector dirLocal = Vector(Math.Cos(azRad) * r, Math.Sin(azRad) * r, z);

                vector worldMat[4];
                subject.GetWorldTransform(worldMat);
                vector worldDir = (worldMat[0] * dirLocal[0])
                    + (worldMat[1] * dirLocal[1])
                    + (worldMat[2] * dirLocal[2]);
                float flen = Math.Sqrt(worldDir[0] * worldDir[0] + worldDir[2] * worldDir[2]);
                if (flen > 0.001)
                    yawDeg = Math.Atan2(worldDir[2], worldDir[0]) * Math.RAD2DEG;
            }

            m_Visualizer.SetSweepGeometry(
                sweep.GetHalfAngleDeg(),
                sweep.GetSweepWidthDeg(),
                yawDeg);
            return;
        }

        RDF_ConicalSampleStrategy conical = RDF_ConicalSampleStrategy.Cast(strategy);
        if (conical)
        {
            // Fixed cone: fan centred on subject forward (sweepWidth 0 → use entity forward).
            m_Visualizer.SetSweepGeometry(conical.GetHalfAngleDeg(), 0.0, 0.0);
            return;
        }

        m_Visualizer.ClearSweepGeometry();
    }

    // When true, installs built-in handler that prints hit count and closest distance after each scan (uses RDF_LidarSampleUtils).
    static void SetDemoVerbose(bool verbose)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst) return;
        if (verbose)
        {
            if (!inst.m_DemoStatsHandler)
                inst.m_DemoStatsHandler = new RDF_LidarDemoStatsHandler();
            inst.m_ScanCompleteHandler = inst.m_DemoStatsHandler;
        }
        else
        {
            if (inst.m_ScanCompleteHandler == inst.m_DemoStatsHandler)
                inst.m_ScanCompleteHandler = null;
        }
    }

    // Get the demo scanner range (m). Used to sync HUD display range.
    static float GetDemoScannerRange()
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner)
            return 1000.0;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s)
            return 1000.0;
        s.Validate();
        return s.m_Range;
    }

    // Set the demo scanner range (m).
    static void SetDemoRange(float rangeM)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner)
            return;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s)
            return;
        s.m_Range = Math.Clamp(rangeM, 0.1, 100000.0);
    }

    // Show / hide PPI HUD and wire it as the scan-complete handler.
    static void SetDemoHudEnabled(bool enabled)
    {
        if (enabled)
        {
            RDF_LidarHUD.Show();
            RDF_LidarHUD.SetDisplayRange(GetDemoScannerRange());
            SetScanCompleteHandler(RDF_LidarHUD.GetInstance());
            GetInstance().EnsureHudAfterglowTick();
        }
        else
        {
            RDF_LidarHUD.Hide();
            GetInstance().StopHudAfterglowTick();
            RDF_LidarAutoRunner inst = GetInstance();
            if (inst && inst.m_ScanCompleteHandler == RDF_LidarHUD.GetInstance())
                inst.m_ScanCompleteHandler = null;
        }
    }

    // Set the demo scanner update interval safely (seconds)
    static void SetDemoUpdateInterval(float interval)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner) return;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s) return;
        s.m_UpdateInterval = Math.Max(0.01, interval);
    }

    // Set trace target mode: 0=terrain only, 1=all (terrain+entities), 2=entities only.
    static void SetDemoTraceTargetMode(int mode)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner) return;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s) return;
        int m = Math.ClampInt(mode, 0, 2);
        if (m == 0)
            s.m_TraceTargetMode = ERDF_TraceTargetMode.TERRAIN_ONLY;
        else if (m == 1)
            s.m_TraceTargetMode = ERDF_TraceTargetMode.ALL;
        else
            s.m_TraceTargetMode = ERDF_TraceTargetMode.ENTITIES_ONLY;
        s.Validate();
    }

    // Enable smoke/particle occlusion: when true, TraceFlags.VISIBILITY is added so
    // visibility occluders (e.g. smoke grenades) block laser rays.
    // When true, each scan overwrites $profile:LiDAR/lidar_live_1.csv with current samples (no dedup).
    static void SetDemoWriteLiveCSV(bool enable)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (inst)
            inst.m_WriteLiveCSV = enable;
    }

    static void SetDemoTraceSmokeOcclusion(bool enable)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst || !inst.m_Scanner) return;
        RDF_LidarSettings s = inst.m_Scanner.GetSettings();
        if (!s) return;
        s.m_TraceSmokeOcclusion = enable;
        s.Validate();
    }

    static void SetDemoConfig(RDF_LidarDemoConfig cfg, bool sync = true)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (!inst) return;
        inst.m_DemoConfig = cfg;

        // Sync to network API if requested
        if (sync && IsNetworkAPIValid())
            s_NetworkAPI.SetConfig(cfg);

        // If demo is already running, apply the new config so e.g. m_RenderWorld takes effect immediately.
        if (s_AutoEnabled && inst.m_DemoConfig)
            inst.ApplyDemoConfig();
    }

    static RDF_LidarDemoConfig GetDemoConfig()
    {
        return GetInstance().m_DemoConfig;
    }

    // Apply current demo config to the running demo (called on SetDemoEnabled(true)).
    void ApplyDemoConfig()
    {
        if (!m_DemoConfig) return;
        m_DemoConfig.ApplyTo(this);
    }

    // Single API entry: apply config and turn demo on. Use RDF_LidarDemoConfig.Create*() for presets.
    static void StartWithConfig(RDF_LidarDemoConfig cfg)
    {
        if (!cfg) return;
        SetDemoConfig(cfg);
        SetDemoEnabled(true);
    }

    static bool IsRunning()
    {
        return GetInstance().m_Running;
    }

    // Set handler called after each scan (e.g. threat detection, export). Pass null to clear.
    static void SetScanCompleteHandler(RDF_LidarScanCompleteHandler handler)
    {
        RDF_LidarAutoRunner inst = GetInstance();
        if (inst)
            inst.m_ScanCompleteHandler = handler;
    }

    static RDF_LidarScanCompleteHandler GetScanCompleteHandler()
    {
        return GetInstance().m_ScanCompleteHandler;
    }

    void RDF_LidarTick()
    {
        if (RDF_LidarDiagMenu.IsRegistered())
            RDF_LidarDiagMenu.ApplyToAutoRunner();

        if (!m_Running)
            return;

        if (!m_Scanner || !m_Visualizer)
            return;

        RDF_LidarSettings settings = m_Scanner.GetSettings();
        if (!settings || !settings.m_Enabled)
            return;

        World world = GetGame().GetWorld();
        if (!world)
            return;

        // Wall-clock cadence gate (same clock as RDF_LidarSensor.Tick), so the
        // demo and the product path stay in sync when world time is frozen or
        // time-scaled in the GM editor.
        float now = System.GetTickCount() * 0.001;
        if (m_LastScanWallS >= 0.0 && (now - m_LastScanWallS) < settings.m_UpdateInterval)
            return;
        m_LastScanWallS = now;

        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);

        UpdateSweepGeometryFromStrategy(subject);
        m_ScanSerial = m_ScanSerial + 1;
        EnsureHudAfterglowTick();

        // Check if we have network API for server-authoritative scanning
        if (IsNetworkAPIValid())
        {
            // Request server to perform scan (no subject parameter)
            s_NetworkAPI.RequestScan();

            // Use synchronized scan results for visualization
            if (s_NetworkAPI.HasSyncedSamples())
            {
                ref array<ref RDF_LidarSample> syncedSamples = s_NetworkAPI.GetLastScanResults();
                if (!syncedSamples)
                    return;
                // Update visualizer with server results
                m_Visualizer.RenderWithSamples(subject, syncedSamples, m_ScanSerial);

                if (m_WriteLiveCSV && syncedSamples.Count() > 0)
                {
                    FileIO.MakeDirectory("$profile:LiDAR");
                    RDF_LidarExport.AppendLiveCSVToFile(syncedSamples, "$profile:LiDAR/lidar_live_1.csv");
                }
                if (m_ScanCompleteHandler)
                    m_ScanCompleteHandler.OnScanComplete(syncedSamples);
            }
        }
        else
        {
            // Local scanning (single-player or no network component)
            m_Visualizer.Render(subject, m_Scanner, m_ScanSerial);
            ref array<ref RDF_LidarSample> samples = m_Visualizer.GetLastSamples();
            if (m_WriteLiveCSV && samples && samples.Count() > 0)
            {
                FileIO.MakeDirectory("$profile:LiDAR");
                RDF_LidarExport.AppendLiveCSVToFile(samples, "$profile:LiDAR/lidar_live_1.csv");
            }
            if (m_ScanCompleteHandler && samples)
                m_ScanCompleteHandler.OnScanComplete(samples);
        }
    }
}
