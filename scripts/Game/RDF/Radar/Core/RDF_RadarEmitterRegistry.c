// One registered radar emitter entry.
class RDF_RadarEmitterEntry
{
    IEntity m_Owner;
    vector m_Position;
    bool m_Emitting;
    float m_Strength;
}

// Global registry of radar emitters. When a radar is "active" it registers with emitting=true;
// other radars can query emitting emitters in range and treat them as targets.
class RDF_RadarEmitterRegistry
{
    protected static ref array<ref RDF_RadarEmitterEntry> s_Entries = new array<ref RDF_RadarEmitterEntry>();

    static void Register(IEntity owner, vector worldPos, bool emitting, float strength)
    {
        if (!owner)
            return;

        RDF_RadarEmitterEntry existing = FindEntry(owner);
        if (existing)
        {
            existing.m_Position = worldPos;
            existing.m_Emitting = emitting;
            existing.m_Strength = strength;
            return;
        }

        RDF_RadarEmitterEntry entry = new RDF_RadarEmitterEntry();
        entry.m_Owner = owner;
        entry.m_Position = worldPos;
        entry.m_Emitting = emitting;
        entry.m_Strength = strength;
        s_Entries.Insert(entry);
    }

    static void Unregister(IEntity owner)
    {
        for (int i = s_Entries.Count() - 1; i >= 0; i--)
        {
            if (s_Entries.Get(i).m_Owner == owner)
            {
                s_Entries.Remove(i);
                return;
            }
        }
    }

    static void SetEmitting(IEntity owner, bool emitting)
    {
        RDF_RadarEmitterEntry entry = FindEntry(owner);
        if (entry)
            entry.m_Emitting = emitting;
    }

    static void GetEmittingInSphere(vector center, float radius, array<ref RDF_RadarTarget> outTargets, float worldTime)
    {
        if (!outTargets)
            return;

        float r2 = radius * radius;
        for (int i = 0; i < s_Entries.Count(); i++)
        {
            RDF_RadarEmitterEntry e = s_Entries.Get(i);
            if (!e.m_Emitting || !e.m_Owner)
                continue;

            vector d = e.m_Position - center;
            float distSq = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (distSq > r2)
                continue;

            float dist = Math.Sqrt(distSq);
            RDF_RadarTarget t = new RDF_RadarTarget();
            t.m_Entity = e.m_Owner;
            t.m_Position = e.m_Position;
            t.m_Distance = dist;
            t.m_Velocity = "0 0 0";
            t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
            t.m_Time = worldTime;
            outTargets.Insert(t);
        }
    }

    protected static RDF_RadarEmitterEntry FindEntry(IEntity owner)
    {
        for (int i = 0; i < s_Entries.Count(); i++)
        {
            if (s_Entries.Get(i).m_Owner == owner)
                return s_Entries.Get(i);
        }
        return null;
    }
}
