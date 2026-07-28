// Synchronous ballistics / WLR math regression (no world tick required).
// Usage (Script Debugger): RDF_RadarBallisticsAutoTest.Start();
class RDF_RadarBallisticsAutoTest
{
    protected static int s_Pass;
    protected static int s_Fail;

    static void Start()
    {
        s_Pass = 0;
        s_Fail = 0;
        // Synthetic XYZ tests assume a flat plane; live DEM/GetSurfaceY would skew them.
        RDF_RadarBallistics.SetUseDemGround(false);
        Print("[RDF Ballistics AutoTest] begin");

        TestFreefallImpactTime();
        TestDragShortensRange();
        TestWindShiftsImpact();
        TestBackwardRecoversLaunch();
        TestSolveLaunchAndImpactPair();
        TestTrackApexWeaponLocate();
        TestAspectRcsElevation();

        RDF_RadarBallistics.SetUseDemGround(true);

        Print(string.Format(
            "[RDF Ballistics AutoTest] done: pass=%1 fail=%2",
            s_Pass.ToString(),
            s_Fail.ToString()));
        if (s_Fail == 0)
            Print("[RDF Ballistics AutoTest] PASS");
        else
            Print("[RDF Ballistics AutoTest] FAIL", LogLevel.ERROR);

        WriteReport();
    }

    protected static void ExpectTrue(string name, bool ok)
    {
        if (ok)
        {
            s_Pass = s_Pass + 1;
            Print("[RDF Ballistics AutoTest] PASS  " + name);
            return;
        }
        s_Fail = s_Fail + 1;
        Print("[RDF Ballistics AutoTest] FAIL  " + name, LogLevel.ERROR);
    }

    protected static void ExpectNear(string name, float actual, float expected, float tol)
    {
        float err = Math.AbsFloat(actual - expected);
        if (err <= tol)
        {
            s_Pass = s_Pass + 1;
            Print(string.Format(
                "[RDF Ballistics AutoTest] PASS  %1 (actual=%2 expected=%3)",
                name,
                actual.ToString(),
                expected.ToString()));
            return;
        }
        s_Fail = s_Fail + 1;
        Print(string.Format(
            "[RDF Ballistics AutoTest] FAIL  %1 (actual=%2 expected=%3 tol=%4 err=%5)",
            name,
            actual.ToString(),
            expected.ToString(),
            tol.ToString(),
            err.ToString()), LogLevel.ERROR);
    }

    protected static RDF_RadarGlobalWind ZeroWind()
    {
        RDF_RadarGlobalWind wind = new RDF_RadarGlobalWind();
        wind.Set(0.0, 0.0);
        return wind;
    }

    // Vacuum freefall from rest: t = sqrt(2*h/g).
    protected static void TestFreefallImpactTime()
    {
        float h = 1000.0;
        float g = 9.81;
        float expectedT = Math.Sqrt(2.0 * h / g);
        vector pos = Vector(0.0, h, 0.0);
        vector vel = Vector(0.0, 0.0, 0.0);
        RDF_RadarGroundHit hit = RDF_RadarBallistics.FindGroundIntersection(
            pos, vel, 0.0, 0.0, ZeroWind(), false);
        ExpectTrue("freefall_hit_valid", hit && hit.m_Valid);
        if (hit && hit.m_Valid)
            ExpectNear("freefall_time_s", hit.m_TimeOffsetS, expectedT, 0.08);
    }

    // Same muzzle state: drag must land shorter than vacuum.
    protected static void TestDragShortensRange()
    {
        vector pos = Vector(0.0, 50.0, 0.0);
        vector vel = Vector(200.0, 80.0, 0.0);
        float groundY = 0.0;
        RDF_RadarGroundHit vac = RDF_RadarBallistics.FindGroundIntersection(
            pos, vel, groundY, 0.0, ZeroWind(), false);
        RDF_RadarGroundHit drag = RDF_RadarBallistics.FindGroundIntersection(
            pos,
            vel,
            groundY,
            RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE,
            ZeroWind(),
            false);
        ExpectTrue("drag_vac_valid", vac && vac.m_Valid);
        ExpectTrue("drag_drag_valid", drag && drag.m_Valid);
        if (!vac || !vac.m_Valid || !drag || !drag.m_Valid)
            return;
        float vacRange = Math.Sqrt(
            vac.m_Position[0] * vac.m_Position[0]
            + vac.m_Position[2] * vac.m_Position[2]);
        float dragRange = Math.Sqrt(
            drag.m_Position[0] * drag.m_Position[0]
            + drag.m_Position[2] * drag.m_Position[2]);
        ExpectTrue("drag_shorter_than_vacuum", dragRange < vacRange - 5.0);
    }

