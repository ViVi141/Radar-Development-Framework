// One-shot manual PPI demo for placing vehicles and verifying physical radar
// detection (SNR / LOS / DEM clutter). Not geometry-only.
//
// Uses RDF_RadarSensor for configure / read; AutoRunner only hosts Tick + HUD.
//
// Usage (Script Debugger):
//   RDF_RadarManualDemo.Start();
//   RDF_RadarManualDemo.Probe();   // print why Det is 0/N
//   RDF_RadarManualDemo.Stop();
class RDF_RadarManualDemo
{
    protected static ref RDF_RadarSettings s_PrevConfig;
    protected static bool s_PrevDemo;
    protected static bool s_PrevHud;
    protected static bool s_PrevForceLocal;
    protected static bool s_Active;

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
        RDF_RadarAutoRunner.SetDemoEnabled(true);
        RDF_RadarAutoRunner.SetHudEnabled(true);
        RDF_RadarHUD.Show();
        s_Active = true;

        Print("[RDF ManualDemo] started physical SEARCH (MTI off, omni beam, world search)");
        Probe();
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
        cfg.m_IncludeProjectiles = false;
        cfg.m_IncludeRadarEmitters = false;
        cfg.m_EnablePhysicalDetection = true;
        cfg.m_DetectionSnrDb = 8.0;
        cfg.m_EnableDemClutter = true;
        cfg.m_EnableCfarGate = false;
        cfg.m_UseScattererRegistry = false;
        cfg.m_UseSphereQuery = true;
        cfg.m_SphereQueryAlsoActive = true;
        cfg.m_MaxLosTracesPerScan = 128;
        cfg.m_KeepUndetected = false;
        cfg.m_KeepEntityTruth = false;
        cfg.m_OriginOffset = Vector(0.0, 12.0, 0.0);

        RDF_RadarHardware hw = RDF_RadarHardware.CreateShorad();
        hw.m_AzimuthBeamwidthDeg = 360.0;
        hw.m_ScanRpm = 0.0;
        hw.m_EnableMti = false;
        hw.ClearElevationBeams();
        hw.AddElevationBeam("manual_omni", 0.0, 90.0, 0.0);
        hw.Validate();
        cfg.m_Hardware = hw;
        cfg.Validate();
        return cfg;
    }

    // Dump live scan / world state so Det 0/0 is diagnosable.
    static void Probe()
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

        RDF_RadarSettings cfg = sensor.GetSettings();
        bool mti = false;
        float azBw = -1.0;
        if (cfg && cfg.m_Hardware)
        {
            mti = cfg.m_Hardware.m_EnableMti;
            azBw = cfg.m_Hardware.m_AzimuthBeamwidthDeg;
        }

        array<ref RDF_RadarTarget> plots = sensor.GetPlots();
        int plotN = 0;
        int detN = sensor.CountDetectedPlots();
        if (plots)
            plotN = plots.Count();

        BaseWorld world = GetGame().GetWorld();
        int activeN = 0;
        int vehicleN = 0;
        int nearVehicleN = 0;
        if (world)
        {
            array<IEntity> active = new array<IEntity>();
            world.GetActiveEntities(active);
            activeN = active.Count();
            vector origin = subject.GetOrigin();
            for (int i = 0; i < active.Count(); i++)
            {
                IEntity ent = active.Get(i);
                if (!ent || ent == subject)
                    continue;
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

        Print(string.Format(
            "[RDF ManualDemo] Probe subject=%1 plots=%2 det=%3 forceLocal=%4 mti=%5 azBw=%6 registry=%7 physical=%8 status=%9",
            subject.GetOrigin().ToString(),
            plotN.ToString(),
            detN.ToString(),
            sensor.IsForceLocalScan().ToString(),
            mti.ToString(),
            azBw.ToString(),
            (cfg && cfg.m_UseScattererRegistry).ToString(),
            (cfg && cfg.m_EnablePhysicalDetection).ToString(),
            sensor.GetStatusShort()));
        Print(string.Format(
            "[RDF ManualDemo] Probe world active=%1 vehicles=%2 vehicles<2km=%3 serial=%4",
            activeN.ToString(),
            vehicleN.ToString(),
            nearVehicleN.ToString(),
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
    }
}
