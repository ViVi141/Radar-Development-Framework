// Radar scanner: scatterers feed the physics chain; published plots are measurements.
// Candidates from sphere query + active fallback; Trace LOS + optional NLOS;
// SNR / CFAR gates; optional measurement synthesis (quantize + noise, no entity truth).
class RDF_RadarScanner
{
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const float RAD_TO_DEG = 57.2957795;
    protected ref RDF_RadarSettings m_Settings;
    protected ref RDF_DemRuntimeCache m_DemCache;

    // Last scan geometry, cached for display (PPI HUD) without recomputing.
    protected vector m_LastOrigin = "0 0 0";
    protected vector m_LastForward = "1 0 0";
    protected float m_LastRange = 2000.0;
    protected ref array<ref RDF_RadarScatterer> m_ScattererCandidates;
    protected ref RDF_RadarCandidateCollect m_CandidateCollect;
    protected ref RDF_RadarCfarProcessor m_CfarProcessor;
    protected ref RDF_RadarLosCache m_LosCache;
    protected ref RDF_RadarScanReuseCache m_ScanReuseCache;
    protected ref RDF_RadarClutterMap m_ClutterMap;
    protected bool m_SettingsValidated;
    protected int m_StatReuseHits;
    protected int m_StatFreshUpdates;
    protected int m_StatBudgetSkips;
    protected int m_StatLosCacheHits;
    // Last-dwell EW noise observability (RF domain, before processing gain).
    protected float m_LastEwNoiseRfW;
    protected float m_LastThermalNoiseW;
    protected float m_LastJnDb;
    protected float m_LastBurnThroughRangeM;
    // Registry pass fair cursor; carry budget cutoff to next scan.
    protected int m_RegistryScanCursor;
    // Resolved once per Scan() when atmospheric loss is enabled.
    protected float m_ScanRainLossDbPerKm;

    void RDF_RadarScanner(RDF_RadarSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_RadarSettings();

        m_DemCache = new RDF_DemRuntimeCache();
        m_ScattererCandidates = new array<ref RDF_RadarScatterer>();
        m_CandidateCollect = new RDF_RadarCandidateCollect();
        m_CfarProcessor = new RDF_RadarCfarProcessor();
        m_LosCache = new RDF_RadarLosCache();
        m_ScanReuseCache = new RDF_RadarScanReuseCache();
        m_ClutterMap = new RDF_RadarClutterMap();
        m_SettingsValidated = false;
        m_RegistryScanCursor = 0;
        m_ScanRainLossDbPerKm = 0.0;
        m_LastEwNoiseRfW = 0.0;
        m_LastThermalNoiseW = 0.0;
        m_LastJnDb = -300.0;
        m_LastBurnThroughRangeM = 0.0;
    }

    RDF_RadarSettings GetSettings()
    {
        return m_Settings;
    }

    vector GetLastOrigin()
    {
        return m_LastOrigin;
    }

    vector GetLastForward()
    {
        return m_LastForward;
    }

    float GetLastRange()
    {
        return m_LastRange;
    }

    string GetDemStatusShort()
    {
        if (!m_Settings || !m_Settings.m_EnableDemClutter)
            return "DEM OFF";
        if (!m_DemCache)
            return "DEM OFF";
        return m_DemCache.GetStatusShort();
    }

    string GetDemStatsLine()
    {
        if (!m_DemCache)
            return "DEM cache unavailable";
        return m_DemCache.GetStatsLine();
    }

    RDF_DemRuntimeCache GetDemCache()
    {
        return m_DemCache;
    }

    void ClearDemCache()
    {
        if (m_DemCache)
            m_DemCache.Clear();
    }

    // Whole-world SURF/DEM RAM load. Prefer calling from Sensor before ScanOnce
    // wall-clock timing; Scan() also no-ops when already resident.
    void EnsureDemPreloaded()
    {
        if (!m_DemCache || !m_Settings)
            return;
        m_DemCache.SetPreloadAll(m_Settings.m_DemPreloadAll);
        m_DemCache.SetMaxTiles(m_Settings.m_DemCacheMaxTiles);
        m_DemCache.InitializeForCurrentWorld();
        if (m_Settings.m_DemPreloadAll)
            m_DemCache.EnsurePreloaded();
    }

    string GetScattererStatsLine()
    {
        return RDF_RadarScattererRegistry.GetStatsLine();
    }

    string GetScanReuseStatsShort()
    {
        return "reuse=" + m_StatReuseHits.ToString()
            + " fresh=" + m_StatFreshUpdates.ToString()
            + " skip=" + m_StatBudgetSkips.ToString()
            + " losHit=" + m_StatLosCacheHits.ToString();
    }

    float GetLastEwNoiseRfW()
    {
        return m_LastEwNoiseRfW;
    }

    float GetLastThermalNoiseW()
    {
        return m_LastThermalNoiseW;
    }

    float GetLastJnDb()
    {
        return m_LastJnDb;
    }

    float GetLastBurnThroughRangeM()
    {
        return m_LastBurnThroughRangeM;
    }

    string GetEwStatsShort()
    {
        if (m_LastEwNoiseRfW <= 0.0)
            return "ew=0";
        float jnRounded = Math.Round(m_LastJnDb * 10.0) * 0.1;
        float reffRounded = Math.Round(m_LastBurnThroughRangeM);
        return "ewJN=" + jnRounded.ToString()
            + "dB Reff=" + reffRounded.ToString() + "m";
    }

    protected void RefreshEwScanStats(vector origin, vector forward, float rangeM)
    {
        m_LastEwNoiseRfW = 0.0;
        m_LastThermalNoiseW = 0.0;
        m_LastJnDb = -300.0;
        m_LastBurnThroughRangeM = rangeM;

        if (!m_Settings || !m_Settings.m_Hardware)
            return;

        RDF_RadarHardware hardware = m_Settings.m_Hardware;
        float thermal = hardware.GetNoisePowerW();
        m_LastThermalNoiseW = thermal;

        float ew = 0.0;
        if (m_Settings.m_EwStack)
        {
            ew = m_Settings.m_EwStack.GetAdditionalNoisePowerW(
                origin,
                forward,
                hardware);
        }
        if (ew < 0.0)
            ew = 0.0;
        m_LastEwNoiseRfW = ew;

        float denom = Math.Max(1.0e-30, thermal);
        m_LastJnDb = RDF_RadarClutterModel.LinToDb(ew / denom);
        m_LastBurnThroughRangeM = RDF_RadarEwBurnThrough.RangeM(
            rangeM,
            thermal,
            ew);
    }

