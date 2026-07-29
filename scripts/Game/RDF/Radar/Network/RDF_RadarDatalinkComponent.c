// Optional network face for DatalinkHub: Reliable Broadcast of typed arrays
// for station tracks + fused tracks; RplSave/RplLoad for JIP.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Datalink hub network sync (typed arrays + RplSave)")]
class RDF_RadarDatalinkComponentClass : RDF_RadarDatalinkAPIClass
{
}

class RDF_RadarDatalinkComponent : RDF_RadarDatalinkAPI
{
    protected RplComponent m_RplComponent;
    protected bool m_BroadcastEnabled = true;

    [Attribute(defvalue: "1", desc: "Broadcast datalink/fused summaries to proxies")]
    bool m_EnableBroadcast;

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

        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        array<int> ints = new array<int>();
        array<float> floats = new array<float>();
        RDF_RadarNetCodec.PackDatalinkRpc(
            hub.GetAllTracks(),
            hub.GetFusedTracks(),
            ints,
            floats);
        Rpc(RpcDo_ReceiveDatalinkPayload, ints, floats);
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
