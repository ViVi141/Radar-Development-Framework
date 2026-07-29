// Optional network face for DatalinkHub: Reliable Broadcast of capped track /
// fused summaries; throttle + interest; RplSave/RplLoad for JIP.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Datalink hub network sync (typed arrays + RplSave)")]
class RDF_RadarDatalinkComponentClass : RDF_RadarDatalinkAPIClass
{
}

class RDF_RadarDatalinkComponent : RDF_RadarDatalinkAPI
{
    protected RplComponent m_RplComponent;
    protected bool m_BroadcastEnabled = true;
    protected float m_LastBroadcastMs = -100000.0;

    [Attribute(defvalue: "1", desc: "Broadcast datalink/fused summaries to proxies")]
    bool m_EnableBroadcast = true;

    [Attribute(defvalue: "48", desc: "Max datalink tracks per Broadcast (0 = unlimited)")]
    int m_MaxSyncedTracks = 48;

    [Attribute(defvalue: "32", desc: "Max fused tracks per Broadcast (0 = unlimited)")]
    int m_MaxSyncedFused = 32;

    [Attribute(defvalue: "0.25", desc: "Min seconds between datalink Broadcasts")]
    float m_MinBroadcastIntervalS = 0.25;

    [Attribute(defvalue: "15000", desc: "Only Broadcast if a player is within this radius of owner (m); 0 = always")]
    float m_InterestRadiusM = 15000.0;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        m_RplComponent = RplComponent.Cast(owner.FindComponent(RplComponent));
        m_BroadcastEnabled = m_EnableBroadcast;
        RDF_RadarDatalinkHub.Get().SetEnabled(true);
    }

    override void PublishTracks(notnull array<ref RDF_RadarDatalinkTrack> tracks)
    {
        RDF_RadarDatalinkHub.Get().PublishTracks(tracks);
        if (m_BroadcastEnabled)
            BroadcastHubState();
    }

    override void GetTracksInRadius(
        vector origin,
        float radiusM,
        notnull array<ref RDF_RadarDatalinkTrack> outTracks)
    {
        RDF_RadarDatalinkHub.Get().GetTracksInRadius(origin, radiusM, outTracks);
    }

    override void Clear()
    {
        RDF_RadarDatalinkHub.Get().Clear();
    }

    override string GetStatusShort()
    {
        return RDF_RadarDatalinkHub.Get().GetStatusShort();
    }

    override array<ref RDF_RadarFusedTrack> GetFusedTracks()
    {
        return RDF_RadarDatalinkHub.Get().GetFusedTracks();
    }

    void BroadcastHubState()
    {
        if (!m_RplComponent)
            return;
        if (m_RplComponent.IsProxy())
            return;
        if (!m_BroadcastEnabled)
            return;
        if (!m_EnableBroadcast)
            return;

        float nowMs = 0.0;
        if (GetGame().GetWorld())
            nowMs = GetGame().GetWorld().GetWorldTime();
        float intervalMs = Math.Max(0.0, m_MinBroadcastIntervalS) * 1000.0;
        if ((nowMs - m_LastBroadcastMs) < intervalMs)
            return;

        if (!HasInterestedAudience())
            return;

        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        array<int> ints = new array<int>();
        array<float> floats = new array<float>();
        RDF_RadarNetCodec.PackDatalinkRpc(
            hub.GetAllTracks(),
            hub.GetFusedTracks(),
            ints,
            floats,
            m_MaxSyncedTracks,
            m_MaxSyncedFused);
        Rpc(RpcDo_ReceiveDatalinkPayload, ints, floats);
        m_LastBroadcastMs = nowMs;
    }

    protected bool HasInterestedAudience()
    {
        if (m_InterestRadiusM <= 0.0)
            return true;

        IEntity owner = GetOwner();
        if (!owner)
            return true;

        PlayerManager pm = GetGame().GetPlayerManager();
        if (!pm)
            return true;

        array<int> players = new array<int>();
        pm.GetPlayers(players);
        if (players.Count() < 1)
            return true;

        vector origin = owner.GetOrigin();
        float radiusSq = m_InterestRadiusM * m_InterestRadiusM;
        for (int i = 0; i < players.Count(); i++)
        {
            IEntity controlled = pm.GetPlayerControlledEntity(players.Get(i));
            if (!controlled)
                continue;
            vector delta = controlled.GetOrigin() - origin;
            float distSq = delta[0] * delta[0] + delta[2] * delta[2];
            if (distSq <= radiusSq)
                return true;
        }
        return false;
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
    protected void RpcDo_ReceiveDatalinkPayload(array<int> ints, array<float> floats)
    {
        ApplyDatalinkPayload(ints, floats);
    }

    protected void ApplyDatalinkPayload(array<int> ints, array<float> floats)
    {
        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        bool prevAuto = hub.GetAutoFusion();
        hub.SetAutoFusion(false);
        hub.Clear();

        array<ref RDF_RadarDatalinkTrack> tracks = new array<ref RDF_RadarDatalinkTrack>();
        array<ref RDF_RadarFusedTrack> fused = new array<ref RDF_RadarFusedTrack>();
        if (!RDF_RadarNetCodec.UnpackDatalinkRpc(ints, floats, tracks, fused))
        {
            hub.SetAutoFusion(prevAuto);
            return;
        }

        if (tracks.Count() > 0)
            hub.PublishTracks(tracks);
        hub.SetFusedTracks(fused);
        hub.SetAutoFusion(prevAuto);
    }

    override bool RplSave(ScriptBitWriter writer)
    {
        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        RDF_RadarNetCodec.WriteDatalinkBits(writer, hub.GetAllTracks(), hub.GetFusedTracks());
        return true;
    }

    override bool RplLoad(ScriptBitReader reader)
    {
        array<ref RDF_RadarDatalinkTrack> tracks = new array<ref RDF_RadarDatalinkTrack>();
        array<ref RDF_RadarFusedTrack> fused = new array<ref RDF_RadarFusedTrack>();
        RDF_RadarNetCodec.ReadDatalinkBits(reader, tracks, fused);

        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        bool prevAuto = hub.GetAutoFusion();
        hub.SetAutoFusion(false);
        hub.Clear();
        if (tracks.Count() > 0)
            hub.PublishTracks(tracks);
        hub.SetFusedTracks(fused);
        hub.SetAutoFusion(prevAuto);
        return true;
    }
}
