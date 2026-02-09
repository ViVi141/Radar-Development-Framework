// Subject resolver for LiDAR scans.
class RDF_LidarSubjectResolver
{
    // Resolve subject for the local player.
    static IEntity ResolveLocalSubject(bool preferVehicle = true)
    {
        PlayerController controller = GetGame().GetPlayerController();
        if (!controller)
            return null;

        IEntity player = controller.GetControlledEntity();
        return ResolveSubject(player, preferVehicle);
    }

    // Resolve subject entity for debug or scan origin.
    // If the player is in a vehicle, returns the vehicle root entity; otherwise returns the player.
    static IEntity ResolveSubject(IEntity player, bool preferVehicle = true)
    {
        if (!player)
            return null;

        if (!preferVehicle)
            return player;

        ChimeraCharacter character = ChimeraCharacter.Cast(player);
        if (!character)
            return player;

        CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
        if (!access || !access.GetCompartment())
            return player;

        IEntity vehicle = FindVehicleRoot(player);
        if (vehicle)
            return vehicle;

        return player;
    }

    // Convenience helper for origin queries.
    static vector ResolveOrigin(IEntity player = null, bool preferVehicle = true)
    {
        IEntity subject;
        if (player)
            subject = ResolveSubject(player, preferVehicle);
        else
            subject = ResolveLocalSubject(preferVehicle);
        if (!subject)
            return vector.Zero;
        return subject.GetOrigin();
    }

    // Walk up the parent chain to find the root entity for the compartmented object.
    protected static IEntity FindVehicleRoot(IEntity entity)
    {
        IEntity parent = entity.GetParent();
        if (!parent)
            return null;

        IEntity lastParent = parent;
        int guard = 0;
        while (parent && guard < 8)
        {
            lastParent = parent;
            parent = parent.GetParent();
            guard++;
        }

        return lastParent;
    }
}
