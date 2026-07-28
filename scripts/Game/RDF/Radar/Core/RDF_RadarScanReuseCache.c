class RDF_RadarScanReuseEntry
{
    ref RDF_RadarTarget m_LastTarget;
    vector m_LastOrigin;
    vector m_LastPosition;
    float m_LastWallS;
    float m_LastFullUpdateWallS;
    float m_LastPhysicalWallS;
    int m_LastPriorityBand;
}

class RDF_RadarScanReuseCache
{
    protected ref map<IEntity, ref RDF_RadarScanReuseEntry> m_ByEntity;
    protected ref array<IEntity> m_PruneScratch;
    protected float m_LastPruneWallS = -1000.0;
    protected static const float PRUNE_INTERVAL_S = 12.0;

    void RDF_RadarScanReuseCache()
    {
        m_ByEntity = new map<IEntity, ref RDF_RadarScanReuseEntry>();
        m_PruneScratch = new array<IEntity>();
    }

    void Clear()
    {
        if (!m_ByEntity)
            return;
        m_ByEntity.Clear();
        m_LastPruneWallS = -1000.0;
    }

    int ClassifyPriorityBand(
        ERDF_RadarTargetType type,
        float distanceM,
        float speedMs,
        RDF_RadarSettings settings)
    {
        float nearM = 800.0;
        float midM = 2200.0;
        float fastMs = 35.0;
        float slowMs = 8.0;
        if (settings)
        {
            nearM = settings.m_PriorityNearRangeM;
            midM = settings.m_PriorityMidRangeM;
            fastMs = settings.m_PriorityFastSpeedMs;
            slowMs = settings.m_PrioritySlowSpeedMs;
        }

        if (type == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 0;
        if (distanceM <= nearM)
            return 0;
        if (speedMs >= fastMs)
            return 0;
        if (distanceM <= midM)
            return 1;
        if (speedMs >= slowMs)
            return 1;
        return 2;
    }

    bool ShouldRunFullUpdate(
        IEntity entity,
        int priorityBand,
        float wallS,
        RDF_RadarSettings settings)
    {
        if (!entity || !m_ByEntity)
            return true;
        if (!m_ByEntity.Contains(entity))
            return true;
        RDF_RadarScanReuseEntry e = m_ByEntity.Get(entity);
        if (!e)
            return true;
        if (e.m_LastFullUpdateWallS <= 0.0)
            return true;
        float due = GetBandIntervalS(priorityBand, settings);
        if (wallS - e.m_LastFullUpdateWallS >= due)
            return true;
        return false;
    }

    bool TryGetReusedTarget(
        IEntity entity,
        float wallS,
        float maxAgeS,
        out RDF_RadarTarget outTarget)
    {
        outTarget = null;
        if (!entity || !m_ByEntity)
            return false;
        if (!m_ByEntity.Contains(entity))
            return false;
        RDF_RadarScanReuseEntry e = m_ByEntity.Get(entity);
        if (!e || !e.m_LastTarget)
            return false;
        if (wallS - e.m_LastWallS > maxAgeS)
            return false;
        outTarget = CopyTarget(e.m_LastTarget);
        return outTarget != null;
    }

    bool TryGetPhysicalReuse(
        IEntity entity,
        vector origin,
        vector position,
        float speedMs,
        float wallS,
        float maxAgeS,
        float maxOriginShiftM,
        float maxTargetShiftM,
        float maxSpeedMs,
        out RDF_RadarTarget outPhysicalSource)
    {
        outPhysicalSource = null;
        if (!entity || !m_ByEntity)
            return false;
        float maxSpeed = 6.0;
        if (maxSpeedMs > 0.0)
            maxSpeed = maxSpeedMs;
        if (speedMs > maxSpeed)
            return false;
        if (!m_ByEntity.Contains(entity))
            return false;
        RDF_RadarScanReuseEntry e = m_ByEntity.Get(entity);
        if (!e || !e.m_LastTarget)
            return false;
        if (e.m_LastPhysicalWallS <= 0.0)
            return false;
        if (wallS - e.m_LastPhysicalWallS > maxAgeS)
            return false;

        vector originDelta = origin - e.m_LastOrigin;
        if (originDelta.LengthSq() > maxOriginShiftM * maxOriginShiftM)
            return false;
        vector posDelta = position - e.m_LastPosition;
        if (posDelta.LengthSq() > maxTargetShiftM * maxTargetShiftM)
            return false;

        outPhysicalSource = e.m_LastTarget;
        return true;
    }

    void StoreResult(
        IEntity entity,
        RDF_RadarTarget target,
        vector origin,
        vector position,
        float wallS,
        int priorityBand,
        bool ranFullUpdate,
        bool ranPhysical)
    {
        if (!entity || !target || !m_ByEntity)
            return;

        RDF_RadarScanReuseEntry e = null;
        if (m_ByEntity.Contains(entity))
            e = m_ByEntity.Get(entity);
        if (!e)
        {
            e = new RDF_RadarScanReuseEntry();
            m_ByEntity.Set(entity, e);
        }

        e.m_LastTarget = CopyTarget(target);
        e.m_LastOrigin = origin;
        e.m_LastPosition = position;
        e.m_LastWallS = wallS;
        e.m_LastPriorityBand = priorityBand;
        if (ranFullUpdate)
            e.m_LastFullUpdateWallS = wallS;
        if (ranPhysical)
            e.m_LastPhysicalWallS = wallS;
    }

    void MaybePrune(float wallS)
    {
        if (!m_ByEntity || !m_PruneScratch)
            return;
        if (wallS - m_LastPruneWallS < PRUNE_INTERVAL_S)
            return;
        m_LastPruneWallS = wallS;

        m_PruneScratch.Clear();
        for (int i = 0; i < m_ByEntity.Count(); i++)
        {
            IEntity key = m_ByEntity.GetKey(i);
            if (key)
                m_PruneScratch.Insert(key);
        }
        if (m_PruneScratch.Count() == m_ByEntity.Count())
            return;

        map<IEntity, ref RDF_RadarScanReuseEntry> rebuilt = new map<IEntity, ref RDF_RadarScanReuseEntry>();
        for (int j = 0; j < m_PruneScratch.Count(); j++)
        {
            IEntity key2 = m_PruneScratch.Get(j);
            if (!key2)
                continue;
            RDF_RadarScanReuseEntry value = m_ByEntity.Get(key2);
            if (!value)
                continue;
            rebuilt.Set(key2, value);
        }
        m_ByEntity = rebuilt;
    }

    protected float GetBandIntervalS(int band, RDF_RadarSettings settings)
    {
        float band1 = 0.10;
        float band2 = 0.30;
        if (settings)
        {
            band1 = settings.m_PriorityBand1IntervalS;
            band2 = settings.m_PriorityBand2IntervalS;
        }
        if (band <= 0)
            return 0.0;
        if (band == 1)
            return band1;
        return band2;
    }

    protected RDF_RadarTarget CopyTarget(RDF_RadarTarget src)
    {
        if (!src)
            return null;
        RDF_RadarTarget t = new RDF_RadarTarget();
        t.m_Entity = src.m_Entity;
        t.m_ScattererId = src.m_ScattererId;
        t.m_Position = src.m_Position;
        t.m_Distance = src.m_Distance;
        t.m_Velocity = src.m_Velocity;
        t.m_Type = src.m_Type;
        t.m_Time = src.m_Time;
        t.m_AzimuthDeg = src.m_AzimuthDeg;
        t.m_ElevationDeg = src.m_ElevationDeg;
        t.m_RadialSpeedMs = src.m_RadialSpeedMs;
        t.m_RcsM2 = src.m_RcsM2;
        t.m_MeanRcsM2 = src.m_MeanRcsM2;
        t.m_SwerlingModel = src.m_SwerlingModel;
        t.m_AglM = src.m_AglM;
        t.m_DemTerrainY = src.m_DemTerrainY;
        t.m_ReceivedPowerW = src.m_ReceivedPowerW;
        t.m_ProcessedPowerW = src.m_ProcessedPowerW;
        t.m_DopplerHz = src.m_DopplerHz;
        t.m_MtiGain = src.m_MtiGain;
        t.m_DemSurfaceClass = src.m_DemSurfaceClass;
        t.m_DemSampleValid = src.m_DemSampleValid;
        t.m_ClutterPowerW = src.m_ClutterPowerW;
        t.m_ClutterToNoiseDb = src.m_ClutterToNoiseDb;
        t.m_SnrDb = src.m_SnrDb;
        t.m_Detected = src.m_Detected;
        t.m_IsAnonymous = src.m_IsAnonymous;
        t.m_IsFalsePlot = src.m_IsFalsePlot;
        t.m_CfarPowerW = src.m_CfarPowerW;
        t.m_LosBlocked = src.m_LosBlocked;
        t.m_LosHitFraction = src.m_LosHitFraction;
        t.m_MultipathFactor = src.m_MultipathFactor;
        t.m_BeamName = src.m_BeamName;
        t.m_ScanNumber = src.m_ScanNumber;
        return t;
    }
}
