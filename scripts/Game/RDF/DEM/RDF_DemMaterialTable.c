// Surface class codes for DEM tiles / radar clutter models.
enum ERDF_DemSurfaceClass
{
    RDF_DEM_SURF_UNKNOWN = 0,
    RDF_DEM_SURF_WATER = 1,
    RDF_DEM_SURF_VEGETATION = 2,
    RDF_DEM_SURF_SOIL = 3,
    RDF_DEM_SURF_SAND = 4,
    RDF_DEM_SURF_GRAVEL = 5,
    RDF_DEM_SURF_ASPHALT = 6,
    RDF_DEM_SURF_HARD = 7,       // concrete / stone / brick / tiles
    RDF_DEM_SURF_WOOD = 8,
    RDF_DEM_SURF_METAL = 9,
    RDF_DEM_SURF_SNOW_ICE = 10,
    RDF_DEM_SURF_FABRIC = 11
}

// Material basename -> density (g/cm³) table for DEM bake / radar clutter.
// Sourced from RSS SCR_RSS_MaterialTerrainTable; kept inside RDF so bake
// does not require the RSS addon at scan time.
class RDF_DemMaterialTable
{
    protected static ref map<string, float> s_DensityByBasename;
    protected static bool s_bInitialized;
    protected static ref TraceParam s_TraceParam;
    // Shared split scratch for the per-cell string helpers (Normalize-
    // MaterialKey / GetMaterialBasename / StripGamematSuffix / GetGeneric-
    // LabelFromBasename). These are called per cell in the bake hot path;
    // reusing one static buffer avoids 3 allocations per cell. Safe because
    // none of the callers hold the array across a nested call to another
    // helper (the result string is extracted before any nested reuse).
    protected static ref array<string> s_SplitScratch;

    //------------------------------------------------------------------------------------------------
    static string NormalizeMaterialKey(string raw)
    {
        if (!raw || raw == "")
            return "";

        if (!s_SplitScratch)
            s_SplitScratch = new array<string>();
        s_SplitScratch.Clear();
        raw.Split("}", s_SplitScratch, false);
        if (s_SplitScratch.Count() >= 2)
            return s_SplitScratch[1];
        return raw;
    }

    //------------------------------------------------------------------------------------------------
    static string GetMaterialBasename(string path)
    {
        if (!path || path == "")
            return "";

        if (!s_SplitScratch)
            s_SplitScratch = new array<string>();
        s_SplitScratch.Clear();
        path.Split("/", s_SplitScratch, false);
        int n = s_SplitScratch.Count();
        if (n < 1)
            return "";
        return s_SplitScratch[n - 1];
    }

    //------------------------------------------------------------------------------------------------
    static string StripGamematSuffix(string basename)
    {
        if (!basename || basename == "")
            return "";

        string s = basename;
        s.ToLower();
        if (!s_SplitScratch)
            s_SplitScratch = new array<string>();
        s_SplitScratch.Clear();
        s.Split(".gamemat", s_SplitScratch, false);
        if (s_SplitScratch.Count() < 1)
            return "";
        return s_SplitScratch[0];
    }

    //------------------------------------------------------------------------------------------------
    static string GetGenericLabelFromBasename(string basename)
    {
        string stem = StripGamematSuffix(basename);
        if (!stem || stem == "")
            return "";

        // StripGamematSuffix above already extracted its result string, so
        // reusing the shared scratch here is safe.
        s_SplitScratch.Clear();
        stem.Split("_", s_SplitScratch, false);
        if (s_SplitScratch.Count() < 1)
            return "";
        return s_SplitScratch[0];
    }

    //------------------------------------------------------------------------------------------------
    static string GetGenericLabel(GameMaterial material)
    {
        if (!material)
            return "";

        string norm = NormalizeMaterialKey(material.ToString());
        string bn = GetMaterialBasename(norm);
        return GetGenericLabelFromBasename(bn);
    }