    // Crosswind must move impact along +Z when wind blows towards +Z.
    protected static void TestWindShiftsImpact()
    {
        vector pos = Vector(0.0, 80.0, 0.0);
        vector vel = Vector(180.0, 60.0, 0.0);
        RDF_RadarGlobalWind calm = ZeroWind();
        RDF_RadarGlobalWind cross = new RDF_RadarGlobalWind();
        cross.Set(12.0, 90.0);

        RDF_RadarGroundHit calmHit = RDF_RadarBallistics.FindGroundIntersection(
            pos,
            vel,
            0.0,
            RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE,
            calm,
            false);
        RDF_RadarGroundHit windHit = RDF_RadarBallistics.FindGroundIntersection(
            pos,
            vel,
            0.0,
            RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE,
            cross,
            false);
        ExpectTrue("wind_calm_valid", calmHit && calmHit.m_Valid);
        ExpectTrue("wind_cross_valid", windHit && windHit.m_Valid);
        if (!calmHit || !calmHit.m_Valid || !windHit || !windHit.m_Valid)
            return;
        float dz = windHit.m_Position[2] - calmHit.m_Position[2];
        ExpectTrue("wind_shifts_impact_plus_z", dz > 5.0);
    }

    // Forward-integrate from launch, then back-project must recover origin.
    protected static void TestBackwardRecoversLaunch()
    {
        vector launch = Vector(100.0, 0.0, -50.0);
        vector muzzle = Vector(150.0, 120.0, 40.0);
        float airDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
        RDF_RadarGlobalWind wind = ZeroWind();
        float gravity = RDF_RadarBallistics.GRAVITY_M_S2;

        vector p = launch;
        vector v = muzzle;
        float t = 0.0;
        float horizon = 8.0;
        float dt = RDF_RadarBallistics.DEFAULT_DT_S;
        while (t + 0.000000000001 < horizon)
        {
            float step = dt;
            if (t + step > horizon)
                step = horizon - t;
            vector nextP;
            vector nextV;
            RDF_RadarBallistics.IntegrateStep(p, v, step, airDrag, wind, gravity, nextP, nextV);
            p = nextP;
            v = nextV;
            t = t + step;
        }

        RDF_RadarGroundHit recovered = RDF_RadarBallistics.FindGroundIntersection(
            p, v, 0.0, airDrag, wind, true);
        ExpectTrue("backprop_valid", recovered && recovered.m_Valid);
        if (!recovered || !recovered.m_Valid)
            return;
        float dx = recovered.m_Position[0] - launch[0];
        float dz = recovered.m_Position[2] - launch[2];
        float err = Math.Sqrt(dx * dx + dz * dz);
        ExpectNear("backprop_launch_xy_err_m", err, 0.0, 25.0);
        ExpectNear("backprop_launch_y_m", recovered.m_Position[1], 0.0, 0.1);
    }

    protected static void TestSolveLaunchAndImpactPair()
    {
        vector launch = Vector(0.0, 0.0, 0.0);
        vector muzzle = Vector(220.0, 90.0, 0.0);
        float airDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
        RDF_RadarGlobalWind wind = ZeroWind();
        float gravity = RDF_RadarBallistics.GRAVITY_M_S2;

        vector p = launch;
        vector v = muzzle;
        float t = 0.0;
        float horizon = 6.0;
        float dt = RDF_RadarBallistics.DEFAULT_DT_S;
        while (t + 0.000000000001 < horizon)
        {
            float step = dt;
            if (t + step > horizon)
                step = horizon - t;
            vector nextP;
            vector nextV;
            RDF_RadarBallistics.IntegrateStep(p, v, step, airDrag, wind, gravity, nextP, nextV);
            p = nextP;
            v = nextV;
            t = t + step;
        }

        RDF_RadarWlrFix fix = RDF_RadarBallistics.SolveLaunchAndImpact(
            p, v, 0.0, 100.0, airDrag, wind);
        ExpectTrue("solve_launch_valid", fix && fix.m_LaunchValid);
        ExpectTrue("solve_impact_valid", fix && fix.m_ImpactValid);
        if (!fix || !fix.m_LaunchValid || !fix.m_ImpactValid)
            return;
        ExpectNear("solve_launch_time_s", fix.m_LaunchTimeS, 100.0 - 6.0, 0.2);
        ExpectTrue("solve_impact_after_anchor", fix.m_ImpactTimeS > 100.0);
        float launchErrX = Math.AbsFloat(fix.m_LaunchPos[0] - launch[0]);
        float launchErrZ = Math.AbsFloat(fix.m_LaunchPos[2] - launch[2]);
        ExpectTrue("solve_launch_near_origin", launchErrX < 20.0 && launchErrZ < 20.0);
    }

