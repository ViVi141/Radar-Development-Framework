[ComponentEditorProps(category: "GameScripted/RDF", description: "Network synchronization component for Radar framework")]
class RDF_RadarNetworkComponentClass : RDF_RadarNetworkAPIClass
{
}

// Server-authority radar sync aligned with stock patterns:
// Reliable = confirmed tracks + lock; Unreliable = optional HUD plots;
// caps / throttle / interest radius; RplSave/RplLoad for JIP.
class RDF_RadarNetworkComponent : RDF_RadarNetworkAPI
{
    protected RplComponent m_RplComponent;
    protected ref RDF_RadarSensor m_Sensor;
    protected ref array<ref RDF_RadarTarget> m_LastTargets;
    protected ref array<ref RDF_RadarTrack> m_LastTracks;
    // Raw GetWorldTime() milliseconds of the last scan (world clock; ms).
    protected float m_LastScanTimeMs = 0.0;
    protected vector m_LastScanOrigin = "0 0 0";
    protected vector m_LastScanForward = "1 0 0";
    protected float m_LastScanRange = 0.0;
    protected bool m_HasSyncedScan;
    protected int m_LastLockState;
    protected int m_LastLockTrackId;
    protected vector m_LastLockAimPos;
    protected bool m_HasSyncedLock;

    protected int m_DatalinkSourceId;
    protected static int s_NextDatalinkSourceId = 1;

    protected float m_LastReliableBroadcastMs = -100000.0;
    protected float m_LastPlotBroadcastMs = -100000.0;
    protected int m_LastSummaryFingerprint = 0;

    // Bandwidth / scale policy (editor Attributes, not RplProp — authority-only knobs).
    [Attribute(defvalue: "1", desc: "Unreliable Broadcast of capped plots for HUD (official: FX-like)")]
    bool m_SyncPlotsUnreliable;

    [Attribute(defvalue: "24", desc: "Max plots in Unreliable plot Rpc (0 = unlimited)")]
    int m_MaxSyncedPlots;

    [Attribute(defvalue: "32", desc: "Max confirmed tracks in Reliable summary Rpc (0 = unlimited)")]
    int m_MaxSyncedTracks;

    [Attribute(defvalue: "0.15", desc: "Min seconds between Reliable summary Broadcasts")]
    float m_MinReliableBroadcastIntervalS;

    [Attribute(defvalue: "0.05", desc: "Min seconds between Unreliable plot Broadcasts")]
    float m_MinPlotBroadcastIntervalS;

    [Attribute(defvalue: "1", desc: "Skip Reliable Broadcast when track/lock fingerprint unchanged")]
    bool m_SkipUnchangedSummary;

    [Attribute(defvalue: "12000", desc: "Only Broadcast if a player is within this radius (m); 0 = always")]
    float m_InterestRadiusM;

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
            array<int> configInts = new array<int>();
            array<float> configFloats = new array<float>();
            RDF_RadarNetCodec.PackConfigRpc(
                config.m_Range,
                config.m_UpdateInterval,
                config.m_Enabled,
                config.m_KeepUndetected,
                config.m_SectorHalfAngleDeg,
                config.m_MaxTargets,
                config.m_EnableWeaponLocate,
                config.m_EnableWlrHudAlerts,
                CfarModeToInt(config.m_CfarMode),
                config.m_DetectionSnrDb,
                config.m_IncludeVehicles,
                config.m_IncludeProjectiles,
                config.m_IncludeRadarEmitters,
                configInts,
                configFloats);
            Rpc(RpcAsk_SetDemoConfig, configInts, configFloats);
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
    protected void RpcAsk_SetDemoConfig(array<int> configInts, array<float> configFloats)
    {
        float rangeM;
        float updateInterval;
        bool enabled;
        bool verbose;
        float sectorHalfAngleDeg;
        int maxTargets;
        bool enableWeaponLocate;
        bool enableWlrHudAlerts;
        int cfarMode;
        float detectionSnrDb;
        bool includeVehicles;
        bool includeProjectiles;
        bool includeRadarEmitters;
        if (!RDF_RadarNetCodec.UnpackConfigRpc(
            configInts,
            configFloats,
            rangeM,
            updateInterval,
            enabled,
            verbose,
            sectorHalfAngleDeg,
            maxTargets,
            enableWeaponLocate,
            enableWlrHudAlerts,
            cfarMode,
            detectionSnrDb,
            includeVehicles,
            includeProjectiles,
            includeRadarEmitters))
        {
            return;
        }

        m_Range = Math.Clamp(rangeM, 1.0, 100000.0);
        m_UpdateInterval = Math.Max(0.05, updateInterval);
        m_DemoEnabled = enabled;
        m_Verbose = verbose;
        m_SectorHalfAngleDeg = Math.Clamp(sectorHalfAngleDeg, 0.0, 180.0);
        m_MaxTargets = Math.Max(1, maxTargets);
        m_EnableWeaponLocate = enableWeaponLocate;
        m_EnableWlrHudAlerts = enableWlrHudAlerts;
        m_CfarMode = Math.Clamp(cfarMode, 0, 2);
        m_DetectionSnrDb = Math.Clamp(detectionSnrDb, -100.0, 200.0);
        m_IncludeVehicles = includeVehicles;
        m_IncludeProjectiles = includeProjectiles;
        m_IncludeRadarEmitters = includeRadarEmitters;
        ApplyNetworkScalarsToSensor();
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
        BroadcastScanState();
    }

