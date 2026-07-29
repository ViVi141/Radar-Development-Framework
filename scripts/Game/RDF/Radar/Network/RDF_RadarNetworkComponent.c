[ComponentEditorProps(category: "GameScripted/RDF", description: "Network synchronization component for Radar framework")]
class RDF_RadarNetworkComponentClass : RDF_RadarNetworkAPIClass
{
}

// Server-authority radar sync: RplProp for slow config, Reliable Rpc for scan
// results (plots + track/WLR/lock summaries). Presentation stays local.
class RDF_RadarNetworkComponent : RDF_RadarNetworkAPI
{
    protected RplComponent m_RplComponent;
    protected ref RDF_RadarSensor m_Sensor;
    protected ref array<ref RDF_RadarTarget> m_LastTargets;
    protected ref array<ref RDF_RadarTrack> m_LastTracks;
    protected float m_LastScanTime = 0.0;
    protected vector m_LastScanOrigin = "0 0 0";
    protected vector m_LastScanForward = "1 0 0";
    protected float m_LastScanRange = 0.0;
    protected bool m_HasSyncedScan;
    protected int m_LastLockState;
    protected int m_LastLockTrackId;
    protected vector m_LastLockAimPos;
    protected bool m_HasSyncedLock;

    // Stable datalink source id (assigned once; not RplId — works without Rpl).
    protected int m_DatalinkSourceId;
    protected static int s_NextDatalinkSourceId = 1;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnDemoEnabledChanged")]
    protected bool m_DemoEnabled = true;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnRangeChanged")]
    protected float m_Range = 2000.0;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnUpdateIntervalChanged")]
    protected float m_UpdateInterval = 0.2;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnVerboseChanged")]
    protected bool m_Verbose = false;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnSectorHalfAngleChanged")]
    protected float m_SectorHalfAngleDeg = 45.0;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnMaxTargetsChanged")]
    protected int m_MaxTargets = 64;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnEnableWeaponLocateChanged")]
    protected bool m_EnableWeaponLocate = true;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnEnableWlrHudAlertsChanged")]
    protected bool m_EnableWlrHudAlerts = true;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnCfarModeChanged")]
    protected int m_CfarMode = 0;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnDetectionSnrDbChanged")]
    protected float m_DetectionSnrDb = 8.0;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnIncludeVehiclesChanged")]
    protected bool m_IncludeVehicles = true;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnIncludeProjectilesChanged")]
    protected bool m_IncludeProjectiles = true;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnIncludeRadarEmittersChanged")]
    protected bool m_IncludeRadarEmitters = true;

