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
}
