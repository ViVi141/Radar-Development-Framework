// One tracked scatterer / emitter in the world. Identity / mean RCS / Swerling
// class are set at insertion; pose, AGL, DEM cache, and kinematics refresh on tick.
class RDF_RadarScatterer
{
    // Stable id for cross-scan / debug association after plots drop m_Entity.
    int m_ScattererId;
    IEntity m_Entity;
    bool m_Alive = true;
    vector m_Position;
    vector m_Velocity;
    vector m_PrevPosition;
    float m_PrevSampleTime = -1.0;
    // Horizontal yaw / pitch (deg) and body forward — aspect RCS input.
    float m_YawDeg;
    float m_PitchDeg;
    vector m_Forward = "1 0 0";
    // Height above terrain (metres); negative means unknown.
    float m_AglM = -1.0;
    // Cached DEM sample at this scatterer (shared across radars).
    bool m_DemSampleValid;
    float m_DemTerrainY;
    int m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
    float m_DemSampleTime = -1000.0;
    float m_DemSampleX;
    float m_DemSampleZ;
    // Prefab key used to look up the shared signature (extents / RCS class).
    string m_SignatureKey;
    // Local AABB extents from the signature table (metres).
    float m_SizeX;
    float m_SizeY;
    float m_SizeZ;
    float m_CharacteristicLengthM;
    ERDF_RadarTargetType m_Type;
    // Mean optical-region RCS; instantaneous fluctuates via Swerling.
    float m_MeanRcsM2;
    float m_RcsM2;
    int m_SwerlingModel = 1;
    int m_RcsFluctSeed = 1;
    // Rotor / micro-Doppler from signature (0 tip = none).
    float m_RotorTipSpeedMs;
    int m_BladeCount;
    float m_RotorRcsFraction;
    float m_HubWidthMs;
    bool m_Emitting;
    float m_EmitStrength = 1.0;
    // Emitter RF summary for ESM / passive detection hooks.
    float m_EmitFrequencyHz;
    float m_EmitPeakPowerW;
    float m_EmitAntennaGainDbi;
    float m_LastRefreshTime = -1.0;
    ProjectileMoveComponent m_MoveComponent;
    int m_GridIx;
    int m_GridIz;
    bool m_HasGridCell = false;
}

// Global table of radar-relevant entities. Entities are scatterers or emitters
// only; all geometry / power / measurement work happens in the radar model,
// which reads this table instead of searching the world every scan.
//
// Cost model: one discovery sweep every s_DiscoveryIntervalS, classification
// amortized over frames, and a bounded kinematics refresh per frame.
class RDF_RadarScattererRegistry
{
    protected static ref array<ref RDF_RadarScatterer> s_Entries;
    // O(1) lookup by entity — kept in sync with s_Entries insert/remove.
    protected static ref map<IEntity, ref RDF_RadarScatterer> s_ByEntity;
    protected static ref array<IEntity> s_Pending;
    protected static ref array<IEntity> s_DiscoveryScratch;
    // Entities a sweep already looked at, accepted or rejected. Without this the
    // sweep re-queues the whole world every interval (11k+ on Eden), so a newly
    // spawned target waits behind a backlog that never drains.
    protected static ref map<IEntity, bool> s_Seen;
    protected static ref array<IEntity> s_SeenPruneScratch;
    protected static ref map<string, ref array<ref RDF_RadarScatterer>> s_CellBuckets;

    protected static float s_GridCellSizeM = 256.0;
    protected static int s_StatGridQueries;
    protected static int s_StatGridHits;

    protected static float s_LastTickTime = -1000.0;
    protected static float s_LastTickWallS = -1000.0;
    protected static float s_LastDiscoveryTime = -1000.0;
    protected static float s_LastDiscoveryWallS = -1000.0;
    protected static float s_LastSeenPruneWallS = -1000.0;
    protected static const float SEEN_PRUNE_INTERVAL_S = 20.0;
    protected static vector s_FocusOrigin = "0 0 0";
    protected static float s_DiscoveryRadiusM = 4000.0;
    protected static int s_RefreshCursor = 0;
    protected static int s_NextScattererId = 1;
    protected static RDF_DemRuntimeCache s_DemCache;
    protected static float s_DemResampleIntervalS = 2.0;
    protected static float s_DemResampleMoveM = 25.0;

    // Multi-radar focus merge: each Scanner registers; one deferred flush/frame
    // serves all (so late radars' foci participate in prune/discovery).
    protected static ref array<vector> s_FocusOrigins;
    protected static ref array<float> s_FocusRadiiM;
    protected static float s_FocusFrameWallS = -1000.0;
    protected static bool s_ConfigSetThisFrame;
    protected static int s_DiscoveryFocusCursor;
    protected static bool s_FlushScheduled;
    protected static BaseWorld s_PendingTickWorld;
    protected static float s_PendingTickWorldTimeS;
    // Open until FlushTickDeferred closes — avoids 1ms wall-clock false splits.
    protected static bool s_FocusWindowOpen;
    protected static const float FOCUS_WINDOW_STALE_S = 0.05;

