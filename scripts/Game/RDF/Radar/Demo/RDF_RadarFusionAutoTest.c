// Datalink + fusion regression (Script Debugger):
//   RDF_RadarFusionAutoTest.Start();
// Standalone — not part of StartAll.
// Feeds two station summaries into RDF_RadarDatalinkHub and checks one fused track.
class RDF_RadarFusionAutoTest
{
    protected static ref RDF_RadarFusionAutoTest s_Instance;

    static RDF_RadarFusionAutoTest GetInstance()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarFusionAutoTest();
        return s_Instance;
    }

    static void Start()
    {
        GetInstance().Run();
    }

    protected void Run()
    {
        Print("[RDF FusionTest] start");
        RDF_RadarDatalinkHub.ResetForTests();
        RDF_RadarFusionService.ResetForTests();

        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        hub.SetEnabled(true);
        hub.SetAutoFusion(true);
        hub.SetTrackTtlS(5.0);
        hub.SetIffResolver(new RDF_RadarForceFoeIffResolver());

        RDF_RadarFusionService.Get().SetAssocGateM(400.0);
        RDF_RadarFusionService.Get().SetMinCrossFixAngleDeg(15.0);

        float tNow = 10.0;
        vector targetPos = Vector(1000.0, 150.0, 1000.0);
        vector targetVel = Vector(20.0, 0.0, 0.0);

        // Station A at origin, bearing ~45° to target.
        array<ref RDF_RadarDatalinkTrack> batchA = new array<ref RDF_RadarDatalinkTrack>();
        RDF_RadarDatalinkTrack a = new RDF_RadarDatalinkTrack();
        a.m_LocalTrackId = 1;
        a.m_WorldPos = targetPos + Vector(15.0, 0.0, -10.0);
        a.m_Velocity = targetVel;
        a.m_RangeM = 1414.0;
        a.m_AzimuthDeg = 45.0;
        a.m_ElevationDeg = 5.0;
        a.m_SnrDb = 18.0;
        a.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
        a.m_Iff = ERDF_RadarIff.RDF_IFF_FOE;
        a.m_RadarOrigin = Vector(0.0, 20.0, 0.0);
        a.m_TimeS = tNow;
        batchA.Insert(a);
        hub.PublishFromRadar(101, a.m_RadarOrigin, tNow, batchA);

        // Station B at (0, 2000), bearing ~-45°.
        array<ref RDF_RadarDatalinkTrack> batchB = new array<ref RDF_RadarDatalinkTrack>();
        RDF_RadarDatalinkTrack b = new RDF_RadarDatalinkTrack();
        b.m_LocalTrackId = 7;
        b.m_WorldPos = targetPos + Vector(-20.0, 0.0, 12.0);
        b.m_Velocity = targetVel + Vector(1.0, 0.0, 0.0);
        b.m_RangeM = 1414.0;
        b.m_AzimuthDeg = -45.0;
        b.m_ElevationDeg = 5.0;
        b.m_SnrDb = 16.0;
        b.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
        b.m_Iff = ERDF_RadarIff.RDF_IFF_FOE;
        b.m_RadarOrigin = Vector(0.0, 20.0, 2000.0);
        b.m_TimeS = tNow;
        batchB.Insert(b);
        hub.PublishFromRadar(202, b.m_RadarOrigin, tNow, batchB);

        array<ref RDF_RadarDatalinkTrack> all = hub.GetAllTracks();
        int nSrc = 0;
        if (all)
            nSrc = all.Count();

        array<ref RDF_RadarFusedTrack> fused = hub.GetFusedTracks();
        int nFused = 0;
        if (fused)
            nFused = fused.Count();

        bool passCount = false;
        if (nSrc == 2 && nFused == 1)
            passCount = true;

        bool passAssoc = false;
        bool passCross = false;
        bool passIff = false;
        if (passCount)
        {
            RDF_RadarFusedTrack f0 = fused.Get(0);
            if (f0 && f0.m_ContributorCount == 2)
                passAssoc = true;
            if (f0 && f0.m_CrossFixUsed)
            {
                float dx = f0.m_WorldPos[0] - targetPos[0];
                float dz = f0.m_WorldPos[2] - targetPos[2];
                float err = Math.Sqrt(dx * dx + dz * dz);
                if (err < 50.0)
                    passCross = true;
            }
            if (f0 && f0.m_Iff == ERDF_RadarIff.RDF_IFF_FOE)
                passIff = true;
        }

        Print(string.Format(
            "[RDF FusionTest] hub=%1 src=%2 fused=%3 assoc=%4 cross=%5 iff=%6",
            hub.GetStatusShort(),
            nSrc,
            nFused,
            passAssoc.ToString(),
            passCross.ToString(),
            passIff.ToString()));

        bool pass = false;
        if (passCount && passAssoc && passCross && passIff)
            pass = true;

        if (pass)
            Print("[RDF FusionTest] PASS");
        else
            Print("[RDF FusionTest] FAIL", LogLevel.WARNING);

        RDF_RadarDatalinkHub.ResetForTests();
        RDF_RadarFusionService.ResetForTests();

        RunNeutralMerge();
    }

    // Regression for the three-state IFF merge (NEUTRAL distinct from UNKNOWN):
    // two stations reporting the same target with NEUTRAL + FRIEND / NEUTRAL +
    // FOE must not collapse to UNKNOWN. Guards RDF_RadarFusionService.MergeIff.
    protected void RunNeutralMerge()
    {
        Print("[RDF FusionTest] neutral-merge regression");
        RDF_RadarDatalinkHub.ResetForTests();
        RDF_RadarFusionService.ResetForTests();

        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        hub.SetEnabled(true);
        hub.SetAutoFusion(true);
        hub.SetTrackTtlS(5.0);
        RDF_RadarFusionService.Get().SetAssocGateM(400.0);

        array<ERDF_RadarIff> iffA = {
            ERDF_RadarIff.RDF_IFF_NEUTRAL,
            ERDF_RadarIff.RDF_IFF_NEUTRAL,
            ERDF_RadarIff.RDF_IFF_FRIEND
        };
        array<ERDF_RadarIff> iffB = {
            ERDF_RadarIff.RDF_IFF_FRIEND,
            ERDF_RadarIff.RDF_IFF_FOE,
            ERDF_RadarIff.RDF_IFF_FOE
        };
        array<ERDF_RadarIff> want = {
            ERDF_RadarIff.RDF_IFF_FRIEND,
            ERDF_RadarIff.RDF_IFF_FOE,
            ERDF_RadarIff.RDF_IFF_UNKNOWN
        };

        int pass = 0;
        for (int c = 0; c < iffA.Count(); c++)
        {
            hub.Clear();
            array<ref RDF_RadarDatalinkTrack> batchA = new array<ref RDF_RadarDatalinkTrack>();
            RDF_RadarDatalinkTrack a = new RDF_RadarDatalinkTrack();
            a.m_LocalTrackId = 1;
            a.m_WorldPos = Vector(1000.0, 150.0, 1000.0);
            a.m_Velocity = Vector(20.0, 0.0, 0.0);
            a.m_AzimuthDeg = 45.0;
            a.m_RangeM = 1414.0;
            a.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
            a.m_Iff = iffA.Get(c);
            a.m_RadarOrigin = Vector(0.0, 20.0, 0.0);
            a.m_TimeS = 10.0;
            batchA.Insert(a);
            hub.PublishFromRadar(101, a.m_RadarOrigin, 10.0, batchA);

            array<ref RDF_RadarDatalinkTrack> batchB = new array<ref RDF_RadarDatalinkTrack>();
            RDF_RadarDatalinkTrack b = new RDF_RadarDatalinkTrack();
            b.m_LocalTrackId = 7;
            b.m_WorldPos = Vector(1008.0, 150.0, 1002.0);
            b.m_Velocity = Vector(20.0, 0.0, 0.0);
            b.m_AzimuthDeg = -45.0;
            b.m_RangeM = 1414.0;
            b.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
            b.m_Iff = iffB.Get(c);
            b.m_RadarOrigin = Vector(0.0, 20.0, 2000.0);
            b.m_TimeS = 10.0;
            batchB.Insert(b);
            hub.PublishFromRadar(202, b.m_RadarOrigin, 10.0, batchB);

            bool ok = false;
            array<ref RDF_RadarFusedTrack> fused = hub.GetFusedTracks();
            if (fused && fused.Count() >= 1)
            {
                if (fused.Get(0).m_Iff == want.Get(c))
                    ok = true;
            }

            if (ok)
                pass = pass + 1;
            Print(string.Format(
                "[RDF FusionTest]   merge{0}/3 {1}+{2} -> {3} ok={4}",
                c + 1,
                IffName(iffA.Get(c)),
                IffName(iffB.Get(c)),
                IffName(want.Get(c)),
                ok.ToString()));
        }

        bool passAll = pass == iffA.Count();
        if (passAll)
            Print("[RDF FusionTest]   neutral-merge PASS (" + pass.ToString() + "/" + iffA.Count().ToString() + ")");
        else
            Print("[RDF FusionTest]   neutral-merge FAIL (" + pass.ToString() + "/" + iffA.Count().ToString() + ")", LogLevel.WARNING);
    }

    protected string IffName(ERDF_RadarIff iff)
    {
        if (iff == ERDF_RadarIff.RDF_IFF_FRIEND)
            return "FRIEND";
        if (iff == ERDF_RadarIff.RDF_IFF_FOE)
            return "FOE";
        if (iff == ERDF_RadarIff.RDF_IFF_NEUTRAL)
            return "NEUTRAL";
        return "UNKNOWN";
    }
}

// Demo IFF resolver: always FOE (for fusion / datalink tests).
class RDF_RadarForceFoeIffResolver : RDF_RadarIffResolver
{
    override ERDF_RadarIff Resolve(IEntity radarSubject, RDF_RadarTrack track)
    {
        return ERDF_RadarIff.RDF_IFF_FOE;
    }
}
