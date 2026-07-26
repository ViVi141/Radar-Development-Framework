// Classify entities for radar: projectile vs vehicle vs other.
// Cheap type checks first; prefab string fallback only when needed.
class RDF_RadarEntityClassifier
{
    static bool IsProjectile(IEntity entity)
    {
        if (!entity)
            return false;

        GenericEntity generic = GenericEntity.Cast(entity);
        if (generic)
        {
            ProjectileMoveComponent pm = ProjectileMoveComponent.Cast(
                generic.FindComponent(ProjectileMoveComponent));
            if (pm)
                return true;
        }

        return PrefabNameContainsProjectileToken(entity);
    }

    static bool IsVehicleOrCharacter(IEntity entity)
    {
        if (!entity)
            return false;

        if (ChimeraCharacter.Cast(entity))
            return true;

        if (Vehicle.Cast(entity))
            return true;

        return PrefabNameContainsVehicleToken(entity);
    }

    // Fast reject used by sphere-query callback before inserting candidates.
    static bool IsRadarCandidate(IEntity entity)
    {
        if (!entity)
            return false;
        if (ChimeraCharacter.Cast(entity))
            return true;
        if (Vehicle.Cast(entity))
            return true;

        GenericEntity generic = GenericEntity.Cast(entity);
        if (generic)
        {
            ProjectileMoveComponent pm = ProjectileMoveComponent.Cast(
                generic.FindComponent(ProjectileMoveComponent));
            if (pm)
                return true;
        }

        return PrefabNameContainsVehicleToken(entity)
            || PrefabNameContainsProjectileToken(entity);
    }

    protected static bool PrefabNameContainsProjectileToken(IEntity entity)
    {
        string className = GetPrefabClassName(entity);
        if (className == "")
            return false;
        if (className.IndexOf("Projectile") >= 0)
            return true;
        if (className.IndexOf("Missile") >= 0)
            return true;
        return false;
    }

    protected static bool PrefabNameContainsVehicleToken(IEntity entity)
    {
        string className = GetPrefabClassName(entity);
        if (className == "")
            return false;
        if (className.IndexOf("Car") >= 0)
            return true;
        if (className.IndexOf("Vehicle") >= 0)
            return true;
        if (className.IndexOf("Tank") >= 0)
            return true;
        if (className.IndexOf("Helicopter") >= 0)
            return true;
        if (className.IndexOf("Aircraft") >= 0)
            return true;
        if (className.IndexOf("Plane") >= 0)
            return true;
        if (className.IndexOf("Rotor") >= 0)
            return true;
        if (className.IndexOf("Character") >= 0)
            return true;
        return false;
    }

    protected static string GetPrefabClassName(IEntity entity)
    {
        if (!entity)
            return "";
        EntityPrefabData prefab = entity.GetPrefabData();
        if (!prefab)
            return "";
        BaseContainer container = prefab.GetPrefab();
        if (!container)
            return "";
        return container.GetClassName();
    }
}