    // Tuning.
    protected static float s_DiscoveryIntervalS = 3.0;
    protected static int s_ClassifyPerTick = 24;
    protected static int s_RefreshPerTick = 96;
    protected static int s_MaxEntries = 512;
    protected static bool s_UseSphereDiscovery = true;

    // Stats.
    protected static int s_StatDiscoveries;
    protected static int s_StatClassified;
    protected static int s_StatRejected;
    protected static int s_StatPruned;

    //------------------------------------------------------------------------------------------------
    // Merge budgets across radars in the same wall-clock frame: shorter discovery
    // interval, larger classify/refresh/maxEntries, sphere OR across callers.
    static void Configure(
        float discoveryIntervalS,
        int classifyPerTick,
        int refreshPerTick,
        int maxEntries,
        bool useSphereDiscovery)
    {
        float wallS = System.GetTickCount() * 0.001;
        BeginFocusFrame(wallS);

        float interval = Math.Max(0.25, discoveryIntervalS);
        int classify = Math.Clamp(classifyPerTick, 1, 256);
        int refresh = Math.Clamp(refreshPerTick, 1, 1024);
        int maxEnt = Math.Clamp(maxEntries, 8, 4096);

        if (!s_ConfigSetThisFrame)
        {
            s_DiscoveryIntervalS = interval;
            s_ClassifyPerTick = classify;
            s_RefreshPerTick = refresh;
            s_MaxEntries = maxEnt;
            s_UseSphereDiscovery = useSphereDiscovery;
            s_ConfigSetThisFrame = true;
            return;
        }

        if (interval < s_DiscoveryIntervalS)
            s_DiscoveryIntervalS = interval;
        if (classify > s_ClassifyPerTick)
            s_ClassifyPerTick = classify;
        if (refresh > s_RefreshPerTick)
            s_RefreshPerTick = refresh;
        if (maxEnt > s_MaxEntries)
            s_MaxEntries = maxEnt;
        if (useSphereDiscovery)
            s_UseSphereDiscovery = true;
    }

    //------------------------------------------------------------------------------------------------
    // Register this radar's discovery focus for the current frame (before Tick).
    static void RegisterFocus(vector focusOrigin, float radiusHintM)
    {
        float wallS = System.GetTickCount() * 0.001;
        BeginFocusFrame(wallS);
        EnsureContainers();

        float radiusM = radiusHintM;
        if (radiusM <= 0.0)
            radiusM = s_DiscoveryRadiusM;
        radiusM = Math.Clamp(radiusM, 100.0, 60000.0);
        s_FocusOrigins.Insert(focusOrigin);
        s_FocusRadiiM.Insert(radiusM);
    }

    //------------------------------------------------------------------------------------------------
    static array<ref RDF_RadarScatterer> GetEntries()
    {
        EnsureContainers();
        return s_Entries;
    }

    //------------------------------------------------------------------------------------------------
    static int GetEntryCount()
    {
        EnsureContainers();
        return s_Entries.Count();
    }

    //------------------------------------------------------------------------------------------------
    static void CollectInSphere(vector origin, float radiusM, notnull array<ref RDF_RadarScatterer> outEntries)
    {
        EnsureContainers();
        outEntries.Clear();
        if (radiusM <= 0.0)
            return;

        s_StatGridQueries = s_StatGridQueries + 1;

        float cell = s_GridCellSizeM;
        if (cell < 1.0)
            cell = 1.0;

        int minIx = Math.Floor((origin[0] - radiusM) / cell);
        int maxIx = Math.Floor((origin[0] + radiusM) / cell);
        int minIz = Math.Floor((origin[2] - radiusM) / cell);
        int maxIz = Math.Floor((origin[2] + radiusM) / cell);
        float radiusSq = radiusM * radiusM;

        for (int ix = minIx; ix <= maxIx; ix++)
        {
            for (int iz = minIz; iz <= maxIz; iz++)
            {
                string key = GridKey(ix, iz);
                ref array<ref RDF_RadarScatterer> bucket = s_CellBuckets.Get(key);
                if (!bucket)
                    continue;

                for (int i = 0; i < bucket.Count(); i++)
                {
                    RDF_RadarScatterer entry = bucket.Get(i);
                    if (!entry || !entry.m_Alive || !entry.m_Entity)
                        continue;
                    vector d = entry.m_Position - origin;
                    if (d.LengthSq() > radiusSq)
                        continue;
                    outEntries.Insert(entry);
                }
            }
        }

        s_StatGridHits = s_StatGridHits + outEntries.Count();
    }

