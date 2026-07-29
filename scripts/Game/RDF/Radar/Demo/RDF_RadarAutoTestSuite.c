// Sequential AutoTest runner. Ballistics is sync; the rest share AutoRunner
// and must never overlap.
//
// Usage:
//   RDF_RadarAutoTestSuite.StartAll();            // ideal channel (logic loop)
//   RDF_RadarAutoTestSuite.StartAllRealistic();   // realistic channel + wider bands
//   RDF_RadarAutoTestSuite.Stop();                // clear stuck "already running"
class RDF_RadarAutoTestSuite
{
    protected static bool s_TickRegistered;
    protected static bool s_Running;
    protected static int s_Step = -1;
    protected static float s_StepStartWallS;
    protected static float s_StepTimeoutS = 120.0;
    protected static bool s_RealisticChannel;

    static void StartAll()
    {
        BeginSuite(false);
    }

    static void StartAllRealistic()
    {
        BeginSuite(true);
    }

    static bool IsRealisticChannel()
    {
        return s_RealisticChannel;
    }

    // Hard reset for stuck static state after aborted Play / Debugger re-entry.
    static void Stop()
    {
        StopChildTests();
        RDF_RadarAutoTestGate.ForceClear();
        s_Running = false;
        s_Step = -1;
        Print("[RDF Radar AutoTestSuite] stopped.");
    }

    static bool IsRunning()
    {
        return s_Running;
    }

    protected static void StopChildTests()
    {
        if (RDF_RadarAutoTest.IsRunning())
            RDF_RadarAutoTest.Stop();
        if (RDF_RadarLockAutoTest.IsRunning())
            RDF_RadarLockAutoTest.Stop();
        if (RDF_RadarAirborneScanTest.IsRunning())
            RDF_RadarAirborneScanTest.Stop();
        if (RDF_RadarShellFireAutoTest.IsRunning())
            RDF_RadarShellFireAutoTest.Stop();
        if (RDF_RadarPerfAutoTest.IsRunning())
            RDF_RadarPerfAutoTest.Stop();
        if (RDF_RadarPlayAutoTest.IsRunning())
            RDF_RadarPlayAutoTest.Stop();
        if (RDF_RadarStressAutoTest.IsRunning())
            RDF_RadarStressAutoTest.Stop();
    }

    protected static void BeginSuite(bool realistic)
    {
        // Recover zombie suite flags: marked running but nothing is actually busy.
        if (s_Running)
        {
            bool stepBusy = false;
            if (s_Step >= 0)
                stepBusy = IsStepRunning(s_Step);
            if (!stepBusy && !RDF_RadarAutoTestGate.IsBusy())
            {
                Print("[RDF Radar AutoTestSuite] clearing stale running flag.");
                s_Running = false;
                s_Step = -1;
            }
            else
            {
                Print("[RDF Radar AutoTestSuite] already running (step="
                    + s_Step.ToString()
                    + "). Call RDF_RadarAutoTestSuite.Stop() first.");
                return;
            }
        }

        if (RDF_RadarAutoTestGate.IsBusy())
        {
            Print("[RDF Radar AutoTestSuite] gate busy by "
                + RDF_RadarAutoTestGate.GetOwner()
                + "; stop that test first, or RDF_RadarAutoTestSuite.Stop().", LogLevel.WARNING);
            return;
        }

        s_RealisticChannel = realistic;
        s_Running = true;
        if (realistic)
            Print("[RDF Radar AutoTestSuite] begin (sequential, REALISTIC channel)");
        else
            Print("[RDF Radar AutoTestSuite] begin (sequential, ideal channel)");

        if (!s_TickRegistered)
        {
            s_TickRegistered = true;
            GetGame().GetCallqueue().CallLater(StaticTick, 500, true);
        }

        RunStep(0);
    }

    protected static void StaticTick()
    {
        if (!s_Running)
            return;
        if (s_Step < 0)
            return;

        float nowS = System.GetTickCount() * 0.001;
        if (nowS - s_StepStartWallS > s_StepTimeoutS)
        {
            Print(string.Format(
                "[RDF Radar AutoTestSuite] step %1 timed out after %2s — forcing stop",
                s_Step.ToString(),
                s_StepTimeoutS.ToString()), LogLevel.ERROR);
            StopChildTests();
            RDF_RadarAutoRunner.SetDemoEnabled(false);
            RDF_RadarAutoRunner.SetHudEnabled(false);
            RDF_RadarAutoRunner.StopAutoRun();
            s_Running = false;
            s_Step = -1;
            Print("[RDF Radar AutoTestSuite] aborted by timeout.", LogLevel.ERROR);
            return;
        }

        if (IsStepRunning(s_Step))
            return;

        int next = s_Step + 1;
        if (next > 6)
        {
            FinishSuite();
            return;
        }

        RunStep(next);
    }

    protected static void FinishSuite()
    {
        // Leave scanner idle: prior steps often restore DemoEnabled, and the
        // first post-suite DEM/SURF dwell can look like a hang in Workbench.
        RDF_RadarAutoRunner.SetDemoEnabled(false);
        RDF_RadarAutoRunner.SetHudEnabled(false);
        RDF_RadarAutoRunner.StopAutoRun();
        s_Running = false;
        s_Step = -1;
        Print("[RDF Radar AutoTestSuite] done (demo/HUD OFF)");
    }

    protected static bool IsStepRunning(int step)
    {
        if (step == 0)
            return false;
        if (step == 1)
            return RDF_RadarAutoTest.IsRunning();
        if (step == 2)
            return RDF_RadarLockAutoTest.IsRunning();
        if (step == 3)
            return RDF_RadarAirborneScanTest.IsRunning();
        if (step == 4)
            return RDF_RadarShellFireAutoTest.IsRunning();
        if (step == 5)
            return RDF_RadarPerfAutoTest.IsRunning();
        if (step == 6)
            return RDF_RadarPlayAutoTest.IsRunning();
        return false;
    }

    protected static void RunStep(int step)
    {
        s_Step = step;
        s_StepStartWallS = System.GetTickCount() * 0.001;

        if (step == 0)
        {
            Print("[RDF Radar AutoTestSuite] step 1/7 Ballistics");
            RDF_RadarBallisticsAutoTest.Start();
            RunStep(1);
            return;
        }

        if (step == 1)
        {
            Print("[RDF Radar AutoTestSuite] step 2/7 DEM AutoTest");
            RDF_RadarAutoTest.Start();
            return;
        }

        if (step == 2)
        {
            Print("[RDF Radar AutoTestSuite] step 3/7 Lock");
            RDF_RadarLockAutoTest.Start();
            return;
        }

        if (step == 3)
        {
            Print("[RDF Radar AutoTestSuite] step 4/7 Airborne");
            RDF_RadarAirborneScanTest.Start();
            return;
        }

        if (step == 4)
        {
            Print("[RDF Radar AutoTestSuite] step 5/7 ShellFire");
            RDF_RadarShellFireAutoTest.Start();
            return;
        }

        if (step == 5)
        {
            Print("[RDF Radar AutoTestSuite] step 6/7 Perf");
            // Perf is fully synchronous; continue to Play without waiting CallLater.
            RDF_RadarPerfAutoTest.Start();
            RunStep(6);
            return;
        }

        if (step == 6)
        {
            Print("[RDF Radar AutoTestSuite] step 7/7 Play");
            RDF_RadarPlayAutoTest.Start();
            return;
        }
    }
}
