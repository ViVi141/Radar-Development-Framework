// Radar component: attach to entity (e.g. vehicle). Public surface is RDF_RadarSensor.
// Registers with emitter registry when scanning so other radars can detect this one.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Radar scanner component: detects vehicles, projectiles, and emitting radars; registers as detectable when active")]
class RDF_RadarComponentClass : ScriptComponentClass
{
}

class RDF_RadarComponent : ScriptComponent
{
    protected ref RDF_RadarSensor m_Sensor;
    protected RDF_RadarNetworkAPI m_NetworkAPI;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        m_Sensor = new RDF_RadarSensor();
        m_Sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
        m_NetworkAPI = RDF_RadarNetworkAPI.Cast(owner.FindComponent(RDF_RadarNetworkAPI));

        vector pos = GetOwnerOrigin(owner);
        RegisterEmitter(owner, pos, true);
    }

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (!m_Sensor || !m_Sensor.IsEnabled())
            return;

        vector pos = GetOwnerOrigin(owner);
        RegisterEmitter(owner, pos, true);

        if (!m_Sensor.Tick(owner, m_NetworkAPI))
            return;

        RegisterEmitter(owner, pos, true);
    }

    void SetEnabled(bool enabled)
    {
        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();
        m_Sensor.SetEnabled(enabled);
        if (!enabled && GetOwner())
        {
            RDF_RadarEmitterRegistry.SetEmitting(GetOwner(), false);
            RDF_RadarEmitterRegistry.Unregister(GetOwner());
        }
    }

    bool IsEnabled()
    {
        if (!m_Sensor)
            return false;
        return m_Sensor.IsEnabled();
    }

    void SetMode(ERDF_RadarSensorMode mode)
    {
        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();
        m_Sensor.SetMode(mode);
    }

    RDF_RadarSensor GetSensor()
    {
        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();
        return m_Sensor;
    }

    RDF_RadarSettings GetSettings()
    {
        return GetSensor().GetSettings();
    }

    array<ref RDF_RadarTarget> GetLastTargets()
    {
        return GetSensor().GetPlots();
    }

    RDF_RadarProjectileTracker GetTracker()
    {
        return GetSensor().GetTracker();
    }

    protected void RegisterEmitter(IEntity owner, vector pos, bool emitting)
    {
        RDF_RadarSettings settings = null;
        if (m_Sensor)
            settings = m_Sensor.GetSettings();
        float freq = 0.0;
        float peak = 0.0;
        float gain = 0.0;
        if (settings && settings.m_Hardware)
        {
            freq = settings.m_Hardware.m_FrequencyHz;
            peak = settings.m_Hardware.m_PeakPowerW;
            gain = settings.m_Hardware.m_AntennaGainDbi;
        }
        RDF_RadarEmitterRegistry.RegisterWithRadio(
            owner, pos, emitting, 1.0, freq, peak, gain);
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
