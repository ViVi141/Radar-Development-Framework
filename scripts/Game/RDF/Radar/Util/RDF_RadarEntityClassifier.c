// Classify entities for radar: projectile vs vehicle vs other.
// Uses ProjectileMoveComponent for projectiles; BaseContainer.GetClassName() for fallback (prefab from EntityPrefabData.GetPrefab()).
class RDF_RadarEntityClassifier
{
    static bool IsProjectile(IEntity entity)
    {
        if (!entity)
            return false;

        GenericEntity generic = GenericEntity.Cast(entity);
        if (generic)
        {
            ProjectileMoveComponent pm = ProjectileMoveComponent.Cast(generic.FindComponent(ProjectileMoveComponent));
            if (pm)
                return true;
        }

        string className = GetPrefabClassName(entity);
        if (className == "")
            return false;
        if (className.IndexOf("Projectile") >= 0)
            return true;
        if (className.IndexOf("Missile") >= 0)
            return true;

        return false;
    }

    static bool IsVehicleOrCharacter(IEntity entity)
    {
        if (!entity)
            return false;

        if (ChimeraCharacter.Cast(entity))
            return true;

        string className = GetPrefabClassName(entity);
        if (className == "")
            return false;
        if (className.IndexOf("Car") >= 0)
            return true;
        if (className.IndexOf("Vehicle") >= 0)
            return true;
        if (className.IndexOf("Tank") >= 0)
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
