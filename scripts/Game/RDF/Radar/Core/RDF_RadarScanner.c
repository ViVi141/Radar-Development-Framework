// Radar scanner: scatterers feed the physics chain; published plots are measurements.
// Candidates from sphere query + active fallback; Trace LOS + optional NLOS;
// SNR / CFAR gates; optional measurement synthesis (quantize + noise, no entity truth).
class RDF_RadarScanner
{
    protected static const float DEG_TO_RAD = 0.0174532925199;
    protected static const float RAD_TO_DEG = 57.2957795;
    protected static const float LIGHT_SPEED = 299792458.0;

    protected ref RDF_RadarSettings m_Settings;
    protected ref RDF_DemRuntimeCache m_DemCache;

    // Last scan geometry, cached for display (PPI HUD) without recomputing.
    protected vector m_LastOrigin = "0 0 0";
    protected vector m_LastForward = "1 0 0";
    protected float m_LastRange = 2000.0;
    protected ref array<IEntity> m_QueryCandidates;
    protected ref array<IEntity> m_CandidateScratch;
    protected ref array<ref array<float>> m_CfarPowerGrid;
    protected ref array<bool> m_CfarRowHits;
    protected int m_CfarCachedAzBins;
    protected int m_CfarCachedRangeBins;
    protected bool m_SettingsValidated;