    protected void BroadcastScanState()
    {
        if (!m_RplComponent)
            return;
        if (m_RplComponent.IsProxy())
            return;

        float nowMs = 0.0;
        if (GetGame().GetWorld())
            nowMs = GetGame().GetWorld().GetWorldTime();

        if (!HasInterestedAudience())
            return;

        int fingerprint = RDF_RadarNetCodec.FingerprintTracksAndLock(
            m_LastTracks,
            m_HasSyncedLock,
            m_LastLockState,
            m_LastLockTrackId);

        bool allowReliable = true;
        float reliableIntervalMs = Math.Max(0.0, m_MinReliableBroadcastIntervalS) * 1000.0;
        if ((nowMs - m_LastReliableBroadcastMs) < reliableIntervalMs)
            allowReliable = false;

        bool summaryChanged = fingerprint != m_LastSummaryFingerprint;
        if (m_SkipUnchangedSummary && !summaryChanged)
            allowReliable = false;

        // Always push when lock engages / changes even if throttled lightly.
        if (summaryChanged && m_HasSyncedLock && m_LastLockState > 0)
            allowReliable = true;

        if (allowReliable)
        {
            array<int> summaryInts = new array<int>();
            array<float> summaryFloats = new array<float>();
            RDF_RadarNetCodec.PackScanRpc(
                m_LastScanOrigin,
                m_LastScanForward,
                m_LastScanRange,
                m_HasSyncedLock,
                m_LastLockState,
                m_LastLockTrackId,
                m_LastLockAimPos,
                null,
                m_LastTracks,
                summaryInts,
                summaryFloats,
                0,
                m_MaxSyncedTracks,
                false);
            Rpc(RpcDo_ReceiveScanSummary, summaryInts, summaryFloats);
            m_LastReliableBroadcastMs = nowMs;
            m_LastSummaryFingerprint = fingerprint;
        }

        if (!m_SyncPlotsUnreliable)
            return;

        float plotIntervalMs = Math.Max(0.0, m_MinPlotBroadcastIntervalS) * 1000.0;
        if ((nowMs - m_LastPlotBroadcastMs) < plotIntervalMs)
            return;

        array<int> plotInts = new array<int>();
        array<float> plotFloats = new array<float>();
        RDF_RadarNetCodec.PackScanPlotsRpc(
            m_LastTargets,
            m_MaxSyncedPlots,
            plotInts,
            plotFloats);
        Rpc(RpcDo_ReceiveScanPlots, plotInts, plotFloats);
        m_LastPlotBroadcastMs = nowMs;
    }

