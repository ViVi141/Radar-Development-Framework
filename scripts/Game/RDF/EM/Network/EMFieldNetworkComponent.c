//
//EMFieldNetworkComponent  Phase 5 server/client network sync for EM voxel field.
//
// Responsibilities
// ------------------------------------------------------------------------
// SERVER
//   - Ticks EMVoxelField.TickDecay() every frame.
//   - Every m_SyncInterval seconds, collects the top-N highest-power voxel cells
//     and broadcasts compact EMDetectionResult packets to all clients via
 //     unreliable broadcast RPC (low-bandwidth, best-effort).
//   - Applies sector-activation pruning: calls
//    EMVoxelField.RegisterActiveSector() for the configured radar origin/azimuth.
//    The radar scanner (or external code) should also call this each sweep.
//
// CLIENT
//  - Never creates or modifies EMVoxelField.
//  - On RPC receipt, deserialises the packed string into EMDetectionResult objects.
//  - Stores results in m_LastDetections for HUD / passive-sensor polling.
//  - Fires the virtual OnDetectionsReceived() callback for extension points.
//
//Usage
//------------------------------------------------------------------------
//   1. Add this component (or a child entity with this component) to a persistent
//     game-mode entity, e.g. the same entity as SCR_BaseGameMode.
//  2. Attach an RplComponent to the same entity for replication.
//  3. On server, EMVoxelField is ticked automatically; injections can still be
//     done from any server-side code (radar scanner, jammer, etc.).
//  4. On clients, call GetLastDetections() or override OnDetectionsReceived().
//
// Notes
//------------------------------------------------------------------------
//  - The voxel field itself is NEVER synced  only derived detection results.
// - Unreliable RPC is used deliberately: a missed packet simply means slightly
//    stale data, which is acceptable for EM display purposes.
//  - To reduce bandwidth further, set m_MaxDetectionsPerPacket 2.
//

[ComponentEditorProps(category: "GameScripted/RDF", description: "Network sync component for EM voxel field (Phase 5)")]
class EMFieldNetworkComponentClass : EMFieldNetworkAPIClass {}

class EMFieldNetworkComponent : EMFieldNetworkAPI
{
    // -- Server configuration (replicated to clients via RplProp) ------------------------------------------------------------------------

    [Attribute("50.0",   UIWidgets.EditBox, "Voxel cell size (m)  must match EMVoxelField.GetInstance() at runtime")]
    protected float m_CellSize;

    [Attribute("1.0",    UIWidgets.EditBox, "Base voxel power decay rate per second")]
    protected float m_DecayRate;

    [Attribute("0.5",    UIWidgets.EditBox, "Detection sync broadcast interval (seconds)")]
    protected float m_SyncInterval;

    [Attribute("48",     UIWidgets.EditBox, "Max cells to include per broadcast packet")]
    protected int m_MaxDetectionsPerPacket;

    [Attribute("-120.0", UIWidgets.EditBox, "Minimum power (dBm) required to include a cell in a packet")]
    protected float m_MinPowerDbm;

    // When true the component also calls DebugDrawColorMapped() every frame (server only).
    [Attribute("0",      UIWidgets.CheckBox, "Draw color-mapped voxel debug spheres (server only)")]
    protected bool m_DebugDrawOnServer;

    // -- Replicated properties ------------------------------------------------------------------------

    // Broadcast the radar origin so clients can compute bearings.
    [RplProp(condition: RplCondition.NoOwner)]
    protected vector m_RadarOrigin;

    // -- Runtime state ------------------------------------------------------------------------
    protected RplComponent m_RplComponent;
    protected float m_SyncTimer;

