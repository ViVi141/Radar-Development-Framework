// One-shot manual PPI demo for placing vehicles / firing shells and verifying
// physical radar detection. Not geometry-only.
//
// Uses RDF_RadarSensor for configure / read; AutoRunner only hosts Tick + HUD.
// Probe() always forces one ScanOnce so debugger dumps are never stale when the
// editor pauses AutoRunner's CallLater tick.
//
// Usage (Script Debugger):
//   RDF_RadarManualDemo.Start();                 // SEARCH, MTI off
//   RDF_RadarManualDemo.StartPulseDoppler();     // MTD bank for heli CPA
//   // place vehicles / fire shells, then:
//   RDF_RadarManualDemo.Probe();
//   RDF_RadarManualDemo.Stop();
class RDF_RadarManualDemo
{
    protected static ref RDF_RadarSettings s_PrevConfig;
    protected static bool s_PrevDemo;
    protected static bool s_PrevHud;
    protected static bool s_PrevForceLocal;
    protected static bool s_Active;
    protected static ref array<IEntity> s_ProbeScratch;

    static void Start()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        s_PrevConfig = null;
        if (sensor)
            s_PrevConfig = sensor.GetSettings();
        s_PrevDemo = RDF_RadarAutoRunner.IsDemoEnabled();
        s_PrevHud = RDF_RadarAutoRunner.IsHudEnabled();
        s_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        RDF_RadarSettings cfg = BuildManualConfig();
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplySensorConfig(cfg);
        if (sensor)
            sensor.ResetSession();

        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();
        RDF_RadarHUD.Show();
        s_Active = true;