    void RDF_RadarScanner(RDF_RadarSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_RadarSettings();

        m_DemCache = new RDF_DemRuntimeCache();
        m_QueryCandidates = new array<IEntity>();
        m_CandidateScratch = new array<IEntity>();
        m_CfarPowerGrid = new array<ref array<float>>();
        m_CfarRowHits = new array<bool>();
        m_CfarCachedAzBins = 0;
        m_CfarCachedRangeBins = 0;
        m_SettingsValidated = false;
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

    string GetScattererStatsLine()
    {
        return RDF_RadarScattererRegistry.GetStatsLine();
    }

    void Scan(IEntity subject, array<ref RDF_RadarTarget> outTargets)
    {
        if (!subject || !m_Settings || !m_Settings.m_Enabled || !outTargets)
            return;

        outTargets.Clear();
        if (!m_SettingsValidated)
        {
            m_Settings.Validate();
            m_SettingsValidated = true;
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
        vector forward = GetScanForward(subject, worldTime);
        float range = m_Settings.m_Range;
        m_LastOrigin = origin;
        m_LastForward = forward;
        m_LastRange = range;
        if (m_DemCache)
        {
            m_DemCache.SetMaxTiles(m_Settings.m_DemCacheMaxTiles);
            m_DemCache.BeginScan(m_Settings.m_DemTileLoadsPerScan);
            // WLR ground sampling needs DEM even when clutter is off.
            m_DemCache.InitializeForCurrentWorld();
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
        int losUsed = 0;
        float rangeSq = range * range;

        if (m_Settings.m_UseScattererRegistry)
        {
            ScanFromRegistry(
                subject,
                world,
                outTargets,
                origin,
                forward,
                worldTime,
                minDistSq,
                rangeSq,
                cosHalfAngle,
                maxTargets,
                param,
                losBudget,
                losUsed);
        }
        else
        {
            ScanFromWorldSearch(
                subject,
                world,
                outTargets,
                origin,
                forward,
                range,
                worldTime,
                minDist,
                minDistSq,
                rangeSq,
                cosHalfAngle,
                maxTargets,
                param,
                losBudget,
                losUsed);
        }

        AddDeceptionFalsePlots(outTargets, origin, forward, worldTime, maxTargets);
        ApplyCfarGate(outTargets, origin, forward, halfAngleRad);
        SynthesizeAllMeasurements(outTargets, origin);
        RemoveUndetectedTargets(outTargets);
    }

    // Table-driven pass: entities are pre-classified scatterers / emitters.
    protected void ScanFromRegistry(
        IEntity subject,
        BaseWorld world,
        notnull array<ref RDF_RadarTarget> outTargets,
        vector origin,
        vector forward,
        float worldTime,
        float minDistSq,
        float rangeSq,
        float cosHalfAngle,
        int maxTargets,
        TraceParam param,
        int losBudget,
        int losUsed)
    {
        array<ref RDF_RadarScatterer> entries = RDF_RadarScattererRegistry.GetEntries();
        if (!entries)
            return;

        for (int i = 0; i < entries.Count() && outTargets.Count() < maxTargets; i++)
        {
            RDF_RadarScatterer entry = entries.Get(i);
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
            vector losEnd = GetScattererLosEnd(entry);
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

            if (losUsed >= losBudget)
                break;

            param.Start = origin;
            param.End = losEnd;
            param.TraceEnt = null;
            float hitFraction = world.TraceMove(param, null);
            losUsed = losUsed + 1;
            bool losClear = IsLineOfSightClear(hitFraction, param.TraceEnt, entry.m_Entity);
            if (!losClear && !m_Settings.m_EnableNlosMultipath)
                continue;

            float losAzimuthDeg = Math.Atan2(toTargetNorm[2], toTargetNorm[0]) * RAD_TO_DEG;
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
            t.m_AglM = entry.m_AglM;
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
            ProcessPhysicalDetection(t, origin, forward, worldTime, world);
            if (t.m_Detected || m_Settings.m_KeepUndetected)
                outTargets.Insert(t);
        }
    }

    // Legacy pass: search the world every scan (kept for comparison / fallback).
    protected void ScanFromWorldSearch(
        IEntity subject,
        BaseWorld world,
        notnull array<ref RDF_RadarTarget> outTargets,
        vector origin,
        vector forward,
        float range,
        float worldTime,
        float minDist,
        float minDistSq,
        float rangeSq,
        float cosHalfAngle,
        int maxTargets,
        TraceParam param,
        int losBudget,
        int losUsed)
    {
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

            vector pos = GetEntityLosEnd(ent);
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

            if (losUsed >= losBudget)
                break;

            param.Start = origin;
            param.End = pos;
            param.TraceEnt = null;
            float hitFraction = world.TraceMove(param, null);
            losUsed = losUsed + 1;
            bool losClear = IsLineOfSightClear(hitFraction, param.TraceEnt, ent);
            if (!losClear && !m_Settings.m_EnableNlosMultipath)
                continue;

            RDF_RadarTarget t = new RDF_RadarTarget();
            t.m_Entity = ent;
            t.m_Position = pos;
            t.m_Distance = dist;
            t.m_Velocity = GetEntityVelocity(ent);
            if (isProjectile)
                t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE;
            else
                t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_VEHICLE;
            t.m_Time = worldTime;
            t.m_LosBlocked = !losClear;
            t.m_LosHitFraction = hitFraction;
            t.m_MultipathFactor = 1.0;
            t.m_IsAnonymous = false;
            t.m_IsFalsePlot = false;
            ProcessPhysicalDetection(t, origin, forward, worldTime, world);
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

            if (losUsed >= losBudget)
                break;

            param.Start = origin;
            param.End = et.m_Position;
            param.TraceEnt = null;
            float hitFractionE = world.TraceMove(param, null);
            losUsed = losUsed + 1;
            bool losClearE = IsLineOfSightClear(hitFractionE, param.TraceEnt, et.m_Entity);
            if (!losClearE && !m_Settings.m_EnableNlosMultipath)
                continue;

            et.m_Velocity = GetEntityVelocity(et.m_Entity);
            et.m_LosBlocked = !losClearE;
            et.m_LosHitFraction = hitFractionE;
            et.m_MultipathFactor = 1.0;
            et.m_IsAnonymous = false;
            et.m_IsFalsePlot = false;
            ProcessPhysicalDetection(et, origin, forward, worldTime, world);
            if (et.m_Detected || m_Settings.m_KeepUndetected)
            {
                if (!ContainsTargetEntity(outTargets, et.m_Entity))
                    outTargets.Insert(et);
            }
        }
    }

    protected void SynthesizeAllMeasurements(
        array<ref RDF_RadarTarget> targets,
        vector origin)
    {
        if (!targets || !m_Settings || !m_Settings.m_EnableMeasurementSynthesis)
            return;
        if (!m_Settings.m_Hardware)
            return;

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
        outCandidates.Clear();
        if (!world)
            return;

        bool haveSphere = false;
        if (m_Settings.m_UseSphereQuery)
        {
            if (!m_QueryCandidates)
                m_QueryCandidates = new array<IEntity>();
            m_QueryCandidates.Clear();
            world.QueryEntitiesBySphere(
                origin,
                range,
                OnSphereCandidateEntity,
                null,
                EQueryEntitiesFlags.DYNAMIC);
            for (int i = 0; i < m_QueryCandidates.Count(); i++)
            {
                IEntity e = m_QueryCandidates.Get(i);
                if (!e || e == subject)
                    continue;
                outCandidates.Insert(e);
            }
            haveSphere = outCandidates.Count() > 0;
        }

        // Active-entity dump is expensive; use when sphere is off, or when the
        // DYNAMIC sphere misses editor-placed / static vehicles (empty result).
        bool needActive = false;
        if (!m_Settings.m_UseSphereQuery)
            needActive = true;
        else if (!haveSphere)
            needActive = true;
        else if (m_Settings.m_SphereQueryAlsoActive)
            needActive = true;

        if (!needActive)
            return;

        if (!m_CandidateScratch)
            m_CandidateScratch = new array<IEntity>();
        m_CandidateScratch.Clear();
        world.GetActiveEntities(m_CandidateScratch);
        for (int j = 0; j < m_CandidateScratch.Count(); j++)
        {
            IEntity ent = m_CandidateScratch.Get(j);
            if (!ent || ent == subject)
                continue;
            if (!RDF_RadarEntityClassifier.IsRadarCandidate(ent))
                continue;
            if (!ContainsEntity(outCandidates, ent))
                outCandidates.Insert(ent);
        }
    }

    protected bool OnSphereCandidateEntity(IEntity entity)
    {
        if (!entity)
            return false;
        if (!RDF_RadarEntityClassifier.IsRadarCandidate(entity))
            return true;
        if (!m_QueryCandidates)
            m_QueryCandidates = new array<IEntity>();
        m_QueryCandidates.Insert(entity);
        return true;
    }

    protected void SortEntitiesByDistance(notnull array<IEntity> entities, vector origin)
    {
        int n = entities.Count();
        if (n <= 1)
            return;

        // Insertion sort: candidate counts after classify are usually modest.
        for (int i = 1; i < n; i++)
        {
            IEntity key = entities.Get(i);
            if (!key)
                continue;
            float keyDistSq = vector.DistanceSq(key.GetOrigin(), origin);
            int j = i - 1;
            while (j >= 0)
            {
                IEntity cur = entities.Get(j);
                float curDistSq = 0.0;
                if (cur)
                    curDistSq = vector.DistanceSq(cur.GetOrigin(), origin);
                if (curDistSq <= keyDistSq)
                    break;
                entities.Set(j + 1, cur);
                j = j - 1;
            }
            entities.Set(j + 1, key);
        }
    }

    protected bool ContainsEntity(
        notnull array<IEntity> entities,
        IEntity entity)
    {
        for (int i = 0; i < entities.Count(); i++)
        {
            if (entities.Get(i) == entity)
                return true;
        }
        return false;
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
        if (!m_Settings || !m_Settings.m_EnableCfarGate)
            return;
        if (outTargets.Count() <= 0)
            return;

        int azBinCount = 24;
        int rangeBinCount = m_Settings.m_RangeBinCount;
        if (rangeBinCount < 8)
            rangeBinCount = 8;

        EnsureCfarGrid(azBinCount, rangeBinCount);

        float rangeM = Math.Max(1.0, m_Settings.m_Range);
        float scanAzimuth = Math.Atan2(scanForward[2], scanForward[0]);
        array<int> targetAzIndices = new array<int>();
        array<int> targetRangeIndices = new array<int>();
        targetAzIndices.Reserve(outTargets.Count());
        targetRangeIndices.Reserve(outTargets.Count());

        for (int i = 0; i < outTargets.Count(); i++)
        {
            RDF_RadarTarget t = outTargets.Get(i);
            int azIndex = -1;
            int rangeIndex = -1;
            if (t)
                GetCfarCellIndices(t, origin, scanAzimuth, halfAngleRad, rangeM, azBinCount, rangeBinCount, azIndex, rangeIndex);
            targetAzIndices.Insert(azIndex);
            targetRangeIndices.Insert(rangeIndex);
            if (!t)
                continue;
            if (azIndex < 0 || rangeIndex < 0)
                continue;
            float power = GetTargetCfarPowerW(t);
            if (power <= 0.0)
                continue;
            float prev = m_CfarPowerGrid.Get(azIndex).Get(rangeIndex);
            m_CfarPowerGrid.Get(azIndex).Set(rangeIndex, prev + power);
        }

        float noiseFloor = EstimateCfarNoiseFloorW(origin, scanForward);
        int flatCount = azBinCount * rangeBinCount;
        if (!m_CfarRowHits)
            m_CfarRowHits = new array<bool>();
        while (m_CfarRowHits.Count() < flatCount)
            m_CfarRowHits.Insert(false);

        for (int azHit = 0; azHit < azBinCount; azHit++)
        {
            array<float> powerRow = m_CfarPowerGrid.Get(azHit);
            for (int rb = 0; rb < rangeBinCount; rb++)
            {
                bool hit = RDF_RadarCfarGate.CellDetected(
                    powerRow,
                    rb,
                    noiseFloor,
                    m_Settings.m_CfarGuardCells,
                    m_Settings.m_CfarTrainingCells,
                    m_Settings.m_CfarPfa);
                m_CfarRowHits.Set(azHit * rangeBinCount + rb, hit);
            }
        }

        for (int k = 0; k < outTargets.Count(); k++)
        {
            RDF_RadarTarget target = outTargets.Get(k);
            if (!target)
                continue;
            int azK = targetAzIndices.Get(k);
            int rangeK = targetRangeIndices.Get(k);
            if (azK < 0 || rangeK < 0)
            {
                target.m_Detected = false;
                continue;
            }
            target.m_Detected = m_CfarRowHits.Get(azK * rangeBinCount + rangeK);
        }
    }

    protected void EnsureCfarGrid(int azBins, int rangeBins)
    {
        if (!m_CfarPowerGrid)
            m_CfarPowerGrid = new array<ref array<float>>();

        if (m_CfarCachedAzBins != azBins || m_CfarCachedRangeBins != rangeBins)
        {
            m_CfarPowerGrid.Clear();
            for (int az = 0; az < azBins; az++)
            {
                array<float> row = new array<float>();
                row.Reserve(rangeBins);
                for (int rb = 0; rb < rangeBins; rb++)
                    row.Insert(0.0);
                m_CfarPowerGrid.Insert(row);
            }
            m_CfarCachedAzBins = azBins;
            m_CfarCachedRangeBins = rangeBins;
            return;
        }

        for (int azClear = 0; azClear < azBins; azClear++)
        {
            array<float> clearRow = m_CfarPowerGrid.Get(azClear);
            for (int rbClear = 0; rbClear < rangeBins; rbClear++)
                clearRow.Set(rbClear, 0.0);
        }
    }

    protected void GetCfarCellIndices(
        RDF_RadarTarget target,
        vector origin,
        float scanAzimuth,
        float halfAngleRad,
        float maxRange,
        int azBins,
        int rangeBins,
        out int outAzIndex,
        out int outRangeIndex)
    {
        outAzIndex = -1;
        outRangeIndex = -1;
        if (!target)
            return;
        vector toTarget = target.m_Position - origin;
        float dist = toTarget.Length();
        if (dist <= 0.001)
            return;
        if (dist > maxRange)
            dist = maxRange;

        float targetAzimuth = Math.Atan2(toTarget[2], toTarget[0]);
        float rel = NormalizeAngleRad(targetAzimuth - scanAzimuth);
        if (halfAngleRad > 0.0001)
        {
            if (rel < -halfAngleRad || rel > halfAngleRad)
                return;
        }

        float azNorm = 0.5;
        if (halfAngleRad > 0.0001)
            azNorm = (rel + halfAngleRad) / (2.0 * halfAngleRad);
        azNorm = Math.Clamp(azNorm, 0.0, 0.999999);
        float rangeNorm = dist / Math.Max(1.0, maxRange);
        rangeNorm = Math.Clamp(rangeNorm, 0.0, 0.999999);

        outAzIndex = Math.Floor(azNorm * azBins);
        outRangeIndex = Math.Floor(rangeNorm * rangeBins);
        if (outAzIndex < 0)
            outAzIndex = 0;
        if (outAzIndex >= azBins)
            outAzIndex = azBins - 1;
        if (outRangeIndex < 0)
            outRangeIndex = 0;
        if (outRangeIndex >= rangeBins)
            outRangeIndex = rangeBins - 1;
    }

    protected float GetTargetCfarPowerW(RDF_RadarTarget target)
    {
        if (!target)
            return 0.0;
        if (target.m_CfarPowerW > 0.0)
            return target.m_CfarPowerW;
        if (target.m_ProcessedPowerW > 0.0)
            return target.m_ProcessedPowerW;
        if (target.m_ReceivedPowerW > 0.0)
            return target.m_ReceivedPowerW;
        return 0.0;
    }

    protected float EstimateCfarNoiseFloorW(
        vector origin,
        vector scanForward)
    {
        if (!m_Settings || !m_Settings.m_Hardware)
            return 0.000000000000000000000000000001;

        RDF_RadarHardware hardware = m_Settings.m_Hardware;
        float processingGain = hardware.GetProcessingGain();
        float noise = hardware.GetNoisePowerW() * processingGain;
        noise = noise + m_Settings.m_AdditionalNoisePowerW;
        if (m_Settings.m_EwStack)
        {
            float ewNoiseRf = m_Settings.m_EwStack.GetAdditionalNoisePowerW(
                origin,
                scanForward,
                hardware);
            noise = noise + ewNoiseRf * processingGain;
        }
        if (noise < 0.000000000000000000000000000001)
            noise = 0.000000000000000000000000000001;
        return noise;
    }

    protected RDF_RadarTarget BuildAnonymousTarget(
        vector origin,
        float scanAzimuth,
        float halfAngleRad,
        float maxRange,
        int azBins,
        int rangeBins,
        int azIndex,
        int rangeIndex,
        float cellPowerW,
        float noiseFloorW)
    {
        float azCenterNorm = (azIndex + 0.5) / azBins;
        float rangeCenterNorm = (rangeIndex + 0.5) / rangeBins;
        float relAz = 0.0;
        if (halfAngleRad > 0.0001)
            relAz = (azCenterNorm * 2.0 - 1.0) * halfAngleRad;
        float azimuth = scanAzimuth + relAz;
        float distance = rangeCenterNorm * maxRange;
        if (distance < m_Settings.m_MinDistance)
            distance = m_Settings.m_MinDistance;
        if (distance > maxRange)
            distance = maxRange;

        vector dir = Vector(Math.Cos(azimuth), 0.0, Math.Sin(azimuth));
        RDF_RadarTarget t = new RDF_RadarTarget();
        t.m_Entity = null;
        t.m_Position = origin + dir * distance;
        t.m_Distance = distance;
        t.m_Velocity = "0 0 0";
        t.m_Type = ERDF_RadarTargetType.RDF_RADAR_TARGET_ANONYMOUS;
        t.m_Time = 0.0;
        t.m_AzimuthDeg = azimuth * RAD_TO_DEG;
        t.m_ElevationDeg = 0.0;
        t.m_RadialSpeedMs = 0.0;
        t.m_RcsM2 = 0.0;
        t.m_ReceivedPowerW = cellPowerW;
        t.m_ProcessedPowerW = cellPowerW;
        t.m_DopplerHz = 0.0;
        t.m_MtiGain = 1.0;
        t.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        t.m_DemSampleValid = false;
        t.m_ClutterPowerW = 0.0;
        t.m_ClutterToNoiseDb = -300.0;
        float snrLinear = cellPowerW / Math.Max(
            0.000000000000000000000000000001,
            noiseFloorW);
        t.m_SnrDb = RDF_RadarClutterModel.LinToDb(snrLinear);
        t.m_Detected = true;
        t.m_IsAnonymous = true;
        t.m_IsFalsePlot = false;
        t.m_CfarPowerW = cellPowerW;
        t.m_LosBlocked = false;
        t.m_LosHitFraction = 1.0;
        t.m_MultipathFactor = 1.0;
        t.m_BeamName = "cfar";
        t.m_ScanNumber = 0;
        return t;
    }

    protected void RemoveUndetectedTargets(notnull array<ref RDF_RadarTarget> targets)
    {
        if (m_Settings && m_Settings.m_KeepUndetected)
            return;
        for (int i = targets.Count() - 1; i >= 0; i--)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
            {
                targets.Remove(i);
                continue;
            }
            if (!t.m_Detected)
                targets.Remove(i);
        }
    }

    protected static bool IsLineOfSightClear(float hitFraction, IEntity traceEnt, IEntity target)
    {
        // Reached the end point with no earlier blocker.
        if (hitFraction >= 0.999)
            return true;
        if (!traceEnt || !target)
            return false;
        // Hit the target root, or any child collider under it (common for vehicles).
        if (IsEntityOrChildOf(traceEnt, target))
            return true;
        return false;
    }

    // True when hitEntity is target, or a descendant of target in the entity tree.
    protected static bool IsEntityOrChildOf(IEntity hitEntity, IEntity target)
    {
        if (!hitEntity || !target)
            return false;
        IEntity cur = hitEntity;
        int guard = 0;
        while (cur && guard < 16)
        {
            if (cur == target)
                return true;
            cur = cur.GetParent();
            guard = guard + 1;
        }
        return false;
    }

    // Prefer geometric center for Trace / range. Chassis origins often sit on
    // the ground plane and lose LOS to terrain before the vehicle body.
    protected static vector GetScattererLosEnd(RDF_RadarScatterer entry)
    {
        if (!entry)
            return "0 0 0";
        if (!entry.m_Entity)
            return entry.m_Position;

        vector worldMat[4];
        entry.m_Entity.GetWorldTransform(worldMat);
        vector mins;
        vector maxs;
        entry.m_Entity.GetBounds(mins, maxs);
        vector centerLocal = (mins + maxs) * 0.5;
        vector size = maxs - mins;
        float extent = Math.AbsFloat(size[0]) + Math.AbsFloat(size[1]) + Math.AbsFloat(size[2]);
        if (extent < 0.05)
        {
            // Degenerate bounds: lift a little above origin.
            vector lifted = entry.m_Position;
            lifted[1] = lifted[1] + 1.2;
            return lifted;
        }

        return worldMat[3]
            + (worldMat[0] * centerLocal[0])
            + (worldMat[1] * centerLocal[1])
            + (worldMat[2] * centerLocal[2]);
    }

    protected static vector GetEntityPosition(IEntity entity)
    {
        if (!entity)
            return "0 0 0";
        vector mat[4];
        entity.GetWorldTransform(mat);
        return mat[3];
    }

    protected static vector GetEntityLosEnd(IEntity entity)
    {
        if (!entity)
            return "0 0 0";
        vector worldMat[4];
        entity.GetWorldTransform(worldMat);
        vector mins;
        vector maxs;
        entity.GetBounds(mins, maxs);
        vector centerLocal = (mins + maxs) * 0.5;
        vector size = maxs - mins;
        float extent = Math.AbsFloat(size[0]) + Math.AbsFloat(size[1]) + Math.AbsFloat(size[2]);
        if (extent < 0.05)
        {
            vector lifted = worldMat[3];
            lifted[1] = lifted[1] + 1.2;
            return lifted;
        }
        return worldMat[3]
            + (worldMat[0] * centerLocal[0])
            + (worldMat[1] * centerLocal[1])
            + (worldMat[2] * centerLocal[2]);
    }

    protected static vector GetEntityVelocity(IEntity entity)
    {
        if (!entity)
            return "0 0 0";
        GenericEntity generic = GenericEntity.Cast(entity);
        if (generic)
        {
            ProjectileMoveComponent pm = ProjectileMoveComponent.Cast(generic.FindComponent(ProjectileMoveComponent));
            if (pm)
                return pm.GetVelocity();
        }
        Physics physics = entity.GetPhysics();
        if (physics)
            return physics.GetVelocity();
        return "0 0 0";
    }

    protected void ProcessPhysicalDetection(
        RDF_RadarTarget target,
        vector origin,
        vector scanForward,
        float worldTime,
        BaseWorld world)
    {
        if (!target)
            return;

        target.m_Detected = true;
        target.m_BeamName = "geometry";
        if (!target.m_DemSampleValid)
        {
            target.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
            target.m_DemSampleValid = false;
        }
        target.m_ClutterPowerW = 0.0;
        target.m_ClutterToNoiseDb = -300.0;
        target.m_CfarPowerW = 0.0;
        if (target.m_LosHitFraction <= 0.0 && !target.m_LosBlocked)
            target.m_LosHitFraction = 1.0;
        if (target.m_MultipathFactor <= 0.0)
            target.m_MultipathFactor = 1.0;

        vector toTarget = target.m_Position - origin;
        float distance = toTarget.Length();
        if (distance <= 0.001)
        {
            target.m_Detected = false;
            target.m_MultipathFactor = 0.0;
            return;
        }

        vector los = toTarget / distance;
        float horizontalRange = Math.Sqrt(
            toTarget[0] * toTarget[0] + toTarget[2] * toTarget[2]);
        float elevationRad = Math.Atan2(toTarget[1], Math.Max(0.001, horizontalRange));
        float elevationDeg = elevationRad * RAD_TO_DEG;

        float scanAzimuth = Math.Atan2(scanForward[2], scanForward[0]);
        float targetAzimuth = Math.Atan2(los[2], los[0]);
        float azimuthOffsetRad = NormalizeAngleRad(targetAzimuth - scanAzimuth);
        float azimuthOffsetDeg = azimuthOffsetRad * RAD_TO_DEG;

        target.m_AzimuthDeg = targetAzimuth * RAD_TO_DEG;
        target.m_ElevationDeg = elevationDeg;
        target.m_RadialSpeedMs = -(
            target.m_Velocity[0] * los[0]
            + target.m_Velocity[1] * los[1]
            + target.m_Velocity[2] * los[2]);

        // Geometry-only mode: keep Detected=true after polar fill. Do not apply
        // NLOS / SNR / CFAR rejects (used by lock-layer and other regressions).
        RDF_RadarHardware hardware = m_Settings.m_Hardware;
        if (!m_Settings.m_EnablePhysicalDetection || !hardware)
        {
            if (target.m_LosBlocked)
                target.m_MultipathFactor = 0.25;
            else
                target.m_MultipathFactor = 1.0;
            return;
        }

        if (target.m_LosBlocked)
        {
            target.m_MultipathFactor = ComputeNlosMultipathFactor(
                origin,
                target.m_Position,
                distance,
                target.m_LosHitFraction,
                world,
                target.m_AglM,
                target.m_DemSampleValid,
                target.m_DemTerrainY);
            if (target.m_MultipathFactor <= 0.0)
            {
                target.m_Detected = false;
                return;
            }
        }
        else
        {
            target.m_MultipathFactor = 1.0;
        }

        string beamName;
        float patternGain = RDF_RadarClutterModel.GetStrongestBeamGain(
            hardware,
            azimuthOffsetDeg,
            elevationDeg,
            beamName);
        target.m_BeamName = beamName;
        if (target.m_LosBlocked)
            target.m_BeamName = beamName + "/nlos";

        // Registry entries carry a cached RCS; only estimate when absent.
        if (target.m_RcsM2 <= 0.0)
        {
            target.m_RcsM2 = RDF_RadarRcsModel.GetEntityRcsM2(
                target.m_Entity,
                target.m_Type);
        }
        target.m_ReceivedPowerW = RDF_RadarClutterModel.ReceivedPowerW(
            hardware.m_PeakPowerW,
            hardware.m_AntennaGainDbi,
            hardware.m_FrequencyHz,
            hardware.m_SystemLossDb,
            target.m_RcsM2,
            distance,
            patternGain);
        target.m_ReceivedPowerW = target.m_ReceivedPowerW * target.m_MultipathFactor;

        float wavelength = hardware.GetWavelengthM();
        target.m_DopplerHz = RDF_RadarClutterModel.DopplerHz(
            target.m_RadialSpeedMs,
            wavelength);
        target.m_MtiGain = 1.0;
        if (hardware.m_EnableMti)
        {
            target.m_MtiGain = RDF_RadarClutterModel.MtiTwoPulseGain(
                target.m_DopplerHz,
                hardware.m_PrfHz);
            if (target.m_MtiGain < 0.000001)
                target.m_MtiGain = 0.000001;
        }

        float processingGain = hardware.GetProcessingGain();
        target.m_ProcessedPowerW =
            target.m_ReceivedPowerW * processingGain * target.m_MtiGain;
        target.m_CfarPowerW = target.m_ProcessedPowerW;

        float clutterPower = 0.0;
        if (m_Settings.m_EnableDemClutter && m_DemCache)
        {
            clutterPower = ComputeDemClutterPower(
                target,
                origin,
                distance,
                azimuthOffsetDeg,
                elevationDeg,
                patternGain,
                hardware,
                processingGain);
        }
        target.m_ClutterPowerW = clutterPower;

        float noisePower = hardware.GetNoisePowerW() * processingGain;
        noisePower = noisePower + m_Settings.m_AdditionalNoisePowerW;
        noisePower = noisePower + clutterPower;
        if (m_Settings.m_EwStack)
        {
            float ewNoiseRf = m_Settings.m_EwStack.GetAdditionalNoisePowerW(
                origin,
                scanForward,
                hardware);
            noisePower = noisePower + ewNoiseRf * processingGain;
        }
        float snrLinear = target.m_ProcessedPowerW / Math.Max(
            0.000000000000000000000000000001,
            noisePower);
        target.m_SnrDb = RDF_RadarClutterModel.LinToDb(snrLinear);
        if (clutterPower > 0.0)
        {
            float clutterFraction = clutterPower / Math.Max(
                0.000000000000000000000000000001,
                noisePower);
            target.m_ClutterToNoiseDb = RDF_RadarClutterModel.LinToDb(clutterFraction);
        }
        target.m_Detected = target.m_SnrDb >= m_Settings.m_DetectionSnrDb;

        float scanPeriod = hardware.GetScanPeriodS();
        target.m_ScanNumber = 0;
        if (scanPeriod < 1000000.0)
            target.m_ScanNumber = Math.Floor(worldTime / scanPeriod);
    }

    protected float ComputeDemClutterPower(
        RDF_RadarTarget target,
        vector origin,
        float distance,
        float azimuthOffsetDeg,
        float elevationDeg,
        float patternGain,
        RDF_RadarHardware hardware,
        float processingGain)
    {
        if (!target || !hardware || !m_DemCache)
            return 0.0;
        if (distance <= 0.001)
            return 0.0;
        if (m_Settings.m_DemClutterScale <= 0.0)
            return 0.0;

        float terrainY = 0.0;
        int surfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        if (target.m_DemSampleValid)
        {
            terrainY = target.m_DemTerrainY;
            surfaceClass = target.m_DemSurfaceClass;
        }
        else
        {
            RDF_DemRuntimeCellSample demSample;
            if (!m_DemCache.TrySampleAt(target.m_Position[0], target.m_Position[2], demSample))
                return 0.0;
            if (!demSample || !demSample.m_Valid)
                return 0.0;
            terrainY = demSample.m_TerrainY;
            surfaceClass = demSample.m_SurfaceClass;
            target.m_DemSampleValid = true;
            target.m_DemSurfaceClass = surfaceClass;
            target.m_DemTerrainY = terrainY;
        }

        float radarAboveGround = origin[1] - terrainY;
        if (radarAboveGround < 0.0)
            radarAboveGround = -radarAboveGround;
        float grazingRad = Math.Atan2(radarAboveGround, Math.Max(1.0, distance));

        float sigma0 = RDF_RadarClutterModel.GetSigma0(
            surfaceClass,
            grazingRad);
        sigma0 = sigma0 * m_Settings.m_DemClutterScale;
        if (sigma0 <= 0.0)
            return 0.0;

        float cellSizeM = m_DemCache.GetCellSizeM();

        float azWidthM = distance * (hardware.m_AzimuthBeamwidthDeg * DEG_TO_RAD);
        azWidthM = Math.AbsFloat(azWidthM);
        if (azWidthM < cellSizeM)
            azWidthM = cellSizeM;

        float rangeResolutionM = EstimateRangeResolutionM(hardware);
        float areaM2 = rangeResolutionM * azWidthM;
        float minArea = cellSizeM * cellSizeM;
        if (areaM2 < minArea)
            areaM2 = minArea;

        float clutterSigmaM2 = sigma0 * areaM2;
        float receivedClutter = RDF_RadarClutterModel.ReceivedPowerW(
            hardware.m_PeakPowerW,
            hardware.m_AntennaGainDbi,
            hardware.m_FrequencyHz,
            hardware.m_SystemLossDb,
            clutterSigmaM2,
            distance,
            patternGain);
        if (receivedClutter <= 0.0)
            return 0.0;

        float clutterMti = 1.0;
        if (hardware.m_EnableMti)
            clutterMti = hardware.m_MtiClutterFloor;
        if (clutterMti < 0.000001)
            clutterMti = 0.000001;

        return receivedClutter * processingGain * clutterMti;
    }

    // NLOS-only bounce scale using image-method path length and |Gamma|^2.
    // Early Trace blockers further weaken the return (hitFraction small).
    protected float ComputeNlosMultipathFactor(
        vector origin,
        vector targetPos,
        float distance,
        float hitFraction,
        BaseWorld world,
        float targetAglM,
        bool haveTargetTerrain,
        float targetTerrainYCached)
    {
        if (!m_Settings || !m_Settings.m_EnableNlosMultipath)
            return 0.0;
        if (distance < 1.0)
            return 0.0;

        float originTerrainY = SampleTerrainY(origin[0], origin[2], world);
        float targetTerrainY = targetTerrainYCached;
        if (!haveTargetTerrain)
            targetTerrainY = SampleTerrainY(targetPos[0], targetPos[2], world);

        float hr = origin[1] - originTerrainY;
        float ht = targetPos[1] - targetTerrainY;
        if (targetAglM >= 0.0)
            ht = targetAglM;
        if (hr < 0.5)
            hr = 0.5;
        if (ht < 0.5)
            ht = 0.5;
        if (ht > m_Settings.m_NlosMaxTargetAglM)
            return 0.0;

        float sumH = hr + ht;
        float bounceRange = Math.Sqrt(distance * distance + sumH * sumH);
        if (bounceRange < 1.0)
            return 0.0;

        float ratio = distance / bounceRange;
        float geom = ratio * ratio * ratio * ratio;
        float gamma = m_Settings.m_NlosReflectionAbs;
        float factor = gamma * gamma * geom;

        float depthScale = hitFraction;
        if (depthScale < 0.2)
            depthScale = 0.2;
        if (depthScale > 1.0)
            depthScale = 1.0;
        factor = factor * depthScale;

        if (factor < m_Settings.m_NlosMinFactor)
            return 0.0;
        if (factor > 1.0)
            factor = 1.0;
        return factor;
    }

    protected float SampleTerrainY(float worldX, float worldZ, BaseWorld world)
    {
        RDF_DemRuntimeCellSample demSample;
        if (m_DemCache && m_Settings && m_Settings.m_EnableDemClutter)
        {
            if (m_DemCache.TrySampleAt(worldX, worldZ, demSample))
            {
                if (demSample && demSample.m_Valid)
                    return demSample.m_TerrainY;
            }
        }

        if (world)
            return world.GetSurfaceY(worldX, worldZ);
        return 0.0;
    }

    protected float EstimateRangeResolutionM(RDF_RadarHardware hardware)
    {
        if (!hardware)
            return 1.0;

        float byBandwidth = 0.0;
        if (hardware.m_BandwidthHz > 0.0)
            byBandwidth = LIGHT_SPEED / (2.0 * hardware.m_BandwidthHz);

        float byPulseWidth = 0.0;
        if (hardware.m_PulseWidthS > 0.0)
            byPulseWidth = LIGHT_SPEED * hardware.m_PulseWidthS * 0.5;

        if (byBandwidth > 0.0)
            return Math.Max(0.5, byBandwidth);
        if (byPulseWidth > 0.0)
            return Math.Max(0.5, byPulseWidth);
        return 1.0;
    }

    protected float NormalizeAngleRad(float angle)
    {
        while (angle > Math.PI)
            angle = angle - Math.PI * 2.0;
        while (angle < -Math.PI)
            angle = angle + Math.PI * 2.0;
        return angle;
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
