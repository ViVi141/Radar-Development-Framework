// Emitter registry facade. The single source of truth is
// RDF_RadarScattererRegistry; these calls flag table entries as emitting so
// other radars can treat them as targets. Kept for API compatibility.
class RDF_RadarEmitterRegistry
{
    static void Register(IEntity owner, vector worldPos, bool emitting, float strength)
    {
        RDF_RadarScattererRegistry.Register(owner, worldPos, emitting, strength);
    }

    static void RegisterWithRadio(
        IEntity owner,
        vector worldPos,
        bool emitting,
        float strength,
        float frequencyHz,
        float peakPowerW,
        float antennaGainDbi)
    {
        RDF_RadarScattererRegistry.RegisterWithRadio(
            owner,
            worldPos,
            emitting,
            strength,
            frequencyHz,
            peakPowerW,
            antennaGainDbi);
    }

    static void Unregister(IEntity owner)
    {
        RDF_RadarScattererRegistry.SetEmitting(owner, false);
    }

    static void SetEmitting(IEntity owner, bool emitting)
    {
        RDF_RadarScattererRegistry.SetEmitting(owner, emitting);
    }

    static bool IsEmitting(IEntity owner)
    {
        if (!owner)
            return false;
        RDF_RadarScatterer entry = RDF_RadarScattererRegistry.Find(owner);
        if (!entry)
            return false;
        return entry.m_Emitting;
    }

    static void GetEmittingInSphere(
        vector center,
        float radius,
        array<ref RDF_RadarTarget> outTargets,
        float worldTime)
    {
        if (!outTargets)
            return;

        array<ref RDF_RadarScatterer> entries = RDF_RadarScattererRegistry.GetEntries();
        if (!entries)
            return;

        float r2 = radius * radius;
        for (int i = 0; i < entries.Count(); i++)
        {
            RDF_RadarScatterer e = entries.Get(i);
            if (!e || !e.m_Emitting || !e.m_Entity)
                continue;

            vector d = e.m_Position - center;
            if (d.LengthSq() > r2)
                continue;

            RDF_RadarTarget t = new RDF_RadarTarget();
            t.m_Entity = e.m_Entity;
            t.m_ScattererId = e.m_ScattererId;
            t.m_Position = e.m_Position;
            t.m_Distance = d.Length();
            t.m_Velocity = e.m_Velocity;
            t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
            t.m_RcsM2 = e.m_RcsM2;
            t.m_MeanRcsM2 = e.m_MeanRcsM2;
            t.m_SwerlingModel = e.m_SwerlingModel;
            t.m_AglM = e.m_AglM;
            t.m_EmitFrequencyHz = e.m_EmitFrequencyHz;
            t.m_EmitPeakPowerW = e.m_EmitPeakPowerW;
            t.m_EmitAntennaGainDbi = e.m_EmitAntennaGainDbi;
            t.m_EmitStrength = e.m_EmitStrength;
            t.m_Time = worldTime;
            outTargets.Insert(t);
        }
    }
}