    // Received detections (populated on all machines by the broadcast RPC).
    protected ref array<ref EMDetectionResult> m_LastDetections;
    protected float m_LastDetectionTime;

    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------

    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);

        m_RplComponent       = RplComponent.Cast(owner.FindComponent(RplComponent));
        m_LastDetections     = new array<ref EMDetectionResult>();
        m_LastDetectionTime  = 0.0;
        m_SyncTimer          = 0.0;

        if (!m_RplComponent)
        {
            Print("[EMFieldNetworkComponent] WARNING: No RplComponent found  network sync disabled.", LogLevel.WARNING);
            return;
        }

        // Server: mark voxel field as server-mode (should already be default).
        if (!m_RplComponent.IsProxy())
        {
            EMVoxelField field = EMVoxelField.GetInstance();
            if (field)
                field.SetServerMode(true);
            Print("[EMFieldNetworkComponent] Server mode  voxel field active.", LogLevel.NORMAL);
        }
        else
        {
            Print("[EMFieldNetworkComponent] Client mode  awaiting detection broadcasts.", LogLevel.NORMAL);
        }

        SetEventMask(owner, EntityEvent.FRAME);
    }

    // ------------------------------------------------------------------------
    // Frame update
    // ------------------------------------------------------------------------

    override void EOnFrame(IEntity owner, float timeSlice)
    {
        // Only the server runs the voxel field logic.
        if (!m_RplComponent || m_RplComponent.IsProxy())
            return;

        EMVoxelField field = EMVoxelField.GetInstance();
        if (!field)
            return;

        // Tick decay (includes sector pruning + budget enforcement).
        field.TickDecay(timeSlice);

        // Optional live debug draw.
        if (m_DebugDrawOnServer)
            field.DebugDrawColorMapped(128, timeSlice);

        // Periodic broadcast.
        m_SyncTimer += timeSlice;
        if (m_SyncTimer >= m_SyncInterval)
        {
            m_SyncTimer = 0.0;
            BroadcastDetections(field);
        }
    }

    // ------------------------------------------------------------------------
    // Server: collect top cells and broadcast
    // ------------------------------------------------------------------------

    protected void BroadcastDetections(EMVoxelField field)
    {
        if (field.GetActiveCellCount() == 0)
            return;

        // Collect top-N cells (all within 100 km  effectively global).
        array<vector> positions = new array<vector>();
        array<float>  powers    = new array<float>();
        field.GetTopCells(Vector(0, 0, 0), 100000.0, m_MaxDetectionsPerPacket, positions, powers);

        if (positions.Count() == 0)
            return;

        // Convert linear watts  dBm and apply minimum threshold filter.
        array<string> tokens = new array<string>();
        float minPowerW;
        if (m_MinPowerDbm <= -200.0) minPowerW = 0.0;
        else minPowerW = Math.Pow(10.0, (m_MinPowerDbm - 30.0) / 10.0);

        for (int i = 0; i < positions.Count(); ++i)
        {
            float pw = powers.Get(i);
            if (pw < minPowerW) continue;

            float dBm;
            if (pw > 0.0) dBm = 10.0 * Math.Log10(pw * 1000.0);
            else          dBm = -200.0;

            // Compute bearing and elevation relative to replicated radar origin.
            vector rel = positions.Get(i) - m_RadarOrigin;
            float bearing   = Math.Atan2(rel[0], rel[2]) * Math.RAD2DEG;  // YAW
            float horizDist = Math.Sqrt(rel[0] * rel[0] + rel[2] * rel[2]);
            float elevation;
            if (horizDist > 0.01) elevation = Math.Atan2(rel[1], horizDist) * Math.RAD2DEG;
            else                  elevation = 0.0;

            // Build serialisable result.
            EMDetectionResult r  = new EMDetectionResult();
            r.m_vPosition        = positions.Get(i);
            r.m_fPowerDbm        = dBm;
            r.m_fBearingDeg      = bearing;
            r.m_fElevationDeg    = elevation;
            // Frequency / waveform info would require per-cell lookup; left as defaults for bandwidth.

            tokens.Insert(r.Serialise());
        }

        if (tokens.Count() == 0)
            return;

        // Pack all tokens separated by '|'.
        string packed = string.Empty;
        for (int t = 0; t < tokens.Count(); ++t)
        {
            if (t > 0) packed += "|";
            packed += tokens.Get(t);
        }

        float worldTime = 0.0;
        if (GetGame() && GetGame().GetWorld())
            worldTime = GetGame().GetWorld().GetWorldTime();

        Rpc(RpcBroadcast_Detections, worldTime, packed);
    }

    // ------------------------------------------------------------------------
    // RPC: server  all (broadcast)
    // Unreliable channel  a dropped packet means slightly stale data, acceptable
    // for EM field visualisation purposes.
    // ------------------------------------------------------------------------

    [RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
    protected void RpcBroadcast_Detections(float timestamp, string packed)
    {
        if (!m_LastDetections)
            m_LastDetections = new array<ref EMDetectionResult>();
        m_LastDetections.Clear();

        if (!packed || packed.IsEmpty())
            return;

        array<string> tokens = {};
        packed.Split("|", tokens, false);

        for (int i = 0; i < tokens.Count(); ++i)
        {
            EMDetectionResult r = EMDetectionResult.Deserialise(tokens.Get(i));
            if (r)
                m_LastDetections.Insert(r);
        }

        m_LastDetectionTime = timestamp;

        // Fire virtual callback for subclass extension points.
        OnDetectionsReceived(m_LastDetections, timestamp);
    }

    // ------------------------------------------------------------------------
    // RPC: client  server  request radar origin update
    // ------------------------------------------------------------------------

    // Called by external code (e.g. radar scanner) on the server to update origin.
    void SetRadarOrigin(vector origin)
    {
        if (m_RplComponent && m_RplComponent.IsProxy())
        {
            Rpc(RpcAsk_SetRadarOrigin, origin[0], origin[1], origin[2]);
            return;
        }
        ApplyRadarOrigin(origin);
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SetRadarOrigin(float x, float y, float z)
    {
        ApplyRadarOrigin(Vector(x, y, z));
    }

    protected void ApplyRadarOrigin(vector origin)
    {
        m_RadarOrigin = origin;
        Replication.BumpMe();
    }

    // ------------------------------------------------------------------------
    // RPC: client  server  register an active scanning sector
    // Allows a client-controlled radar to inform the server of its current sweep.
    // ------------------------------------------------------------------------

    void RequestRegisterSector(vector origin, float azimuthDeg, float halfWidthDeg, float range, float sectorTTL = 2.0)
    {
        if (!m_RplComponent) return;
        if (m_RplComponent.IsProxy())
        {
            Rpc(RpcAsk_RegisterSector, origin[0], origin[1], origin[2], azimuthDeg, halfWidthDeg, range, sectorTTL);
            return;
        }
        ApplyRegisterSector(origin, azimuthDeg, halfWidthDeg, range, sectorTTL);
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_RegisterSector(float ox, float oy, float oz, float az, float hw, float range, float ttl)
    {
        ApplyRegisterSector(Vector(ox, oy, oz), az, hw, range, ttl);
    }

    protected void ApplyRegisterSector(vector origin, float az, float hw, float range, float ttl)
    {
        EMVoxelField field = EMVoxelField.GetInstance();
        if (field)
            field.RegisterActiveSector(origin, az, hw, range, ttl);
    }

    // ------------------------------------------------------------------------
    // EMFieldNetworkAPI overrides
    // ------------------------------------------------------------------------

    override bool IsNetworkAvailable()
    {
        return m_RplComponent != null;
    }

    override void SetSyncInterval(float intervalSec)
    {
        m_SyncInterval = Math.Max(intervalSec, 0.05);
    }

    override void SetMaxDetectionsPerPacket(int count)
    {
        m_MaxDetectionsPerPacket = Math.Max(count, 1);
    }

    override bool HasReceivedDetections()
    {
        return m_LastDetections && m_LastDetections.Count() > 0;
    }

    override array<ref EMDetectionResult> GetLastDetections()
    {
        return m_LastDetections;
    }

    override float GetLastDetectionTime()
    {
        return m_LastDetectionTime;
    }

    // ------------------------------------------------------------------------
    // Static helper: locate the component on any entity or its parents.
    // ------------------------------------------------------------------------

    static EMFieldNetworkComponent FindOnEntity(IEntity entity)
    {
        IEntity cur = entity;
        int guard = 0;
        while (cur && guard < 8)
        {
            EMFieldNetworkComponent comp = EMFieldNetworkComponent.Cast(cur.FindComponent(EMFieldNetworkComponent));
            if (comp) return comp;
            cur = cur.GetParent();
            ++guard;
        }
        return null;
    }
}