    //------------------------------------------------------------------------------------------------
    static void EnsureInit()
    {
        if (s_bInitialized)
            return;

        s_bInitialized = true;
        s_DensityByBasename = new map<string, float>();

        // Terrain / vegetation / water (radar-critical)
        s_DensityByBasename.Insert("default.gamemat", 0.5);
        s_DensityByBasename.Insert("vegetation_base.gamemat", 0.5);
        s_DensityByBasename.Insert("grass.gamemat", 0.5);
        s_DensityByBasename.Insert("grass_dry.gamemat", 0.3);
        s_DensityByBasename.Insert("grass_dry_tall.gamemat", 0.3);
        s_DensityByBasename.Insert("grass_dry_low_density.gamemat", 0.02);
        s_DensityByBasename.Insert("grass_lush.gamemat", 1.2);
        s_DensityByBasename.Insert("grass_lush_short.gamemat", 1.2);
        s_DensityByBasename.Insert("grass_lush_tall.gamemat", 1.2);
        s_DensityByBasename.Insert("grass_heather.gamemat", 0.5);
        s_DensityByBasename.Insert("grass_crops.gamemat", 1.4);
        s_DensityByBasename.Insert("grass_crops_cut.gamemat", 1.4);
        s_DensityByBasename.Insert("grass_crops_corn.gamemat", 1.6);
        s_DensityByBasename.Insert("foliage.gamemat", 0.7);
        s_DensityByBasename.Insert("foliage_1mm.gamemat", 0.7);
        s_DensityByBasename.Insert("foliage_conifer.gamemat", 0.7);
        s_DensityByBasename.Insert("foliage_fallen_leaves.gamemat", 0.7);
        s_DensityByBasename.Insert("moss.gamemat", 1.0);
        s_DensityByBasename.Insert("seaweed.gamemat", 0.75);
        s_DensityByBasename.Insert("dirt.gamemat", 1.33);
        s_DensityByBasename.Insert("dirt_road.gamemat", 1.33);
        s_DensityByBasename.Insert("soil.gamemat", 1.33);
        s_DensityByBasename.Insert("soil_forest.gamemat", 1.35);
        s_DensityByBasename.Insert("soil_forest_deciduous.gamemat", 1.28);
        s_DensityByBasename.Insert("soil_forest_coniferous.gamemat", 1.44);
        s_DensityByBasename.Insert("sand.gamemat", 1.63);
        s_DensityByBasename.Insert("sand_beach_grass.gamemat", 1.63);
        s_DensityByBasename.Insert("gravel.gamemat", 1.682);
        s_DensityByBasename.Insert("pebbles.gamemat", 1.79);
        s_DensityByBasename.Insert("pebbles_01.gamemat", 1.7);
        s_DensityByBasename.Insert("pebbles_02.gamemat", 1.79);
        s_DensityByBasename.Insert("pebbles_03.gamemat", 1.79);
        s_DensityByBasename.Insert("debris_rock.gamemat", 1.682);
        s_DensityByBasename.Insert("loose_base.gamemat", 1.6);
        s_DensityByBasename.Insert("asphalt.gamemat", 2.243);
        s_DensityByBasename.Insert("asphalt_roof.gamemat", 2.243);
        s_DensityByBasename.Insert("asphalt_rugged.gamemat", 2.243);
        s_DensityByBasename.Insert("concrete.gamemat", 2.3);
        s_DensityByBasename.Insert("concrete_100mm.gamemat", 2.3);
        s_DensityByBasename.Insert("concrete_100mm_min.gamemat", 2.3);
        s_DensityByBasename.Insert("stone.gamemat", 2.75);
        s_DensityByBasename.Insert("cobblestone.gamemat", 2.75);
        s_DensityByBasename.Insert("hard_base.gamemat", 2.75);
        s_DensityByBasename.Insert("brick.gamemat", 1.922);
        s_DensityByBasename.Insert("tiles.gamemat", 2.94);
        s_DensityByBasename.Insert("tiles_roof.gamemat", 2.94);
        s_DensityByBasename.Insert("tiles_ceramic.gamemat", 2.94);
        s_DensityByBasename.Insert("tiles_stone.gamemat", 2.94);
        s_DensityByBasename.Insert("water.gamemat", 1.0);
        s_DensityByBasename.Insert("water_base.gamemat", 1.0);
        s_DensityByBasename.Insert("snow.gamemat", 0.35);
        s_DensityByBasename.Insert("ice.gamemat", 0.93);
        s_DensityByBasename.Insert("frost_base.gamemat", 0.917);

        // Wood / trees
        s_DensityByBasename.Insert("wood.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_base.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_floor.gamemat", 0.85);
        s_DensityByBasename.Insert("wood_logs.gamemat", 0.7);
        s_DensityByBasename.Insert("wood_5mm.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_10mm.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_15mm.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_30mm.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_30mm_min.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_50mm.gamemat", 0.65);
        s_DensityByBasename.Insert("wood_50mm_min.gamemat", 0.65);
        s_DensityByBasename.Insert("tree_base.gamemat", 0.65);
        s_DensityByBasename.Insert("tree_picea_abies.gamemat", 0.42);
        s_DensityByBasename.Insert("tree_carpinus_betulus.gamemat", 0.76);
        s_DensityByBasename.Insert("tree_pinus_sylvestris.gamemat", 0.52);
        s_DensityByBasename.Insert("tree_alnus_glutinosa.gamemat", 0.43);
        s_DensityByBasename.Insert("tree_sorbus_aucuparia.gamemat", 0.68);
        s_DensityByBasename.Insert("tree_prunus_domestica.gamemat", 0.7);
        s_DensityByBasename.Insert("tree_tilia_cordata.gamemat", 0.48);
        s_DensityByBasename.Insert("tree_larix_decidua.gamemat", 0.59);
        s_DensityByBasename.Insert("tree_salix_fragilis.gamemat", 0.4);
        s_DensityByBasename.Insert("tree_betula_pendula.gamemat", 0.65);
        s_DensityByBasename.Insert("tree_malus_domestica.gamemat", 0.65);
        s_DensityByBasename.Insert("tree_populus_nigra.gamemat", 0.42);

        // Metal / glass / fabric (structures, vehicles)
        s_DensityByBasename.Insert("metal.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_base.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_1mm.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_1_5mm.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_1.6mm.gamemat", 7.85);
        s_DensityByBasename.Insert("metal_2mm.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_3mm.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_3mm_min.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_3mm_clamp.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_5mm.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_5mm_min.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_5mm_clamp.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_7mm.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_10mm_min.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_sheet.gamemat", 2.7);
        s_DensityByBasename.Insert("metal_grid.gamemat", 7.8);
        s_DensityByBasename.Insert("metal_wire_mesh.gamemat", 0.001);
        s_DensityByBasename.Insert("metal_interior.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_accessory.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_higherAIcost.gamemat", 7.87);
        s_DensityByBasename.Insert("metal_AINullArea.gamemat", 7.87);
        s_DensityByBasename.Insert("armor.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_1mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_3mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_5mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_5mm_min.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_5mm_max.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_7mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_7mm_clamp.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_9mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_10mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_10mm_clamp.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_14mm_clamp.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_20mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_25mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_25mm_min.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_25mm_max.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_30mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_40mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_80mm.gamemat", 7.86);
        s_DensityByBasename.Insert("armor_100mm.gamemat", 7.86);
        s_DensityByBasename.Insert("iron_cast.gamemat", 7.87);
        s_DensityByBasename.Insert("titanium_base.gamemat", 4.51);
        s_DensityByBasename.Insert("titanium_1.25mm.gamemat", 4.51);
        s_DensityByBasename.Insert("titanium_6.5mm.gamemat", 4.51);
        s_DensityByBasename.Insert("titanium_1.25_soft_aramid_5mm_6b2.gamemat", 4.51);
        s_DensityByBasename.Insert("manganese_steel_1.12mm.gamemat", 7.75);
        s_DensityByBasename.Insert("engine.gamemat", 7.87);
        s_DensityByBasename.Insert("fuel_tank.gamemat", 7.87);
        s_DensityByBasename.Insert("differential.gamemat", 7.87);
        s_DensityByBasename.Insert("drive_shaft.gamemat", 7.87);
        s_DensityByBasename.Insert("skids.gamemat", 7.87);
        s_DensityByBasename.Insert("vehicle_battery.gamemat", 7.87);
        s_DensityByBasename.Insert("vehicle_body.gamemat", 7.87);
        s_DensityByBasename.Insert("vehicle_body_bottom_slide.gamemat", 7.87);
        s_DensityByBasename.Insert("weapon_metal.gamemat", 7.87);
        s_DensityByBasename.Insert("gadget_metal.gamemat", 7.87);
        s_DensityByBasename.Insert("shovel_metal.gamemat", 7.87);
        s_DensityByBasename.Insert("glass.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_tempered.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_tempered_4mm.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_2mm_min.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_4mm.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_4mm_min.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_laminated.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_laminated_4mm.gamemat", 2.6);
        s_DensityByBasename.Insert("glass_armored.gamemat", 5.7);
        s_DensityByBasename.Insert("glass_armored_10mm.gamemat", 5.7);
        s_DensityByBasename.Insert("glass_armored_15mm.gamemat", 5.7);
        s_DensityByBasename.Insert("plexiglass.gamemat", 2.6);
        s_DensityByBasename.Insert("plastic.gamemat", 0.92);
        s_DensityByBasename.Insert("plastic_3mm.gamemat", 0.92);
        s_DensityByBasename.Insert("plastic_6mm.gamemat", 0.92);
        s_DensityByBasename.Insert("plastic_10mm_min.gamemat", 0.92);
        s_DensityByBasename.Insert("plastic_15mm_min.gamemat", 0.92);
        s_DensityByBasename.Insert("weapon_plastic.gamemat", 0.92);
        s_DensityByBasename.Insert("gadget_plastic.gamemat", 1.35);
        s_DensityByBasename.Insert("rubber.gamemat", 1.522);
        s_DensityByBasename.Insert("rubber_tire.gamemat", 1.522);
        s_DensityByBasename.Insert("rubber_tire_4mm_min.gamemat", 1.522);
        s_DensityByBasename.Insert("rubber_tire_8mm_min.gamemat", 1.522);
        s_DensityByBasename.Insert("fabric.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_0_5mm.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_1mm.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_2mm_min.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_3mm.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_3mm_min.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_6mm_min.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_military_plastic.gamemat", 1.55);
        s_DensityByBasename.Insert("fabric_military_camonet_higherAIcost_3mm_min.gamemat", 1.55);
        s_DensityByBasename.Insert("carpet.gamemat", 1.13);
        s_DensityByBasename.Insert("linoleum.gamemat", 1.35);
        s_DensityByBasename.Insert("paper.gamemat", 1.2);
        s_DensityByBasename.Insert("paper_5mm.gamemat", 1.2);
        s_DensityByBasename.Insert("paper_10mm.gamemat", 1.2);
        s_DensityByBasename.Insert("paper_15mm.gamemat", 1.2);
        s_DensityByBasename.Insert("flesh.gamemat", 1.027);
        s_DensityByBasename.Insert("flesh_base.gamemat", 1.027);
        s_DensityByBasename.Insert("flesh_4mm_min.gamemat", 1.027);
        s_DensityByBasename.Insert("soft_aramid_base.gamemat", 1.43);
        s_DensityByBasename.Insert("soft_aramid_4mm.gamemat", 1.43);
        s_DensityByBasename.Insert("soft_aramid_5mm.gamemat", 1.43);
        s_DensityByBasename.Insert("soft_aramid_6.5mm.gamemat", 1.43);
        s_DensityByBasename.Insert("soft_aramid_8mm.gamemat", 1.43);
        s_DensityByBasename.Insert("hard_aramid_base.gamemat", 1.65);
        s_DensityByBasename.Insert("hard_aramid_4_57mm.gamemat", 1.65);
        s_DensityByBasename.Insert("hard_aramid_7.3mm.gamemat", 1.65);
        s_DensityByBasename.Insert("resin_fiberglass_2_5mm.gamemat", 1.1);
        s_DensityByBasename.Insert("artificial_base.gamemat", 1.1);
        s_DensityByBasename.Insert("weapon_wood.gamemat", 0.65);
    }

