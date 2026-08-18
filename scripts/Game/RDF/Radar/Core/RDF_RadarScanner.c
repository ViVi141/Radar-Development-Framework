// Radar scanner: scatterers feed the physics chain; published plots are measurements.
// Candidates come from the global scatterer registry; Trace LOS + optional NLOS;
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
    protected ref RDF_RadarCfarProcessor m_CfarProcessor;
    protected ref RDF_RadarLosCache m_LosCache;
    protected ref RDF_RadarScanReuseCache m_ScanReuseCache;
    protected ref RDF_RadarClutterMap m_ClutterMap;
    protected ref RDF_RadarCoarseRdMap m_CoarseRdMap;
    // Reused TraceParam / ExcludeArray (SCR_PlacedCommandInfoDisplay / nametag style).
    // Never write owned ColliderName / TraceMaterial in the hot loop.
    protected ref TraceParam m_TraceParam;
    protected ref array<IEntity> m_LosExclude;
    protected bool m_SettingsValidated;
    protected int m_StatReuseHits;
    protected int m_StatFreshUpdates;
    protected int m_StatBudgetSkips;
    protected int m_StatLosCacheHits;
    protected int m_StatTraceMoves;
    protected int m_StatDemLosBlocks;
    protected int m_StatMtdRotorAided;
    protected int m_StatMtdDetected;
    protected int m_LastMtdWinBin;
    protected int m_LastMtdPrfIndex;
    // Last-dwell EW noise observability (RF domain, before processing gain).
    protected float m_LastEwNoiseRfW;
    protected float m_LastThermalNoiseW;
    protected float m_LastJnDb;
    protected float m_LastBurnThroughRangeM;
    // Registry pass fair cursor; carry budget cutoff to next scan.
    protected int m_RegistryScanCursor;
    // Resolved once per Scan() when atmospheric loss is enabled.
    protected float m_ScanRainLossDbPerKm;
    // Reused pass context (avoid per-scan allocation).
    protected ref RDF_RadarScanPassContext m_PassCtx;
    // Cross-frame LOS queue: scatterers whose fresh LOS pass was deferred when
    // the per-tick TraceMove budget (m_LosTracesPerTick) was exhausted. Drained
    // at the start of the next Scan() so starved candidates get priority before
    // the normal sweep consumes the budget.
    protected ref array<ref RDF_RadarScatterer> m_LosQueue;
    protected int m_StatLosQueued;
    protected int m_StatLosQueueDrained;
    // Queue-drain scratch: candidates popped from m_LosQueue for this tick's
    // deferred LOS slice (passed to ScanFromRegistry as the candidate source).
    protected ref array<ref RDF_RadarScatterer> m_QueueCandidates;
    // Scatterer ids already published this Scan() — dedups the same entity when
    // the queue drain and the sweep both touch it in one tick.
    protected ref array<int> m_PublishedScattererIds;
    // Reused truth buffer: ScanFromRegistry processes candidates serially, and
    // every consumer of RDF_RadarTruthSample (StoreResult deep-copies fields,
    // PublishFromTruth deep-copies into a new RDF_RadarTarget, PhysicalDetect /
    // ApplyPhysicalReuse / DepositCoarseRd / NoteMtdPlot are synchronous) drops
    // the reference before the next candidate, so one scratch instance is safe.
    protected ref RDF_RadarTruthSample m_TruthScratch;
    // Forward-side truth bypass for AutoTest / ARM (scattererId -> entity).
    protected ref map<int, IEntity> m_DebugTruthByScattererId;
    // Scatterer ids the dwell scheduler forces to a full update this scan.
    protected ref array<int> m_DwellScattererIds;
    // Deception false-plot count of the last scan (ECCM decision observable).
    protected int m_LastFalsePlotCount;

    void RDF_RadarScanner(RDF_RadarSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_RadarSettings();

        m_DemCache = new RDF_DemRuntimeCache();
        m_ScattererCandidates = new array<ref RDF_RadarScatterer>();
        m_CfarProcessor = new RDF_RadarCfarProcessor();
        m_LosCache = new RDF_RadarLosCache();
        m_ScanReuseCache = new RDF_RadarScanReuseCache();
        m_ClutterMap = new RDF_RadarClutterMap();
        m_CoarseRdMap = new RDF_RadarCoarseRdMap();
        m_TraceParam = new TraceParam();
        m_LosExclude = new array<IEntity>();
        m_PassCtx = new RDF_RadarScanPassContext();
        m_LosQueue = new array<ref RDF_RadarScatterer>();
        m_StatLosQueued = 0;
        m_StatLosQueueDrained = 0;
        m_QueueCandidates = new array<ref RDF_RadarScatterer>();
        m_PublishedScattererIds = new array<int>();
        m_TruthScratch = new RDF_RadarTruthSample();
        m_DebugTruthByScattererId = new map<int, IEntity>();
        m_DwellScattererIds = new array<int>();
        m_LastFalsePlotCount = 0;
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

    // Drop LOS / scan reuse so the next Scan() re-runs DEM precheck + TraceMove.
    void ClearScanOptimizationCaches()
    {
        if (m_LosCache)
            m_LosCache.Clear();
        if (m_ScanReuseCache)
            m_ScanReuseCache.Clear();
        // Flush variants must also drop deferred LOS work so the next scan
        // re-runs every candidate from scratch.
        if (m_LosQueue)
            m_LosQueue.Clear();
        if (m_QueueCandidates)
            m_QueueCandidates.Clear();
    }

    int GetLastDemLosBlocks()
    {
        return m_StatDemLosBlocks;
    }

    int GetLastTraceMoves()
    {
        return m_StatTraceMoves;
    }

    int GetLastLosCacheHits()
    {
        return m_StatLosCacheHits;
    }

    int GetLastReuseHits()
    {
        return m_StatReuseHits;
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
            + " losHit=" + m_StatLosCacheHits.ToString()
            + " demBlk=" + m_StatDemLosBlocks.ToString()
            + " trace=" + m_StatTraceMoves.ToString()
            + " losQ=" + m_StatLosQueued.ToString()
            + "/" + m_StatLosQueueDrained.ToString();
    }

    string GetMtdStatsShort()
    {
        if (!m_Settings || !m_Settings.m_Hardware)
            return "mtd=off";
        if (m_Settings.m_Hardware.m_MtiMode != ERDF_MtiMode.RDF_MTI_MTD_BANK)
        {
            if (m_Settings.m_Hardware.m_MtiMode == ERDF_MtiMode.RDF_MTI_THREE_PULSE)
                return "mtd=three_pulse";
            return "mtd=twopulse";
        }

        string leak = m_Settings.m_Hardware.m_MtdClutterLeakage.ToString();
        string calib = "0";
        if (m_Settings.m_Hardware.m_HwCalibApplied)
            calib = "1";
        return "mtd bin=" + m_LastMtdWinBin.ToString()
            + " prf=" + m_LastMtdPrfIndex.ToString()
            + " rotor=" + m_StatMtdRotorAided.ToString()
            + "/" + m_StatMtdDetected.ToString()
            + " leak=" + leak
            + " calib=" + calib;
    }

    protected void NoteMtdPlot(RDF_RadarTruthSample target)
    {
        if (!target || !target.m_Detected)
            return;
        if (target.m_DopplerBin < 0)
            return;

        m_StatMtdDetected = m_StatMtdDetected + 1;
        m_LastMtdWinBin = target.m_DopplerBin;
        m_LastMtdPrfIndex = target.m_PrfIndex;
        if (target.m_RotorSidebandUsed)
            m_StatMtdRotorAided = m_StatMtdRotorAided + 1;
    }

    IEntity GetDebugTruthEntity(int scattererId)
    {
        if (!m_DebugTruthByScattererId)
            return null;
        if (scattererId <= 0)
            return null;
        if (!m_DebugTruthByScattererId.Contains(scattererId))
            return null;
        return m_DebugTruthByScattererId.Get(scattererId);
    }

    void CopyDebugTruthMap(notnull map<int, IEntity> outMap)
    {
        outMap.Clear();
        if (!m_DebugTruthByScattererId)
            return;
        for (int i = 0; i < m_DebugTruthByScattererId.Count(); i++)
        {
            int key = m_DebugTruthByScattererId.GetKey(i);
            if (key <= 0)
                continue;
            if (!m_DebugTruthByScattererId.Contains(key))
                continue;
            IEntity ent = m_DebugTruthByScattererId.Get(key);
            if (ent)
                outMap.Set(key, ent);
        }
    }

    protected void RememberDebugTruth(RDF_RadarTruthSample truth)
    {
        if (!truth || !m_DebugTruthByScattererId)
            return;
        if (truth.m_ScattererId <= 0)
            return;
        if (!truth.m_Entity)
            return;
        m_DebugTruthByScattererId.Set(truth.m_ScattererId, truth.m_Entity);
    }

    protected void PublishTruthToPlots(
        notnull array<ref RDF_RadarTarget> outTargets,
        RDF_RadarTruthSample truth)
    {
        if (!truth)
            return;
        RememberDebugTruth(truth);
        if (!truth.m_Detected && !(m_Settings && m_Settings.m_KeepUndetected))
            return;
        // Dedup: the queue drain and the sweep may both touch the same scatterer
        // in one Scan(); publish each scatterer id once per scan.
        if (truth.m_ScattererId > 0)
        {
            if (m_PublishedScattererIds)
            {
                for (int i = 0; i < m_PublishedScattererIds.Count(); i++)
                {
                    if (m_PublishedScattererIds.Get(i) == truth.m_ScattererId)
                        return;
                }
            }
            m_PublishedScattererIds.Insert(truth.m_ScattererId);
        }
        RDF_RadarTarget plot = RDF_RadarMeasurement.PublishFromTruth(truth);
        if (!plot)
            return;
        // Optional debug cheat for PPI / legacy tests; inverse must ignore.
        if (m_Settings && m_Settings.m_KeepEntityTruth)
        {
            plot.m_Entity = truth.m_Entity;
            plot.m_Type = truth.m_Type;
            plot.m_IsAnonymous = false;
        }
        outTargets.Insert(plot);
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

    int GetLastFalsePlotCount()
    {
        return m_LastFalsePlotCount;
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

    // Dwell scheduler (Sensor-side) marks which scatterer ids must be fully
    // updated this scan (FIRE_CONTROL / TRACK dwells). SEARCH still uses the
    // fair cursor + reuse budget. Cleared each local scan by the Sensor.
    void SetDwellScattererIds(notnull array<int> ids)
    {
        m_DwellScattererIds.Clear();
        for (int i = 0; i < ids.Count(); i++)
            m_DwellScattererIds.Insert(ids.Get(i));
    }

    void ClearDwellScattererIds()
    {
        m_DwellScattererIds.Clear();
    }

    protected bool IsDwellScatterer(int scattererId)
    {
        if (!m_DwellScattererIds)
            return false;
        for (int i = 0; i < m_DwellScattererIds.Count(); i++)
        {
            if (m_DwellScattererIds.Get(i) == scattererId)
                return true;
        }
        return false;
    }

    void Scan(IEntity subject, array<ref RDF_RadarTarget> outTargets)
    {
        if (!subject || !m_Settings || !m_Settings.m_Enabled || !outTargets)
            return;

        outTargets.Clear();
        if (m_DebugTruthByScattererId)
            m_DebugTruthByScattererId.Clear();
        if (m_PublishedScattererIds)
            m_PublishedScattererIds.Clear();
        m_StatReuseHits = 0;
        m_StatFreshUpdates = 0;
        m_StatBudgetSkips = 0;
        m_StatLosCacheHits = 0;
        m_StatTraceMoves = 0;
        m_StatDemLosBlocks = 0;
        m_StatLosQueued = 0;
        m_StatLosQueueDrained = 0;
        m_StatMtdRotorAided = 0;
        m_StatMtdDetected = 0;
        m_LastMtdWinBin = -1;
        m_LastMtdPrfIndex = 0;
        m_ScanRainLossDbPerKm = 0.0;
        m_LastFalsePlotCount = 0;
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
            float rainFreqHz = 0.0;
            if (m_Settings.m_Hardware)
                rainFreqHz = m_Settings.m_Hardware.m_FrequencyHz;
            m_ScanRainLossDbPerKm = m_Settings.ResolveRainLossDbPerKm(weather, rainFreqHz);
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
            m_ClutterMap.ConfigureAsym(
                m_Settings.m_ClutterMapAzBinCount,
                m_Settings.m_RangeBinCount,
                m_Settings.m_ClutterMapAlpha,
                m_Settings.m_ClutterMapAlphaDown,
                range);
        }
        if (m_CoarseRdMap && m_Settings.m_EnableCoarseRd)
        {
            int dopplerBins = 16;
            if (m_Settings.m_Hardware)
                dopplerBins = m_Settings.m_Hardware.m_DopplerBinCount;
            m_CoarseRdMap.Configure(
                m_Settings.m_RangeBinCount,
                dopplerBins,
                range,
                m_Settings.m_RdMapAlpha,
                m_Settings.m_RdCellsPerScan);
            m_CoarseRdMap.AmortizeDecay(m_Settings.m_RdDecayPerScan);
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
            RDF_RadarScattererRegistry.SetDemCache(m_DemCache);
        }
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

        float minDist = m_Settings.GetEffectiveMinDistance();
        float minDistSq = minDist * minDist;
        float halfAngleRad = m_Settings.m_SectorHalfAngleDeg * 0.01745329;
        if (m_Settings.m_EnableMechanicalScan && m_Settings.m_Hardware)
            halfAngleRad = m_Settings.m_Hardware.m_AzimuthBeamwidthDeg * 0.5 * 0.01745329;
        int maxTargets = m_Settings.m_MaxTargets;

        // Sector gate is azimuth-only. Flatten pitch so a nose-up platform
        // does not shrink the horizontal acceptance cone.
        float scanAzimuthRad = 0.0;
        float fwdHorizX = forward[0];
        float fwdHorizZ = forward[2];
        float fwdHorizLen = Math.Sqrt(fwdHorizX * fwdHorizX + fwdHorizZ * fwdHorizZ);
        if (fwdHorizLen > 0.001)
            scanAzimuthRad = Math.Atan2(fwdHorizZ, fwdHorizX);
        else
            scanAzimuthRad = Math.Atan2(forward[2], forward[0]);

        EnsureLosTrace();
        m_LosExclude.Clear();
        RDF_RadarScanGeometry.ConfigureLosParam(m_TraceParam, m_LosExclude);
        int losBudget = ResolveLosBudgetPerTick();
        int freshBudget = ComputeFreshUpdateBudget();
        float rangeSq = range * range;

        if (!m_PassCtx)
            m_PassCtx = new RDF_RadarScanPassContext();
        RDF_RadarScanPassContext passCtx = m_PassCtx;
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
        passCtx.m_HalfAngleRad = halfAngleRad;
        passCtx.m_ScanAzimuthRad = scanAzimuthRad;
        passCtx.m_MaxTargets = maxTargets;
        passCtx.m_Param = m_TraceParam;
        passCtx.m_LosBudget = losBudget;
        passCtx.m_LosUsed = 0;
        passCtx.m_FreshBudget = freshBudget;

        if (m_LosCache)
            m_LosCache.MaybePrune(wallTime);
        if (m_ScanReuseCache)
            m_ScanReuseCache.MaybePrune(wallTime);

        // Cross-frame LOS smoothing: drain the deferred LOS queue first so
        // budget-starved candidates get their fresh pass before the sweep
        // consumes this tick's trace budget. Queue runs inside the same
        // per-tick budget (m_LosBudget), so combined trace cost stays bounded.
        DrainLosQueue(outTargets, passCtx);

        ScanFromRegistry(outTargets, passCtx);

        AddDeceptionFalsePlots(outTargets, origin, forward, worldTime, maxTargets);
        ApplyCfarGate(outTargets, origin, forward, halfAngleRad);
        SynthesizeAllMeasurements(outTargets, origin);
        RemoveUndetectedTargets(outTargets);
    }

    // Table-driven pass: entities are pre-classified scatterers / emitters.
    // candidates: optional queue slice for the deferred-LOS drain pass; when
    // non-null, the fair cursor and sphere collection are skipped (queue drain
    // runs inside the same per-tick LOS budget).
    protected void ScanFromRegistry(
        notnull array<ref RDF_RadarTarget> outTargets,
        notnull RDF_RadarScanPassContext ctx,
        array<ref RDF_RadarScatterer> candidates = null)
    {
        bool queueDrain = candidates != null;
        IEntity subject = ctx.m_Subject;
        BaseWorld world = ctx.m_World;
        vector origin = ctx.m_Origin;
        vector forward = ctx.m_Forward;
        float worldTime = ctx.m_WorldTime;
        float wallTime = ctx.m_WallTime;
        float minDistSq = ctx.m_MinDistSq;
        float rangeSq = ctx.m_RangeSq;
        float halfAngleRad = ctx.m_HalfAngleRad;
        float scanAzimuthRad = ctx.m_ScanAzimuthRad;
        int maxTargets = ctx.m_MaxTargets;
        TraceParam param = ctx.m_Param;
        int losBudget = ctx.m_LosBudget;
        int losUsed = ctx.m_LosUsed;
        int freshBudget = ctx.m_FreshBudget;

        if (!m_ScattererCandidates)
            m_ScattererCandidates = new array<ref RDF_RadarScatterer>();
        if (queueDrain)
            m_ScattererCandidates = candidates;
        else
            RDF_RadarScattererRegistry.CollectInSphere(origin, Math.Sqrt(rangeSq), m_ScattererCandidates);

        int candidateCount = m_ScattererCandidates.Count();
        if (candidateCount <= 0)
        {
            ctx.m_LosUsed = losUsed;
            ctx.m_FreshBudget = freshBudget;
            return;
        }

        int startIndex = 0;
        if (!queueDrain && m_Settings.m_FairScanCursor)
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
            // Sector / range gate uses registry position (cheap). Geometric LOS
            // end (GetBounds) is deferred until a full update is required.
            vector toTarget = pos - origin;
            float distSq = toTarget.LengthSq();
            if (distSq < minDistSq || distSq > rangeSq)
                continue;

            float dist = Math.Sqrt(distSq);
            vector toTargetNorm = toTarget;
            if (dist > 0.001)
                toTargetNorm = toTarget / dist;

            // Azimuth-only hard gate (search sector / dwell beam). Elevation is
            // owned by PhysicalDetect Gaussian beams — do not 3D-cone here.
            float horizontalRange = Math.Sqrt(
                toTarget[0] * toTarget[0] + toTarget[2] * toTarget[2]);
            if (horizontalRange < 0.001)
                continue;

            float targetAzimuthRad = Math.Atan2(toTarget[2], toTarget[0]);
            float azDiffRad = NormalizeAngleRad(targetAzimuthRad - scanAzimuthRad);
            if (azDiffRad < 0.0)
                azDiffRad = -azDiffRad;
            if (azDiffRad > halfAngleRad)
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
            // Dwell scheduler forces a full update for scheduled FIRE_CONTROL /
            // TRACK dwells regardless of reuse budget or priority band.
            if (IsDwellScatterer(entry.m_ScattererId))
            {
                runFullUpdate = true;
                highPriority = true;
            }
            if (!runFullUpdate || (!highPriority && freshBudget <= 0))
            {
                m_StatBudgetSkips = m_StatBudgetSkips + 1;
                TryInsertReusedScattererTarget(
                    outTargets,
                    entry,
                    pos,
                    dist,
                    priorityType,
                    worldTime,
                    wallTime);
                continue;
            }

            // Aim LOS at the geometric center so ground vehicles are not
            // blocked by terrain before their chassis origin (often on the ground).
            vector losEnd = RDF_RadarScanGeometry.GetScattererLosEnd(entry);
            toTarget = losEnd - origin;
            distSq = toTarget.LengthSq();
            if (distSq < minDistSq || distSq > rangeSq)
                continue;
            dist = Math.Sqrt(distSq);
            if (dist > 0.001)
                toTargetNorm = toTarget / dist;
            else
                toTargetNorm = toTarget;

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
                bool demBlocked = false;
                if (m_Settings.m_EnableDemLosPrecheck && m_DemCache)
                {
                    demBlocked = RDF_RadarScanGeometry.TryDemTerrainBlock(
                        origin,
                        losEnd,
                        m_DemCache,
                        m_Settings,
                        hitFraction);
                    if (demBlocked)
                    {
                        losClear = false;
                        m_StatDemLosBlocks = m_StatDemLosBlocks + 1;
                        if (m_LosCache)
                        {
                            m_LosCache.Store(
                                entry.m_Entity,
                                origin,
                                losEnd,
                                hitFraction,
                                false,
                                wallTime);
                        }
                    }
                }

                if (!demBlocked)
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
                        if (!queueDrain && m_Settings && m_Settings.m_EnableLosFrameQueue && m_LosQueue)
                        {
                            // Cross-frame smoothing: defer the rest of this scan's
                            // fresh-LOS candidates to the queue instead of dropping
                            // them; the next Scan() drains them first.
                            EnqueueRemainingLosCandidates(i, step, candidateCount);
                        }
                        break;
                    }
                    RDF_RadarScanGeometry.FillLosExclude(m_LosExclude, subject, entry.m_Entity);
                    int traceMoves = 0;
                    hitFraction = RDF_RadarScanGeometry.TraceLineOfSightCounted(
                        world,
                        param,
                        origin,
                        losEnd,
                        RDF_RadarScanGeometry.LOS_START_CLEARANCE_M,
                        traceMoves);
                    if (traceMoves < 1)
                        traceMoves = 1;
                    losUsed = losUsed + traceMoves;
                    m_StatTraceMoves = m_StatTraceMoves + traceMoves;
                    losClear = RDF_RadarScanGeometry.IsLineOfSightClear(
                        hitFraction, param.TraceEnt, entry.m_Entity);
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

            // Reused scratch truth (consumers copy synchronously; see field note).
            // Reset fields that are conditionally assigned below so no values
            // leak from the previous candidate through the reuse.
            RDF_RadarTruthSample t = m_TruthScratch;
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
            t.m_DemSampleValid = false;
            t.m_DemSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
            t.m_DemTerrainY = 0.0;
            t.m_RotorSidebandUsed = false;
            t.m_BeamName = "";
            if (isEmitter)
            {
                t.m_EmitFrequencyHz = entry.m_EmitFrequencyHz;
                t.m_EmitPeakPowerW = entry.m_EmitPeakPowerW;
                t.m_EmitAntennaGainDbi = entry.m_EmitAntennaGainDbi;
                t.m_EmitStrength = entry.m_EmitStrength;
            }
            else
            {
                t.m_EmitFrequencyHz = 0.0;
                t.m_EmitPeakPowerW = 0.0;
                t.m_EmitAntennaGainDbi = 0.0;
                t.m_EmitStrength = 1.0;
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
            RDF_RadarTruthSample physicalSource;
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
            {
                RDF_RadarPhysicalDetect.Process(
                    t, origin, forward, worldTime, world,
                    m_Settings, m_DemCache, m_ScanRainLossDbPerKm, m_ClutterMap, m_CoarseRdMap);
            }
            DepositCoarseRd(t);
            NoteMtdPlot(t);

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
            PublishTruthToPlots(outTargets, t);
        }

        if (!queueDrain)
        {
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
        }

        ctx.m_LosUsed = losUsed;
        ctx.m_FreshBudget = freshBudget;
    }

    // Effective per-tick LOS TraceMove budget. With the frame queue on and
    // m_LosTracesPerTick > 0, one Scan() (one Tick) spends at most this many
    // traces; overflow is deferred to m_LosQueue and drained by later scans.
    // Otherwise fall back to the legacy per-scan cap.
    protected int ResolveLosBudgetPerTick()
    {
        if (!m_Settings)
            return 48;
        if (m_Settings.m_EnableLosFrameQueue && m_Settings.m_LosTracesPerTick > 0)
            return m_Settings.m_LosTracesPerTick;
        return m_Settings.m_MaxLosTracesPerScan;
    }

    // Cross-frame LOS drain: pop a bounded slice of deferred scatterers and run
    // the standard candidate pass over it (same gates / LOS / physics path).
    // Shares ctx.m_LosBudget with the sweep that follows, so the combined trace
    // cost of drain + sweep stays within the per-tick budget.
    protected void DrainLosQueue(
        notnull array<ref RDF_RadarTarget> outTargets,
        RDF_RadarScanPassContext ctx)
    {
        if (!m_Settings || !m_Settings.m_EnableLosFrameQueue)
            return;
        if (!m_LosQueue || m_LosQueue.Count() <= 0)
            return;

        int budget = ctx.m_LosBudget;
        int used = ctx.m_LosUsed;
        if (used >= budget)
            return;

        // Each deferred candidate may consume up to 2 TraceMove calls, so pop at
        // most (budget - used)/2 + 1 candidates; ScanFromRegistry still enforces
        // the trace budget itself via losUsed/losBudget. If the drain stops
        // early, unprocessed slice candidates are harmless — the next sweep's
        // CollectInSphere re-collects them (the registry is the source of truth;
        // the queue only reorders priority).
        int remaining = budget - used;
        int maxPop = Math.Max(1, remaining / 2 + 1);
        m_QueueCandidates.Clear();
        while (m_LosQueue.Count() > 0 && m_QueueCandidates.Count() < maxPop)
        {
            RDF_RadarScatterer e = m_LosQueue.Get(0);
            m_LosQueue.Remove(0);
            if (!e || !e.m_Alive || !e.m_Entity)
                continue;
            m_QueueCandidates.Insert(e);
            m_StatLosQueueDrained = m_StatLosQueueDrained + 1;
        }
        if (m_QueueCandidates.Count() <= 0)
            return;

        ScanFromRegistry(outTargets, ctx, m_QueueCandidates);
        m_QueueCandidates.Clear();
    }

    // Defer the unprocessed tail of the current sweep to the LOS queue so the
    // next Tick drains it first (priority over fresh candidates). Bounded by
    // m_LosQueueMax; duplicates by scatterer id are skipped.
    protected void EnqueueRemainingLosCandidates(
        int currentIndex,
        int currentStep,
        int candidateCount)
    {
        if (!m_LosQueue || !m_Settings)
            return;
        if (m_LosQueue.Count() >= m_Settings.m_LosQueueMax)
            return;

        int remaining = candidateCount - (currentStep + 1);
        for (int k = 0; k < remaining; k++)
        {
            if (m_LosQueue.Count() >= m_Settings.m_LosQueueMax)
                break;
            int idx = currentIndex + 1 + k;
            while (idx >= candidateCount)
                idx = idx - candidateCount;
            RDF_RadarScatterer e = m_ScattererCandidates.Get(idx);
            if (!e || !e.m_Alive || !e.m_Entity)
                continue;
            if (LosQueueContains(e.m_ScattererId))
                continue;
            m_LosQueue.Insert(e);
            m_StatLosQueued = m_StatLosQueued + 1;
        }
    }

    protected bool LosQueueContains(int scattererId)
    {
        if (!m_LosQueue || scattererId <= 0)
            return false;
        for (int i = 0; i < m_LosQueue.Count(); i++)
        {
            RDF_RadarScatterer e = m_LosQueue.Get(i);
            if (e && e.m_ScattererId == scattererId)
                return true;
        }
        return false;
    }


    protected void DepositCoarseRd(RDF_RadarTruthSample target)
    {
        if (!target || !m_Settings || !m_Settings.m_EnableCoarseRd)
            return;
        if (!m_CoarseRdMap)
            return;
        int bin = target.m_DopplerBin;
        if (bin < 0)
            bin = 0;
        m_CoarseRdMap.Deposit(target.m_Distance, bin, target.m_ProcessedPowerW);
    }

    protected void EnsureLosTrace()
    {
        if (!m_TraceParam)
            m_TraceParam = new TraceParam();
        if (!m_LosExclude)
            m_LosExclude = new array<IEntity>();
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

        RDF_RadarTruthSample reusedTruth;
        if (!m_ScanReuseCache.TryGetReusedTruth(
            entry.m_Entity,
            wallTime,
            m_Settings.m_TargetReuseMaxAgeS,
            reusedTruth))
        {
            return;
        }

        m_StatReuseHits = m_StatReuseHits + 1;
        reusedTruth.m_Entity = entry.m_Entity;
        reusedTruth.m_ScattererId = entry.m_ScattererId;
        reusedTruth.m_Position = losEnd;
        reusedTruth.m_Distance = dist;
        reusedTruth.m_Velocity = entry.m_Velocity;
        reusedTruth.m_Type = priorityType;
        reusedTruth.m_AglM = entry.m_AglM;
        reusedTruth.m_Time = worldTime;
        if (entry.m_DemSampleValid)
        {
            reusedTruth.m_DemSampleValid = true;
            reusedTruth.m_DemSurfaceClass = entry.m_DemSurfaceClass;
            reusedTruth.m_DemTerrainY = entry.m_DemTerrainY;
        }
        PublishTruthToPlots(outTargets, reusedTruth);
    }

    protected bool ContainsScattererId(
        notnull array<ref RDF_RadarTarget> targets,
        int scattererId)
    {
        if (scattererId <= 0)
            return false;
        for (int i = 0; i < targets.Count(); i++)
        {
            RDF_RadarTarget t = targets.Get(i);
            if (!t)
                continue;
            if (t.m_ScattererId == scattererId)
                return true;
        }
        return false;
    }

    // Deception false plots are inverse-only observations (no TruthSample).
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

        m_LastFalsePlotCount = falsePlots.Count();
        float scanAzimuth = Math.Atan2(scanForward[2], scanForward[0]);
        for (int i = 0; i < falsePlots.Count() && outTargets.Count() < maxTargets; i++)
        {
            RDF_RadarFalsePlot p = falsePlots.Get(i);
            if (!p)
                continue;
            if (p.m_PowerW <= 0.0)
                continue;
            float rangeM = p.m_RangeM;
            if (rangeM <= m_Settings.GetEffectiveMinDistance())
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
        RDF_RadarTruthSample target,
        RDF_RadarTruthSample source)
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
        target.m_RotorSidebandUsed = source.m_RotorSidebandUsed;
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
        if (m_Settings.m_bScanAngleLocked)
        {
            float locked = NormalizeAngleRad(m_Settings.m_ScanPhaseOffsetRad);
            return Vector(Math.Cos(locked), 0.0, Math.Sin(locked));
        }

        if (rpm <= 0.0)
            return GetSubjectForward(subject);

        // World-time absolute scan angle plus an optional phase offset. The
        // offset lets a mod resume a paused (disabled) antenna from its frozen
        // bearing; with 0 it is the stock world-clock scan.
        float angle = worldTime * rpm * Math.PI * 2.0 / 60.0 + m_Settings.m_ScanPhaseOffsetRad;
        angle = NormalizeAngleRad(angle);
        return Vector(Math.Cos(angle), 0.0, Math.Sin(angle));
    }
}
