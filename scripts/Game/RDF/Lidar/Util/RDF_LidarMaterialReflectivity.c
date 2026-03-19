// LiDAR-only reflectivity helper (replaces dependency on removed RDF_RCSModel).
// Maps GameMaterial / BallisticInfo to a 0..1 reflectivity for visualization and CSV export.
class RDF_LidarMaterialReflectivity
{
    static const float REFL_MIN = 0.05;
    static const float REFL_MAX = 1.0;
    static const float DENSITY_SCALE = 8.0;  // density at which refl = REFL_MAX
    static const float WATER_REFL = 0.6;

    void RDF_LidarMaterialReflectivity() {}

    // Returns reflectivity in [0, 1] for the given surface. fallbackName is ignored (kept for API compatibility).
    static float GetReflectivityFromGameMaterial(GameMaterial surface, string fallbackName)
    {
        if (!surface)
            return 0.3;

        BallisticInfo bi = surface.GetBallisticInfo();
        if (bi)
        {
            if (bi.IsWaterSurface())
                return WATER_REFL;
            float density = bi.GetDensity();
            if (density < 0.0)
                return 0.3;
            float t = density / DENSITY_SCALE;
            if (t > 1.0)
                t = 1.0;
            return REFL_MIN + (REFL_MAX - REFL_MIN) * t;
        }
        return 0.3;
    }
}