    protected bool HasInterestedAudience()
    {
        if (m_InterestRadiusM <= 0.0)
            return true;

        PlayerManager pm = GetGame().GetPlayerManager();
        if (!pm)
            return true;

        array<int> players = new array<int>();
        pm.GetPlayers(players);
        if (players.Count() < 1)
            return true;

        float radiusSq = m_InterestRadiusM * m_InterestRadiusM;
        for (int i = 0; i < players.Count(); i++)
        {
            IEntity controlled = pm.GetPlayerControlledEntity(players.Get(i));
            if (!controlled)
                continue;
            vector delta = controlled.GetOrigin() - m_LastScanOrigin;
            float distSq = delta[0] * delta[0] + delta[2] * delta[2];
            if (distSq <= radiusSq)
                return true;
        }
        return false;
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
            m_LastScanTimeMs = GetGame().GetWorld().GetWorldTime();
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
            dt.m_NctrClass = src.m_NctrClass;
            dt.m_NctrConfidence = src.m_NctrConfidence;
            dt.m_Confidence = src.m_Confidence;
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
    protected void RpcDo_ReceiveScanSummary(array<int> ints, array<float> floats)
    {
        ApplyScanSummary(ints, floats);
    }

    [RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
    protected void RpcDo_ReceiveScanPlots(array<int> ints, array<float> floats)
    {
        ApplyScanPlots(ints, floats);
    }

    protected void ApplyScanSummary(array<int> ints, array<float> floats)
    {
        if (!m_LastTracks)
            m_LastTracks = new array<ref RDF_RadarTrack>();

        array<ref RDF_RadarTarget> ignoredPlots = new array<ref RDF_RadarTarget>();
        vector origin;
        vector forward;
        float rangeM;
        bool hasLock;
        int lockState;
        int lockTrackId;
        vector lockAim;
        if (!RDF_RadarNetCodec.UnpackScanRpc(
            ints,
            floats,
            origin,
            forward,
            rangeM,
            hasLock,
            lockState,
            lockTrackId,
            lockAim,
            ignoredPlots,
            m_LastTracks))
        {
            return;
        }

        m_LastScanOrigin = origin;
        m_LastScanForward = forward;
        m_LastScanRange = rangeM;
        m_HasSyncedLock = hasLock;
        m_LastLockState = lockState;
        m_LastLockTrackId = lockTrackId;
        m_LastLockAimPos = lockAim;

        if (GetGame().GetWorld())
            m_LastScanTimeMs = GetGame().GetWorld().GetWorldTime();
        m_HasSyncedScan = true;
    }

    protected void ApplyScanPlots(array<int> ints, array<float> floats)
    {
        if (!m_LastTargets)
            m_LastTargets = new array<ref RDF_RadarTarget>();
        if (!RDF_RadarNetCodec.UnpackScanPlotsRpc(ints, floats, m_LastTargets))
            return;
        if (GetGame().GetWorld())
            m_LastScanTimeMs = GetGame().GetWorld().GetWorldTime();
        m_HasSyncedScan = true;
    }

    override bool RplSave(ScriptBitWriter writer)
    {
        writer.WriteBool(m_HasSyncedScan);
        if (!m_HasSyncedScan)
            return true;
        RDF_RadarNetCodec.WriteScanBits(
            writer,
            m_LastScanOrigin,
            m_LastScanForward,
            m_LastScanRange,
            m_HasSyncedLock,
            m_LastLockState,
            m_LastLockTrackId,
            m_LastLockAimPos,
            m_LastTargets,
            m_LastTracks);
        return true;
    }

    override bool RplLoad(ScriptBitReader reader)
    {
        bool hasScan;
        reader.ReadBool(hasScan);
        m_HasSyncedScan = hasScan;
        if (!hasScan)
            return true;

        if (!m_LastTargets)
            m_LastTargets = new array<ref RDF_RadarTarget>();
        if (!m_LastTracks)
            m_LastTracks = new array<ref RDF_RadarTrack>();

        vector origin;
        vector forward;
        float rangeM;
        bool hasLock;
        int lockState;
        int lockTrackId;
        vector lockAim;
        RDF_RadarNetCodec.ReadScanBits(
            reader,
            origin,
            forward,
            rangeM,
            hasLock,
            lockState,
            lockTrackId,
            lockAim,
            m_LastTargets,
            m_LastTracks);
        m_LastScanOrigin = origin;
        m_LastScanForward = forward;
        m_LastScanRange = rangeM;
        m_HasSyncedLock = hasLock;
        m_LastLockState = lockState;
        m_LastLockTrackId = lockTrackId;
        m_LastLockAimPos = lockAim;
        if (GetGame().GetWorld())
            m_LastScanTimeMs = GetGame().GetWorld().GetWorldTime();
        return true;
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
        if (now - m_LastScanTimeMs > 60000.0)
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