    //------------------------------------------------------------------------------------------------
    static string GetStatsLine()
    {
        EnsureContainers();
        string line = "scat=" + s_Entries.Count().ToString();
        line = line + " pend=" + s_Pending.Count().ToString();
        line = line + " seen=" + s_Seen.Count().ToString();
        line = line + " cells=" + s_CellBuckets.Count().ToString();
        line = line + " gridQ=" + s_StatGridQueries.ToString();
        line = line + " gridHit=" + s_StatGridHits.ToString();
        line = line + " sweeps=" + s_StatDiscoveries.ToString();
        line = line + " ok=" + s_StatClassified.ToString();
        line = line + " rej=" + s_StatRejected.ToString();
        line = line + " prune=" + s_StatPruned.ToString();
        line = line + " " + RDF_RadarSignatureLibrary.GetStatsLine();
        return line;
    }

    //------------------------------------------------------------------------------------------------
    static void SetDemCache(RDF_DemRuntimeCache demCache)
    {
        s_DemCache = demCache;
    }

    //------------------------------------------------------------------------------------------------
    // Explicit registration for entities that announce themselves, i.e. a radar
    // flagging its own owner as an emitter. Safe to call every frame; updates in
    // place. Do NOT use it to inject targets a radar should have to find on its
    // own — that bypasses discovery and classification and invalidates any
    // detection measurement taken afterwards.
    static RDF_RadarScatterer Register(
        IEntity entity,
        vector worldPos,
        bool emitting,
        float strength)
    {
        return RegisterWithRadio(entity, worldPos, emitting, strength, 0.0, 0.0, 0.0);
    }

    //------------------------------------------------------------------------------------------------
    static RDF_RadarScatterer RegisterWithRadio(
        IEntity entity,
        vector worldPos,
        bool emitting,
        float strength,
        float frequencyHz,
        float peakPowerW,
        float antennaGainDbi)
    {
        if (!entity)
            return null;

        EnsureContainers();
        RDF_RadarScatterer entry = Find(entity);
        if (!entry)
        {
            entry = CreateEntry(entity);
            if (!entry)
                return null;
            s_Entries.Insert(entry);
            s_ByEntity.Set(entity, entry);
        }

        entry.m_Position = worldPos;
        entry.m_Alive = true;
        UpdateEntryGridCell(entry);
        entry.m_Emitting = emitting;
        entry.m_EmitStrength = strength;
        if (frequencyHz > 0.0)
            entry.m_EmitFrequencyHz = frequencyHz;
        if (peakPowerW > 0.0)
            entry.m_EmitPeakPowerW = peakPowerW;
        if (antennaGainDbi != 0.0 || frequencyHz > 0.0)
            entry.m_EmitAntennaGainDbi = antennaGainDbi;
        return entry;
    }

