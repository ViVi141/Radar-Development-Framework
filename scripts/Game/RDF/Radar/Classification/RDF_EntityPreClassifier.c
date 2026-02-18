// Pre-classify hit entity by attributes (hierarchy, ChimeraCharacter cast, name) before
// radar physics. Drives RCS priors and final target classification.
// Flow: Trace -> set sample.m_Entity -> PreClassify -> ApplyRadarPhysics -> ClassifyTarget.

enum EEntityKind
{
    ENTITY_UNKNOWN   = 0,
    ENTITY_VEHICLE   = 1,
    ENTITY_CHARACTER = 2,
    ENTITY_BUILDING  = 3,
    ENTITY_STATIC    = 4
}

class RDF_EntityPreClassifier
{
    static EEntityKind ClassifyEntity(IEntity entity)
    {
        if (!entity)
            return EEntityKind.ENTITY_UNKNOWN;

        IEntity root = GetRootEntity(entity);
        if (!root)
            root = entity;

        ChimeraCharacter character = ChimeraCharacter.Cast(root);
        if (character)
            return EEntityKind.ENTITY_CHARACTER;

        string name = root.GetName();
        if (name != "" && name.Length() > 0)
        {
            if (NameLooksLikeVehicle(name))
                return EEntityKind.ENTITY_VEHICLE;
            if (NameLooksLikeBuilding(name))
                return EEntityKind.ENTITY_BUILDING;
        }

        return EEntityKind.ENTITY_STATIC;
    }

    static IEntity GetRootEntity(IEntity entity)
    {
        if (!entity)
            return null;
        IEntity parent = entity.GetParent();
        if (!parent)
            return entity;
        int guard = 0;
        while (parent && guard < 16)
        {
            IEntity next = parent.GetParent();
            if (!next)
                return parent;
            parent = next;
            guard = guard + 1;
        }
        return parent;
    }

    static bool NameLooksLikeVehicle(string name)
    {
        if (name.Length() <= 0)
            return false;
        if (name.Contains("Vehicle")) return true;
        if (name.Contains("vehicle")) return true;
        if (name.Contains("Car")) return true;
        if (name.Contains("Tank")) return true;
        if (name.Contains("Truck")) return true;
        if (name.Contains("Heli")) return true;
        if (name.Contains("BTR")) return true;
        if (name.Contains("APC")) return true;
        if (name.Contains("IFV")) return true;
        if (name.Contains("MRAP")) return true;
        return false;
    }

    static bool NameLooksLikeBuilding(string name)
    {
        if (name.Length() <= 0)
            return false;
        if (name.Contains("Building")) return true;
        if (name.Contains("building")) return true;
        if (name.Contains("House")) return true;
        if (name.Contains("Tower")) return true;
        if (name.Contains("Bunker")) return true;
        if (name.Contains("Wall")) return true;
        return false;
    }

    static string GetKindName(EEntityKind kind)
    {
        switch (kind)
        {
            case EEntityKind.ENTITY_VEHICLE:   return "Vehicle";
            case EEntityKind.ENTITY_CHARACTER: return "Character";
            case EEntityKind.ENTITY_BUILDING:  return "Building";
            case EEntityKind.ENTITY_STATIC:    return "Static";
            default:                           return "Unknown";
        }
        return "Unknown";
    }
}
