// In-memory station-to-station track hub (authority-side). Optional component
// RDF_RadarDatalinkComponent can broadcast D|/F| rows to proxies.
class RDF_RadarDatalinkHub
{
    protected static ref RDF_RadarDatalinkHub s_Instance;

    protected bool m_Enabled = true;
    protected float m_TrackTtlS = 2.5;
    protected ref array<ref RDF_RadarDatalinkTrack> m_Tracks;
    protected ref array<ref RDF_RadarFusedTrack> m_FusedTracks;
    protected ref RDF_RadarIffResolver m_IffResolver;
    protected bool m_AutoFusion = true;
    protected int m_PublishCount;
    protected int m_FusionCount;

    static RDF_RadarDatalinkHub Get()
    {
        if (!s_Instance)
            s_Instance = new RDF_RadarDatalinkHub();
        return s_Instance;
    }

    static void ResetForTests()
    {
        if (s_Instance)
            s_Instance.Clear();
        s_Instance = null;
    }

    void RDF_RadarDatalinkHub()
    {
        m_Tracks = new array<ref RDF_RadarDatalinkTrack>();
        m_FusedTracks = new array<ref RDF_RadarFusedTrack>();
        m_IffResolver = new RDF_RadarDefaultIffResolver();
    }

    void SetEnabled(bool enabled)
    {
        m_Enabled = enabled;
    }

    bool IsEnabled()
    {
        return m_Enabled;
    }

    void SetTrackTtlS(float ttlS)
    {
        m_TrackTtlS = Math.Clamp(ttlS, 0.2, 30.0);
    }

    void SetAutoFusion(bool enabled)
    {
        m_AutoFusion = enabled;
    }

    bool GetAutoFusion()
    {
        return m_AutoFusion;
    }

    void SetIffResolver(RDF_RadarIffResolver resolver)
    {
        if (resolver)
            m_IffResolver = resolver;
        else
            m_IffResolver = new RDF_RadarDefaultIffResolver();
    }

    RDF_RadarIffResolver GetIffResolver()
    {
        return m_IffResolver;
    }

    void Clear()
    {
        if (m_Tracks)
            m_Tracks.Clear();
        if (m_FusedTracks)
            m_FusedTracks.Clear();
        m_PublishCount = 0;
        m_FusionCount = 0;
    }

    string GetStatusShort()
    {
        if (!m_Enabled)
            return "DL OFF";
        int n = 0;
        if (m_Tracks)
            n = m_Tracks.Count();
        int f = 0;
        if (m_FusedTracks)
            f = m_FusedTracks.Count();
        return "DL tracks=" + n.ToString()
            + " fused=" + f.ToString()
            + " pub=" + m_PublishCount.ToString();
    }

    // Replace all summaries from one radar source, then age + optional fusion.
    void PublishFromRadar(
        int sourceRadarId,
        vector radarOrigin,
        float worldTimeS,
        notnull array<ref RDF_RadarDatalinkTrack> tracks)
    {
        if (!m_Enabled)
            return;
        if (sourceRadarId <= 0)
            return;

        RemoveSource(sourceRadarId);
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarDatalinkTrack t = tracks.Get(i);
            if (!t)
                continue;
            t.m_SourceRadarId = sourceRadarId;
            t.m_RadarOrigin = radarOrigin;
            if (t.m_TimeS <= 0.0)
                t.m_TimeS = worldTimeS;
            m_Tracks.Insert(t);
        }
        m_PublishCount = m_PublishCount + 1;
        AgeOut(worldTimeS);
        if (m_AutoFusion)
            RDF_RadarFusionService.Get().UpdateFromHub(this, worldTimeS);
    }

    void PublishTracks(notnull array<ref RDF_RadarDatalinkTrack> tracks)
    {
        if (!m_Enabled)
            return;
        float now = 0.0;
        if (GetGame() && GetGame().GetWorld())
            now = GetGame().GetWorld().GetWorldTime() * 0.001;
        for (int i = 0; i < tracks.Count(); i++)
        {
            RDF_RadarDatalinkTrack t = tracks.Get(i);
            if (!t || t.m_SourceRadarId <= 0)
                continue;
            if (t.m_TimeS <= 0.0)
                t.m_TimeS = now;
            m_Tracks.Insert(t);
        }
        m_PublishCount = m_PublishCount + 1;
        AgeOut(now);
        if (m_AutoFusion)
            RDF_RadarFusionService.Get().UpdateFromHub(this, now);
    }

    void RemoveSource(int sourceRadarId)
    {
        if (!m_Tracks)
            return;
        for (int i = m_Tracks.Count() - 1; i >= 0; i--)
        {
            RDF_RadarDatalinkTrack t = m_Tracks.Get(i);
            if (!t || t.m_SourceRadarId == sourceRadarId)
                m_Tracks.Remove(i);
        }
    }

    void AgeOut(float worldTimeS)
    {
        if (!m_Tracks || m_TrackTtlS <= 0.0)
            return;
        for (int i = m_Tracks.Count() - 1; i >= 0; i--)
        {
            RDF_RadarDatalinkTrack t = m_Tracks.Get(i);
            if (!t)
            {
                m_Tracks.Remove(i);
                continue;
            }
            if (worldTimeS - t.m_TimeS > m_TrackTtlS)
                m_Tracks.Remove(i);
        }
    }

    void GetTracksInRadius(
        vector origin,
        float radiusM,
        notnull array<ref RDF_RadarDatalinkTrack> outTracks)
    {
        outTracks.Clear();
        if (!m_Enabled || !m_Tracks)
            return;
        float r = radiusM;
        if (r < 1.0)
            r = 1.0;
        float r2 = r * r;
        for (int i = 0; i < m_Tracks.Count(); i++)
        {
            RDF_RadarDatalinkTrack t = m_Tracks.Get(i);
            if (!t)
                continue;
            vector d = t.m_WorldPos - origin;
            float dist2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (dist2 <= r2)
                outTracks.Insert(t);
        }
    }

    array<ref RDF_RadarDatalinkTrack> GetAllTracks()
    {
        return m_Tracks;
    }

    void SetFusedTracks(notnull array<ref RDF_RadarFusedTrack> fused)
    {
        if (!m_FusedTracks)
            m_FusedTracks = new array<ref RDF_RadarFusedTrack>();
        m_FusedTracks.Clear();
        for (int i = 0; i < fused.Count(); i++)
        {
            RDF_RadarFusedTrack f = fused.Get(i);
            if (f)
                m_FusedTracks.Insert(f);
        }
        m_FusionCount = m_FusionCount + 1;
    }

    array<ref RDF_RadarFusedTrack> GetFusedTracks()
    {
        return m_FusedTracks;
    }

    int GetPublishCount()
    {
        return m_PublishCount;
    }

    int GetFusionCount()
    {
        return m_FusionCount;
    }
}
