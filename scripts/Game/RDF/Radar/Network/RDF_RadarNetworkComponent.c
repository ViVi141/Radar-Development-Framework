[ComponentEditorProps(category: "GameScripted/RDF", description: "Network synchronization component for Radar framework")]
class RDF_RadarNetworkComponentClass : RDF_RadarNetworkAPIClass
{
}

class RDF_RadarNetworkComponent : RDF_RadarNetworkAPI
{
    protected RplComponent m_RplComponent;
    protected ref RDF_RadarScanner m_Scanner;
    protected ref array<ref RDF_RadarTarget> m_LastTargets;
    protected float m_LastScanTime = 0.0;
    protected vector m_LastScanOrigin = "0 0 0";
    protected vector m_LastScanForward = "1 0 0";
    protected float m_LastScanRange = 0.0;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoEnabledChanged")]
    protected bool m_DemoEnabled = true;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnRangeChanged")]
    protected float m_Range = 2000.0;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnUpdateIntervalChanged")]
    protected float m_UpdateInterval = 0.2;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnVerboseChanged")]
    protected bool m_Verbose = false;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        m_RplComponent = RplComponent.Cast(owner.FindComponent(RplComponent));
        if (!m_LastTargets)
            m_LastTargets = new array<ref RDF_RadarTarget>();
        if (!m_Scanner)
            m_Scanner = new RDF_RadarScanner();
    }

    override bool IsNetworkAvailable()
    {
        return m_RplComponent != null;
    }

    override void SetDemoEnabled(bool enabled)
    {
        if (!IsNetworkAvailable())
            return;

        if (m_RplComponent.IsProxy())
        {
            Rpc(RpcAsk_SetDemoEnabled, enabled);
            return;
        }

        m_DemoEnabled = enabled;
        Replication.BumpMe();
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SetDemoEnabled(bool enabled)
    {
        SetDemoEnabled(enabled);
    }

    override void SetDemoConfig(RDF_RadarSettings config)
    {
        if (!config || !IsNetworkAvailable())
            return;

        if (m_RplComponent.IsProxy())
        {
            Rpc(
                RpcAsk_SetDemoConfig,
                config.m_Range,
                config.m_UpdateInterval,
                config.m_Enabled,
                config.m_KeepUndetected);
            return;
        }

        m_Range = Math.Clamp(config.m_Range, 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, config.m_UpdateInterval);
        m_DemoEnabled = config.m_Enabled;
        m_Verbose = config.m_KeepUndetected;
        // Keep a full settings object on the authority scanner (not just the
        // handful of replicated scalars), so projectile / WLR test configs apply.
        m_Scanner = new RDF_RadarScanner(config);
        Replication.BumpMe();
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SetDemoConfig(
        float rangeM,
        float updateInterval,
        bool enabled,
        bool verbose)
    {
        m_Range = Math.Clamp(rangeM, 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, updateInterval);
        m_DemoEnabled = enabled;
        m_Verbose = verbose;
        Replication.BumpMe();
    }

    override void RequestScan()
    {
        if (!IsNetworkAvailable())
            return;
        if (!m_DemoEnabled)
            return;

        if (m_RplComponent.IsProxy())
        {
            Rpc(RpcAsk_PerformScan);
            return;
        }

        PerformScanInternal();
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_PerformScan()
    {
        PerformScanInternal();
    }

    protected void PerformScanInternal()
    {
        IEntity subject = GetOwner();
        if (!subject)
            return;

        if (!m_Scanner)
            m_Scanner = new RDF_RadarScanner();
        RDF_RadarSettings settings = m_Scanner.GetSettings();
        if (!settings)
            return;
        settings.m_Enabled = m_DemoEnabled;
        settings.m_Range = m_Range;
        settings.m_UpdateInterval = m_UpdateInterval;
        settings.m_KeepUndetected = m_Verbose;

        array<ref RDF_RadarTarget> results = new array<ref RDF_RadarTarget>();
        m_Scanner.Scan(subject, results);
        UpdateLocalResults(results);

        string payload = SerializePayload(results);
        if (!payload || payload == string.Empty)
            return;
        Rpc(RpcDo_ReceiveScanPayload, payload);
    }

    protected void UpdateLocalResults(array<ref RDF_RadarTarget> results)
    {
        if (!m_LastTargets)
            m_LastTargets = new array<ref RDF_RadarTarget>();
        m_LastTargets.Clear();
        if (results)
        {
            for (int i = 0; i < results.Count(); i++)
            {
                RDF_RadarTarget t = results.Get(i);
                if (t)
                    m_LastTargets.Insert(t);
            }
        }
        if (m_Scanner)
        {
            m_LastScanOrigin = m_Scanner.GetLastOrigin();
            m_LastScanForward = m_Scanner.GetLastForward();
            m_LastScanRange = m_Scanner.GetLastRange();
        }
        if (GetGame().GetWorld())
            m_LastScanTime = GetGame().GetWorld().GetWorldTime();
        Replication.BumpMe();
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
    protected void RpcDo_ReceiveScanPayload(string payload)
    {
        if (!payload || payload == string.Empty)
            return;
        ParsePayload(payload);
    }

    protected string SerializePayload(array<ref RDF_RadarTarget> targets)
    {
        string originCsv = VectorToCsv(m_LastScanOrigin);
        string forwardCsv = VectorToCsv(m_LastScanForward);
        string rangeText = m_LastScanRange.ToString();
        string outCsv = "M|";
        outCsv = outCsv + originCsv;
        outCsv = outCsv + "|";
        outCsv = outCsv + forwardCsv;
        outCsv = outCsv + "|";
        outCsv = outCsv + rangeText;

        if (!targets || targets.Count() <= 0)
            return outCsv;

        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;

            string typeText = TargetTypeToIntString(t.m_Type);
            string detectedFlag = BoolToFlag(t.m_Detected);
            string anonFlag = BoolToFlag(t.m_IsAnonymous);
            string falseFlag = BoolToFlag(t.m_IsFalsePlot);
            string losFlag = BoolToFlag(t.m_LosBlocked);
            string distanceText = t.m_Distance.ToString();
            string snrText = t.m_SnrDb.ToString();
            string powerText = t.m_CfarPowerW.ToString();
            string positionCsv = VectorToCsv(t.m_Position);

            outCsv = outCsv + ";T|";
            outCsv = outCsv + typeText;
            outCsv = outCsv + "|";
            outCsv = outCsv + detectedFlag;
            outCsv = outCsv + "|";
            outCsv = outCsv + anonFlag;
            outCsv = outCsv + "|";
            outCsv = outCsv + falseFlag;
            outCsv = outCsv + "|";
            outCsv = outCsv + losFlag;
            outCsv = outCsv + "|";
            outCsv = outCsv + distanceText;
            outCsv = outCsv + "|";
            outCsv = outCsv + snrText;
            outCsv = outCsv + "|";
            outCsv = outCsv + powerText;
            outCsv = outCsv + "|";
            outCsv = outCsv + positionCsv;
        }
        return outCsv;
    }

    protected void ParsePayload(string payload)
    {
        if (!m_LastTargets)
            m_LastTargets = new array<ref RDF_RadarTarget>();
        m_LastTargets.Clear();

        array<string> parts = new array<string>();
        payload.Split(";", parts, false);
        array<string> fields = new array<string>();
        for (int i = 0; i < parts.Count(); i++)
        {
            string row = parts.Get(i);
            fields.Clear();
            row.Split("|", fields, false);
            if (fields.Count() <= 0)
                continue;

            string tag = fields.Get(0);
            if (tag == "M")
            {
                if (fields.Count() >= 4)
                {
                    m_LastScanOrigin = CsvToVector(fields.Get(1));
                    m_LastScanForward = CsvToVector(fields.Get(2));
                    m_LastScanRange = fields.Get(3).ToFloat();
                }
                continue;
            }
            if (tag != "T")
                continue;
            if (fields.Count() < 10)
                continue;

            RDF_RadarTarget t = new RDF_RadarTarget();
            int typeValue = fields.Get(1).ToInt();
            t.m_Type = IntToTargetType(typeValue);
            t.m_Detected = FlagToBool(fields.Get(2));
            t.m_IsAnonymous = FlagToBool(fields.Get(3));
            t.m_IsFalsePlot = FlagToBool(fields.Get(4));
            t.m_LosBlocked = FlagToBool(fields.Get(5));
            t.m_Distance = fields.Get(6).ToFloat();
            t.m_SnrDb = fields.Get(7).ToFloat();
            t.m_CfarPowerW = fields.Get(8).ToFloat();
            t.m_Position = CsvToVector(fields.Get(9));
            t.m_Entity = null;
            t.m_LosHitFraction = 1.0;
            t.m_MultipathFactor = 1.0;
            m_LastTargets.Insert(t);
        }

        if (GetGame().GetWorld())
            m_LastScanTime = GetGame().GetWorld().GetWorldTime();
    }

    override bool HasSyncedTargets()
    {
        if (!m_LastTargets)
            return false;
        if (m_LastTargets.Count() <= 0)
            return false;

        float now = 0.0;
        if (GetGame().GetWorld())
            now = GetGame().GetWorld().GetWorldTime();
        if (now - m_LastScanTime > 60.0)
            return false;
        return true;
    }

    override array<ref RDF_RadarTarget> GetLastTargets()
    {
        return m_LastTargets;
    }

    override vector GetLastScanOrigin()
    {
        return m_LastScanOrigin;
    }

    override vector GetLastScanForward()
    {
        return m_LastScanForward;
    }

    override float GetLastScanRange()
    {
        return m_LastScanRange;
    }

    protected string BoolToFlag(bool value)
    {
        if (value)
            return "1";
        return "0";
    }

    protected bool FlagToBool(string flag)
    {
        return flag == "1";
    }

    protected string TargetTypeToIntString(ERDF_RadarTargetType type)
    {
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return "1";
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return "2";
        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS)
            return "3";
        return "0";
    }

    protected ERDF_RadarTargetType IntToTargetType(int typeValue)
    {
        if (typeValue == 1)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
        if (typeValue == 2)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
        if (typeValue == 3)
            return ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        return ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
    }

    protected string VectorToCsv(vector v)
    {
        string x = v[0].ToString();
        string y = v[1].ToString();
        string z = v[2].ToString();
        string csv = x;
        csv = csv + ",";
        csv = csv + y;
        csv = csv + ",";
        csv = csv + z;
        return csv;
    }

    protected vector CsvToVector(string csv)
    {
        array<string> vals = new array<string>();
        csv.Split(",", vals, false);
        if (vals.Count() < 3)
            return "0 0 0";
        float x = vals.Get(0).ToFloat();
        float y = vals.Get(1).ToFloat();
        float z = vals.Get(2).ToFloat();
        return Vector(x, y, z);
    }

    void OnDemoEnabledChanged()
    {
    }

    void OnRangeChanged()
    {
    }

    void OnUpdateIntervalChanged()
    {
    }

    void OnVerboseChanged()
    {
    }
}
