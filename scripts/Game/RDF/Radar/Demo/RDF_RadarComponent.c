// Radar component: attach to entity (e.g. vehicle). Registers with emitter registry when
// scanning so other radars can detect this one. Runs scan at interval and updates tracker.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Radar scanner component: detects vehicles, projectiles, and emitting radars; registers as detectable when active")]
class RDF_RadarComponentClass : ScriptComponentClass
{
}

class RDF_RadarComponent : ScriptComponent
{
    protected ref RDF_RadarSettings m_Settings;
    protected ref RDF_RadarScanner m_Scanner;
    protected ref RDF_RadarProjectileTracker m_Tracker;
    protected ref array<ref RDF_RadarTarget> m_LastTargets;
    protected float m_LastScanTime = -1000.0;
    protected bool m_Enabled = true;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        m_Settings = new RDF_RadarSettings();
        m_Scanner = new RDF_RadarScanner(m_Settings);
        m_Tracker = new RDF_RadarProjectileTracker();
        m_LastTargets = new array<ref RDF_RadarTarget>();

        vector pos = GetOwnerOrigin(owner);
        RDF_RadarEmitterRegistry.Register(owner, pos, false, 1.0);
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (!m_Enabled || !m_Scanner || !m_Settings)
            return;

        BaseWorld world = owner.GetWorld();
        if (!world)
            return;

        float now = world.GetWorldTime() * 0.001;
        float interval = m_Settings.m_UpdateInterval;
        if (now - m_LastScanTime < interval)
            return;

        m_LastScanTime = now;
        vector pos = GetOwnerOrigin(owner);
        RDF_RadarEmitterRegistry.Register(owner, pos, true, 1.0);

        m_LastTargets.Clear();
        m_Scanner.Scan(owner, m_LastTargets);
        m_Tracker.Update(m_LastTargets, now);

        RDF_RadarEmitterRegistry.Register(owner, pos, false, 1.0);
    }

    void SetEnabled(bool enabled)
    {
        m_Enabled = enabled;
        if (!enabled && GetOwner())
        {
            RDF_RadarEmitterRegistry.SetEmitting(GetOwner(), false);
            RDF_RadarEmitterRegistry.Unregister(GetOwner());
        }
    }

    bool IsEnabled()
    {
        return m_Enabled;
    }

    RDF_RadarSettings GetSettings()
    {
        return m_Settings;
    }

    array<ref RDF_RadarTarget> GetLastTargets()
    {
        return m_LastTargets;
    }

    RDF_RadarProjectileTracker GetTracker()
    {
        return m_Tracker;
    }

    protected vector GetOwnerOrigin(IEntity owner)
    {
        if (!owner)
            return "0 0 0";
        vector mat[4];
        owner.GetWorldTransform(mat);
        return mat[3];
    }
}
