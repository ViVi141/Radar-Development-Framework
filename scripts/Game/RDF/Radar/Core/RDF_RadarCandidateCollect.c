class RDF_RadarCandidateCollect
{
    protected ref array<IEntity> m_QueryCandidates;
    protected ref array<IEntity> m_CandidateScratch;

    void RDF_RadarCandidateCollect()
    {
        m_QueryCandidates = new array<IEntity>();
        m_CandidateScratch = new array<IEntity>();
    }

    void CollectCandidateEntities(
        BaseWorld world,
        IEntity subject,
        vector origin,
        float range,
        RDF_RadarSettings settings,
        notnull array<IEntity> outCandidates)
    {
        outCandidates.Clear();
        if (!world || !settings)
            return;

        bool haveSphere = false;
        if (settings.m_UseSphereQuery)
        {
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

        bool needActive = false;
        if (!settings.m_UseSphereQuery)
            needActive = true;
        else if (!haveSphere)
            needActive = true;
        else if (settings.m_SphereQueryAlsoActive)
            needActive = true;

        if (!needActive)
            return;

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

    void SortEntitiesByDistance(notnull array<IEntity> entities, vector origin)
    {
        int n = entities.Count();
        if (n <= 1)
            return;

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

    protected bool OnSphereCandidateEntity(IEntity entity)
    {
        if (!entity)
            return false;
        if (!RDF_RadarEntityClassifier.IsRadarCandidate(entity))
            return true;
        m_QueryCandidates.Insert(entity);
        return true;
    }

    protected bool ContainsEntity(notnull array<IEntity> entities, IEntity entity)
    {
        for (int i = 0; i < entities.Count(); i++)
        {
            if (entities.Get(i) == entity)
                return true;
        }
        return false;
    }
}