// Platform-side RWR: "I am being searched / tracked / locked" by other RDF radars.
// Illuminators write threats after each dwell; victims query via HasSearchWarning / HasLockWarning.

enum ERDF_RadarRwrLevel
{
    RDF_RWR_SEARCH = 1,
    RDF_RWR_TRACK = 2,
    RDF_RWR_LOCK = 3
}

class RDF_RadarRwrThreat
{
    IEntity m_Source;          // illuminating radar platform
    IEntity m_Victim;          // illuminated entity
    ERDF_RadarRwrLevel m_Level;
    float m_WorldTimeS;
    float m_WallTimeS;
    float m_RangeM;
    float m_BearingDeg;        // victim -> source azimuth (world XZ)
    int m_ScanSerial;
}

class RDF_RadarRwr
{
    protected static ref array<ref RDF_RadarRwrThreat> s_Threats;
    protected static float s_ThreatTtlS = 1.5;
    protected static const float RAD_TO_DEG = 57.29577951;

    protected static void Ensure()
    {
        if (!s_Threats)
            s_Threats = new array<ref RDF_RadarRwrThreat>();
    }

    static void SetThreatTtlS(float ttlS)
    {
        s_ThreatTtlS = Math.Clamp(ttlS, 0.2, 10.0);
    }

    static float GetThreatTtlS()
    {
        return s_ThreatTtlS;
    }

    static void Clear()
    {
        Ensure();
        s_Threats.Clear();
    }

    // Illuminator reports a detection / track / lock on victim.
    static void Report(
        IEntity source,
        IEntity victim,
        ERDF_RadarRwrLevel level,
        vector sourceOrigin,
        float worldTimeS,
        int scanSerial)
    {
        if (!source || !victim)
            return;
        if (source == victim)
            return;

        Ensure();
        Prune(System.GetTickCount() * 0.001);

        vector victimPos = victim.GetOrigin();
        vector delta = sourceOrigin - victimPos;
        float rangeM = delta.Length();
        float bearingDeg = Math.Atan2(delta[2], delta[0]) * RAD_TO_DEG;

        // Upgrade existing row from same source, or insert.
        for (int i = 0; i < s_Threats.Count(); i++)
        {
            RDF_RadarRwrThreat t = s_Threats.Get(i);
            if (!t)
                continue;
            if (t.m_Source != source)
                continue;
            if (t.m_Victim != victim)
                continue;

            if (level > t.m_Level)
                t.m_Level = level;
            t.m_WorldTimeS = worldTimeS;
            t.m_WallTimeS = System.GetTickCount() * 0.001;
            t.m_RangeM = rangeM;
            t.m_BearingDeg = bearingDeg;
            t.m_ScanSerial = scanSerial;
            return;
        }

        RDF_RadarRwrThreat neu = new RDF_RadarRwrThreat();
        neu.m_Source = source;
        neu.m_Victim = victim;
        neu.m_Level = level;
        neu.m_WorldTimeS = worldTimeS;
        neu.m_WallTimeS = System.GetTickCount() * 0.001;
        neu.m_RangeM = rangeM;
        neu.m_BearingDeg = bearingDeg;
        neu.m_ScanSerial = scanSerial;
        s_Threats.Insert(neu);
    }

    static void CollectForVictim(
        IEntity victim,
        notnull array<ref RDF_RadarRwrThreat> outThreats)
    {
        outThreats.Clear();
        if (!victim)
            return;

        Ensure();
        float wallNow = System.GetTickCount() * 0.001;
        Prune(wallNow);

        for (int i = 0; i < s_Threats.Count(); i++)
        {
            RDF_RadarRwrThreat t = s_Threats.Get(i);
            if (!t || t.m_Victim != victim)
                continue;
            outThreats.Insert(t);
        }
    }

    static bool HasWarningAtLeast(IEntity victim, ERDF_RadarRwrLevel minLevel)
    {
        if (!victim)
            return false;

        Ensure();
        float wallNow = System.GetTickCount() * 0.001;
        Prune(wallNow);

        for (int i = 0; i < s_Threats.Count(); i++)
        {
            RDF_RadarRwrThreat t = s_Threats.Get(i);
            if (!t || t.m_Victim != victim)
                continue;
            if (t.m_Level >= minLevel)
                return true;
        }
        return false;
    }

    static bool HasSearchWarning(IEntity victim)
    {
        return HasWarningAtLeast(victim, ERDF_RadarRwrLevel.RDF_RWR_SEARCH);
    }

    static bool HasTrackWarning(IEntity victim)
    {
        return HasWarningAtLeast(victim, ERDF_RadarRwrLevel.RDF_RWR_TRACK);
    }

    static bool HasLockWarning(IEntity victim)
    {
        return HasWarningAtLeast(victim, ERDF_RadarRwrLevel.RDF_RWR_LOCK);
    }

    // Highest active level for HUD (0 = none).
    static int GetHighestLevelInt(IEntity victim)
    {
        if (!victim)
            return 0;

        Ensure();
        float wallNow = System.GetTickCount() * 0.001;
        Prune(wallNow);

        int best = 0;
        for (int i = 0; i < s_Threats.Count(); i++)
        {
            RDF_RadarRwrThreat t = s_Threats.Get(i);
            if (!t || t.m_Victim != victim)
                continue;
            int lvl = t.m_Level;
            if (lvl > best)
                best = lvl;
        }
        return best;
    }

    static string GetStatusShort(IEntity victim)
    {
        int lvl = GetHighestLevelInt(victim);
        if (lvl >= ERDF_RadarRwrLevel.RDF_RWR_LOCK)
            return "RWR LOCK";
        if (lvl >= ERDF_RadarRwrLevel.RDF_RWR_TRACK)
            return "RWR TRACK";
        if (lvl >= ERDF_RadarRwrLevel.RDF_RWR_SEARCH)
            return "RWR SEARCH";
        return "RWR CLEAR";
    }

    protected static void Prune(float wallNowS)
    {
        if (!s_Threats)
            return;

        for (int i = s_Threats.Count() - 1; i >= 0; i--)
        {
            RDF_RadarRwrThreat t = s_Threats.Get(i);
            if (!t || !t.m_Source || !t.m_Victim)
            {
                s_Threats.Remove(i);
                continue;
            }
            if (wallNowS - t.m_WallTimeS > s_ThreatTtlS)
                s_Threats.Remove(i);
        }
    }
}
