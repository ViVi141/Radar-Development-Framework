// Synchronous inverse-path regression: frozen anonymous observations → tracks.
// No world Trace / scatterer entity. Proves Tracker does not need TruthSample.
// Usage (Script Debugger): RDF_RadarInverseTrackAutoTest.Start();
class RDF_RadarInverseTrackAutoTest
{
    protected static int s_Pass;
    protected static int s_Fail;
    protected static bool s_LastPass;

    static void Start()
    {
        s_Pass = 0;
        s_Fail = 0;
        Print("[RDF InverseTrack AutoTest] begin");

        TestFrozenPlotsConfirmTrack();
        TestFalsePlotSeedsTrack();

        Print(string.Format(
            "[RDF InverseTrack AutoTest] done: pass=%1 fail=%2",
            s_Pass.ToString(),
            s_Fail.ToString()));
        s_LastPass = s_Fail == 0;
        if (s_LastPass)
            Print("[RDF InverseTrack AutoTest] PASS");
        else
            Print("[RDF InverseTrack AutoTest] FAIL", LogLevel.ERROR);
    }

    static bool DidLastPass()
    {
        return s_LastPass;
    }

    protected static void ExpectTrue(string name, bool ok)
    {
        if (ok)
        {
            s_Pass = s_Pass + 1;
            Print("[RDF InverseTrack AutoTest] PASS  " + name);
            return;
        }
        s_Fail = s_Fail + 1;
        Print("[RDF InverseTrack AutoTest] FAIL  " + name, LogLevel.ERROR);
    }

    protected static RDF_RadarTarget MakeObs(
        float timeS,
        float rangeM,
        float azDeg,
        float rangeRateMs,
        float snrDb,
        bool falsePlot)
    {
        float azRad = azDeg * 0.0174532925199;
        vector los = Vector(Math.Cos(azRad), 0.0, Math.Sin(azRad));
        RDF_RadarTarget t = new RDF_RadarTarget();
        t.m_Entity = null;
        t.m_ScattererId = 0;
        t.m_Position = los * rangeM;
        t.m_Distance = rangeM;
        t.m_Velocity = los * (-rangeRateMs);
        t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        t.m_Time = timeS;
        t.m_AzimuthDeg = azDeg;
        t.m_ElevationDeg = 0.0;
        t.m_RadialSpeedMs = rangeRateMs;
        t.m_SnrDb = snrDb;
        t.m_Detected = true;
        t.m_IsAnonymous = true;
        t.m_IsFalsePlot = falsePlot;
        t.m_LosHitFraction = 1.0;
        t.m_MultipathFactor = 1.0;
        t.m_BeamName = "frozen";
        t.m_ScanNumber = Math.Floor(timeS);
        return t;
    }

    protected static void TestFrozenPlotsConfirmTrack()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreateSearchSettings(16);
        settings.m_KeepEntityTruth = false;
        settings.m_TrackConfirmHits = 3;
        settings.m_TrackGateRangeM = 200.0;
        settings.m_TrackGateAzimuthDeg = 6.0;

        RDF_RadarProjectileTracker tracker = new RDF_RadarProjectileTracker();
        tracker.ConfigureFromSettings(settings);

        vector origin = "0 0 0";
        for (int scan = 1; scan <= 5; scan++)
        {
            float t = scan * 2.0;
            float rng = 4000.0 - 40.0 * (scan - 1);
            array<ref RDF_RadarTarget> plots = new array<ref RDF_RadarTarget>();
            plots.Insert(MakeObs(t, rng, 10.0, 20.0, 18.0, false));
            tracker.UpdateWithOrigin(plots, t, origin);
        }

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        ExpectTrue("frozen_has_tracks", tracks && tracks.Count() > 0);
        bool confirmed = false;
        bool noEntity = true;
        if (tracks)
        {
            for (int i = 0; i < tracks.Count(); i++)
            {
                RDF_RadarTrack tr = tracks.Get(i);
                if (!tr)
                    continue;
                if (tr.m_Entity)
                    noEntity = false;
                if (tr.m_Confirmed)
                    confirmed = true;
            }
        }
        ExpectTrue("frozen_confirmed", confirmed);
        ExpectTrue("frozen_no_entity", noEntity);
    }

    protected static void TestFalsePlotSeedsTrack()
    {
        RDF_RadarSettings settings = RDF_RadarSensor.CreateSearchSettings(16);
        settings.m_TrackConfirmHits = 2;
        settings.m_TrackGateRangeM = 250.0;
        settings.m_TrackGateAzimuthDeg = 8.0;

        RDF_RadarProjectileTracker tracker = new RDF_RadarProjectileTracker();
        tracker.ConfigureFromSettings(settings);

        vector origin = "0 0 0";
        for (int scan = 1; scan <= 4; scan++)
        {
            float t = scan * 1.5;
            array<ref RDF_RadarTarget> plots = new array<ref RDF_RadarTarget>();
            plots.Insert(MakeObs(t, 2500.0, -5.0, 0.0, 14.0, true));
            tracker.UpdateWithOrigin(plots, t, origin);
        }

        array<ref RDF_RadarTrack> tracks = tracker.GetAllTracks();
        ExpectTrue("false_plot_tracks", tracks && tracks.Count() > 0);
    }
}