    //------------------------------------------------------------------------------------------------
    static float ResolveDensity(GameMaterial material, float ballisticDensity)
    {
        EnsureInit();
        if (!material)
            return ballisticDensity;

        string norm = NormalizeMaterialKey(material.ToString());
        string bn = GetMaterialBasename(norm);
        if (bn != "" && s_DensityByBasename.Contains(bn))
            return s_DensityByBasename.Get(bn);
        return ballisticDensity;
    }

    //------------------------------------------------------------------------------------------------
    static int ClassifySurface(string basename, float density, int waterType)
    {
        if (waterType != 0)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER;

        string label = GetGenericLabelFromBasename(basename);
        if (label != "")
        {
            if (label == "water" || label == "seaweed")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER;
            if (label == "grass" || label == "foliage" || label == "vegetation" || label == "moss")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_VEGETATION;
            if (label == "dirt" || label == "soil" || label == "loose")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_SOIL;
            if (label == "sand")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_SAND;
            if (label == "gravel" || label == "pebbles" || label == "debris")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_GRAVEL;
            if (label == "asphalt")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_ASPHALT;
            if (label == "concrete" || label == "stone" || label == "cobblestone" || label == "brick" || label == "tiles")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_HARD;
            // hard_base (~2.75) vs hard_aramid (~1.65)
            if (label == "hard")
            {
                if (density > 2.0)
                    return ERDF_DemSurfaceClass.RDF_DEM_SURF_HARD;
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_FABRIC;
            }
            if (label == "wood" || label == "tree")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_WOOD;
            if (label == "metal" || label == "armor" || label == "iron" || label == "titanium" || label == "engine" || label == "vehicle" || label == "weapon" || label == "gadget" || label == "skids" || label == "fuel" || label == "drive" || label == "differential" || label == "manganese" || label == "shovel")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_METAL;
            if (label == "snow" || label == "ice" || label == "frost")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_SNOW_ICE;
            if (label == "fabric" || label == "carpet" || label == "soft")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_FABRIC;
            if (label == "glass" || label == "plexiglass")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_HARD;
            if (label == "plastic" || label == "rubber" || label == "paper" || label == "linoleum" || label == "resin" || label == "artificial")
                return ERDF_DemSurfaceClass.RDF_DEM_SURF_FABRIC;
        }

        // Density fallback bands (aligned with RSS terrain mapping)
        if (density <= 0.0)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        if (density < 0.4)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_VEGETATION;
        if (density < 0.9)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_WOOD;
        if (density < 1.2)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_VEGETATION;
        if (density < 1.5)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_SOIL;
        if (density < 1.75)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_SAND;
        if (density < 2.1)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_GRAVEL;
        if (density < 2.5)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_ASPHALT;
        if (density < 4.0)
            return ERDF_DemSurfaceClass.RDF_DEM_SURF_HARD;
        return ERDF_DemSurfaceClass.RDF_DEM_SURF_METAL;
    }