    void Scan(IEntity subject, array<ref RDF_RadarTarget> outTargets)
    {
        if (!subject || !m_Settings || !m_Settings.m_Enabled || !outTargets)
            return;

        outTargets.Clear();
        m_StatReuseHits = 0;
        m_StatFreshUpdates = 0;
        m_StatBudgetSkips = 0;
        m_StatLosCacheHits = 0;
        m_ScanRainLossDbPerKm = 0.0;
        if (!m_SettingsValidated)
        {
            m_Settings.Validate();
            m_SettingsValidated = true;
        }

        if (m_Settings.m_EnableAtmosphericLoss)
        {
            RDF_RadarWeatherSnapshot weather = null;
            if (m_Settings.m_EnableWeatherDrivenRainLoss)
                weather = RDF_RadarBallistics.SampleWorldWeather();
            m_ScanRainLossDbPerKm = m_Settings.ResolveRainLossDbPerKm(weather);
        }

        BaseWorld world = subject.GetWorld();
        if (!world)
        {
            world = GetGame().GetWorld();
            if (!world)
                return;
        }

        vector origin = GetSubjectOrigin(subject);
        float worldTime = world.GetWorldTime() * 0.001;
        float wallTime = System.GetTickCount() * 0.001;
        vector forward = GetScanForward(subject, worldTime);
        float range = m_Settings.m_Range;
        m_LastOrigin = origin;
        m_LastForward = forward;
        m_LastRange = range;
        RefreshEwScanStats(origin, forward, range);
        if (m_ClutterMap && m_Settings.m_EnableClutterMap)
        {
            m_ClutterMap.Configure(
                36,
                m_Settings.m_RangeBinCount,
                m_Settings.m_ClutterMapAlpha,
                range);
        }
        if (m_DemCache)
        {
            m_DemCache.SetPreloadAll(m_Settings.m_DemPreloadAll);
            m_DemCache.SetMaxTiles(m_Settings.m_DemCacheMaxTiles);
            m_DemCache.BeginScan(m_Settings.m_DemTileLoadsPerScan);
            // WLR ground sampling needs DEM even when clutter is off.
            m_DemCache.InitializeForCurrentWorld();
            // Prefer Sensor.EnsureDemPreloaded() before ScanOnce timing; keep as
            // safety net if a caller skipped the explicit preload.
            if (m_Settings.m_DemPreloadAll)
                m_DemCache.EnsurePreloaded();
            RDF_RadarBallistics.SetDemCache(m_DemCache);
            if (m_Settings.m_UseScattererRegistry)
                RDF_RadarScattererRegistry.SetDemCache(m_DemCache);
        }
        if (m_Settings.m_UseScattererRegistry)
        {
            RDF_RadarScattererRegistry.Configure(
                m_Settings.m_ScattererDiscoveryIntervalS,
                m_Settings.m_ScattererClassifyPerTick,
                m_Settings.m_ScattererRefreshPerTick,
                m_Settings.m_ScattererMaxEntries,
                true);
            RDF_RadarScattererRegistry.Tick(
                world,
                worldTime,
                origin,
                range * m_Settings.m_ScattererDiscoveryRangeScale);
        }

        float minDist = m_Settings.m_MinDistance;
        float minDistSq = minDist * minDist;
        float halfAngleRad = m_Settings.m_SectorHalfAngleDeg * 0.01745329;
        if (m_Settings.m_EnableMechanicalScan && m_Settings.m_Hardware)
            halfAngleRad = m_Settings.m_Hardware.m_AzimuthBeamwidthDeg * 0.5 * 0.01745329;
        float cosHalfAngle = Math.Cos(halfAngleRad);
        int maxTargets = m_Settings.m_MaxTargets;

        TraceParam param = new TraceParam();
        param.LayerMask = EPhysicsLayerPresets.Projectile;
        param.Exclude = subject;
        param.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
        int losBudget = m_Settings.m_MaxLosTracesPerScan;
        int freshBudget = ComputeFreshUpdateBudget();
        float rangeSq = range * range;

        RDF_RadarScanPassContext passCtx = new RDF_RadarScanPassContext();
        passCtx.m_Subject = subject;
        passCtx.m_World = world;
        passCtx.m_Origin = origin;
        passCtx.m_Forward = forward;
        passCtx.m_Range = range;
        passCtx.m_WorldTime = worldTime;
        passCtx.m_WallTime = wallTime;
        passCtx.m_MinDist = minDist;
        passCtx.m_MinDistSq = minDistSq;
        passCtx.m_RangeSq = rangeSq;
        passCtx.m_CosHalfAngle = cosHalfAngle;
        passCtx.m_MaxTargets = maxTargets;
        passCtx.m_Param = param;
        passCtx.m_LosBudget = losBudget;
        passCtx.m_LosUsed = 0;
        passCtx.m_FreshBudget = freshBudget;

        if (m_LosCache)
            m_LosCache.MaybePrune(wallTime);
        if (m_ScanReuseCache)
            m_ScanReuseCache.MaybePrune(wallTime);

        if (m_Settings.m_UseScattererRegistry)
            ScanFromRegistry(outTargets, passCtx);
        else
            ScanFromWorldSearch(outTargets, passCtx);

        AddDeceptionFalsePlots(outTargets, origin, forward, worldTime, maxTargets);
        ApplyCfarGate(outTargets, origin, forward, halfAngleRad);
        SynthesizeAllMeasurements(outTargets, origin);
        RemoveUndetectedTargets(outTargets);
    }

