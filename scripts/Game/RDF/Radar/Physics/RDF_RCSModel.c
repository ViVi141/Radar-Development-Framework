// Radar Cross Section (RCS) models.
// Provides analytic shape models and an entity-based estimation helper.
// All methods are static utility functions.
class RDF_RCSModel
{
    void RDF_RCSModel() {}

    // Sphere RCS - exact for optical region, approximations for Rayleigh/transition.
    //   Optical  region (2*PI*r >> lambda):  sigma = PI*r^2
    //   Rayleigh region (2*PI*r << lambda): sigma ~= 9*PI*V^2*k^4
    static float CalculateSphereRCS(float radius, float wavelength)
    {
        if (radius <= 0 || wavelength <= 0)
            return 0.0;

        float circumference = 2.0 * Math.PI * radius;

        // Optical region.
        if (circumference > 10.0 * wavelength)
            return Math.PI * radius * radius;

        // Rayleigh region.
        if (circumference < 0.1 * wavelength)
        {
            float k   = 2.0 * Math.PI / wavelength;
            float vol = (4.0 / 3.0) * Math.PI * radius * radius * radius;
            return 9.0 * Math.PI * vol * vol * k * k * k * k;
        }

        // Transition / Mie region - linear interpolation to optical.
        return Math.PI * radius * radius * 0.5;
    }

    // Flat plate RCS - normal incidence.
    //   sigma = 4*PI * A^2 / lambda^2
    static float CalculatePlateRCS(float area, float wavelength)
    {
        if (area <= 0 || wavelength <= 0)
            return 0.0;

        return (4.0 * Math.PI * area * area) / (wavelength * wavelength);
    }

    // Cylinder RCS - broadside incidence (simplified optical-region formula).
    //   sigma ~= 2*PI * r * L^2 / lambda
    static float CalculateCylinderRCS(float length, float radius, float wavelength)
    {
        if (length <= 0 || radius <= 0 || wavelength <= 0)
            return 0.0;

        return 2.0 * Math.PI * radius * length * length / wavelength;
    }

    // Material reflectivity look-up (0 = absorber, 1 = perfect reflector).
    // Reforger surface names are lowercase snake_case (e.g. "metal_01", "grass").
    // All substring patterns are lowercase to match the engine convention.
    static float GetMaterialReflectivity(string matName)
    {
        if (matName.Contains("metal")  || matName.Contains("steel") ||
            matName.Contains("iron")   || matName.Contains("armor") ||
            matName.Contains("armour") || matName.Contains("Metal") ||
            matName.Contains("Steel"))
            return 1.0;

        if (matName.Contains("concrete") || matName.Contains("brick") ||
            matName.Contains("stone")    || matName.Contains("Concrete"))
            return 0.3;

        if (matName.Contains("wood") || matName.Contains("timber") ||
            matName.Contains("Wood"))
            return 0.1;

        if (matName.Contains("glass") || matName.Contains("Glass"))
            return 0.15;

        if (matName.Contains("vegetation") || matName.Contains("grass") ||
            matName.Contains("leaf")       || matName.Contains("foliage") ||
            matName.Contains("Grass")      || matName.Contains("Foliage"))
            return 0.05;

        if (matName.Contains("dirt")   || matName.Contains("soil") ||
            matName.Contains("sand")   || matName.Contains("gravel") ||
            matName.Contains("Ground") || matName.Contains("ground"))
            return 0.1;

        if (matName.Contains("water") || matName.Contains("Water"))
            return 0.6;

        if (matName.Contains("rubber") || matName.Contains("plastic") ||
            matName.Contains("Rubber") || matName.Contains("Plastic"))
            return 0.05;

        // Unknown material - assume moderate reflectivity.
        return 0.5;
    }

    // Angle-dependent reflection factor.
    // Metallic surfaces: near-constant (Fresnel).
    // Non-metallic: cos^2(theta) roll-off with incidence angle.
    static float GetAngleDependentReflection(float incidenceAngleDeg, bool isMetal)
    {
        float cosTheta = Math.Cos(incidenceAngleDeg * Math.DEG2RAD);
        cosTheta = Math.Clamp(cosTheta, 0.0, 1.0);

        if (isMetal)
            return 1.0;

        return cosTheta * cosTheta;
    }

