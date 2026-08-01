// Shared LOS / entity geometry helpers for RDF_RadarScanner passes.
// TraceMove patterns follow stock Enfusion usage:
//   SCR_NameTagRulesetBase (ExcludeArray + percent==1)
//   SCR_NearbyContextDisplay (GetRootParent same-hierarchy = not obstructed)
//   SCR_PlacedCommandInfoDisplay (reuse TraceParam, clear TraceEnt, ANY_CONTACT)
//   TraceFlags.ANY_CONTACT docs: "The best for visibility testing"
class RDF_RadarScanGeometry
{
    static const float LOS_CLEAR_FRACTION = 0.999;
    // Push Start out of the subject hull (same SEH risk as LiDAR in-solid starts).
    static const float LOS_START_CLEARANCE_M = 0.15;
    // Below this path length, clearance shrinks aggressively (near-target LOS).
    static const float LOS_NEAR_PATH_M = 2.0;

    //------------------------------------------------------------------------------------------------
    // Configure a reusable TraceParam for radar LOS (call once per Scan).
    static void ConfigureLosParam(
        notnull TraceParam param,
        notnull array<IEntity> excludeArray)
    {
        param.LayerMask = EPhysicsLayerPresets.Projectile;
        param.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.ANY_CONTACT;
        // TraceParam: use Exclude OR ExcludeArray, never both.
        param.Exclude = null;
        param.ExcludeArray = excludeArray;
    }

    //------------------------------------------------------------------------------------------------
    // Insert entity and its GetRootParent (vehicles: child colliders ≠ root).
    protected static void InsertEntityWithRoot(
        notnull array<IEntity> excludeArray,
        IEntity ent)
    {
        if (!ent)
            return;
        excludeArray.Insert(ent);
        IEntity root = ent.GetRootParent();
        if (root && root != ent)
            excludeArray.Insert(root);
    }

    //------------------------------------------------------------------------------------------------
    // Fill ExcludeArray like nametag / command HUD: subject (+ optional target so a
    // hit on the candidate itself counts as a clear path to the aim point).
    // Also excludes GetRootParent so vehicle child hulls do not self-block.
    static void FillLosExclude(
        notnull array<IEntity> excludeArray,
        IEntity subject,
        IEntity target)
    {
        excludeArray.Clear();
        InsertEntityWithRoot(excludeArray, subject);
        if (target && target != subject)
            InsertEntityWithRoot(excludeArray, target);
    }

    //------------------------------------------------------------------------------------------------
    // Cap start clearance for short paths so near occluders stay visible.
    static float EffectiveStartClearanceM(float fullLenM, float requestedClearanceM)
    {
        if (requestedClearanceM <= 0.0 || fullLenM <= 0.001)
            return 0.0;

        float clear = requestedClearanceM;
        if (fullLenM < LOS_NEAR_PATH_M)
        {
            float nearCap = fullLenM * 0.05;
            if (clear > nearCap)
                clear = nearCap;
            return clear;
        }

        if (clear > fullLenM * 0.25)
            clear = fullLenM * 0.05;
        return clear;
    }

    //------------------------------------------------------------------------------------------------
    // One LOS TraceMove. Returns hit fraction remapped onto origin→losEnd [0,1]
    // so NLOS / knife-edge keep a stable parameter after start clearance.
    // Clearance segment is traced first so obstacles between origin and the
    // pushed Start are not silently skipped.
    static float TraceLineOfSight(
        notnull BaseWorld world,
        notnull TraceParam param,
        vector origin,
        vector losEnd,
        float startClearanceM)
    {
        vector delta = losEnd - origin;
        float fullLen = delta.Length();
        vector start = origin;
        float clear = EffectiveStartClearanceM(fullLen, startClearanceM);
        if (fullLen > 0.001 && clear > 0.0)
            start = origin + (delta * (clear / fullLen));

        float startOffset = (start - origin).Length();
        if (startOffset > 0.001)
        {
            param.Start = origin;
            param.End = start;
            param.TraceEnt = null;
            param.SurfaceProps = null;
            float clearHit = world.TraceMove(param, null);
            if (clearHit < LOS_CLEAR_FRACTION)
            {
                float alongClear = clearHit * startOffset;
                float remappedClear = alongClear / fullLen;
                if (remappedClear > 1.0)
                    remappedClear = 1.0;
                return remappedClear;
            }
        }

        param.Start = start;
        param.End = losEnd;
        // SCR_PhysicsHelper / SCR_PlacedCommandInfoDisplay: reset outs every cast.
        param.TraceEnt = null;
        param.SurfaceProps = null;

        float segmentHit = world.TraceMove(param, null);

        if (fullLen <= 0.001)
            return segmentHit;

        float segmentLen = fullLen - startOffset;
        if (segmentLen < 0.001)
            return segmentHit;

        float along = startOffset + (segmentHit * segmentLen);
        float remapped = along / fullLen;
        if (remapped > 1.0)
            remapped = 1.0;
        return remapped;
    }

    //------------------------------------------------------------------------------------------------
    static bool IsLineOfSightClear(float hitFraction, IEntity traceEnt, IEntity target)
    {
        // Nametag style: target excluded ⇒ clear path reaches the endpoint.
        if (hitFraction >= LOS_CLEAR_FRACTION)
            return true;
        if (!traceEnt || !target)
            return false;
        // NearbyContextDisplay: same root hierarchy is not an obstruction.
        if (traceEnt.GetRootParent() == target.GetRootParent())
            return true;
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