        Print("[RDF ManualDemo] started physical SEARCH (MTI off, wide beam, DEM clutter OFF, world search)");
        Print("[RDF ManualDemo] tip: keep Play running; close GM free-cam or possess a character; Probe() after placing targets");
        ForceScanAndProbe();
    }

    // Pulse-Doppler MTD path for tangential heli / CPA verification.
    static void StartPulseDoppler()
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        s_PrevConfig = null;
        if (sensor)
            s_PrevConfig = sensor.GetSettings();
        s_PrevDemo = RDF_RadarAutoRunner.IsDemoEnabled();
        s_PrevHud = RDF_RadarAutoRunner.IsHudEnabled();
        s_PrevForceLocal = RDF_RadarAutoRunner.IsForceLocalScan();

        RDF_RadarSettings cfg = BuildPulseDopplerManualConfig();
        RDF_RadarAutoRunner.SetForceLocalScan(true);
        ApplySensorConfig(cfg);
        if (sensor)
            sensor.ResetSession();

        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarAutoRunner.StartAutoRun();
        RDF_RadarHUD.Show();
        s_Active = true;

        Print("[RDF ManualDemo] started PULSE_DOPPLER (MTD bank, rotor sidebands, DEM clutter OFF)");
        Print("[RDF ManualDemo] tip: fly UH-1/Mi-8 across CPA; Probe() should keep paint near vr=0");
        ForceScanAndProbe();
    }

    static void Stop()
    {
        s_Active = false;
        if (s_PrevConfig)
            ApplySensorConfig(s_PrevConfig);
        RDF_RadarAutoRunner.SetDemoEnabled(s_PrevDemo);
        RDF_RadarAutoRunner.SetHudEnabled(s_PrevHud);
        RDF_RadarAutoRunner.SetForceLocalScan(s_PrevForceLocal);
        Print("[RDF ManualDemo] stopped");
    }

    static bool IsActive()
    {
        return s_Active;
    }

    protected static void ApplySensorConfig(RDF_RadarSettings cfg)
    {
        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor || !cfg)
            return;
        sensor.SetForceLocalScan(true);
        sensor.Configure(cfg);
    }

    static RDF_RadarSettings BuildManualConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(128);
        cfg.m_Range = 5000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        // Live mortar / gun fire is discoverable; Zeus-placed inert shells are not.
        cfg.m_IncludeProjectiles = true;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 8.0;
        // Wide beams make the DEM clutter cell huge and bury skin returns.
        // Clutter has its own AutoTest; this demo is for PPI visibility.
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_UseScattererRegistry = false;
        cfg.m_UseSphereQuery = true;
        // Editor-placed vehicles are often missing from DYNAMIC sphere results.
        cfg.m_SphereQueryAlsoActive = true;
        cfg.m_MaxLosTracesPerScan = 128;
        cfg.m_KeepUndetected = true;
        cfg.m_KeepEntityTruth = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 90.0;
        hw.m_ScanRpm = 0.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("manual_low", 0.0, 30.0, 0.0);
        hw.AddElevationBeam("manual_mid", 15.0, 40.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.Validate();
        return cfg;
    }

    static RDF_RadarSettings BuildPulseDopplerManualConfig()
    {
        RDF_RadarSettings cfg = RDF_RadarSensor.CreatePulseDopplerSettings(128);
        cfg.m_Range = 5000.0;
        cfg.m_SectorHalfAngleDeg = 180.0;
        cfg.m_UpdateInterval = 0.2;
        cfg.m_IncludeVehicles = true;
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 8.0;
        cfg.m_EnableDemClutter = false;
        cfg.m_EnableCfarGate = false;
        cfg.m_UseScattererRegistry = true;
        cfg.m_MaxLosTracesPerScan = 128;
        cfg.m_KeepUndetected = true;
        cfg.m_KeepEntityTruth = true;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);

        RDF_RadarHardware hw = cfg.m_Hardware;
        if (!hw)
            hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 30.0;
        hw.m_ScanRpm = 12.0;
        hw.m_EnableMti = true;
        hw.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;
        hw.m_DopplerBinCount = 16;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("pd_low", 2.0, 12.0, 0.0);
        hw.AddElevationBeam("pd_high", 14.0, 16.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.Validate();
        return cfg;
    }

    // Force one dwell then dump diagnostics. Safe to call from Script Debugger
    // even when AutoRunner's periodic tick is stalled by the GM editor.
    static void Probe()
    {
        ForceScanAndProbe();
    }

    protected static void ForceScanAndProbe()
    {
        IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
        if (!subject)
        {
            Print("[RDF ManualDemo] Probe FAIL: no local subject (close GM editor / possess character)", LogLevel.WARNING);
            return;
        }

        RDF_RadarSensor sensor = RDF_RadarAutoRunner.GetSensor();
        if (!sensor)
        {
            Print("[RDF ManualDemo] Probe FAIL: no RDF_RadarSensor", LogLevel.WARNING);
            return;
        }

        BaseWorld world = GetGame().GetWorld();
        float worldTimeS = 0.0;
        if (world)
            worldTimeS = world.GetWorldTime() * 0.001;

        // Debugger Probe must not depend on CallLater advancing while the editor
        // is open; ScanOnce always produces a fresh dwell.
        sensor.SetForceLocalScan(true);
        sensor.SetEnabled(true);
        sensor.ScanOnce(subject, null, worldTimeS);

        RDF_RadarSettings cfg = sensor.GetSettings();
        bool mti = false;
        float azBw = -1.0;
        bool demClutter = false;
        if (cfg)
        {
            demClutter = cfg.m_EnableDemClutter;
            if (cfg.m_Hardware)
            {
                mti = cfg.m_Hardware.m_EnableMti;
                azBw = cfg.m_Hardware.m_AzimuthBeamwidthDeg;
            }
        }

        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        int plotN = 0;
        int detN = sensor.CountDetectedPlots();
        float maxSnr = -300.0;
        if (plots)
        {
            plotN = plots.Count();
            foreach (RDF_RadarTarget t : plots)
            {
                if (!t)
                    continue;
                if (t.m_SnrDb > maxSnr)
                    maxSnr = t.m_SnrDb;
            }
        }

        int vehicleN = 0;
        int nearVehicleN = 0;
        int projectileN = 0;
        int activeN = 0;
        CountNearbyCandidates(subject, vehicleN, nearVehicleN, projectileN, activeN);

        Print(string.Format(
            "[RDF ManualDemo] Probe subject=%1 plots=%2 det=%3 maxSnr=%4 running=%5 mti=%6 azBw=%7 demClutter=%8 status=%9",
            subject.GetOrigin().ToString(),
            plotN.ToString(),
            detN.ToString(),
            maxSnr.ToString(),
            RDF_RadarAutoRunner.IsRunning().ToString(),
            mti.ToString(),
            azBw.ToString(),
            demClutter.ToString(),
            sensor.GetStatusShort()));
        Print(string.Format(
            "[RDF ManualDemo] Probe world candidates=%1 vehicles=%2 vehicles<2km=%3 projectiles=%4 serial=%5",
            activeN.ToString(),
            vehicleN.ToString(),
            nearVehicleN.ToString(),
            projectileN.ToString(),
            sensor.GetScanSerial().ToString()));

        if (plots)
        {
            int printed = 0;
            foreach (RDF_RadarTarget t : plots)
            {
                if (!t || printed >= 8)
                    continue;
                Print(string.Format(
                    "[RDF ManualDemo] plot det=%1 snr=%2 dist=%3 type=%4 los=%5 beam=%6",
                    t.m_Detected.ToString(),
                    t.m_SnrDb.ToString(),
                    t.m_Distance.ToString(),
                    t.m_Type.ToString(),
                    t.m_LosBlocked.ToString(),
                    t.m_BeamName));
                printed = printed + 1;
            }
        }
        else if (nearVehicleN > 0)
        {
            Print("[RDF ManualDemo] vehicles nearby but no plots — check LOS / sector / IncludeVehicles", LogLevel.WARNING);
        }
    }

    protected static void CountNearbyCandidates(
        IEntity subject,
        out int vehicleN,
        out int nearVehicleN,
        out int projectileN,
        out int activeN)
    {
        vehicleN = 0;
        nearVehicleN = 0;
        projectileN = 0;
        activeN = 0;
        if (!subject)
            return;

        BaseWorld world = subject.GetWorld();
        if (!world)
            world = GetGame().GetWorld();
        if (!world)
            return;

        if (!s_ProbeScratch)
            s_ProbeScratch = new array<IEntity>();
        s_ProbeScratch.Clear();

        // ALL catches editor-placed static vehicles that DYNAMIC misses.
        world.QueryEntitiesBySphere(
            subject.GetOrigin(),
            2000.0,
            OnProbeEntity,
            null,
            EQueryEntitiesFlags.ALL);
        activeN = s_ProbeScratch.Count();

        vector origin = subject.GetOrigin();
        for (int i = 0; i < s_ProbeScratch.Count(); i++)
        {
            IEntity ent = s_ProbeScratch.Get(i);
            if (!ent || ent == subject)
                continue;
            if (RDF_RadarEntityClassifier.IsProjectile(ent))
            {
                projectileN = projectileN + 1;
                continue;
            }
            if (!RDF_RadarEntityClassifier.IsVehicleOrCharacter(ent))
                continue;
            if (ChimeraCharacter.Cast(ent))
                continue;
            vehicleN = vehicleN + 1;
            float d = vector.Distance(ent.GetOrigin(), origin);
            if (d < 2000.0)
                nearVehicleN = nearVehicleN + 1;
        }
    }

    protected static bool OnProbeEntity(IEntity entity)
    {
        if (!entity)
            return true;
        // Only keep radar-relevant entities so the count is not every bush in 2 km.
        if (!RDF_RadarEntityClassifier.IsRadarCandidate(entity))
            return true;
        if (!s_ProbeScratch)
            s_ProbeScratch = new array<IEntity>();
        s_ProbeScratch.Insert(entity);
        return true;
    }
}
