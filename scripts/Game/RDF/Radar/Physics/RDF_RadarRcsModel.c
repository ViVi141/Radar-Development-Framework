// In-game target RCS defaults. These are configurable engineering values, not
// exact signatures; entity-specific providers can replace this table later.
class RDF_RadarRcsModel
{
    static float GetDefaultRcsM2(ERDF_RadarTargetType targetType)
    {
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_PROJECTILE)
            return 0.01;
        if (targetType == ERDF_RadarTargetType.RDF_RADAR_TARGET_RADAR_EMITTER)
            return 10.0;
        return 5.0;
    }

    static float GetEntityRcsM2(
        IEntity entity,
        ERDF_RadarTargetType targetType)
    {
        float fallback = GetDefaultRcsM2(targetType);
        if (!entity)
            return fallback;

        vector mins;
        vector maxs;
        entity.GetBounds(mins, maxs);
        vector size = maxs - mins;
        float width = Math.AbsFloat(size[0]);
        float height = Math.AbsFloat(size[1]);
        float length = Math.AbsFloat(size[2]);

        float projectedA = width * height;
        float projectedB = length * height;
        float projected = Math.Max(projectedA, projectedB);
        if (projected <= 0.01)
            return fallback;

        // Conservative optical-region proxy; cap pathological world bounds.
        float estimate = projected * 0.25;
        return Math.Clamp(estimate, fallback * 0.25, 1000.0);
    }
}