    // Estimate RCS for a game entity using bounding-box geometry + material.
    // materialName: surface material string from the hit sample (may be empty).
    // Falls back gracefully when entity or material data are unavailable.
    static float EstimateEntityRCS(
        IEntity entity,
        vector radarPos,
        vector rayDir,
        float wavelength,
        string materialName)
    {
        if (wavelength <= 0)
            return 0.0;

        float reflectivity   = GetMaterialReflectivity(materialName);
        float incidenceDeg   = 0.0;

        // No entity means a pure terrain/world geometry hit (entity=null, SurfaceProps set).
        // Use a diffuse area-scattering model: sigma = sigma0 * patchArea,
        // where sigma0 is the normalised radar backscattering coefficient for the surface.
        // This is far more realistic than the specular plate formula for natural terrain.
        //
        // Representative sigma0 values at X-band:
        //   Metal / concrete  reflectivity >= 0.3 : -10 dB/m^2 (sigma0 = 0.100)
        //   Grass / vegetation reflectivity ~0.05  : -20 dB/m^2 (sigma0 = 0.010)
        //   Dirt / gravel     reflectivity ~0.10   : -16 dB/m^2 (sigma0 = 0.025)
        // Linear interpolation from reflectivity -> sigma0:
        //   sigma0 = 0.01 + reflectivity * 0.09   (range 0.01 to 0.10 for refl 0..1)
        if (!entity)
        {
            float sigma0      = 0.01 + reflectivity * 0.09;
            float patchArea   = 1.0;  // 1 m^2 representative ground cell
            float terrainRCS  = sigma0 * patchArea;
            // Clamp: min -40 dBsm (0.0001), max +5 dBsm (3.16)
            return Math.Clamp(terrainRCS, 0.0001, 3.16);
        }

        // Bounding-box volume -> equivalent sphere radius.
        vector mins, maxs;
        entity.GetBounds(mins, maxs);
        vector size = maxs - mins;

        float sizeX = Math.Max(size[0], 0.1);
        float sizeY = Math.Max(size[1], 0.1);
        float sizeZ = Math.Max(size[2], 0.1);
        float volume = sizeX * sizeY * sizeZ;

        // Guard: if bounds are unreasonably large (> 500 m per axis),
        // the entity is likely the terrain mesh - treat as large flat plate.
        if (sizeX > 500.0 || sizeY > 500.0 || sizeZ > 500.0)
        {
            float patchArea = 4.0; // 2 m x 2 m representative patch
            float terrainRCS = CalculatePlateRCS(patchArea, wavelength) * reflectivity;
            return Math.Clamp(terrainRCS, 0.001, 10000.0);
        }

        // 4/3 * PI ~= 4.18879020479
        float equivRadius = Math.Pow(volume / 4.18879020479, 0.333333);
        // Ensure radius is at least 5 cm so we stay out of extreme Rayleigh regime.
        equivRadius = Math.Max(equivRadius, 0.05);

        // Base RCS from sphere model.
        float baseRCS = CalculateSphereRCS(equivRadius, wavelength);

        // Angle-of-incidence modifier.
        vector entityPos     = entity.GetOrigin();
        vector toEntity      = (entityPos - radarPos).Normalized();
        float  cosAngle      = Math.AbsFloat(vector.Dot(rayDir, toEntity));
        incidenceDeg         = Math.Acos(Math.Clamp(cosAngle, 0.0, 1.0)) * Math.RAD2DEG;

        bool  isMetal  = reflectivity >= 0.8;
        float angleMod = GetAngleDependentReflection(incidenceDeg, isMetal);

        float rcs = baseRCS * reflectivity * angleMod;
        float minEntityRCS = 0.1;
        if (rcs < minEntityRCS)
            rcs = minEntityRCS;
        return rcs;
    }
}
