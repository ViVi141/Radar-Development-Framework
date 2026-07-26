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
    protected RDF_RadarNetworkAPI m_NetworkAPI;
    protected float m_LastScanTime = -1000.0;
    protected bool m_Enabled = true;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        m_Settings = new RDF_RadarSettings();
        m_Scanner = new RDF_RadarScanner(m_Settings);
        m_Tracker = new RDF_RadarProjectileTracker();
        m_Tracker.ConfigureFromSettings(m_Settings);
        m_LastTargets = new array<ref RDF_RadarTarget>();
        m_NetworkAPI = RDF_RadarNetworkAPI.Cast(owner.FindComponent(RDF_RadarNetworkAPI));

        vector pos = GetOwnerOrigin(owner);
        float freq = 0.0;
        float peak = 0.0;
        float gain = 0.0;
        if (m_Settings && m_Settings.m_Hardware)
        {
            freq = m_Settings.m_Hardware.m_FrequencyHz;
            peak = m_Settings.m_Hardware.m_PeakPowerW;
            gain = m_Settings.m_Hardware.m_AntennaGainDbi;
        }
        RDF_RadarEmitterRegistry.RegisterWithRadio(owner, pos, m_Enabled, 1.0, freq, peak, gain);
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
        float freq = 0.0;
        float peak = 0.0;
        float gain = 0.0;
        if (m_Settings.m_Hardware)
        {
            freq = m_Settings.m_Hardware.m_FrequencyHz;
            peak = m_Settings.m_Hardware.m_PeakPowerW;
            gain = m_Settings.m_Hardware.m_AntennaGainDbi;
        }
        RDF_RadarEmitterRegistry.RegisterWithRadio(owner, pos, true, 1.0, freq, peak, gain);

        m_LastTargets.Clear();
        vector trackOrigin = pos;
        if (m_NetworkAPI && m_NetworkAPI.IsNetworkAvailable())
        {
            m_NetworkAPI.RequestScan();
            if (m_NetworkAPI.HasSyncedTargets())
            {
                array<ref RDF_RadarTarget> syncedTargets = m_NetworkAPI.GetLastTargets();
                if (syncedTargets)
                {
                    for (int i = 0; i < syncedTargets.Count(); i++)
                    {
                        RDF_RadarTarget t = syncedTargets.Get(i);
                        if (t)
                            m_LastTargets.Insert(t);
                    }
                }
            }
            trackOrigin = m_NetworkAPI.GetLastScanOrigin();
        }
        else
        {
            m_Scanner.Scan(owner, m_LastTargets);
            trackOrigin = m_Scanner.GetLastOrigin();
        }
        m_Tracker.ConfigureFromSettings(m_Settings);
        m_Tracker.UpdateWithOrigin(m_LastTargets, now, trackOrigin);
        m_Tracker.RefreshWeaponLocates(trackOrigin[1]);

        // Keep the emitter active between dwells; otherwise another radar can
        // almost never observe the sub-frame "emitting" window.
        RDF_RadarEmitterRegistry.RegisterWithRadio(owner, pos, true, 1.0, freq, peak, gain);
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
