// Classify entities for radar: projectile vs vehicle vs other.
// Cheap type checks first; prefab string fallback only when needed.
// The prefab class name is fetched ONCE per entity and lowercased, so the
// vehicle/projectile token scans never re-run GetPrefabData()/GetPrefab()/
// GetClassName() (costly per candidate) and case differences cannot cause
// classification false negatives.
class RDF_RadarEntityClassifier
{
    // Lazy-initialised token tables. Enforce requires `ref` on reference-type
    // statics and has no static ctor, so fill on first use (this is a hot
    // path — the sphere-query callback calls IsRadarCandidate per entity).
    protected static ref array<string> s_ProjectileTokens;
    protected static ref array<string> s_VehicleTokens;
    protected static bool s_TokensInit;

    protected static void EnsureTokens()
    {
        if (s_TokensInit)
            return;
        s_TokensInit = true;

        s_ProjectileTokens = new array<string>();
        s_ProjectileTokens.Insert("projectile");
        s_ProjectileTokens.Insert("missile");

        s_VehicleTokens = new array<string>();
        s_VehicleTokens.Insert("car");
        s_VehicleTokens.Insert("vehicle");
        s_VehicleTokens.Insert("tank");
        s_VehicleTokens.Insert("helicopter");
        s_VehicleTokens.Insert("aircraft");
        s_VehicleTokens.Insert("plane");
        s_VehicleTokens.Insert("rotor");
        s_VehicleTokens.Insert("character");
        // Script-driven airframes (e.g. SIGINT Pchela) are GenericEntity; match
        // prefab resource path tokens, not only the entity class name.
        s_VehicleTokens.Insert("drone");
        s_VehicleTokens.Insert("pchela");
    }

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

        EnsureTokens();
        return MatchesTokens(entity, s_ProjectileTokens);
    }

    static bool IsVehicleOrCharacter(IEntity entity)
    {
        if (!entity)
            return false;

        if (ChimeraCharacter.Cast(entity))
            return true;

        if (Vehicle.Cast(entity))
            return true;

        EnsureTokens();
        return MatchesTokens(entity, s_VehicleTokens);
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

        EnsureTokens();
        if (MatchesTokens(entity, s_VehicleTokens))
            return true;
        return MatchesTokens(entity, s_ProjectileTokens);
    }

    //------------------------------------------------------------------------------------------------
    // Class name first (cheap for Vehicle/Character roots); resource path next so
    // GenericEntity airframes under Prefabs/Vehicles/.../Pchela still qualify.
    protected static bool MatchesTokens(IEntity entity, array<string> tokens)
    {
        string lowerClass = GetLowerPrefabClassName(entity);
        if (ContainsAnyToken(lowerClass, tokens))
            return true;
        string lowerRes = GetLowerPrefabResourceName(entity);
        return ContainsAnyToken(lowerRes, tokens);
    }

    //------------------------------------------------------------------------------------------------
    protected static bool ContainsAnyToken(string haystack, array<string> tokens)
    {
        if (haystack == "")
            return false;
        for (int i = 0; i < tokens.Count(); i++)
        {
            if (haystack.IndexOf(tokens.Get(i)) >= 0)
                return true;
        }
        return false;
    }

    protected static string GetLowerPrefabClassName(IEntity entity)
    {
        string name = GetPrefabClassName(entity);
        if (name == "")
            return "";
        // Enforce string.ToLower() mutates in place and returns the length
        // (int), not a new string.
        name.ToLower();
        return name;
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

    protected static string GetLowerPrefabResourceName(IEntity entity)
    {
        if (!entity)
            return "";
        EntityPrefabData prefab = entity.GetPrefabData();
        if (!prefab)
            return "";
        ResourceName prefabName = prefab.GetPrefabName();
        if (prefabName.IsEmpty())
            return "";
        string asString = prefabName;
        asString.ToLower();
        return asString;
    }
}
