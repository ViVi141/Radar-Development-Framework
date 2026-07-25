// Holds trajectory and alpha-beta state for one tracked radar entity.
class RDF_RadarTrack
{
    IEntity m_Entity;
    ref array<vector> m_Positions = new array<vector>();
    ref array<vector> m_Velocities = new array<vector>();
    ref array<float> m_Times = new array<float>();
    static const int MAX_POINTS = 32;
    vector m_FilteredPosition;
    vector m_FilteredVelocity;
    int m_HitCount;
    int m_LastScanNumber = -1;
    bool m_Confirmed;

    void Push(vector pos, vector vel, float time)
    {
        m_Positions.Insert(pos);
        m_Velocities.Insert(vel);
        m_Times.Insert(time);
        while (m_Positions.Count() > MAX_POINTS)
        {
            m_Positions.Remove(0);
            m_Velocities.Remove(0);
            m_Times.Remove(0);
        }
    }

    float GetLastTime()
    {
        if (m_Times.Count() == 0)
            return -1.0;
        return m_Times.Get(m_Times.Count() - 1);
    }

    void FilterUpdate(
        RDF_RadarTarget target,
        float alpha,
        float beta)
    {
        if (!target)
            return;

        float lastTime = GetLastTime();
        if (lastTime < 0.0)
        {
            m_FilteredPosition = target.m_Position;
            m_FilteredVelocity = target.m_Velocity;
        }
        else
        {
            float dt = Math.Max(0.001, target.m_Time - lastTime);
            vector prediction = m_FilteredPosition + m_FilteredVelocity * dt;
            vector residual = target.m_Position - prediction;
            m_FilteredPosition = prediction + residual * alpha;
            m_FilteredVelocity = m_FilteredVelocity + residual * (beta / dt);
        }

        m_HitCount = m_HitCount + 1;
        m_LastScanNumber = target.m_ScanNumber;
        m_Confirmed = m_HitCount >= 2;
        Push(m_FilteredPosition, m_FilteredVelocity, target.m_Time);
    }
}

// Multi-frame generic radar tracker. The legacy class name remains API-compatible.
class RDF_RadarProjectileTracker
{
    protected ref array<ref RDF_RadarTrack> m_Tracks = new array<ref RDF_RadarTrack>();
    protected float m_PruneAgeSec = 30.0;
    protected float m_Alpha = 0.5;
    protected float m_Beta = 0.2;

    void Update(array<ref RDF_RadarTarget> targets, float worldTimeSec)
    {
        if (!targets)
            return;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t || !t.m_Detected || !t.m_Entity)
                continue;

            RDF_RadarTrack track = FindTrack(t.m_Entity);
            if (!track)
            {
                track = new RDF_RadarTrack();
                track.m_Entity = t.m_Entity;
                m_Tracks.Insert(track);
            }
            track.FilterUpdate(t, m_Alpha, m_Beta);
        }

        for (int j = m_Tracks.Count() - 1; j >= 0; j--)
        {
            RDF_RadarTrack tr = m_Tracks.Get(j);
            if (worldTimeSec - tr.GetLastTime() > m_PruneAgeSec)
                m_Tracks.Remove(j);
        }
    }

    RDF_RadarTrack GetTrajectory(IEntity entity)
    {
        return FindTrack(entity);
    }

    array<ref RDF_RadarTrack> GetAllTracks()
    {
        return m_Tracks;
    }

    protected RDF_RadarTrack FindTrack(IEntity entity)
    {
        for (int i = 0; i < m_Tracks.Count(); i++)
        {
            if (m_Tracks.Get(i).m_Entity == entity)
                return m_Tracks.Get(i);
        }
        return null;
    }
}
