// Shared LOS / entity geometry helpers for RDF_RadarScanner passes.
class RDF_RadarScanGeometry
{
    static bool IsLineOfSightClear(float hitFraction, IEntity traceEnt, IEntity target)
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
    static bool IsEntityOrChildOf(IEntity hitEntity, IEntity target)
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
    static vector GetScattererLosEnd(RDF_RadarScatterer entry)
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

    static vector GetEntityLosEnd(IEntity entity)
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

    static vector GetEntityVelocity(IEntity entity)
    {
        if (!entity)
            return "0 0 0";
        GenericEntity generic = GenericEntity.Cast(entity);
        if (generic)
        {
            ProjectileMoveComponent pm = ProjectileMoveComponent.Cast(
                generic.FindComponent(ProjectileMoveComponent));
            if (pm)
                return pm.GetVelocity();
        }
        Physics physics = entity.GetPhysics();
        if (physics)
            return physics.GetVelocity();
        return "0 0 0";
    }

    static float NormalizeAngleRad(float angle)
    {
        while (angle > Math.PI)
            angle = angle - Math.PI * 2.0;
        while (angle < -Math.PI)
            angle = angle + Math.PI * 2.0;
        return angle;
    }
}
