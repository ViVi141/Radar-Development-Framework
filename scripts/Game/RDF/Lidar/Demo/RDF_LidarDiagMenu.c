// LiDAR demo / Sensor preset helpers.
// NOTE: Engine DiagMenu hard-caps at 512 entries (id must be < 512).
// SCR_DebugMenuID already occupies that table; registering private high IDs
// (e.g. 5800100) crashes with "Too many Diags (maximum is 512)".
// DiagMenu registration is therefore disabled for workshop-safe play.
// Use RDF_LidarSensor.ConfigureMode / RDF_LidarDemoConfig / AutoRunner API instead.
class RDF_LidarDiagMenu
{
    protected static bool s_DisabledLogged = false;

    // No-op: never calls DiagMenu.Register* (engine 512-slot limit).
    static void EnsureRegistered()
    {
        if (s_DisabledLogged)
            return;
        s_DisabledLogged = true;
        Print(
            "[RDF LiDAR] DiagMenu disabled (engine max 512 diags; use LidarSensor / DemoConfig).",
            LogLevel.NORMAL);
    }

    static bool IsRegistered()
    {
        return false;
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

    static void ApplyToSensor(RDF_LidarSensor sensor)
    {
    }

    static void ApplyToAutoRunner()
    {
    }
}
