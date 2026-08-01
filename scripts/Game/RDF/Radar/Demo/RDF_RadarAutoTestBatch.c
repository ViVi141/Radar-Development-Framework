// Flag-driven AutoTest suite starter (no Script Debugger paste required).
// Create $profile:RDF/RunAutoTestSuite.flag (optional line: "realistic")
// while Workbench Play is running. Polled from SCR_BaseGameMode OnGameStart /
// EOnFrame via RDF_RadarAutoTestBatch.OnFrame.
class RDF_RadarAutoTestBatch
{
    static const string FLAG_FILE = "$profile:RDF/RunAutoTestSuite.flag";
    static const string RESULT_FILE = "$profile:RDF/RadarTests/radar_autotest_suite_result.txt";

    protected static bool s_PollRegistered;
    protected static bool s_ConsumedThisPlay;
    protected static bool s_WaitingForSuite;
    protected static bool s_RequestedRealistic;
    protected static float s_SuiteStartWallS;

    //------------------------------------------------------------------------------------------------
    static bool IsFlagPresent()
    {
        return FileIO.FileExists(FLAG_FILE);
    }

    //------------------------------------------------------------------------------------------------
    static void WriteFlag(bool realistic)
    {
        if (!FileIO.FileExists("$profile:RDF"))
            FileIO.MakeDirectory("$profile:RDF");

        array<string> lines = new array<string>();
        if (realistic)
            lines.Insert("realistic");
        else
            lines.Insert("ideal");
        SCR_FileIOHelper.WriteFileContent(FLAG_FILE, lines);
    }

    //------------------------------------------------------------------------------------------------
    static void ClearFlag()
    {
        if (FileIO.FileExists(FLAG_FILE))
            FileIO.DeleteFile(FLAG_FILE);
    }

    //------------------------------------------------------------------------------------------------
    // Call once from OnGameStart to arm polling.
    static void ArmPoll()
    {
        s_ConsumedThisPlay = false;
        s_WaitingForSuite = false;
        s_PollRegistered = true;
    }

    //------------------------------------------------------------------------------------------------
    static void OnFrame(float timeSlice)
    {
        if (!s_PollRegistered)
            return;

        if (s_WaitingForSuite)
        {
            MaybeFinishSuite();
            return;
        }

        if (s_ConsumedThisPlay)
            return;
        if (!IsFlagPresent())
            return;
        if (RDF_RadarAutoTestSuite.IsRunning())
            return;
        if (RDF_RadarAutoTestGate.IsBusy())
            return;

        s_RequestedRealistic = ReadRealisticFromFlag();
        ClearFlag();
        s_ConsumedThisPlay = true;
        s_WaitingForSuite = true;
        s_SuiteStartWallS = System.GetTickCount() * 0.001;

        Print("[RDF AutoTestBatch] flag consumed → starting suite"
            + " realistic=" + s_RequestedRealistic.ToString());
        if (s_RequestedRealistic)
            RDF_RadarAutoTestSuite.StartAllRealistic();
        else
            RDF_RadarAutoTestSuite.StartAll();
    }

    //------------------------------------------------------------------------------------------------
    protected static bool ReadRealisticFromFlag()
    {
        array<string> lines = SCR_FileIOHelper.ReadFileContent(FLAG_FILE, false);
        if (!lines)
            return false;
        for (int i = 0; i < lines.Count(); i++)
        {
            string line = lines.Get(i);
            if (!line)
                continue;
            line.ToLower();
            if (line.Contains("realistic"))
                return true;
            if (line.Contains("real"))
                return true;
        }
        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static void MaybeFinishSuite()
    {
        if (RDF_RadarAutoTestSuite.IsRunning())
            return;

        s_WaitingForSuite = false;
        float elapsed = System.GetTickCount() * 0.001 - s_SuiteStartWallS;
        WriteResult(elapsed);
        Print("[RDF AutoTestBatch] suite finished in "
            + elapsed.ToString()
            + "s → "
            + RESULT_FILE);
    }

    //------------------------------------------------------------------------------------------------
    protected static void WriteResult(float elapsedS)
    {
        if (!FileIO.FileExists("$profile:RDF"))
            FileIO.MakeDirectory("$profile:RDF");
        if (!FileIO.FileExists("$profile:RDF/RadarTests"))
            FileIO.MakeDirectory("$profile:RDF/RadarTests");

        string channel = "ideal";
        if (s_RequestedRealistic)
            channel = "realistic";

        array<string> lines = new array<string>();
        lines.Insert("ok=1");
        lines.Insert("channel=" + channel);
        lines.Insert("elapsed_s=" + elapsedS.ToString());
        lines.Insert("note=Play-mode batch; not headless Workbench CI");
        SCR_FileIOHelper.WriteFileContent(RESULT_FILE, lines);
    }
}
