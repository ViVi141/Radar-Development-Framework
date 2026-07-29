// Mutual exclusion for AutoTests that share RDF_RadarAutoRunner.
// Running DEM / Lock / Air / ShellFire / Play / Stress together overwrites the singleton
// config and produces false FAIL (clutter=0, acquire=0, tracks=0, etc.).
class RDF_RadarAutoTestGate
{
    protected static string s_Owner;

    static bool TryAcquire(string owner)
    {
        if (!owner || owner == "")
            return false;

        if (s_Owner && s_Owner != "" && s_Owner != owner)
        {
            Print(string.Format(
                "[RDF Radar AutoTestGate] busy by '%1'; refused '%2'. Run one test at a time, or RDF_RadarAutoTestSuite.StartAll().",
                s_Owner,
                owner), LogLevel.WARNING);
            return false;
        }

        s_Owner = owner;
        return true;
    }

    static void Release(string owner)
    {
        if (!owner || owner == "")
            return;
        if (s_Owner == owner)
            s_Owner = "";
    }

    // Suite.Stop() / emergency clear when owner is unknown or zombie.
    static void ForceClear()
    {
        s_Owner = "";
    }

    static bool IsBusy()
    {
        if (!s_Owner)
            return false;
        return s_Owner != "";
    }

    static string GetOwner()
    {
        if (!s_Owner)
            return "";
        return s_Owner;
    }
}