    [RplProp(condition: RplCondition.NoOwner, onRplName: "OnEmittingChanged")]
    protected bool m_IsEmitting = true;

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        m_RplComponent = RplComponent.Cast(owner.FindComponent(RplComponent));
        if (!m_LastTargets)
            m_LastTargets = new array<ref RDF_RadarTarget>();
        if (!m_LastTracks)
            m_LastTracks = new array<ref RDF_RadarTrack>();
        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();
        m_HasSyncedScan = false;
        m_HasSyncedLock = false;
        m_LastLockState = 0;
        m_LastLockTrackId = -1;
        m_LastLockAimPos = "0 0 0";
        if (m_DatalinkSourceId <= 0)
        {
            m_DatalinkSourceId = s_NextDatalinkSourceId;
            s_NextDatalinkSourceId = s_NextDatalinkSourceId + 1;
        }
        ApplyNetworkScalarsToSensor();
    }

    override bool IsNetworkAvailable()
    {
        return m_RplComponent != null;
    }

    override void SetEnabled(bool enabled)
    {
        if (!IsNetworkAvailable())
            return;

        if (m_RplComponent.IsProxy())
        {
            Rpc(RpcAsk_SetDemoEnabled, enabled);
            return;
        }

        m_DemoEnabled = enabled;
        ApplyNetworkScalarsToSensor();
        Replication.BumpMe();
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SetDemoEnabled(bool enabled)
    {
        SetEnabled(enabled);
    }

    override void SetEmitting(bool emitting)
    {
        if (!IsNetworkAvailable())
        {
            ApplyLocalEmitting(emitting);
            return;
        }

        if (m_RplComponent.IsProxy())
        {
            if (m_IsEmitting == emitting)
                return;
            Rpc(RpcAsk_SetEmitting, emitting);
            return;
        }

        if (m_IsEmitting == emitting)
        {
            ApplyLocalEmitting(emitting);
            return;
        }

        m_IsEmitting = emitting;
        ApplyLocalEmitting(emitting);
        Replication.BumpMe();
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SetEmitting(bool emitting)
    {
        SetEmitting(emitting);
    }

    override bool IsEmitting()
    {
        return m_IsEmitting;
    }

    override void SetConfig(RDF_RadarSettings config)
    {
        if (!config || !IsNetworkAvailable())
            return;

        if (m_RplComponent.IsProxy())
        {
            // Enforce Rpc() has a low arity cap — pack scalars into one string.
            Rpc(RpcAsk_SetDemoConfig, SerializeConfigScalars(config));
            return;
        }

        ApplyConfigScalarsFromSettings(config);
        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();
        m_Sensor.Configure(config);
        m_Sensor.SetForceLocalScan(true);
        ApplyNetworkScalarsToSensor();
        Replication.BumpMe();
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SetDemoConfig(string configCsv)
    {
        if (!ParseConfigScalars(configCsv))
            return;
        ApplyNetworkScalarsToSensor();
        Replication.BumpMe();
    }

    // C|range|interval|enabled|verbose|sector|maxTargets|wlr|wlrHud|cfar|snr|veh|proj|emit
    protected string SerializeConfigScalars(RDF_RadarSettings config)
    {
        if (!config)
            return string.Empty;

        string csv = "C|";
        csv = csv + config.m_Range.ToString();
        csv = csv + "|";
        csv = csv + config.m_UpdateInterval.ToString();
        csv = csv + "|";
        csv = csv + BoolToFlag(config.m_Enabled);
        csv = csv + "|";
        csv = csv + BoolToFlag(config.m_KeepUndetected);
        csv = csv + "|";
        csv = csv + config.m_SectorHalfAngleDeg.ToString();
        csv = csv + "|";
        csv = csv + config.m_MaxTargets.ToString();
        csv = csv + "|";
        csv = csv + BoolToFlag(config.m_EnableWeaponLocate);
        csv = csv + "|";
        csv = csv + BoolToFlag(config.m_EnableWlrHudAlerts);
        csv = csv + "|";
        csv = csv + CfarModeToInt(config.m_CfarMode).ToString();
        csv = csv + "|";
        csv = csv + config.m_DetectionSnrDb.ToString();
        csv = csv + "|";
        csv = csv + BoolToFlag(config.m_IncludeVehicles);
        csv = csv + "|";
        csv = csv + BoolToFlag(config.m_IncludeProjectiles);
        csv = csv + "|";
        csv = csv + BoolToFlag(config.m_IncludeRadarEmitters);
        return csv;
    }

    protected bool ParseConfigScalars(string configCsv)
    {
        if (!configCsv || configCsv == string.Empty)
            return false;

        array<string> fields = new array<string>();
        configCsv.Split("|", fields, false);
        if (fields.Count() < 14)
            return false;
        if (fields.Get(0) != "C")
            return false;

        m_Range = Math.Clamp(fields.Get(1).ToFloat(), 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, fields.Get(2).ToFloat());
        m_DemoEnabled = FlagToBool(fields.Get(3));
        m_Verbose = FlagToBool(fields.Get(4));
        m_SectorHalfAngleDeg = Math.Clamp(fields.Get(5).ToFloat(), 0.0, 180.0);
        m_MaxTargets = Math.Max(1, fields.Get(6).ToInt());
        m_EnableWeaponLocate = FlagToBool(fields.Get(7));
        m_EnableWlrHudAlerts = FlagToBool(fields.Get(8));
        m_CfarMode = Math.Clamp(fields.Get(9).ToInt(), 0, 2);
        m_DetectionSnrDb = Math.Clamp(fields.Get(10).ToFloat(), -100.0, 200.0);
        m_IncludeVehicles = FlagToBool(fields.Get(11));
        m_IncludeProjectiles = FlagToBool(fields.Get(12));
        m_IncludeRadarEmitters = FlagToBool(fields.Get(13));
        return true;
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

        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();
        ApplyNetworkScalarsToSensor();

        BaseWorld world = GetGame().GetWorld();
        float worldTimeS = 0.0;
        if (world)
            worldTimeS = world.GetWorldTime() * 0.001;

        m_Sensor.SetForceLocalScan(true);
        m_Sensor.ScanOnce(subject, null, worldTimeS);
        UpdateLocalResults(m_Sensor.GetPlots());
        CaptureLocalTracksAndLock();
        PublishTracksToDatalink(worldTimeS);

        string payload = SerializePayload(m_LastTargets);
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
        if (m_Sensor)
        {
            RDF_RadarScanContext ctx = m_Sensor.GetScanContext();
            if (ctx)
            {
                m_LastScanOrigin = ctx.m_Origin;
                m_LastScanForward = ctx.m_Forward;
                m_LastScanRange = ctx.m_RangeM;
            }
        }
        if (GetGame().GetWorld())
            m_LastScanTime = GetGame().GetWorld().GetWorldTime();
        m_HasSyncedScan = true;
    }

    protected void CaptureLocalTracksAndLock()
    {
        if (!m_LastTracks)
            m_LastTracks = new array<ref RDF_RadarTrack>();
        m_LastTracks.Clear();
        m_HasSyncedLock = false;
        m_LastLockState = 0;
        m_LastLockTrackId = -1;
        m_LastLockAimPos = "0 0 0";

        if (!m_Sensor)
            return;

        array<ref RDF_RadarTrack> tracks = m_Sensor.GetTracks();
        if (tracks)
        {
            for (int i = 0; i < tracks.Count(); i++)
            {
                RDF_RadarTrack src = tracks.Get(i);
                if (!src || !src.m_Confirmed)
                    continue;
                RDF_RadarTrack copy = CloneTrackSummary(src);
                if (copy)
                    m_LastTracks.Insert(copy);
            }
        }

        RDF_RadarLockManager lockMgr = m_Sensor.GetLockManager();
        if (lockMgr)
        {
            m_LastLockState = LockStateToInt(lockMgr.GetState());
            m_LastLockTrackId = lockMgr.GetLockedTrackId();
            m_LastLockAimPos = lockMgr.GetLockedPosition();
            m_HasSyncedLock = true;
        }
    }

    // Push confirmed track summaries into the station-to-station datalink hub.
    protected void PublishTracksToDatalink(float worldTimeS)
    {
        RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
        if (!hub || !hub.IsEnabled())
            return;
        if (m_DatalinkSourceId <= 0)
        {
            m_DatalinkSourceId = s_NextDatalinkSourceId;
            s_NextDatalinkSourceId = s_NextDatalinkSourceId + 1;
        }
        if (!m_LastTracks || m_LastTracks.Count() < 1)
        {
            hub.RemoveSource(m_DatalinkSourceId);
            RDF_RadarFusionService.Get().UpdateFromHub(hub, worldTimeS);
            return;
        }

        array<ref RDF_RadarDatalinkTrack> batch = new array<ref RDF_RadarDatalinkTrack>();
        RDF_RadarIffResolver iff = hub.GetIffResolver();
        IEntity subject = GetOwner();
        for (int i = 0; i < m_LastTracks.Count(); i++)
        {
            RDF_RadarTrack src = m_LastTracks.Get(i);
            if (!src)
                continue;
            RDF_RadarDatalinkTrack dt = new RDF_RadarDatalinkTrack();
            dt.m_SourceRadarId = m_DatalinkSourceId;
            dt.m_LocalTrackId = src.m_TrackId;
            dt.m_WorldPos = src.m_FilteredPosition;
            dt.m_Velocity = src.m_FilteredVelocity;
            dt.m_RangeM = src.m_FilteredRangeM;
            dt.m_AzimuthDeg = src.m_FilteredAzimuthDeg;
            dt.m_ElevationDeg = src.m_FilteredElevationDeg;
            dt.m_RangeRateMs = src.m_FilteredRangeRateMs;
            dt.m_SnrDb = src.m_LastSnrDb;
            dt.m_Type = src.m_Type;
            dt.m_TimeS = worldTimeS;
            dt.m_RadarOrigin = m_LastScanOrigin;
            if (iff)
                dt.m_Iff = iff.Resolve(subject, src);
            if (src.m_LastWlrFix)
            {
                dt.m_WlrLaunchValid = src.m_LastWlrFix.m_LaunchValid;
                dt.m_WlrLaunchPos = src.m_LastWlrFix.m_LaunchPos;
                dt.m_WlrImpactValid = src.m_LastWlrFix.m_ImpactValid;
                dt.m_WlrImpactPos = src.m_LastWlrFix.m_ImpactPos;
            }
            batch.Insert(dt);
        }
        hub.PublishFromRadar(m_DatalinkSourceId, m_LastScanOrigin, worldTimeS, batch);

        // If a datalink network component exists on this entity, push Broadcast.
        RDF_RadarDatalinkComponent dlNet = RDF_RadarDatalinkComponent.Cast(
            GetOwner().FindComponent(RDF_RadarDatalinkComponent));
        if (dlNet)
            dlNet.BroadcastHubState();
    }

    int GetDatalinkSourceId()
    {
        return m_DatalinkSourceId;
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

        if (targets)
        {
            for (int i = 0; i < targets.Count(); i++)
            {
                RDF_RadarTarget t = targets.Get(i);
                if (!t)
                    continue;

                outCsv = outCsv + ";T|";
                outCsv = outCsv + TargetTypeToIntString(t.m_Type);
                outCsv = outCsv + "|";
                outCsv = outCsv + BoolToFlag(t.m_Detected);
                outCsv = outCsv + "|";
                outCsv = outCsv + BoolToFlag(t.m_IsAnonymous);
                outCsv = outCsv + "|";
                outCsv = outCsv + BoolToFlag(t.m_IsFalsePlot);
                outCsv = outCsv + "|";
                outCsv = outCsv + BoolToFlag(t.m_LosBlocked);
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_Distance.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_SnrDb.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_CfarPowerW.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(t.m_Position);
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(t.m_Velocity);
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_AzimuthDeg.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_ElevationDeg.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_RadialSpeedMs.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + t.m_ScattererId.ToString();
            }
        }

        if (m_LastTracks)
        {
            for (int k = 0; k < m_LastTracks.Count(); k++)
            {
                RDF_RadarTrack tr = m_LastTracks.Get(k);
                if (!tr)
                    continue;
                outCsv = outCsv + ";K|";
                outCsv = outCsv + tr.m_TrackId.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + TargetTypeToIntString(tr.m_Type);
                outCsv = outCsv + "|";
                outCsv = outCsv + BoolToFlag(tr.m_Confirmed);
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(tr.m_FilteredPosition);
                outCsv = outCsv + "|";
                outCsv = outCsv + VectorToCsv(tr.m_FilteredVelocity);
                outCsv = outCsv + "|";
                outCsv = outCsv + tr.m_FilteredRangeM.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + tr.m_FilteredAzimuthDeg.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + tr.m_FilteredElevationDeg.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + tr.m_FilteredRangeRateMs.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + tr.m_LastSnrDb.ToString();
                outCsv = outCsv + "|";
                outCsv = outCsv + tr.m_HitCount.ToString();

                RDF_RadarWlrFix fix = tr.m_LastWlrFix;
                if (fix && (fix.m_LaunchValid || fix.m_ImpactValid))
                {
                    outCsv = outCsv + ";W|";
                    outCsv = outCsv + tr.m_TrackId.ToString();
                    outCsv = outCsv + "|";
                    outCsv = outCsv + BoolToFlag(fix.m_LaunchValid);
                    outCsv = outCsv + "|";
                    outCsv = outCsv + VectorToCsv(fix.m_LaunchPos);
                    outCsv = outCsv + "|";
                    outCsv = outCsv + BoolToFlag(fix.m_ImpactValid);
                    outCsv = outCsv + "|";
                    outCsv = outCsv + VectorToCsv(fix.m_ImpactPos);
                }
            }
        }

        if (m_HasSyncedLock)
        {
            outCsv = outCsv + ";L|";
            outCsv = outCsv + m_LastLockState.ToString();
            outCsv = outCsv + "|";
            outCsv = outCsv + m_LastLockTrackId.ToString();
            outCsv = outCsv + "|";
            outCsv = outCsv + VectorToCsv(m_LastLockAimPos);
        }

        return outCsv;
    }

    protected void ParsePayload(string payload)
    {
        if (!m_LastTargets)
            m_LastTargets = new array<ref RDF_RadarTarget>();
        if (!m_LastTracks)
            m_LastTracks = new array<ref RDF_RadarTrack>();
        m_LastTargets.Clear();
        m_LastTracks.Clear();
        m_HasSyncedLock = false;
        m_LastLockState = 0;
        m_LastLockTrackId = -1;
        m_LastLockAimPos = "0 0 0";

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
            if (tag == "T")
            {
                ParsePlotRow(fields);
                continue;
            }
            if (tag == "K")
            {
                ParseTrackRow(fields);
                continue;
            }
            if (tag == "W")
            {
                ParseWlrRow(fields);
                continue;
            }
            if (tag == "L")
            {
                ParseLockRow(fields);
                continue;
            }
        }

        if (GetGame().GetWorld())
            m_LastScanTime = GetGame().GetWorld().GetWorldTime();
        m_HasSyncedScan = true;
    }

    protected void ParsePlotRow(array<string> fields)
    {
        if (fields.Count() < 10)
            return;

        RDF_RadarTarget t = new RDF_RadarTarget();
        t.m_Type = IntToTargetType(fields.Get(1).ToInt());
        t.m_Detected = FlagToBool(fields.Get(2));
        t.m_IsAnonymous = FlagToBool(fields.Get(3));
        t.m_IsFalsePlot = FlagToBool(fields.Get(4));
        t.m_LosBlocked = FlagToBool(fields.Get(5));
        t.m_Distance = fields.Get(6).ToFloat();
        t.m_SnrDb = fields.Get(7).ToFloat();
        t.m_CfarPowerW = fields.Get(8).ToFloat();
        t.m_Position = CsvToVector(fields.Get(9));
        t.m_Entity = null;
        t.m_Velocity = "0 0 0";
        t.m_AzimuthDeg = 0.0;
        t.m_ElevationDeg = 0.0;
        t.m_RadialSpeedMs = 0.0;
        t.m_ScattererId = 0;
        t.m_LosHitFraction = 1.0;
        t.m_MultipathFactor = 1.0;

        if (fields.Count() >= 11)
            t.m_Velocity = CsvToVector(fields.Get(10));
        if (fields.Count() >= 12)
            t.m_AzimuthDeg = fields.Get(11).ToFloat();
        if (fields.Count() >= 13)
            t.m_ElevationDeg = fields.Get(12).ToFloat();
        if (fields.Count() >= 14)
            t.m_RadialSpeedMs = fields.Get(13).ToFloat();
        if (fields.Count() >= 15)
            t.m_ScattererId = fields.Get(14).ToInt();

        m_LastTargets.Insert(t);
    }

    protected void ParseTrackRow(array<string> fields)
    {
        if (fields.Count() < 11)
            return;

        RDF_RadarTrack tr = new RDF_RadarTrack();
        tr.m_TrackId = fields.Get(1).ToInt();
        tr.m_Type = IntToTargetType(fields.Get(2).ToInt());
        tr.m_Confirmed = FlagToBool(fields.Get(3));
        tr.m_FilteredPosition = CsvToVector(fields.Get(4));
        tr.m_FilteredVelocity = CsvToVector(fields.Get(5));
        tr.m_FilteredRangeM = fields.Get(6).ToFloat();
        tr.m_FilteredAzimuthDeg = fields.Get(7).ToFloat();
        tr.m_FilteredElevationDeg = fields.Get(8).ToFloat();
        tr.m_FilteredRangeRateMs = fields.Get(9).ToFloat();
        tr.m_LastSnrDb = fields.Get(10).ToFloat();
        if (fields.Count() >= 12)
            tr.m_HitCount = fields.Get(11).ToInt();
        else
            tr.m_HitCount = 1;
        tr.m_MissCount = 0;
        tr.m_Entity = null;
        tr.m_LastUpdateTime = 0.0;
        tr.Push(tr.m_FilteredPosition, tr.m_FilteredVelocity, 0.0);
        m_LastTracks.Insert(tr);
    }

    protected void ParseWlrRow(array<string> fields)
    {
        if (fields.Count() < 6)
            return;
        int trackId = fields.Get(1).ToInt();
        RDF_RadarTrack tr = FindTrackById(trackId);
        if (!tr)
            return;

        RDF_RadarWlrFix fix = new RDF_RadarWlrFix();
        fix.m_LaunchValid = FlagToBool(fields.Get(2));
        fix.m_LaunchPos = CsvToVector(fields.Get(3));
        fix.m_ImpactValid = FlagToBool(fields.Get(4));
        fix.m_ImpactPos = CsvToVector(fields.Get(5));
        tr.m_LastWlrFix = fix;
    }

    protected void ParseLockRow(array<string> fields)
    {
        if (fields.Count() < 4)
            return;
        m_LastLockState = fields.Get(1).ToInt();
        m_LastLockTrackId = fields.Get(2).ToInt();
        m_LastLockAimPos = CsvToVector(fields.Get(3));
        m_HasSyncedLock = true;
    }

    protected RDF_RadarTrack FindTrackById(int trackId)
    {
        if (!m_LastTracks)
            return null;
        for (int i = 0; i < m_LastTracks.Count(); i++)
        {
            RDF_RadarTrack tr = m_LastTracks.Get(i);
            if (tr && tr.m_TrackId == trackId)
                return tr;
        }
        return null;
    }

    protected RDF_RadarTrack CloneTrackSummary(RDF_RadarTrack src)
    {
        if (!src)
            return null;
        RDF_RadarTrack tr = new RDF_RadarTrack();
        tr.m_TrackId = src.m_TrackId;
        tr.m_Type = src.m_Type;
        tr.m_Confirmed = src.m_Confirmed;
        tr.m_FilteredPosition = src.m_FilteredPosition;
        tr.m_FilteredVelocity = src.m_FilteredVelocity;
        tr.m_FilteredRangeM = src.m_FilteredRangeM;
        tr.m_FilteredAzimuthDeg = src.m_FilteredAzimuthDeg;
        tr.m_FilteredElevationDeg = src.m_FilteredElevationDeg;
        tr.m_FilteredRangeRateMs = src.m_FilteredRangeRateMs;
        tr.m_LastSnrDb = src.m_LastSnrDb;
        tr.m_HitCount = src.m_HitCount;
        tr.m_MissCount = src.m_MissCount;
        tr.m_Entity = null;
        tr.m_LastUpdateTime = src.m_LastUpdateTime;
        tr.m_AirDrag = src.m_AirDrag;
        tr.m_UseBallisticPrediction = src.m_UseBallisticPrediction;
        if (src.m_LastWlrFix)
        {
            RDF_RadarWlrFix fix = new RDF_RadarWlrFix();
            fix.m_LaunchValid = src.m_LastWlrFix.m_LaunchValid;
            fix.m_LaunchPos = src.m_LastWlrFix.m_LaunchPos;
            fix.m_ImpactValid = src.m_LastWlrFix.m_ImpactValid;
            fix.m_ImpactPos = src.m_LastWlrFix.m_ImpactPos;
            fix.m_LaunchTimeS = src.m_LastWlrFix.m_LaunchTimeS;
            fix.m_ImpactTimeS = src.m_LastWlrFix.m_ImpactTimeS;
            fix.m_AnchorTimeS = src.m_LastWlrFix.m_AnchorTimeS;
            tr.m_LastWlrFix = fix;
        }
        return tr;
    }

    override bool HasSyncedTargets()
    {
        if (!m_HasSyncedScan)
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

    override array<ref RDF_RadarTrack> GetLastTracks()
    {
        return m_LastTracks;
    }

    override bool GetLastLockState(out int lockState, out int trackId, out vector aimPos)
    {
        if (!m_HasSyncedLock)
        {
            lockState = 0;
            trackId = -1;
            aimPos = "0 0 0";
            return false;
        }
        lockState = m_LastLockState;
        trackId = m_LastLockTrackId;
        aimPos = m_LastLockAimPos;
        return true;
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

    protected void ApplyConfigScalarsFromSettings(RDF_RadarSettings config)
    {
        if (!config)
            return;
        m_Range = Math.Clamp(config.m_Range, 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, config.m_UpdateInterval);
        m_DemoEnabled = config.m_Enabled;
        m_Verbose = config.m_KeepUndetected;
        m_SectorHalfAngleDeg = Math.Clamp(config.m_SectorHalfAngleDeg, 0.0, 180.0);
        m_MaxTargets = Math.Max(1, config.m_MaxTargets);
        m_EnableWeaponLocate = config.m_EnableWeaponLocate;
        m_EnableWlrHudAlerts = config.m_EnableWlrHudAlerts;
        m_CfarMode = CfarModeToInt(config.m_CfarMode);
        m_DetectionSnrDb = Math.Clamp(config.m_DetectionSnrDb, -100.0, 200.0);
        m_IncludeVehicles = config.m_IncludeVehicles;
        m_IncludeProjectiles = config.m_IncludeProjectiles;
        m_IncludeRadarEmitters = config.m_IncludeRadarEmitters;
    }

    protected void ApplyNetworkScalarsToSensor()
    {
        if (!m_Sensor)
            m_Sensor = new RDF_RadarSensor();
        RDF_RadarSettings settings = m_Sensor.GetSettings();
        if (!settings)
            return;

        settings.m_Enabled = m_DemoEnabled;
        settings.m_Range = m_Range;
        settings.m_UpdateInterval = m_UpdateInterval;
        settings.m_KeepUndetected = m_Verbose;
        settings.m_SectorHalfAngleDeg = m_SectorHalfAngleDeg;
        settings.m_MaxTargets = m_MaxTargets;
        settings.m_EnableWeaponLocate = m_EnableWeaponLocate;
        settings.m_EnableWlrHudAlerts = m_EnableWlrHudAlerts;
        settings.m_CfarMode = IntToCfarMode(m_CfarMode);
        settings.m_DetectionSnrDb = m_DetectionSnrDb;
        settings.m_IncludeVehicles = m_IncludeVehicles;
        settings.m_IncludeProjectiles = m_IncludeProjectiles;
        settings.m_IncludeRadarEmitters = m_IncludeRadarEmitters;
        settings.Validate();
        m_Sensor.SetEnabled(m_DemoEnabled);
        m_Sensor.SetForceLocalScan(true);
    }

    protected void ApplyLocalEmitting(bool emitting)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return;
        RDF_RadarEmitterRegistry.SetEmitting(owner, emitting);
        if (!emitting)
            RDF_RadarEmitterRegistry.Unregister(owner);
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

    protected int CfarModeToInt(ERDF_CfarMode mode)
    {
        if (mode == ERDF_CfarMode.RDF_CFAR_GO)
            return 1;
        if (mode == ERDF_CfarMode.RDF_CFAR_SO)
            return 2;
        return 0;
    }

    protected ERDF_CfarMode IntToCfarMode(int mode)
    {
        if (mode == 1)
            return ERDF_CfarMode.RDF_CFAR_GO;
        if (mode == 2)
            return ERDF_CfarMode.RDF_CFAR_SO;
        return ERDF_CfarMode.RDF_CFAR_CA;
    }

    protected int LockStateToInt(ERDF_RadarLockState state)
    {
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_ACQUIRING)
            return 1;
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_TRACKING)
            return 2;
        if (state == ERDF_RadarLockState.RDF_RADAR_LOCK_COAST)
            return 3;
        return 0;
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
        string csv = v[0].ToString();
        csv = csv + ",";
        csv = csv + v[1].ToString();
        csv = csv + ",";
        csv = csv + v[2].ToString();
        return csv;
    }

    protected vector CsvToVector(string csv)
    {
        array<string> vals = new array<string>();
        csv.Split(",", vals, false);
        if (vals.Count() < 3)
            return "0 0 0";
        return Vector(vals.Get(0).ToFloat(), vals.Get(1).ToFloat(), vals.Get(2).ToFloat());
    }

    void OnDemoEnabledChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnRangeChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnUpdateIntervalChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnVerboseChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnSectorHalfAngleChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnMaxTargetsChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnEnableWeaponLocateChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnEnableWlrHudAlertsChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnCfarModeChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnDetectionSnrDbChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnIncludeVehiclesChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnIncludeProjectilesChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnIncludeRadarEmittersChanged()
    {
        ApplyNetworkScalarsToSensor();
    }

    void OnEmittingChanged()
    {
        ApplyLocalEmitting(m_IsEmitting);
    }
}
