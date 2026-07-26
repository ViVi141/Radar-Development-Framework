class RDF_RadarLosCacheEntry
{
    vector m_Origin;
    vector m_End;
    float m_HitFraction;
    bool m_LosClear;
    float m_WallTimeS;
}

class RDF_RadarLosCache
{
    protected ref map<IEntity, ref RDF_RadarLosCacheEntry> m_ByEntity;
    protected ref array<IEntity> m_PruneScratch;
    protected float m_LastPruneWallS = -1000.0;

    protected static const float PRUNE_INTERVAL_S = 10.0;

    void RDF_RadarLosCache()
    {
        m_ByEntity = new map<IEntity, ref RDF_RadarLosCacheEntry>();
        m_PruneScratch = new array<IEntity>();
    }

    void Clear()
    {
        if (!m_ByEntity)
            return;
        m_ByEntity.Clear();
        m_LastPruneWallS = -1000.0;
    }

    bool TryReuse(
        IEntity entity,
        vector origin,
        vector end,
        float wallS,
        float maxAgeS,
        float maxOriginShiftM,
        float maxEndShiftM,
        out float outHitFraction,
        out bool outLosClear)
    {
        outHitFraction = 1.0;
        outLosClear = false;
        if (!entity || !m_ByEntity)
            return false;
        if (!m_ByEntity.Contains(entity))
            return false;

        RDF_RadarLosCacheEntry e = m_ByEntity.Get(entity);
        if (!e)
            return false;
        if (wallS - e.m_WallTimeS > maxAgeS)
            return false;

        vector originDelta = origin - e.m_Origin;
        if (originDelta.LengthSq() > maxOriginShiftM * maxOriginShiftM)
            return false;

        vector endDelta = end - e.m_End;
        if (endDelta.LengthSq() > maxEndShiftM * maxEndShiftM)
            return false;

        outHitFraction = e.m_HitFraction;
        outLosClear = e.m_LosClear;
        return true;
    }

    void Store(
        IEntity entity,
        vector origin,
        vector end,
        float hitFraction,
        bool losClear,
        float wallS)
    {
        if (!entity || !m_ByEntity)
            return;

        RDF_RadarLosCacheEntry e = null;
        if (m_ByEntity.Contains(entity))
            e = m_ByEntity.Get(entity);
        if (!e)
        {
            e = new RDF_RadarLosCacheEntry();
            m_ByEntity.Set(entity, e);
        }

        e.m_Origin = origin;
        e.m_End = end;
        e.m_HitFraction = hitFraction;
        e.m_LosClear = losClear;
        e.m_WallTimeS = wallS;
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

        map<IEntity, ref RDF_RadarLosCacheEntry> rebuilt = new map<IEntity, ref RDF_RadarLosCacheEntry>();
        for (int j = 0; j < m_PruneScratch.Count(); j++)
        {
            IEntity k = m_PruneScratch.Get(j);
            if (!k)
                continue;
            RDF_RadarLosCacheEntry value = m_ByEntity.Get(k);
            if (!value)
                continue;
            rebuilt.Set(k, value);
        }
        m_ByEntity = rebuilt;
    }
}