    //------------------------------------------------------------------------------------------------
    // Sample material density / surface class.
    // enableTrace=false skips TraceMove (required for bake: TraceMove on seabed
    // cells with WST_NONE causes native heap corruption 0xc0000374).
    static void SampleAt(
        notnull World world,
        float cx,
        float cz,
        float terrainY,
        int waterType,
        bool enableTrace,
        out float outDensity,
        out int outSurfaceClass,
        out string outBasename)
    {
        outDensity = 0.5;
        outSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        outBasename = "";

        if (waterType != 0)
        {
            outDensity = 1.0;
            outSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER;
            outBasename = "water.gamemat";
            return;
        }

        float oceanBaseY = world.GetOceanBaseHeight();
        if (terrainY < oceanBaseY - 0.5)
        {
            outDensity = 1.0;
            outSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER;
            outBasename = "water.gamemat";
            return;
        }

        if (!enableTrace)
        {
            outDensity = 0.5;
            outSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_SOIL;
            outBasename = "soil.gamemat";
            return;
        }

        if (!s_TraceParam)
            s_TraceParam = new TraceParam();

        s_TraceParam.Start = Vector(cx, terrainY + 2.0, cz);
        s_TraceParam.End = Vector(cx, terrainY - 1.0, cz);
        s_TraceParam.Flags = TraceFlags.WORLD;
        s_TraceParam.LayerMask = EPhysicsLayerPresets.Projectile;
        s_TraceParam.Exclude = null;
        s_TraceParam.TraceEnt = null;
        s_TraceParam.SurfaceProps = null;
        // Do not write ColliderName (engine-owned buffer on TraceParam).

        world.TraceMove(s_TraceParam, null);

        GameMaterial material = s_TraceParam.SurfaceProps;
        if (!material)
            return;

        string norm = NormalizeMaterialKey(material.ToString());
        outBasename = GetMaterialBasename(norm);

        float ballistic = 0.5;
        BallisticInfo bi = material.GetBallisticInfo();
        if (bi)
        {
            ballistic = bi.GetDensity();
            if (bi.IsWaterSurface())
            {
                outDensity = ResolveDensity(material, 1.0);
                outSurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER;
                return;
            }
        }

        outDensity = ResolveDensity(material, ballistic);
        outSurfaceClass = ClassifySurface(outBasename, outDensity, waterType);
    }
}
