// Radar scanner: entity-first scan with visibility ray. Detects vehicles, projectiles,
// and emitting radars (from registry). Uses GetActiveEntities + sector + TraceMove.
class RDF_RadarScanner
{
    protected ref RDF_RadarSettings m_Settings;

    void RDF_RadarScanner(RDF_RadarSettings settings = null)
    {
        if (settings)
            m_Settings = settings;
        else
            m_Settings = new RDF_RadarSettings();
    }

    RDF_RadarSettings GetSettings()
    {
        return m_Settings;
    }

    void Scan(IEntity subject, array<ref RDF_RadarTarget> outTargets)
    {
        if (!subject || !m_Settings || !m_Settings.m_Enabled || !outTargets)
            return;

        outTargets.Clear();
        m_Settings.Validate();

        BaseWorld world = subject.GetWorld();
        if (!world)
        {
            world = GetGame().GetWorld();
            if (!world)
                return;
        }

        vector origin = GetSubjectOrigin(subject);
        vector forward = GetSubjectForward(subject);
        float range = m_Settings.m_Range;
        float minDist = m_Settings.m_MinDistance;
        float halfAngleRad = m_Settings.m_SectorHalfAngleDeg * 0.01745329;
        float cosHalfAngle = Math.Cos(halfAngleRad);
        int maxTargets = m_Settings.m_MaxTargets;
        float worldTime = world.GetWorldTime() * 0.001;

        array<IEntity> active = new array<IEntity>();
        world.GetActiveEntities(active);

        TraceParam param = new TraceParam();
        param.LayerMask = EPhysicsLayerPresets.Projectile;
        param.Exclude = subject;
        param.Flags = TraceFlags.WORLD | TraceFlags.ENTS;

        for (int i = 0; i < active.Count() && outTargets.Count() < maxTargets; i++)
        {
            IEntity ent = active.Get(i);
            if (!ent || ent == subject)
                continue;

            vector pos = GetEntityPosition(ent);
            vector toTarget = pos - origin;
            float dist = toTarget.Length();
            if (dist < minDist || dist > range)
                continue;

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

            param.Start = origin;
            param.End = pos;
            param.TraceEnt = null;
            float hitFraction = world.TraceMove(param, null);
            if (param.TraceEnt != ent)
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
            outTargets.Insert(t);
        }

        if (m_Settings.m_IncludeRadarEmitters && outTargets.Count() < maxTargets)
        {
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
                outTargets.Insert(et);
            }
        }
    }

    protected static vector GetEntityPosition(IEntity entity)
    {
        if (!entity)
            return "0 0 0";
        vector mat[4];
        entity.GetWorldTransform(mat);
        return mat[3];
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
        return "0 0 0";
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

    protected vector GetSubjectForward(IEntity subject)
    {
        vector worldMat[4];
        subject.GetWorldTransform(worldMat);
        vector fwd = worldMat[0];
        float len = fwd.Length();
        if (len > 0.001)
            return fwd / len;
        return "1 0 0";
    }
}
