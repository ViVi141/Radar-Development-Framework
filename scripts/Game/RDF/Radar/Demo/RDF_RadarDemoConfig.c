// Preset factory for radar demo config (RDF_RadarSettings).
class RDF_RadarDemoConfig
{
    static RDF_RadarSettings CreateDefault(int maxTargets = 64)
    {
        RDF_RadarSettings s = new RDF_RadarSettings();
        s.m_Range = 2000.0;
        s.m_UpdateInterval = 0.2;
        s.m_SectorHalfAngleDeg = 45.0;
        s.m_MaxTargets = maxTargets;
        s.m_IncludeVehicles = true;
        s.m_IncludeProjectiles = true;
        s.m_IncludeRadarEmitters = true;
        s.m_Hardware = RDF_RadarHardware.CreateShorad();
        s.m_EnablePhysicalDetection = true;
        s.m_DetectionSnrDb = 8.0;
        s.Validate();
        return s;
    }

    static RDF_RadarSettings CreateP18Like(int maxTargets = 128)
    {
        RDF_RadarSettings s = CreateDefault(maxTargets);
        s.m_Range = 13000.0;
        s.m_UpdateInterval = 0.1;
        s.m_SectorHalfAngleDeg = 180.0;
        s.m_Hardware = RDF_RadarHardware.CreateP18Like();
        s.m_EnableMechanicalScan = true;
        s.m_DetectionSnrDb = 6.0;
        s.Validate();
        return s;
    }

    static RDF_RadarSettings CreateLongRange(float range = 5000.0, int maxTargets = 32)
    {
        RDF_RadarSettings s = CreateDefault(maxTargets);
        s.m_Range = range;
        s.m_UpdateInterval = 0.5;
        s.m_SectorHalfAngleDeg = 30.0;
        s.Validate();
        return s;
    }

    static RDF_RadarSettings CreateProjectileOnly(int maxTargets = 128)
    {
        RDF_RadarSettings s = CreateDefault(maxTargets);
        s.m_IncludeVehicles = false;
        s.m_IncludeRadarEmitters = false;
        s.m_IncludeProjectiles = true;
        s.m_SectorHalfAngleDeg = 60.0;
        s.Validate();
        return s;
    }
}
