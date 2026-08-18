// Preset factory for radar demo config (RDF_RadarSettings).
class RDF_RadarDemoConfig
{
    static RDF_RadarSettings CreateDefault(int maxTargets = 64)
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
        s.m_EnablePhysicalDetection = true;
        s.m_DetectionSnrDb = 8.0;
        s.Validate();
        return s;
    }

    static RDF_RadarSettings CreateP18Like(int maxTargets = 128)
    {
        RDF_RadarSettings s = CreateDefault(maxTargets);
        s.m_Range = 13000.0;
        s.m_UpdateInterval = 1.0;
        s.m_SectorHalfAngleDeg = 180.0;
        s.m_MaxLosTracesPerScan = 32;
        s.m_Hardware = RDF_RadarHardware.CreateP18Like();
        s.m_EnableMechanicalScan = true;
        s.m_DetectionSnrDb = 6.0;
        s.Validate();
        return s;
    }

    // VHF search + X-band track/fire-control on the same platform. Two antennas
    // (do not ScaleApertureToFrequency between them). One carrier per scan:
    // SEARCH uses VHF unless a TRACK/FIRE_CONTROL dwell is scheduled.
    static RDF_RadarSettings CreateDualBandVhfSearchXTrack(int maxTargets = 96)
    {
        RDF_RadarSettings s = CreateP18Like(maxTargets);
        s.m_EnableMultiBand = true;
        s.m_BandChannels = new array<ref RDF_RadarHardware>();
        s.m_BandChannels.Insert(s.m_Hardware);
        RDF_RadarHardware xBand = RDF_RadarHardware.CreateShorad();
        s.m_BandChannels.Insert(xBand);
        s.m_SearchBandIndex = 0;
        s.m_TrackBandIndex = 1;
        s.m_FireControlBandIndex = 1;
        s.m_EnableDwellScheduler = true;
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

    static RDF_RadarSettings CreateSearch(int maxTargets = 64)
    {
        return RDF_RadarSensor.CreateSearchSettings(maxTargets);
    }

    static RDF_RadarSettings CreateStare(int maxTargets = 96)
    {
        return RDF_RadarSensor.CreateStareSettings(maxTargets);
    }

    static RDF_RadarSettings CreateWlr(int maxTargets = 128)
    {
        return RDF_RadarSensor.CreateWlrSettings(maxTargets);
    }

    static RDF_RadarSettings CreateEsm(int maxTargets = 64)
    {
        return RDF_RadarSensor.CreateEsmSettings(maxTargets);
    }

    static RDF_RadarSettings CreatePulseDoppler(int maxTargets = 96)
    {
        return RDF_RadarSensor.CreatePulseDopplerSettings(maxTargets);
    }

    static RDF_RadarSettings CreateWithDeceptionJammer(int maxTargets = 64)
    {
        RDF_RadarSettings s = CreateDefault(maxTargets);
        RDF_RadarDeceptionJammerEffect jammer = new RDF_RadarDeceptionJammerEffect();
        jammer.AddFalsePlot(1600.0, -18.0, 0.0000000000012, 25.0, 0.0);
        jammer.AddFalsePlot(2200.0, 12.0, 0.0000000000009, -10.0, 0.0);
        s.m_EwStack.Add(jammer);
        s.Validate();
        return s;
    }
}