    //------------------------------------------------------------------------------------------------
    static void Unregister(IEntity entity)
    {
        if (!entity)
            return;
        EnsureContainers();
        for (int i = s_Entries.Count() - 1; i >= 0; i--)
        {
            RDF_RadarScatterer e = s_Entries.Get(i);
            if (e && e.m_Entity == entity)
            {
                e.m_Alive = false;
                e.m_Emitting = false;
                e.m_Entity = null;
                RemoveEntryFromGrid(e);
                s_ByEntity.Remove(entity);
                s_Entries.Remove(i);
                ForgetSeen(entity);
                return;
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    static void SetEmitting(IEntity entity, bool emitting)
    {
        RDF_RadarScatterer entry = Find(entity);
        if (entry)
            entry.m_Emitting = emitting;
    }

    //------------------------------------------------------------------------------------------------
    static RDF_RadarScatterer Find(IEntity entity)
    {
        if (!entity)
            return null;
        EnsureContainers();
        if (!s_ByEntity.Contains(entity))
            return null;
        RDF_RadarScatterer e = s_ByEntity.Get(entity);
        if (!e || !e.m_Alive || e.m_Entity != entity)
        {
            s_ByEntity.Remove(entity);
            return null;
        }
        return e;
    }

    //------------------------------------------------------------------------------------------------
    static RDF_RadarScatterer FindById(int scattererId)
    {
        if (scattererId <= 0)
            return null;
        EnsureContainers();
        for (int i = 0; i < s_Entries.Count(); i++)
        {
            RDF_RadarScatterer e = s_Entries.Get(i);
            if (e && e.m_ScattererId == scattererId)
                return e;
        }
        return null;
    }

    //------------------------------------------------------------------------------------------------
    static void Clear()
    {
        EnsureContainers();
        s_Entries.Clear();
        s_ByEntity.Clear();
        s_Pending.Clear();
        s_Seen.Clear();
        s_CellBuckets.Clear();
        s_RefreshCursor = 0;
        s_LastDiscoveryTime = -1000.0;
        s_LastDiscoveryWallS = -1000.0;
        s_LastSeenPruneWallS = -1000.0;
        s_LastTickWallS = -1000.0;
        s_FocusFrameWallS = -1000.0;
        s_ConfigSetThisFrame = false;
        s_FocusWindowOpen = false;
        s_FlushScheduled = false;
        s_PendingTickWorld = null;
        s_DiscoveryFocusCursor = 0;
        if (s_FocusOrigins)
            s_FocusOrigins.Clear();
        if (s_FocusRadiiM)
            s_FocusRadiiM.Clear();
    }

    //------------------------------------------------------------------------------------------------
    // Drop cached DEM cell hits on every entry. Used when the runtime cache is
    // flushed so stale terrainY / surfaceClass cannot keep demValid=true.
    static void InvalidateDemSamples()
    {
        EnsureContainers();
        for (int i = 0; i < s_Entries.Count(); i++)
        {
            RDF_RadarScatterer e = s_Entries.Get(i);
            if (!e)
                continue;
            e.m_DemSampleValid = false;
            e.m_DemTerrainY = 0.0;
            e.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
            e.m_DemSampleTime = -1000.0;
        }
    }

    //------------------------------------------------------------------------------------------------
    // Phase 1: register this radar's focus. Phase 2 (FlushTickDeferred) runs once
    // after the call queue drains so every same-frame RegisterFocus is visible to
    // prune / discovery / kinematics. N radars still cost one flush per ~frame.
    static void Tick(
        BaseWorld world,
        float worldTimeS,
        vector focusOrigin,
        float radiusHintM)
    {
        if (!world)
            return;

        EnsureContainers();
        RegisterFocus(focusOrigin, radiusHintM);
        s_PendingTickWorld = world;
        s_PendingTickWorldTimeS = worldTimeS;
        ScheduleFlush();
    }

    //------------------------------------------------------------------------------------------------
    protected static void ScheduleFlush()
    {
        if (s_FlushScheduled)
            return;

        if (!GetGame() || !GetGame().GetCallqueue())
        {
            FlushTickDeferred();
            return;
        }

        s_FlushScheduled = true;
        GetGame().GetCallqueue().CallLater(FlushTickDeferred, 0, false);
    }

    //------------------------------------------------------------------------------------------------
    // Phase 2: consume all foci registered since the last flush.
    static void FlushTickDeferred()
    {
        s_FlushScheduled = false;

        BaseWorld world = s_PendingTickWorld;
        if (!world)
        {
            CloseFocusWindow();
            return;
        }

        float worldTimeS = s_PendingTickWorldTimeS;
        float wallS = System.GetTickCount() * 0.001;
        // Collapse duplicate flushes in the same ~ms (re-entrant / double schedule).
        // Do not CloseFocusWindow here — a sibling flush already owns the cycle.
        if (wallS - s_LastTickWallS < 0.001)
            return;
        s_LastTickWallS = wallS;
        s_LastTickTime = worldTimeS;

        SelectActiveDiscoveryFocus();

        bool intervalElapsed = wallS - s_LastDiscoveryWallS >= s_DiscoveryIntervalS;
        if (intervalElapsed && s_Pending.Count() == 0)
        {
            s_LastDiscoveryWallS = wallS;
            s_LastDiscoveryTime = worldTimeS;
            MaybePruneSeen(wallS);
            RunDiscovery(world);
            s_DiscoveryFocusCursor = s_DiscoveryFocusCursor + 1;
        }

        ProcessPending();
        RefreshKinematics(world, worldTimeS);
        RDF_RadarSignatureLibrary.MaybeAutoExport(worldTimeS);
        CloseFocusWindow();
    }

    //------------------------------------------------------------------------------------------------
    // Focus collection window stays open across the whole deferred flush cycle
    // (not a 1ms wall tick), so multi-radar Configure/Tick never mid-clear.
    protected static void BeginFocusFrame(float wallS)
    {
        EnsureContainers();
        if (s_FocusWindowOpen)
        {
            // Stale open window (CallLater delayed): force consume then reopen.
            if (wallS - s_FocusFrameWallS < FOCUS_WINDOW_STALE_S)
                return;
            if (s_FlushScheduled)
                FlushTickDeferred();
            else
                CloseFocusWindow();
        }

        s_FocusWindowOpen = true;
        s_FocusFrameWallS = wallS;
        s_FocusOrigins.Clear();
        s_FocusRadiiM.Clear();
        s_ConfigSetThisFrame = false;
    }

    //------------------------------------------------------------------------------------------------
    protected static void CloseFocusWindow()
    {
        s_FocusWindowOpen = false;
        if (s_FocusOrigins)
            s_FocusOrigins.Clear();
        if (s_FocusRadiiM)
            s_FocusRadiiM.Clear();
        s_ConfigSetThisFrame = false;
    }

    //------------------------------------------------------------------------------------------------
    protected static void SelectActiveDiscoveryFocus()
    {
        int focusCount = s_FocusOrigins.Count();
        if (focusCount <= 0)
            return;

        int index = s_DiscoveryFocusCursor;
        while (index < 0)
            index = index + focusCount;
        while (index >= focusCount)
            index = index - focusCount;

        s_FocusOrigin = s_FocusOrigins.Get(index);
        s_DiscoveryRadiusM = s_FocusRadiiM.Get(index);
    }

    //------------------------------------------------------------------------------------------------
    // True when position is within 2× discovery radius of any registered focus.
    protected static bool IsNearAnyFocus(vector position)
    {
        int focusCount = s_FocusOrigins.Count();
        if (focusCount <= 0)
        {
            vector delta = position - s_FocusOrigin;
            float pruneRadius = s_DiscoveryRadiusM * 2.0;
            return delta.LengthSq() <= pruneRadius * pruneRadius;
        }

        for (int i = 0; i < focusCount; i++)
        {
            vector focus = s_FocusOrigins.Get(i);
            float radiusM = s_FocusRadiiM.Get(i);
            float pruneRadius = radiusM * 2.0;
            vector delta = position - focus;
            if (delta.LengthSq() <= pruneRadius * pruneRadius)
                return true;
        }
        return false;
    }

    //------------------------------------------------------------------------------------------------
    // Cheap sweep: collect raw candidates only. Classification is deferred.
    protected static void RunDiscovery(BaseWorld world)
    {
        s_Pending.Clear();
        s_DiscoveryScratch.Clear();

        if (s_UseSphereDiscovery)
        {
            world.QueryEntitiesBySphere(
                s_FocusOrigin,
                s_DiscoveryRadiusM,
                OnDiscoveredEntity,
                null,
                EQueryEntitiesFlags.DYNAMIC);
        }
        else
        {
            world.GetActiveEntities(s_DiscoveryScratch);
            for (int i = 0; i < s_DiscoveryScratch.Count(); i++)
                QueueIfUnseen(s_DiscoveryScratch.Get(i));
        }

        s_StatDiscoveries = s_StatDiscoveries + 1;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool OnDiscoveredEntity(IEntity entity)
    {
        QueueIfUnseen(entity);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static void QueueIfUnseen(IEntity entity)
    {
        if (!entity)
            return;
        if (s_Seen.Contains(entity))
            return;
        s_Seen.Insert(entity, true);
        s_Pending.Insert(entity);
    }

    //------------------------------------------------------------------------------------------------
    // Classify a bounded slice per tick; prefab lookups are the expensive part.
    protected static void ProcessPending()
    {
        int processed = 0;
        while (s_Pending.Count() > 0 && processed < s_ClassifyPerTick)
        {
            int last = s_Pending.Count() - 1;
            IEntity ent = s_Pending.Get(last);
            s_Pending.Remove(last);
            processed = processed + 1;

            if (!ent)
                continue;
            if (Find(ent))
                continue;
            if (s_Entries.Count() >= s_MaxEntries)
            {
                // Table full: retry on a later sweep instead of memoizing a skip.
                ForgetSeen(ent);
                continue;
            }

            if (!RDF_RadarEntityClassifier.IsRadarCandidate(ent))
            {
                s_StatRejected = s_StatRejected + 1;
                continue;
            }

            RDF_RadarScatterer entry = CreateEntry(ent);
            if (!entry)
                continue;
            s_Entries.Insert(entry);
            s_ByEntity.Set(ent, entry);
            InsertEntryToGrid(entry);
            s_StatClassified = s_StatClassified + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected static RDF_RadarScatterer CreateEntry(IEntity entity)
    {
        if (!entity)
            return null;

        RDF_RadarScatterer entry = new RDF_RadarScatterer();
        entry.m_ScattererId = s_NextScattererId;
        s_NextScattererId = s_NextScattererId + 1;
        entry.m_Entity = entity;
        entry.m_Alive = true;

        GenericEntity generic = GenericEntity.Cast(entity);
        if (generic)
        {
            entry.m_MoveComponent = ProjectileMoveComponent.Cast(
                generic.FindComponent(ProjectileMoveComponent));
        }

        if (entry.m_MoveComponent || RDF_RadarEntityClassifier.IsProjectile(entity))
            entry.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
        else
            entry.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;

        ApplySignature(entity, entry);
        entry.m_RcsFluctSeed = entry.m_ScattererId * 1103515245 + 12345;
        if (entry.m_RcsFluctSeed < 0)
            entry.m_RcsFluctSeed = -entry.m_RcsFluctSeed;
        if (entry.m_RcsFluctSeed == 0)
            entry.m_RcsFluctSeed = 1;

        entry.m_Position = ReadPosition(entity);
        entry.m_PrevPosition = entry.m_Position;
        entry.m_PrevSampleTime = -1.0;
        entry.m_Velocity = ReadVelocity(entity, entry.m_MoveComponent);
        ReadPose(entity, entry);
        entry.m_Emitting = false;
        entry.m_EmitStrength = 1.0;
        entry.m_EmitFrequencyHz = 0.0;
        entry.m_EmitPeakPowerW = 0.0;
        entry.m_EmitAntennaGainDbi = 0.0;
        entry.m_AglM = -1.0;
        entry.m_DemSampleValid = false;
        entry.m_DemTerrainY = 0.0;
        entry.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        entry.m_DemSampleTime = -1000.0;
        return entry;
    }

    //------------------------------------------------------------------------------------------------
    // Extents / mean RCS / Swerling class come from the per-model signature table.
    // Baked models are pure table reads; an unknown model is measured once and reused.
    protected static void ApplySignature(IEntity entity, RDF_RadarScatterer entry)
    {
        if (!entity || !entry)
            return;

        RDF_RadarSignature sig = RDF_RadarSignatureLibrary.Resolve(entity, entry.m_Type);
        if (!sig)
        {
            FillExtents(entity, entry);
            entry.m_MeanRcsM2 = RDF_RadarRcsModel.GetEntityRcsM2(entity, entry.m_Type);
            entry.m_RcsM2 = entry.m_MeanRcsM2;
            entry.m_SwerlingModel = RDF_RadarRcsModel.GetDefaultSwerlingModel(entry.m_Type);
            return;
        }

        entry.m_SignatureKey = sig.m_Key;
        entry.m_SizeX = sig.m_SizeX;
        entry.m_SizeY = sig.m_SizeY;
        entry.m_SizeZ = sig.m_SizeZ;
        entry.m_CharacteristicLengthM = sig.m_CharacteristicLengthM;
        entry.m_MeanRcsM2 = sig.m_MeanRcsM2;
        entry.m_RcsM2 = sig.m_MeanRcsM2;
        entry.m_SwerlingModel = sig.m_SwerlingModel;
        entry.m_RotorTipSpeedMs = sig.m_RotorTipSpeedMs;
        entry.m_BladeCount = sig.m_BladeCount;
        entry.m_RotorRcsFraction = sig.m_RotorRcsFraction;
        entry.m_HubWidthMs = sig.m_HubWidthMs;
    }

    //------------------------------------------------------------------------------------------------
    // Direct bounds read; only used when no signature key can be derived.
    protected static void FillExtents(IEntity entity, RDF_RadarScatterer entry)
    {
        if (!entity || !entry)
            return;

        vector mins;
        vector maxs;
        entity.GetBounds(mins, maxs);
        vector size = maxs - mins;
        entry.m_SizeX = Math.AbsFloat(size[0]);
        entry.m_SizeY = Math.AbsFloat(size[1]);
        entry.m_SizeZ = Math.AbsFloat(size[2]);

        float longest = entry.m_SizeX;
        if (entry.m_SizeY > longest)
            longest = entry.m_SizeY;
        if (entry.m_SizeZ > longest)
            longest = entry.m_SizeZ;
        if (longest < 0.1)
            longest = 0.1;
        entry.m_CharacteristicLengthM = longest;
    }

    //------------------------------------------------------------------------------------------------
    protected static void ReadPose(IEntity entity, RDF_RadarScatterer entry)
    {
        if (!entity || !entry)
            return;

        vector mat[4];
        entity.GetWorldTransform(mat);
        // Same convention as RDF_RadarScanner.GetSubjectForward: mat[2] is facing.
        vector forward = mat[2];
        float flen = forward.Length();
        if (flen < 0.001)
            forward = "1 0 0";
        else
            forward = forward / flen;
        entry.m_Forward = forward;
        entry.m_YawDeg = Math.Atan2(forward[2], forward[0]) * 57.2957795;
        float fy = forward[1];
        if (fy > 1.0)
            fy = 1.0;
        if (fy < -1.0)
            fy = -1.0;
        entry.m_PitchDeg = Math.Asin(fy) * 57.2957795;
    }

    //------------------------------------------------------------------------------------------------
    // Round-robin kinematics + pose + DEM/AGL refresh; prune dead / far entries.
    protected static void RefreshKinematics(BaseWorld world, float worldTimeS)
    {
        int count = s_Entries.Count();
        if (count <= 0)
        {
            s_RefreshCursor = 0;
            return;
        }

        int budget = s_RefreshPerTick;
        if (budget > count)
            budget = count;

        for (int step = 0; step < budget; step++)
        {
            if (s_RefreshCursor >= s_Entries.Count())
                s_RefreshCursor = 0;

            int index = s_RefreshCursor;
            RDF_RadarScatterer entry = s_Entries.Get(index);
            if (!entry || !entry.m_Alive || !entry.m_Entity)
            {
                if (entry)
                {
                    if (entry.m_Entity)
                        s_ByEntity.Remove(entry.m_Entity);
                    entry.m_Alive = false;
                    RemoveEntryFromGrid(entry);
                }
                s_Entries.Remove(index);
                s_StatPruned = s_StatPruned + 1;
                continue;
            }

            vector newPos = ReadPosition(entry.m_Entity);
            vector physicsVel = ReadVelocity(entry.m_Entity, entry.m_MoveComponent);
            ApplyKinematicsSample(entry, newPos, physicsVel, worldTimeS);
            UpdateEntryGridCell(entry);
            ReadPose(entry.m_Entity, entry);
            RefreshDemAndAgl(entry, world, worldTimeS);

            // Keep if near ANY registered radar focus (multi-radar merge).
            if (!IsNearAnyFocus(entry.m_Position))
            {
                entry.m_Alive = false;
                // Out of range now, but a later sweep must be able to re-add it.
                ForgetSeen(entry.m_Entity);
                RemoveEntryFromGrid(entry);
                s_ByEntity.Remove(entry.m_Entity);
                s_Entries.Remove(index);
                s_StatPruned = s_StatPruned + 1;
                continue;
            }

            s_RefreshCursor = s_RefreshCursor + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected static void RefreshDemAndAgl(
        RDF_RadarScatterer entry,
        BaseWorld world,
        float worldTimeS)
    {
        if (!entry)
            return;

        bool needSample = false;
        if (!entry.m_DemSampleValid)
            needSample = true;
        if (worldTimeS - entry.m_DemSampleTime >= s_DemResampleIntervalS)
            needSample = true;

        if (entry.m_DemSampleValid)
        {
            float dx = entry.m_Position[0] - entry.m_DemSampleX;
            float dz = entry.m_Position[2] - entry.m_DemSampleZ;
            if ((dx * dx + dz * dz) > (s_DemResampleMoveM * s_DemResampleMoveM))
                needSample = true;
        }

        float terrainY = 0.0;
        bool haveTerrain = false;

        if (needSample && s_DemCache)
        {
            RDF_DemRuntimeCellSample demSample;
            if (s_DemCache.TrySampleAt(entry.m_Position[0], entry.m_Position[2], demSample))
            {
                if (demSample && demSample.m_Valid)
                {
                    entry.m_DemSampleValid = true;
                    entry.m_DemTerrainY = demSample.m_TerrainY;
                    entry.m_DemSurfaceClass = demSample.m_SurfaceClass;
                    entry.m_DemSampleTime = worldTimeS;
                    entry.m_DemSampleX = entry.m_Position[0];
                    entry.m_DemSampleZ = entry.m_Position[2];
                    terrainY = demSample.m_TerrainY;
                    haveTerrain = true;
                }
                else
                {
                    entry.m_DemSampleValid = false;
                }
            }
            else
            {
                // Cache miss / empty budget: do not keep a stale demValid hit.
                entry.m_DemSampleValid = false;
            }
        }
        else if (entry.m_DemSampleValid)
        {
            terrainY = entry.m_DemTerrainY;
            haveTerrain = true;
        }

        if (!haveTerrain && world)
        {
            terrainY = world.GetSurfaceY(entry.m_Position[0], entry.m_Position[2]);
            haveTerrain = true;
            if (!entry.m_DemSampleValid)
            {
                entry.m_DemTerrainY = terrainY;
                entry.m_DemSampleTime = worldTimeS;
            }
        }

        if (haveTerrain)
        {
            entry.m_AglM = entry.m_Position[1] - terrainY;
            if (entry.m_AglM < 0.0)
                entry.m_AglM = 0.0;
        }
    }

    // Instantaneous RCS for a look direction / scan (3D aspect × Swerling).
    static float EvaluateInstantRcsM2(
        RDF_RadarScatterer entry,
        float losAzimuthDeg,
        float losElevationDeg,
        int scanNumber)
    {
        if (!entry)
            return 0.0;

        float aspectRcs = RDF_RadarRcsModel.AspectRcsFromExtents3D(
            entry.m_MeanRcsM2,
            entry.m_SizeX,
            entry.m_SizeY,
            entry.m_SizeZ,
            entry.m_YawDeg,
            entry.m_PitchDeg,
            losAzimuthDeg,
            losElevationDeg);
        float fluct = RDF_RadarRcsModel.SampleSwerling(
            aspectRcs,
            entry.m_SwerlingModel,
            entry.m_RcsFluctSeed,
            scanNumber,
            entry.m_ScattererId);
        entry.m_RcsM2 = fluct;
        return fluct;
    }

    //------------------------------------------------------------------------------------------------
    // Prefer physics velocity; if near-zero, fall back to position differencing.
    protected static void ApplyKinematicsSample(
        RDF_RadarScatterer entry,
        vector newPos,
        vector physicsVel,
        float worldTimeS)
    {
        if (!entry)
            return;

        float dt = -1.0;
        if (entry.m_LastRefreshTime >= 0.0)
            dt = worldTimeS - entry.m_LastRefreshTime;

        vector diffVel = "0 0 0";
        if (dt > 0.001)
        {
            vector deltaPos = newPos - entry.m_Position;
            diffVel = deltaPos * (1.0 / dt);
        }

        float physSpeedSq = physicsVel.LengthSq();
        if (physSpeedSq > 0.01)
            entry.m_Velocity = physicsVel;
        else if (dt > 0.001)
            entry.m_Velocity = diffVel;
        else
            entry.m_Velocity = physicsVel;

        entry.m_PrevPosition = entry.m_Position;
        entry.m_PrevSampleTime = entry.m_LastRefreshTime;
        entry.m_Position = newPos;
        entry.m_LastRefreshTime = worldTimeS;
    }

    //------------------------------------------------------------------------------------------------
    protected static vector ReadPosition(IEntity entity)
    {
        if (!entity)
            return "0 0 0";
        vector mat[4];
        entity.GetWorldTransform(mat);
        return mat[3];
    }

    //------------------------------------------------------------------------------------------------
    protected static vector ReadVelocity(
        IEntity entity,
        ProjectileMoveComponent moveComponent)
    {
        if (!entity)
            return "0 0 0";
        if (moveComponent)
            return moveComponent.GetVelocity();
        Physics physics = entity.GetPhysics();
        if (physics)
            return physics.GetVelocity();
        return "0 0 0";
    }

    //------------------------------------------------------------------------------------------------
    protected static string GridKey(int ix, int iz)
    {
        return ix.ToString() + ":" + iz.ToString();
    }

    //------------------------------------------------------------------------------------------------
    protected static void GetGridCoords(vector pos, out int ix, out int iz)
    {
        float cell = s_GridCellSizeM;
        if (cell < 1.0)
            cell = 1.0;
        ix = Math.Floor(pos[0] / cell);
        iz = Math.Floor(pos[2] / cell);
    }

    //------------------------------------------------------------------------------------------------
    protected static void InsertEntryToGrid(RDF_RadarScatterer entry)
    {
        if (!entry)
            return;

        int ix, iz;
        GetGridCoords(entry.m_Position, ix, iz);
        string key = GridKey(ix, iz);

        ref array<ref RDF_RadarScatterer> bucket = s_CellBuckets.Get(key);
        if (!bucket)
        {
            bucket = new array<ref RDF_RadarScatterer>();
            s_CellBuckets.Insert(key, bucket);
        }

        bucket.Insert(entry);
        entry.m_GridIx = ix;
        entry.m_GridIz = iz;
        entry.m_HasGridCell = true;
    }

    //------------------------------------------------------------------------------------------------
    protected static void RemoveEntryFromGrid(RDF_RadarScatterer entry)
    {
        if (!entry || !entry.m_HasGridCell)
            return;

        string key = GridKey(entry.m_GridIx, entry.m_GridIz);
        ref array<ref RDF_RadarScatterer> bucket = s_CellBuckets.Get(key);
        if (bucket)
        {
            for (int i = bucket.Count() - 1; i >= 0; i--)
            {
                if (bucket.Get(i) == entry)
                    bucket.Remove(i);
            }
            if (bucket.Count() <= 0)
                s_CellBuckets.Remove(key);
        }

        entry.m_HasGridCell = false;
    }

    //------------------------------------------------------------------------------------------------
    protected static void UpdateEntryGridCell(RDF_RadarScatterer entry)
    {
        if (!entry)
            return;

        int ix, iz;
        GetGridCoords(entry.m_Position, ix, iz);
        if (entry.m_HasGridCell)
        {
            if (entry.m_GridIx == ix && entry.m_GridIz == iz)
                return;
            RemoveEntryFromGrid(entry);
        }

        InsertEntryToGrid(entry);
    }

    //------------------------------------------------------------------------------------------------
    protected static void EnsureContainers()
    {
        if (!s_Entries)
            s_Entries = new array<ref RDF_RadarScatterer>();
        if (!s_ByEntity)
            s_ByEntity = new map<IEntity, ref RDF_RadarScatterer>();
        if (!s_Pending)
            s_Pending = new array<IEntity>();
        if (!s_DiscoveryScratch)
            s_DiscoveryScratch = new array<IEntity>();
        if (!s_Seen)
            s_Seen = new map<IEntity, bool>();
        if (!s_SeenPruneScratch)
            s_SeenPruneScratch = new array<IEntity>();
        if (!s_CellBuckets)
            s_CellBuckets = new map<string, ref array<ref RDF_RadarScatterer>>();
        if (!s_FocusOrigins)
            s_FocusOrigins = new array<vector>();
        if (!s_FocusRadiiM)
            s_FocusRadiiM = new array<float>();
    }

    //------------------------------------------------------------------------------------------------
    // Deleted entities leave keys that were hashed on a now-null pointer, so they
    // cannot be removed by key. Rebuild from the survivors instead. Rebuilding is
    // O(memo), so it runs on a slow cadence rather than on every sweep.
    protected static void MaybePruneSeen(float wallS)
    {
        if (!s_Seen)
            return;
        if (wallS - s_LastSeenPruneWallS < SEEN_PRUNE_INTERVAL_S)
            return;
        s_LastSeenPruneWallS = wallS;

        s_SeenPruneScratch.Clear();
        for (int i = 0; i < s_Seen.Count(); i++)
        {
            IEntity key = s_Seen.GetKey(i);
            if (key)
                s_SeenPruneScratch.Insert(key);
        }
        if (s_SeenPruneScratch.Count() == s_Seen.Count())
        {
            s_SeenPruneScratch.Clear();
            return;
        }

        s_Seen.Clear();
        for (int j = 0; j < s_SeenPruneScratch.Count(); j++)
            s_Seen.Insert(s_SeenPruneScratch.Get(j), true);
        s_SeenPruneScratch.Clear();
    }

    //------------------------------------------------------------------------------------------------
    // Let a dropped entity be picked up again by a later sweep.
    protected static void ForgetSeen(IEntity entity)
    {
        if (!entity || !s_Seen)
            return;
        s_Seen.Remove(entity);
    }
}
