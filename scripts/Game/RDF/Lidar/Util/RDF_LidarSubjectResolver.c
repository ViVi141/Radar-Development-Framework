// Subject resolver for LiDAR scans. // LiDAR 扫描的主体解析器。
class RDF_LidarSubjectResolver
{
    // Resolve subject for the local player. // 为本地玩家解析扫描主体。
    static IEntity ResolveLocalSubject(bool preferVehicle = true)
    {
        PlayerController controller = GetGame().GetPlayerController();
        if (!controller)
            return null;

        IEntity player = controller.GetControlledEntity();
        return ResolveSubject(player, preferVehicle);
    }

    // Resolve subject entity for debug or scan origin. // 为调试或扫描原点解析主体实体。
    // If the player is in a vehicle, returns the vehicle root entity; otherwise returns the player. // 如果玩家在车辆内，返回车辆根实体；否则返回玩家。
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

    // Convenience helper for origin queries. // 方便的原点查询助手。
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

    // Walk up the parent chain to find the root entity for the compartmented object. // 沿父级链向上查找隔间对象的根实体。
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
