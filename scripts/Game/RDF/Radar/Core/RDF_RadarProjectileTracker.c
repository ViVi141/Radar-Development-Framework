// Holds trajectory for one tracked projectile (last N positions/velocities/times).
class RDF_RadarTrack
{
    IEntity m_Entity;
    ref array<vector> m_Positions = new array<vector>();
    ref array<vector> m_Velocities = new array<vector>();
    ref array<float> m_Times = new array<float>();
    static const int MAX_POINTS = 32;

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
}

// Multi-frame projectile tracker. Update with current scan results; query trajectory by entity.
class RDF_RadarProjectileTracker
{
    protected ref array<ref RDF_RadarTrack> m_Tracks = new array<ref RDF_RadarTrack>();
    protected float m_PruneAgeSec = 2.0;

    void Update(array<ref RDF_RadarTarget> targets, float worldTimeSec)
    {
        if (!targets)
            return;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (t.m_Type != ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE || !t.m_Entity)
                continue;

            RDF_RadarTrack track = FindTrack(t.m_Entity);
            if (!track)
            {
                track = new RDF_RadarTrack();
                track.m_Entity = t.m_Entity;
                m_Tracks.Insert(track);
            }
            track.Push(t.m_Position, t.m_Velocity, worldTimeSec);
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