    // Table-driven pass: entities are pre-classified scatterers / emitters.
    protected void ScanFromRegistry(
        notnull array<ref RDF_RadarTarget> outTargets,
        notnull RDF_RadarScanPassContext ctx)
    {
        IEntity subject = ctx.m_Subject;
        BaseWorld world = ctx.m_World;
        vector origin = ctx.m_Origin;
        vector forward = ctx.m_Forward;
        float worldTime = ctx.m_WorldTime;
        float wallTime = ctx.m_WallTime;
        float minDistSq = ctx.m_MinDistSq;
        float rangeSq = ctx.m_RangeSq;
        float cosHalfAngle = ctx.m_CosHalfAngle;
        int maxTargets = ctx.m_MaxTargets;
        TraceParam param = ctx.m_Param;
        int losBudget = ctx.m_LosBudget;
        int losUsed = ctx.m_LosUsed;
        int freshBudget = ctx.m_FreshBudget;

        if (!m_ScattererCandidates)
            m_ScattererCandidates = new array<ref RDF_RadarScatterer>();
        RDF_RadarScattererRegistry.CollectInSphere(origin, Math.Sqrt(rangeSq), m_ScattererCandidates);

        int candidateCount = m_ScattererCandidates.Count();
        if (candidateCount <= 0)
        {
            ctx.m_LosUsed = losUsed;
            ctx.m_FreshBudget = freshBudget;
            return;
        }

        int startIndex = 0;
        if (m_Settings.m_FairScanCursor)
        {
            startIndex = m_RegistryScanCursor;
            while (startIndex >= candidateCount)
                startIndex = startIndex - candidateCount;
            while (startIndex < 0)
                startIndex = startIndex + candidateCount;
        }

        int processedCount = 0;
        for (int step = 0; step < candidateCount && outTargets.Count() < maxTargets; step++)
        {
            int i = step + startIndex;
            if (i >= candidateCount)
                i = i - candidateCount;
            processedCount = step + 1;

            RDF_RadarScatterer entry = m_ScattererCandidates.Get(i);
            if (!entry || !entry.m_Alive || !entry.m_Entity || entry.m_Entity == subject)
                continue;

            bool isEmitter = entry.m_Emitting && m_Settings.m_IncludeRadarEmitters;
            bool isProjectile = entry.m_Type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
            if (!isEmitter)
            {
                if (isProjectile && !m_Settings.m_IncludeProjectiles)
                    continue;
                if (!isProjectile && !m_Settings.m_IncludeVehicles)
                    continue;
            }

            vector pos = entry.m_Position;
            // Aim LOS at the geometric center so ground vehicles are not
            // blocked by terrain before their chassis origin (often on the ground).
            vector losEnd = RDF_RadarScanGeometry.GetScattererLosEnd(entry);
            vector toTarget = losEnd - origin;
            float distSq = toTarget.LengthSq();
            if (distSq < minDistSq || distSq > rangeSq)
                continue;

            float dist = Math.Sqrt(distSq);
            vector toTargetNorm = toTarget;
            if (dist > 0.001)
                toTargetNorm = toTarget / dist;
            float dot = forward[0] * toTargetNorm[0] + forward[1] * toTargetNorm[1] + forward[2] * toTargetNorm[2];
            if (dot < cosHalfAngle)
                continue;

            ERDF_RadarTargetType priorityType = entry.m_Type;
            if (isEmitter)
                priorityType = ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
            float speedMs = entry.m_Velocity.Length();
            int priorityBand = 2;
            if (m_ScanReuseCache)
            {
                priorityBand = m_ScanReuseCache.ClassifyPriorityBand(
                    priorityType,
                    dist,
                    speedMs,
                    m_Settings);
            }
            bool highPriority = priorityBand <= 0;
            bool runFullUpdate = true;
            if (m_ScanReuseCache)
                runFullUpdate = m_ScanReuseCache.ShouldRunFullUpdate(entry.m_Entity, priorityBand, wallTime, m_Settings);
            if (IsDirtyRegistryCandidate(priorityType, dist, speedMs, runFullUpdate))
                highPriority = true;
            if (!runFullUpdate || (!highPriority && freshBudget <= 0))
            {
                m_StatBudgetSkips = m_StatBudgetSkips + 1;
                TryInsertReusedScattererTarget(
                    outTargets,
                    entry,
                    losEnd,
                    dist,
                    priorityType,
                    worldTime,
                    wallTime);
                continue;
            }

            float hitFraction = 1.0;
            bool losClear = false;
            bool reusedLos = false;
            if (m_LosCache)
            {
                reusedLos = m_LosCache.TryReuse(
                    entry.m_Entity,
                    origin,
                    losEnd,
                    wallTime,
                    m_Settings.m_LosCacheMaxAgeS,
                    m_Settings.m_LosCacheMaxOriginShiftM,
                    m_Settings.m_LosCacheMaxTargetShiftM,
                    hitFraction,
                    losClear);
            }
            if (reusedLos)
                m_StatLosCacheHits = m_StatLosCacheHits + 1;
            if (!reusedLos)
            {
                if (losUsed >= losBudget)
                {
                    m_StatBudgetSkips = m_StatBudgetSkips + 1;
                    TryInsertReusedScattererTarget(
                        outTargets,
                        entry,
                        losEnd,
                        dist,
                        priorityType,
                        worldTime,
                        wallTime);
                    break;
                }
                param.Start = origin;
                param.End = losEnd;
                param.TraceEnt = null;
                hitFraction = world.TraceMove(param, null);
                losUsed = losUsed + 1;
                losClear = RDF_RadarScanGeometry.IsLineOfSightClear(hitFraction, param.TraceEnt, entry.m_Entity);
                if (m_LosCache)
                {
                    m_LosCache.Store(
                        entry.m_Entity,
                        origin,
                        losEnd,
                        hitFraction,
                        losClear,
                        wallTime);
                }
            }
            if (!losClear && !m_Settings.m_EnableNlosMultipath)
                continue;

            float losAzimuthDeg = Math.Atan2(toTargetNorm[2], toTargetNorm[0]) * RAD_TO_DEG;
            float horiz = Math.Sqrt(
                toTargetNorm[0] * toTargetNorm[0] + toTargetNorm[2] * toTargetNorm[2]);
            float losElevationDeg = Math.Atan2(toTargetNorm[1], Math.Max(0.001, horiz)) * RAD_TO_DEG;
            int scanNumber = 0;
            if (m_Settings.m_Hardware)
            {
                float scanPeriod = m_Settings.m_Hardware.GetScanPeriodS();
                if (scanPeriod < 1000000.0)
                    scanNumber = Math.Floor(worldTime / scanPeriod);
            }
            float instantRcs = RDF_RadarScattererRegistry.EvaluateInstantRcsM2(
                entry,
                losAzimuthDeg,
                losElevationDeg,
                scanNumber);

            RDF_RadarTarget t = new RDF_RadarTarget();
            t.m_Entity = entry.m_Entity;
            t.m_ScattererId = entry.m_ScattererId;
            t.m_Position = losEnd;
            t.m_Distance = dist;
            t.m_Velocity = entry.m_Velocity;
            if (isEmitter)
                t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
            else
                t.m_Type = entry.m_Type;
            t.m_RcsM2 = instantRcs;
            t.m_MeanRcsM2 = entry.m_MeanRcsM2;
            t.m_SwerlingModel = entry.m_SwerlingModel;
            t.m_RotorTipSpeedMs = entry.m_RotorTipSpeedMs;
            t.m_BladeCount = entry.m_BladeCount;
            t.m_RotorRcsFraction = entry.m_RotorRcsFraction;
            t.m_HubWidthMs = entry.m_HubWidthMs;
            t.m_AglM = entry.m_AglM;
            if (isEmitter)
            {
                t.m_EmitFrequencyHz = entry.m_EmitFrequencyHz;
                t.m_EmitPeakPowerW = entry.m_EmitPeakPowerW;
                t.m_EmitAntennaGainDbi = entry.m_EmitAntennaGainDbi;
                t.m_EmitStrength = entry.m_EmitStrength;
            }
            if (entry.m_DemSampleValid)
            {
                t.m_DemSampleValid = true;
                t.m_DemSurfaceClass = entry.m_DemSurfaceClass;
                t.m_DemTerrainY = entry.m_DemTerrainY;
            }
            t.m_Time = worldTime;
            t.m_LosBlocked = !losClear;
            t.m_LosHitFraction = hitFraction;
            t.m_MultipathFactor = 1.0;
            t.m_IsAnonymous = false;
            t.m_IsFalsePlot = false;
            bool reusedPhysical = false;
            RDF_RadarTarget physicalSource;
            if (m_ScanReuseCache)
            {
                reusedPhysical = m_ScanReuseCache.TryGetPhysicalReuse(
                    entry.m_Entity,
                    origin,
                    losEnd,
                    speedMs,
                    wallTime,
                    m_Settings.m_PhysicalReuseMaxAgeS,
                    m_Settings.m_PhysicalReuseMaxOriginShiftM,
                    m_Settings.m_PhysicalReuseMaxTargetShiftM,
                    m_Settings.m_PhysicalReuseMaxSpeedMs,
                    physicalSource);
            }
            if (reusedPhysical)
                ApplyPhysicalReuse(t, physicalSource);
            else
                RDF_RadarPhysicalDetect.Process(
                    t, origin, forward, worldTime, world,
                    m_Settings, m_DemCache, m_ScanRainLossDbPerKm, m_ClutterMap);

            if (m_ScanReuseCache)
            {
                m_ScanReuseCache.StoreResult(
                    entry.m_Entity,
                    t,
                    origin,
                    losEnd,
                    wallTime,
                    priorityBand,
                    true,
                    !reusedPhysical);
            }
            m_StatFreshUpdates = m_StatFreshUpdates + 1;
            if (!highPriority && freshBudget > 0)
                freshBudget = freshBudget - 1;
            if (t.m_Detected || m_Settings.m_KeepUndetected)
                outTargets.Insert(t);
        }

        if (m_Settings.m_FairScanCursor)
        {
            int nextIndex = startIndex + processedCount;
            while (nextIndex >= candidateCount)
                nextIndex = nextIndex - candidateCount;
            while (nextIndex < 0)
                nextIndex = nextIndex + candidateCount;
            m_RegistryScanCursor = nextIndex;
        }
        else
        {
            m_RegistryScanCursor = 0;
        }

        ctx.m_LosUsed = losUsed;
        ctx.m_FreshBudget = freshBudget;
    }