    // Track history multi-point fit feeds WLR (uses SampleGlobalWind).
    protected static void TestTrackApexWeaponLocate()
    {
        RDF_RadarTrack track = new RDF_RadarTrack();
        track.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
        track.m_AirDrag = RDF_RadarBallistics.AIR_DRAG_SHELL_82MM_HE;
        track.m_UseBallisticPrediction = true;
        track.m_HitCount = 6;
        track.m_Confirmed = true;
        track.m_WlrMinHits = 5;
        track.m_WlrMinSpanS = 1.0;
        track.m_WlrMaxFitRmsM = 120.0;
        track.m_WlrFitWindow = 20;
        track.m_WlrSmoothAlpha = 1.0;

        // Synthetic ascending→descending samples over 2.5 s (fit needs span).
        track.Push(Vector(0.0, 200.0, 0.0), Vector(180.0, 40.0, 0.0), 10.0);
        track.Push(Vector(90.0, 250.0, 0.0), Vector(175.0, 30.0, 0.0), 10.5);
        track.Push(Vector(180.0, 280.0, 0.0), Vector(170.0, 20.0, 0.0), 11.0);
        track.Push(Vector(270.0, 305.0, 0.0), Vector(165.0, 10.0, 0.0), 11.5);
        track.Push(Vector(360.0, 320.0, 0.0), Vector(160.0, 0.0, 0.0), 12.0);
        track.Push(Vector(530.0, 280.0, 0.0), Vector(150.0, -25.0, 0.0), 12.5);
        track.m_FilteredPosition = Vector(530.0, 280.0, 0.0);
        track.m_FilteredVelocity = Vector(150.0, -25.0, 0.0);
        track.m_LastUpdateTime = 12.5;

        RDF_RadarWlrFix fix = track.SolveWeaponLocate(0.0);
        ExpectTrue("track_wlr_launch_valid", fix && fix.m_LaunchValid);
        ExpectTrue("track_wlr_impact_valid", fix && fix.m_ImpactValid);
        ExpectTrue("track_wlr_fit_valid", fix && fix.m_FitValid);
        if (!fix || !fix.m_LaunchValid)
            return;
        ExpectTrue("track_wlr_launch_behind_apex_x", fix.m_LaunchPos[0] < 360.0);
        if (fix.m_ImpactValid)
            ExpectTrue("track_wlr_impact_ahead_apex_x", fix.m_ImpactPos[0] > 360.0);
    }

    protected static void TestAspectRcsElevation()
    {
        // Flat horizon nose-on vs look-down planform for a long thin body.
        float noseHoriz = RDF_RadarRcsModel.AspectRcsFromExtents3D(
            1.0, 2.0, 2.0, 8.0, 0.0, 0.0, 0.0, 0.0);
        float noseLookDown = RDF_RadarRcsModel.AspectRcsFromExtents3D(
            1.0, 2.0, 2.0, 8.0, 0.0, 0.0, 0.0, 60.0);
        float broadside = RDF_RadarRcsModel.AspectRcsFromExtents3D(
            1.0, 2.0, 2.0, 8.0, 0.0, 0.0, 90.0, 0.0);

        ExpectTrue("aspect3d_nose_horiz_gt_zero", noseHoriz > 0.0);
        ExpectTrue("aspect3d_lookdown_differs_nose", Math.AbsFloat(noseLookDown - noseHoriz) > 0.05);
        ExpectTrue("aspect3d_broadside_differs_nose", Math.AbsFloat(broadside - noseHoriz) > 0.05);

        float elHoriz = RDF_RadarRcsModel.ElevationFactor(0.0, 0.0);
        float elDown = RDF_RadarRcsModel.ElevationFactor(0.0, 90.0);
        ExpectTrue("elev_factor_horizon_gt_nadir", elHoriz > elDown);
        ExpectNear("elev_factor_horizon", elHoriz, 1.0, 0.01);
        ExpectNear("elev_factor_nadir", elDown, 0.5, 0.01);
    }

    protected static void WriteReport()
    {
        array<string> lines = new array<string>();
        lines.Insert("RDF Radar Ballistics AutoTest");
        lines.Insert(string.Format("pass=%1", s_Pass.ToString()));
        lines.Insert(string.Format("fail=%1", s_Fail.ToString()));
        if (s_Fail == 0)
            lines.Insert("result=PASS");
        else
            lines.Insert("result=FAIL");

        FileIO.MakeDirectory("$profile:RDF");
        FileIO.MakeDirectory("$profile:RDF/RadarTests");
        string reportPath = "$profile:RDF/RadarTests/radar_ballistics_autotest_"
            + System.GetTickCount().ToString() + ".txt";
        SCR_FileIOHelper.WriteFileContent(reportPath, lines);
        Print("[RDF Ballistics AutoTest] report=" + reportPath);
    }
}
