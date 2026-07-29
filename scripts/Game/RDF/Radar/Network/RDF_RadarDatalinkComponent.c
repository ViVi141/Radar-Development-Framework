// Optional network face for DatalinkHub: Reliable Broadcast of D| track rows
// and F| fused rows after authority publish/fusion.
[ComponentEditorProps(category: "GameScripted/RDF", description: "Datalink hub network sync (D|/F| payloads)")]
class RDF_RadarDatalinkComponentClass : RDF_RadarDatalinkAPIClass
{
}

class RDF_RadarDatalinkComponent : RDF_RadarDatalinkAPI
{
    protected RplComponent m_RplComponent;
    protected bool m_BroadcastEnabled = true;

    [Attribute(defvalue: "1", desc: "Broadcast D|/F| summaries to proxies")]
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
        string payload = SerializeHubPayload();
        if (!payload || payload == string.Empty)
            return;
        Rpc(RpcDo_ReceiveDatalinkPayload, payload);
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
    protected void RpcDo_ReceiveDatalinkPayload(string payload)
    {
        if (!payload || payload == string.Empty)
            return;
        ParseHubPayload(payload);
    }

    protected string SerializeHubPayload()
    {
        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        array<ref RDF_RadarDatalinkTrack> tracks = hub.GetAllTracks();
        array<ref RDF_RadarFusedTrack> fused = hub.GetFusedTracks();
        string outCsv = "";

        if (tracks)
        {
            for (int i = 0; i < tracks.Count(); i++)
            {
                RDF_RadarDatalinkTrack t = tracks.Get(i);
                if (!t)
                    continue;
                if (outCsv != string.Empty)
                    outCsv = outCsv + ";";
                outCsv = outCsv + "D|";
                outCsv = outCsv + t.m_SourceRadarId.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_LocalTrackId.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(t.m_WorldPos);
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(t.m_Velocity);
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_RangeM.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_AzimuthDeg.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_ElevationDeg.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_RangeRateMs.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_SnrDb.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + TargetTypeToInt(t.m_Type).ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + IffToInt(t.m_Iff).ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_TimeS.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(t.m_RadarOrigin);
            }
        }

        if (fused)
        {
            for (int f = 0; f < fused.Count(); f++)
            {
                RDF_RadarFusedTrack ft = fused.Get(f);
                if (!ft)
                    continue;
                if (outCsv != string.Empty)
                    outCsv = outCsv + ";";
                outCsv = outCsv + "F|";
                outCsv = outCsv + ft.m_FusedId.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(ft.m_WorldPos);
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(ft.m_Velocity);
                outCsv = outCsv + "|";
                outCsv = outCsv + ft.m_SnrDb.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + TargetTypeToInt(ft.m_Type).ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + IffToInt(ft.m_Iff).ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + ft.m_ContributorCount.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + ft.m_ContributorRadarId0.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + ft.m_ContributorRadarId1.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + BoolToFlag(ft.m_CrossFixUsed);
                outCsv = outCsv + "|";
                outCsv = outCsv + ft.m_TimeS.ToString();
            }
        }

        return outCsv;
    }