    // Legacy pass: search the world every scan (kept for comparison / fallback).
    protected void ScanFromWorldSearch(
        notnull array<ref RDF_RadarTarget> outTargets,
        notnull RDF_RadarScanPassContext ctx)
    {
        IEntity subject = ctx.m_Subject;
        BaseWorld world = ctx.m_World;
        vector origin = ctx.m_Origin;
        vector forward = ctx.m_Forward;
        float range = ctx.m_Range;
        float worldTime = ctx.m_WorldTime;
        float wallTime = ctx.m_WallTime;
        float minDist = ctx.m_MinDist;
        float minDistSq = ctx.m_MinDistSq;
        float rangeSq = ctx.m_RangeSq;
        float cosHalfAngle = ctx.m_CosHalfAngle;
        int maxTargets = ctx.m_MaxTargets;
        TraceParam param = ctx.m_Param;
        int losBudget = ctx.m_LosBudget;
        int losUsed = ctx.m_LosUsed;
        int freshBudget = ctx.m_FreshBudget;

        array<IEntity> candidates = new array<IEntity>();
        CollectCandidateEntities(world, subject, origin, range, candidates);
        // Trace budget is small; process nearest first so nearby vehicles are not
        // starved by distant characters / clutter entities earlier in the dump.
        SortEntitiesByDistance(candidates, origin);

        for (int i = 0; i < candidates.Count() && outTargets.Count() < maxTargets; i++)
        {
            IEntity ent = candidates.Get(i);
            if (!ent || ent == subject)
                continue;

            vector pos = RDF_RadarScanGeometry.GetEntityLosEnd(ent);
            vector toTarget = pos - origin;
            float distSq = toTarget.LengthSq();
            if (distSq < minDistSq || distSq > rangeSq)
                continue;

            float dist = Math.Sqrt(distSq);
            vector toTargetNorm = toTarget;
            if (dist > 0.001)
                toTargetNorm = toTarget / dist;
            float dot = forward[0] * toTargetNorm[0] + forward[1] * toTargetNorm[1] + forward[2] * toTargetNorm[2];
            if (dot < cosHalfAngle)
                continue;

            bool isProjectile = RDF_RadarEntityClassifier.IsProjectile(ent);
            bool isVehicle = RDF_RadarEntityClassifier.IsVehicleOrCharacter(ent);
            if (isProjectile && !m_Settings.m_IncludeProjectiles)
                continue;
            if (isVehicle && !m_Settings.m_IncludeVehicles)
                continue;
            if (!isProjectile && !isVehicle)
                continue;

            vector entityVelocity = RDF_RadarScanGeometry.GetEntityVelocity(ent);
            float speedMs = entityVelocity.Length();
            ERDF_RadarTargetType priorityType = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
            if (isProjectile)
                priorityType = ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
            int priorityBand = 2;
            if (m_ScanReuseCache)
            {
                priorityBand = m_ScanReuseCache.ClassifyPriorityBand(
                    priorityType,
                    dist,
                    speedMs,
                    m_Settings);
            }
            bool highPriority = priorityBand <= 0;
            bool runFullUpdate = true;
            if (m_ScanReuseCache)
                runFullUpdate = m_ScanReuseCache.ShouldRunFullUpdate(ent, priorityBand, wallTime, m_Settings);
            if (!runFullUpdate || (!highPriority && freshBudget <= 0))
            {
                m_StatBudgetSkips = m_StatBudgetSkips + 1;
                RDF_RadarTarget reusedTarget;
                if (m_ScanReuseCache
                    && m_ScanReuseCache.TryGetReusedTarget(
                        ent,
                        wallTime,
                        m_Settings.m_TargetReuseMaxAgeS,
                        reusedTarget))
                {
                    m_StatReuseHits = m_StatReuseHits + 1;
                    reusedTarget.m_Entity = ent;
                    reusedTarget.m_Position = pos;
                    reusedTarget.m_Distance = dist;
                    reusedTarget.m_Velocity = entityVelocity;
                    reusedTarget.m_Type = priorityType;
                    reusedTarget.m_Time = worldTime;
                    if (reusedTarget.m_Detected || m_Settings.m_KeepUndetected)
                        outTargets.Insert(reusedTarget);
                }
                continue;
            }

            float hitFraction = 1.0;
            bool losClear = false;
            bool reusedLos = false;
            if (m_LosCache)
            {
                reusedLos = m_LosCache.TryReuse(
                    ent,
                    origin,
                    pos,
                    wallTime,
                    m_Settings.m_LosCacheMaxAgeS,
                    m_Settings.m_LosCacheMaxOriginShiftM,
                    m_Settings.m_LosCacheMaxTargetShiftM,
                    hitFraction,
                    losClear);
            }
            if (reusedLos)
                m_StatLosCacheHits = m_StatLosCacheHits + 1;
            if (!reusedLos)
            {
                if (losUsed >= losBudget)
                    break;
                param.Start = origin;
                param.End = pos;
                param.TraceEnt = null;
                hitFraction = world.TraceMove(param, null);
                losUsed = losUsed + 1;
                losClear = RDF_RadarScanGeometry.IsLineOfSightClear(hitFraction, param.TraceEnt, ent);
                if (m_LosCache)
                {
                    m_LosCache.Store(
                        ent,
                        origin,
                        pos,
                        hitFraction,
                        losClear,
                        wallTime);
                }
            }
            if (!losClear && !m_Settings.m_EnableNlosMultipath)
                continue;

            RDF_RadarTarget t = new RDF_RadarTarget();
            t.m_Entity = ent;
            t.m_Position = pos;
            t.m_Distance = dist;
            t.m_Velocity = entityVelocity;
            t.m_Type = priorityType;
            t.m_Time = worldTime;
            t.m_LosBlocked = !losClear;
            t.m_LosHitFraction = hitFraction;
            t.m_MultipathFactor = 1.0;
            t.m_IsAnonymous = false;
            t.m_IsFalsePlot = false;
            RDF_RadarScatterer worldEntry = RDF_RadarScattererRegistry.Find(ent);
            if (worldEntry)
            {
                t.m_ScattererId = worldEntry.m_ScattererId;
                t.m_RotorTipSpeedMs = worldEntry.m_RotorTipSpeedMs;
                t.m_BladeCount = worldEntry.m_BladeCount;
                t.m_RotorRcsFraction = worldEntry.m_RotorRcsFraction;
                t.m_HubWidthMs = worldEntry.m_HubWidthMs;
                t.m_MeanRcsM2 = worldEntry.m_MeanRcsM2;
                t.m_SwerlingModel = worldEntry.m_SwerlingModel;
                if (priorityType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
                {
                    t.m_EmitFrequencyHz = worldEntry.m_EmitFrequencyHz;
                    t.m_EmitPeakPowerW = worldEntry.m_EmitPeakPowerW;
                    t.m_EmitAntennaGainDbi = worldEntry.m_EmitAntennaGainDbi;
                    t.m_EmitStrength = worldEntry.m_EmitStrength;
                }
            }
            bool reusedPhysical = false;
            RDF_RadarTarget physicalSource;
            if (m_ScanReuseCache)
            {
                reusedPhysical = m_ScanReuseCache.TryGetPhysicalReuse(
                    ent,
                    origin,
                    pos,
                    speedMs,
                    wallTime,
                    m_Settings.m_PhysicalReuseMaxAgeS,
                    m_Settings.m_PhysicalReuseMaxOriginShiftM,
                    m_Settings.m_PhysicalReuseMaxTargetShiftM,
                    m_Settings.m_PhysicalReuseMaxSpeedMs,
                    physicalSource);
            }
            if (reusedPhysical)
                ApplyPhysicalReuse(t, physicalSource);
            else
                RDF_RadarPhysicalDetect.Process(
                    t, origin, forward, worldTime, world,
                    m_Settings, m_DemCache, m_ScanRainLossDbPerKm, m_ClutterMap);

            if (m_ScanReuseCache)
            {
                m_ScanReuseCache.StoreResult(
                    ent,
                    t,
                    origin,
                    pos,
                    wallTime,
                    priorityBand,
                    true,
                    !reusedPhysical);
            }
            m_StatFreshUpdates = m_StatFreshUpdates + 1;
            if (!highPriority && freshBudget > 0)
                freshBudget = freshBudget - 1;
            if (t.m_Detected || m_Settings.m_KeepUndetected)
                outTargets.Insert(t);
        }

        if (!m_Settings.m_IncludeRadarEmitters || outTargets.Count() >= maxTargets)
            return;

        array<ref RDF_RadarTarget> emitterTargets = new array<ref RDF_RadarTarget>();
        RDF_RadarEmitterRegistry.GetEmittingInSphere(origin, range, emitterTargets, worldTime);
        for (int j = 0; j < emitterTargets.Count() && outTargets.Count() < maxTargets; j++)
        {
            RDF_RadarTarget et = emitterTargets.Get(j);
            if (et.m_Entity == subject)
                continue;
            vector toE = et.m_Position - origin;
            float distE = toE.Length();
            if (distE < minDist)
                continue;
            vector toENorm = toE / distE;
            float dotE = forward[0] * toENorm[0] + forward[1] * toENorm[1] + forward[2] * toENorm[2];
            if (dotE < cosHalfAngle)
                continue;

            vector emitterVelocity = RDF_RadarScanGeometry.GetEntityVelocity(et.m_Entity);
            float emitterSpeedMs = emitterVelocity.Length();
            int emitterPriorityBand = 2;
            if (m_ScanReuseCache)
            {
                emitterPriorityBand = m_ScanReuseCache.ClassifyPriorityBand(
                    ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER,
                    distE,
                    emitterSpeedMs,
                    m_Settings);
            }
            bool emitterHighPriority = emitterPriorityBand <= 0;
            bool emitterRunFull = true;
            if (m_ScanReuseCache)
            {
                emitterRunFull = m_ScanReuseCache.ShouldRunFullUpdate(
                    et.m_Entity, emitterPriorityBand, wallTime, m_Settings);
            }
            if (!emitterRunFull || (!emitterHighPriority && freshBudget <= 0))
            {
                m_StatBudgetSkips = m_StatBudgetSkips + 1;
                RDF_RadarTarget reusedEmitter;
                if (m_ScanReuseCache
                    && m_ScanReuseCache.TryGetReusedTarget(
                        et.m_Entity,
                        wallTime,
                        m_Settings.m_TargetReuseMaxAgeS,
                        reusedEmitter))
                {
                    m_StatReuseHits = m_StatReuseHits + 1;
                    reusedEmitter.m_Entity = et.m_Entity;
                    reusedEmitter.m_Position = et.m_Position;
                    reusedEmitter.m_Distance = distE;
                    reusedEmitter.m_Velocity = emitterVelocity;
                    reusedEmitter.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER;
                    reusedEmitter.m_Time = worldTime;
                    if (reusedEmitter.m_Detected || m_Settings.m_KeepUndetected)
                    {
                        if (!ContainsTargetEntity(outTargets, reusedEmitter.m_Entity))
                            outTargets.Insert(reusedEmitter);
                    }
                }
                continue;
            }

            float hitFractionE = 1.0;
            bool losClearE = false;
            bool reusedLosE = false;
            if (m_LosCache && et.m_Entity)
            {
                reusedLosE = m_LosCache.TryReuse(
                    et.m_Entity,
                    origin,
                    et.m_Position,
                    wallTime,
                    m_Settings.m_LosCacheMaxAgeS,
                    m_Settings.m_LosCacheMaxOriginShiftM,
                    m_Settings.m_LosCacheMaxTargetShiftM,
                    hitFractionE,
                    losClearE);
            }
            if (reusedLosE)
                m_StatLosCacheHits = m_StatLosCacheHits + 1;
            if (!reusedLosE)
            {
                if (losUsed >= losBudget)
                    break;
                param.Start = origin;
                param.End = et.m_Position;
                param.TraceEnt = null;
                hitFractionE = world.TraceMove(param, null);
                losUsed = losUsed + 1;
                losClearE = RDF_RadarScanGeometry.IsLineOfSightClear(hitFractionE, param.TraceEnt, et.m_Entity);
                if (m_LosCache && et.m_Entity)
                {
                    m_LosCache.Store(
                        et.m_Entity,
                        origin,
                        et.m_Position,
                        hitFractionE,
                        losClearE,
                        wallTime);
                }
            }
            if (!losClearE && !m_Settings.m_EnableNlosMultipath)
                continue;

            et.m_Velocity = emitterVelocity;
            et.m_LosBlocked = !losClearE;
            et.m_LosHitFraction = hitFractionE;
            et.m_MultipathFactor = 1.0;
            et.m_IsAnonymous = false;
            et.m_IsFalsePlot = false;
            bool reusedEmitterPhysical = false;
            RDF_RadarTarget emitterPhysicalSource;
            if (m_ScanReuseCache)
            {
                reusedEmitterPhysical = m_ScanReuseCache.TryGetPhysicalReuse(
                    et.m_Entity,
                    origin,
                    et.m_Position,
                    emitterSpeedMs,
                    wallTime,
                    m_Settings.m_PhysicalReuseMaxAgeS,
                    m_Settings.m_PhysicalReuseMaxOriginShiftM,
                    m_Settings.m_PhysicalReuseMaxTargetShiftM,
                    m_Settings.m_PhysicalReuseMaxSpeedMs,
                    emitterPhysicalSource);
            }
            if (reusedEmitterPhysical)
                ApplyPhysicalReuse(et, emitterPhysicalSource);
            else
                RDF_RadarPhysicalDetect.Process(
                    et, origin, forward, worldTime, world,
                    m_Settings, m_DemCache, m_ScanRainLossDbPerKm, m_ClutterMap);

            if (m_ScanReuseCache)
            {
                m_ScanReuseCache.StoreResult(
                    et.m_Entity,
                    et,
                    origin,
                    et.m_Position,
                    wallTime,
                    emitterPriorityBand,
                    true,
                    !reusedEmitterPhysical);
            }
            m_StatFreshUpdates = m_StatFreshUpdates + 1;
            if (!emitterHighPriority && freshBudget > 0)
                freshBudget = freshBudget - 1;
            if (et.m_Detected || m_Settings.m_KeepUndetected)
            {
                if (!ContainsTargetEntity(outTargets, et.m_Entity))
                    outTargets.Insert(et);
            }
        }
        ctx.m_LosUsed = losUsed;
        ctx.m_FreshBudget = freshBudget;
    }

    protected void SynthesizeAllMeasurements(
        array<ref RDF_RadarTarget> targets,
        vector origin)
    {
        if (!targets || !m_Settings || !m_Settings.m_EnableMeasurementSynthesis)
            return;
        if (!m_Settings.m_Hardware)
            return;

        RDF_RadarMeasurementModel model = m_Settings.m_MeasurementModel;
        if (model)
        {
            model.SynthesizeAll(targets, origin, m_Settings.m_Hardware, m_Settings);
            return;
        }

        // Fallback when settings cleared the model reference.
        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t || !t.m_Detected)
                continue;
            RDF_RadarMeasurement.Synthesize(t, origin, m_Settings.m_Hardware, m_Settings);
        }
    }

    protected void CollectCandidateEntities(
        BaseWorld world,
        IEntity subject,
        vector origin,
        float range,
        notnull array<IEntity> outCandidates)
    {
        if (!m_CandidateCollect)
            m_CandidateCollect = new RDF_RadarCandidateCollect();
        m_CandidateCollect.CollectCandidateEntities(
            world,
            subject,
            origin,
            range,
            m_Settings,
            outCandidates);
    }

    protected void SortEntitiesByDistance(notnull array<IEntity> entities, vector origin)
    {
        if (!m_CandidateCollect)
            m_CandidateCollect = new RDF_RadarCandidateCollect();
        m_CandidateCollect.SortEntitiesByDistance(entities, origin);
    }

    protected bool IsDirtyRegistryCandidate(
        ERDF_RadarTargetType priorityType,
        float distanceM,
        float speedMs,
        bool runFullUpdate)
    {
        if (!runFullUpdate)
            return false;
        if (priorityType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return true;
        if (priorityType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return true;
        if (distanceM <= m_Settings.m_PriorityNearRangeM)
            return true;
        if (speedMs >= m_Settings.m_PriorityFastSpeedMs)
            return true;
        return false;
    }

    protected void TryInsertReusedScattererTarget(
        notnull array<ref RDF_RadarTarget> outTargets,
        RDF_RadarScatterer entry,
        vector losEnd,
        float dist,
        ERDF_RadarTargetType priorityType,
        float worldTime,
        float wallTime)
    {
        if (!entry || !entry.m_Entity || !m_ScanReuseCache)
            return;

        RDF_RadarTarget reusedTarget;
        if (!m_ScanReuseCache.TryGetReusedTarget(
            entry.m_Entity,
            wallTime,
            m_Settings.m_TargetReuseMaxAgeS,
            reusedTarget))
        {
            return;
        }

        m_StatReuseHits = m_StatReuseHits + 1;
        reusedTarget.m_Entity = entry.m_Entity;
        reusedTarget.m_ScattererId = entry.m_ScattererId;
        reusedTarget.m_Position = losEnd;
        reusedTarget.m_Distance = dist;
        reusedTarget.m_Velocity = entry.m_Velocity;
        reusedTarget.m_Type = priorityType;
        reusedTarget.m_AglM = entry.m_AglM;
        reusedTarget.m_Time = worldTime;
        if (entry.m_DemSampleValid)
        {
            reusedTarget.m_DemSampleValid = true;
            reusedTarget.m_DemSurfaceClass = entry.m_DemSurfaceClass;
            reusedTarget.m_DemTerrainY = entry.m_DemTerrainY;
        }
        if (reusedTarget.m_Detected || m_Settings.m_KeepUndetected)
            outTargets.Insert(reusedTarget);
    }

    protected bool ContainsTargetEntity(
        notnull array<ref RDF_RadarTarget> targets,
        IEntity entity)
    {
        if (!entity)
            return false;
        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;
            if (t.m_Entity == entity)
                return true;
        }
        return false;
    }

    protected void AddDeceptionFalsePlots(
        notnull array<ref RDF_RadarTarget> outTargets,
        vector origin,
        vector scanForward,
        float worldTimeS,
        int maxTargets)
    {
        if (!m_Settings || !m_Settings.m_EwStack)
            return;
        if (outTargets.Count() >= maxTargets)
            return;

        array<ref RDF_RadarFalsePlot> falsePlots = new array<ref RDF_RadarFalsePlot>();
        m_Settings.m_EwStack.CollectFalsePlots(
            origin,
            scanForward,
            m_Settings.m_Hardware,
            worldTimeS,
            falsePlots);
        if (!falsePlots || falsePlots.Count() <= 0)
            return;

        float scanAzimuth = Math.Atan2(scanForward[2], scanForward[0]);
        for (int i = 0; i < falsePlots.Count() && outTargets.Count() < maxTargets; i++)
        {
            RDF_RadarFalsePlot p = falsePlots.Get(i);
            if (!p)
                continue;
            if (p.m_PowerW <= 0.0)
                continue;
            float rangeM = p.m_RangeM;
            if (rangeM <= m_Settings.m_MinDistance)
                continue;
            if (rangeM > m_Settings.m_Range)
                continue;

            float azimuthDeg = p.m_AzimuthDeg;
            float azimuthRad = scanAzimuth + azimuthDeg * DEG_TO_RAD;
            float elevationRad = p.m_ElevationDeg * DEG_TO_RAD;
            float cosElev = Math.Cos(elevationRad);
            vector dir = Vector(
                Math.Cos(azimuthRad) * cosElev,
                Math.Sin(elevationRad),
                Math.Sin(azimuthRad) * cosElev);
            RDF_RadarTarget t = new RDF_RadarTarget();
            t.m_Entity = null;
            t.m_Position = origin + dir * rangeM;
            t.m_Distance = rangeM;
            t.m_Velocity = dir * p.m_RangeRateMs;
            t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
            t.m_Time = worldTimeS;
            t.m_AzimuthDeg = Math.Atan2(dir[2], dir[0]) * RAD_TO_DEG;
            t.m_ElevationDeg = p.m_ElevationDeg;
            t.m_RadialSpeedMs = p.m_RangeRateMs;
            t.m_RcsM2 = 0.0;
            t.m_ReceivedPowerW = p.m_PowerW;
            t.m_ProcessedPowerW = p.m_PowerW;
            t.m_DopplerHz = 0.0;
            t.m_MtiGain = 1.0;
            t.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
            t.m_DemSampleValid = false;
            t.m_ClutterPowerW = 0.0;
            t.m_ClutterToNoiseDb = -300.0;
            t.m_SnrDb = m_Settings.m_DetectionSnrDb + 3.0;
            t.m_Detected = true;
            t.m_IsAnonymous = true;
            t.m_IsFalsePlot = true;
            t.m_CfarPowerW = p.m_PowerW;
            t.m_LosBlocked = false;
            t.m_LosHitFraction = 1.0;
            t.m_MultipathFactor = 1.0;
            t.m_BeamName = "deception";
            t.m_ScanNumber = 0;
            outTargets.Insert(t);
        }
    }

    protected void ApplyCfarGate(
        notnull array<ref RDF_RadarTarget> outTargets,
        vector origin,
        vector scanForward,
        float halfAngleRad)
    {
        if (!m_CfarProcessor)
            m_CfarProcessor = new RDF_RadarCfarProcessor();
        m_CfarProcessor.ApplyGate(outTargets, origin, scanForward, halfAngleRad, m_Settings);
    }

    protected void RemoveUndetectedTargets(notnull array<ref RDF_RadarTarget> targets)
    {
        if (!m_CfarProcessor)
            m_CfarProcessor = new RDF_RadarCfarProcessor();
        m_CfarProcessor.RemoveUndetectedTargets(targets, m_Settings);
    }

    protected int ComputeFreshUpdateBudget()
    {
        int budget = m_Settings.m_MaxLosTracesPerScan / 2;
        budget = budget + m_Settings.m_MaxTargets / 6;
        int budgetMin = m_Settings.m_FreshUpdateBudgetMin;
        int budgetMax = m_Settings.m_FreshUpdateBudgetMax;
        if (budget < budgetMin)
            budget = budgetMin;
        if (budget > budgetMax)
            budget = budgetMax;
        return budget;
    }

    protected void ApplyPhysicalReuse(
        RDF_RadarTarget target,
        RDF_RadarTarget source)
    {
        if (!target || !source)
            return;
        target.m_AzimuthDeg = source.m_AzimuthDeg;
        target.m_ElevationDeg = source.m_ElevationDeg;
        target.m_RadialSpeedMs = source.m_RadialSpeedMs;
        target.m_RcsM2 = source.m_RcsM2;
        target.m_MeanRcsM2 = source.m_MeanRcsM2;
        target.m_SwerlingModel = source.m_SwerlingModel;
        target.m_AglM = source.m_AglM;
        target.m_DemTerrainY = source.m_DemTerrainY;
        target.m_ReceivedPowerW = source.m_ReceivedPowerW;
        target.m_ProcessedPowerW = source.m_ProcessedPowerW;
        target.m_DopplerHz = source.m_DopplerHz;
        target.m_MtiGain = source.m_MtiGain;
        target.m_DopplerBin = source.m_DopplerBin;
        target.m_PrfIndex = source.m_PrfIndex;
        target.m_RotorTipSpeedMs = source.m_RotorTipSpeedMs;
        target.m_BladeCount = source.m_BladeCount;
        target.m_RotorRcsFraction = source.m_RotorRcsFraction;
        target.m_HubWidthMs = source.m_HubWidthMs;
        target.m_DemSurfaceClass = source.m_DemSurfaceClass;
        target.m_DemSampleValid = source.m_DemSampleValid;
        target.m_ClutterPowerW = source.m_ClutterPowerW;
        target.m_ClutterToNoiseDb = source.m_ClutterToNoiseDb;
        target.m_SnrDb = source.m_SnrDb;
        target.m_Detected = source.m_Detected;
        target.m_CfarPowerW = source.m_CfarPowerW;
        target.m_MultipathFactor = source.m_MultipathFactor;
        target.m_BeamName = source.m_BeamName;
        target.m_ScanNumber = source.m_ScanNumber;
        target.m_EmitFrequencyHz = source.m_EmitFrequencyHz;
        target.m_EmitPeakPowerW = source.m_EmitPeakPowerW;
        target.m_EmitAntennaGainDbi = source.m_EmitAntennaGainDbi;
        target.m_EmitStrength = source.m_EmitStrength;
    }

    protected float NormalizeAngleRad(float angle)
    {
        return RDF_RadarScanGeometry.NormalizeAngleRad(angle);
    }

    protected vector GetSubjectOrigin(IEntity subject)
    {
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector origin = worldMat[3];

        if (m_Settings.m_UseBoundsCenter)
        {
            vector mins, maxs;
            subject.GetBounds(mins, maxs);
            vector centerLocal = (mins + maxs) * 0.5;
            vector centerWorld = worldMat[3]
                + (worldMat[0] * centerLocal[0])
                + (worldMat[1] * centerLocal[1])
                + (worldMat[2] * centerLocal[2]);
            origin = centerWorld;
        }

        if (m_Settings.m_UseLocalOffset)
        {
            origin = origin
                + (worldMat[0] * m_Settings.m_OriginOffset[0])
                + (worldMat[1] * m_Settings.m_OriginOffset[1])
                + (worldMat[2] * m_Settings.m_OriginOffset[2]);
        }
        else
        {
            origin = origin + m_Settings.m_OriginOffset;
        }

        return origin;
    }

    // Entity facing direction in Enfusion is transform[2] (same as heading /
    // mortar / LOS helpers in vanilla). transform[0] is local right.
    protected vector GetSubjectForward(IEntity subject)
    {
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector fwd = worldMat[2];
        float len = fwd.Length();
        if (len > 0.001)
            return fwd / len;
        return "0 0 1";
    }

    protected vector GetScanForward(IEntity subject, float worldTime)
    {
        if (!m_Settings.m_EnableMechanicalScan || !m_Settings.m_Hardware)
            return GetSubjectForward(subject);

        float rpm = m_Settings.m_Hardware.m_ScanRpm;
        if (rpm <= 0.0)
            return GetSubjectForward(subject);

        float angle = worldTime * rpm * Math.PI * 2.0 / 60.0;
        angle = NormalizeAngleRad(angle);
        return Vector(Math.Cos(angle), 0.0, Math.Sin(angle));
    }
}
