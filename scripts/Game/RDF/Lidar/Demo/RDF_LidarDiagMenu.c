// Workbench DiagMenu entries for LiDAR demo / Sensor presets.
// IDs sit in a high private range to avoid colliding with SCR_DebugMenuID.
enum ERDF_LidarDiagMenuId
{
    RDF_LIDAR_DIAG_MENU = 5800100,
    RDF_LIDAR_DIAG_DEMO_ENABLED,
    RDF_LIDAR_DIAG_MODE,
    RDF_LIDAR_DIAG_RAYS,
    RDF_LIDAR_DIAG_RANGE,
    RDF_LIDAR_DIAG_SHOWCASE,
    RDF_LIDAR_DIAG_HUD,
    RDF_LIDAR_DIAG_VERBOSE
}

class RDF_LidarDiagMenu
{
    protected static bool s_Registered = false;
    protected static bool s_HaveDemoEnabledSnapshot = false;
    protected static bool s_LastDemoEnabled = false;
    protected static int s_LastAppliedMode = -1;
    protected static int s_LastAppliedRays = -1;
    protected static float s_LastAppliedRange = -1.0;
    protected static int s_LastShowcase = -1;
    protected static int s_LastHud = -1;
    protected static int s_LastVerbose = -1;

    static void EnsureRegistered()
    {
        if (s_Registered)
            return;

        DiagMenu.RegisterMenu(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_MENU, "RDF LiDAR", "");
        DiagMenu.RegisterBool(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_DEMO_ENABLED,
            "",
            "Demo enabled",
            "RDF LiDAR");
        DiagMenu.RegisterItem(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_MODE,
            "",
            "Sensor mode",
            "RDF LiDAR",
            "FULL,CONE,RECT,SWEEP,ENTS");
        DiagMenu.RegisterRange(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RAYS,
            "",
            "Ray count",
            "RDF LiDAR",
            "64,4096,512,64");
        DiagMenu.RegisterRange(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RANGE,
            "",
            "Range m",
            "RDF LiDAR",
            "5,500,50,5");
        DiagMenu.RegisterBool(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_SHOWCASE,
            "",
            "Showcase world draw",
            "RDF LiDAR");
        DiagMenu.RegisterBool(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_HUD,
            "",
            "PPI HUD",
            "RDF LiDAR");
        DiagMenu.RegisterBool(
            ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_VERBOSE,
            "",
            "Verbose log",
            "RDF LiDAR");

        DiagMenu.SetValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_MODE, 0);
        DiagMenu.SetRangeValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RAYS, 512.0);
        DiagMenu.SetRangeValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RANGE, 50.0);
        s_Registered = true;
    }

    static bool IsRegistered()
    {
        return s_Registered;
    }

    static ERDF_LidarSensorMode ModeFromDiagValue(int value)
    {
        if (value == 1)
            return ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_CONE;
        if (value == 2)
            return ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_RECT;
        if (value == 3)
            return ERDF_LidarSensorMode.RDF_LIDAR_MODE_SWEEP;
        if (value == 4)
            return ERDF_LidarSensorMode.RDF_LIDAR_MODE_ENTITIES_NEAR;
        return ERDF_LidarSensorMode.RDF_LIDAR_MODE_FULL_SPHERE;
    }

    // Push DiagMenu knobs onto a Sensor (mod / unit-test consumers).
    static void ApplyToSensor(RDF_LidarSensor sensor)
    {
        if (!sensor || !s_Registered)
            return;

        int modeVal = DiagMenu.GetValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_MODE);
        int rays = Math.Round(
            DiagMenu.GetRangeValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RAYS));
        float rangeM = DiagMenu.GetRangeValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RANGE);

        ERDF_LidarSensorMode mode = ModeFromDiagValue(modeVal);
        if (modeVal != s_LastAppliedMode || rays != s_LastAppliedRays)
        {
            sensor.ConfigureMode(mode, rays);
            s_LastAppliedMode = modeVal;
            s_LastAppliedRays = rays;
        }

        RDF_LidarSettings settings = sensor.GetSettings();
        if (settings && Math.AbsFloat(rangeM - s_LastAppliedRange) > 0.01)
        {
            settings.m_Range = rangeM;
            settings.Validate();
            s_LastAppliedRange = rangeM;
        }

        bool demoOn = DiagMenu.GetBool(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_DEMO_ENABLED);
        if (!s_HaveDemoEnabledSnapshot)
        {
            s_LastDemoEnabled = demoOn;
            s_HaveDemoEnabledSnapshot = true;
        }
        else if (demoOn != s_LastDemoEnabled)
        {
            sensor.SetEnabled(demoOn);
            s_LastDemoEnabled = demoOn;
        }
    }

    // Push DiagMenu knobs onto the demo AutoRunner (Workbench Play).
    // Edge-triggered: default false Bools do not kill an already-running demo.
    static void ApplyToAutoRunner()
    {
        if (!s_Registered)
            return;

        bool demoOn = DiagMenu.GetBool(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_DEMO_ENABLED);
        if (!s_HaveDemoEnabledSnapshot)
        {
            s_LastDemoEnabled = demoOn;
            s_HaveDemoEnabledSnapshot = true;
        }
        else if (demoOn != s_LastDemoEnabled)
        {
            RDF_LidarAutoRunner.SetDemoEnabled(demoOn, false);
            s_LastDemoEnabled = demoOn;
        }

        if (!RDF_LidarAutoRunner.IsDemoEnabled())
            return;

        int modeVal = DiagMenu.GetValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_MODE);
        int rays = Math.Round(
            DiagMenu.GetRangeValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RAYS));
        float rangeM = DiagMenu.GetRangeValue(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_RANGE);
        bool showcase = DiagMenu.GetBool(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_SHOWCASE);
        bool hud = DiagMenu.GetBool(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_HUD);
        bool verbose = DiagMenu.GetBool(ERDF_LidarDiagMenuId.RDF_LIDAR_DIAG_VERBOSE);

        // First pass: snapshot Diag defaults without stomping bootstrap/config.
        if (s_LastAppliedMode < 0)
        {
            s_LastAppliedMode = modeVal;
            s_LastAppliedRays = rays;
            s_LastAppliedRange = rangeM;
            s_LastShowcase = 0;
            if (showcase)
                s_LastShowcase = 1;
            s_LastHud = 0;
            if (hud)
                s_LastHud = 1;
            s_LastVerbose = 0;
            if (verbose)
                s_LastVerbose = 1;
            return;
        }

        if (modeVal != s_LastAppliedMode || rays != s_LastAppliedRays)
        {
            ERDF_LidarSensorMode mode = ModeFromDiagValue(modeVal);
            RDF_LidarSampleStrategy strategy = BuildStrategyForMode(mode);
            if (strategy)
                RDF_LidarAutoRunner.SetDemoSampleStrategy(strategy);
            RDF_LidarAutoRunner.SetDemoRayCount(rays);
            if (modeVal == 4)
                RDF_LidarAutoRunner.SetDemoTraceTargetMode(2);
            else
                RDF_LidarAutoRunner.SetDemoTraceTargetMode(1);
            s_LastAppliedMode = modeVal;
            s_LastAppliedRays = rays;
        }

        if (Math.AbsFloat(rangeM - s_LastAppliedRange) > 0.01)
        {
            RDF_LidarAutoRunner.SetDemoRange(rangeM);
            s_LastAppliedRange = rangeM;
        }

        int showcaseInt = 0;
        if (showcase)
            showcaseInt = 1;
        if (showcaseInt != s_LastShowcase)
        {
            RDF_LidarAutoRunner.SetDemoUseShowcase(showcase);
            s_LastShowcase = showcaseInt;
        }

        int hudInt = 0;
        if (hud)
            hudInt = 1;
        if (hudInt != s_LastHud)
        {
            RDF_LidarAutoRunner.SetDemoHudEnabled(hud);
            s_LastHud = hudInt;
        }

        int verboseInt = 0;
        if (verbose)
            verboseInt = 1;
        if (verboseInt != s_LastVerbose)
        {
            RDF_LidarAutoRunner.SetDemoVerbose(verbose);
            s_LastVerbose = verboseInt;
        }
    }

    protected static RDF_LidarSampleStrategy BuildStrategyForMode(ERDF_LidarSensorMode mode)
    {
        if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_CONE)
            return new RDF_ConicalSampleStrategy(45.0);
        if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_FORWARD_RECT)
            return new RDF_RectangularFOVSampleStrategy(120.0, 25.4, 120, 32);
        if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_SWEEP)
            return new RDF_SweepSampleStrategy(30.0, 20.0, 45.0);
        if (mode == ERDF_LidarSensorMode.RDF_LIDAR_MODE_ENTITIES_NEAR)
            return new RDF_ConicalSampleStrategy(35.0);
        return new RDF_UniformSampleStrategy();
    }
}