    protected void ParseHubPayload(string payload)
    {
        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        bool prevAuto = hub.GetAutoFusion();
        hub.SetAutoFusion(false);
        hub.Clear();
        array<string> rows = new array<string>();
        payload.Split(";", rows, true);
        array<ref RDF_RadarDatalinkTrack> tracks = new array<ref RDF_RadarDatalinkTrack>();
        array<ref RDF_RadarFusedTrack> fused = new array<ref RDF_RadarFusedTrack>();

        for (int i = 0; i < rows.Count(); i++)
        {
            string row = rows.Get(i);
            if (!row || row == string.Empty)
                continue;
            array<string> fields = new array<string>();
            row.Split("|", fields, true);
            if (fields.Count() < 1)
                continue;
            string tag = fields.Get(0);
            if (tag == "D" && fields.Count() >= 14)
            {
                RDF_RadarDatalinkTrack t = new RDF_RadarDatalinkTrack();
                t.m_SourceRadarId = fields.Get(1).ToInt();
                t.m_LocalTrackId = fields.Get(2).ToInt();
                t.m_WorldPos = CsvToVector(fields.Get(3));
                t.m_Velocity = CsvToVector(fields.Get(4));
                t.m_RangeM = fields.Get(5).ToFloat();
                t.m_AzimuthDeg = fields.Get(6).ToFloat();
                t.m_ElevationDeg = fields.Get(7).ToFloat();
                t.m_RangeRateMs = fields.Get(8).ToFloat();
                t.m_SnrDb = fields.Get(9).ToFloat();
                t.m_Type = IntToTargetType(fields.Get(10).ToInt());
                t.m_Iff = IntToIff(fields.Get(11).ToInt());
                t.m_TimeS = fields.Get(12).ToFloat();
                t.m_RadarOrigin = CsvToVector(fields.Get(13));
                tracks.Insert(t);
            }
            else if (tag == "F" && fields.Count() >= 12)
            {
                RDF_RadarFusedTrack ft = new RDF_RadarFusedTrack();
                ft.m_FusedId = fields.Get(1).ToInt();
                ft.m_WorldPos = CsvToVector(fields.Get(2));
                ft.m_Velocity = CsvToVector(fields.Get(3));
                ft.m_SnrDb = fields.Get(4).ToFloat();
                ft.m_Type = IntToTargetType(fields.Get(5).ToInt());
                ft.m_Iff = IntToIff(fields.Get(6).ToInt());
                ft.m_ContributorCount = fields.Get(7).ToInt();
                ft.m_ContributorRadarId0 = fields.Get(8).ToInt();
                ft.m_ContributorRadarId1 = fields.Get(9).ToInt();
                ft.m_CrossFixUsed = fields.Get(10) == "1";
                ft.m_TimeS = fields.Get(11).ToFloat();
                fused.Insert(ft);
            }
        }

        if (tracks.Count() > 0)
            hub.PublishTracks(tracks);
        hub.SetFusedTracks(fused);
        hub.SetAutoFusion(prevAuto);
    }

    protected string VectorToCsv(vector v)
    {
        return v[0].ToString() + "," + v[1].ToString() + "," + v[2].ToString();
    }

    protected vector CsvToVector(string csv)
    {
        array<string> parts = new array<string>();
        csv.Split(",", parts, true);
        if (parts.Count() < 3)
            return "0 0 0";
        return Vector(parts.Get(0).ToFloat(), parts.Get(1).ToFloat(), parts.Get(2).ToFloat());
    }

    protected string BoolToFlag(bool value)
    {
        if (value)
            return "1";
        return "0";
    }

    protected int TargetTypeToInt(ERDF_RadarTargetType t)
    {
        if (t == ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE)
            return 0;
        if (t == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 1;
        if (t == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 2;
        return 3;
    }

    protected ERDF_RadarTargetType IntToTargetType(int v)
    {
        if (v == 0)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
        if (v == 1)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
        if (v == 2)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
        return ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
    }

    protected int IffToInt(ERDF_RadarIff iff)
    {
        if (iff == ERDF_RadarIff.RDF_IFF_FRIEND)
            return 1;
        if (iff == ERDF_RadarIff.RDF_IFF_FOE)
            return 2;
        if (iff == ERDF_RadarIff.RDF_IFF_NEUTRAL)
            return 3;
        return 0;
    }

    protected ERDF_RadarIff IntToIff(int v)
    {
        if (v == 1)
            return ERDF_RadarIff.RDF_IFF_FRIEND;
        if (v == 2)
            return ERDF_RadarIff.RDF_IFF_FOE;
        if (v == 3)
            return ERDF_RadarIff.RDF_IFF_NEUTRAL;
        return ERDF_RadarIff.RDF_IFF_UNKNOWN;
    }
}
