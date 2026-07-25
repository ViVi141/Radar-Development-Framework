// RDF DEM tile bake: scan world heightfield into CSV tiles (ANNA-style).
// Writes $profile:RDF/DemData/<worldKey>/tiles/tile_ix_iz.csv
// Trigger: create $profile:RDF/BakeDemFull.flag then keep Workbench Play running.
class RDF_DemTileBake
{
    protected static bool s_Active;
    protected static string s_WorldKey;
    protected static string s_OutputDir;
    protected static vector s_BoundsMin;
    protected static vector s_BoundsMax;
    protected static float s_CellM;
    protected static int s_TileCells;
    protected static int s_TileCountX;
    protected static int s_TileCountZ;
    protected static int s_NextTileX;
    protected static int s_NextTileZ;
    protected static int s_TilesWritten;
    protected static int s_TilesSkipped;
    protected static float s_AccumS;
    protected static int s_TileWriteFails;
    protected static int s_LastProgressPct;
    protected static bool s_QueryHit;
    protected static int s_QueryHits;

    //------------------------------------------------------------------------------------------------
    static bool IsBakeRequested()
    {
        return FileIO.FileExists(RDF_DemBakeConstants.BAKE_FLAG_FILE);
    }

    //------------------------------------------------------------------------------------------------
    static void ClearBakeRequest()
    {
        if (FileIO.FileExists(RDF_DemBakeConstants.BAKE_FLAG_FILE))
            FileIO.DeleteFile(RDF_DemBakeConstants.BAKE_FLAG_FILE);
    }

    //------------------------------------------------------------------------------------------------
    static void WriteBakeRequest()
    {
        string flagDir = RDF_DemBakeConstants.PROFILE_ROOT;
        if (!FileIO.FileExists(flagDir))
            FileIO.MakeDirectory(flagDir);

        FileHandle handle = FileIO.OpenFile(RDF_DemBakeConstants.BAKE_FLAG_FILE, FileMode.WRITE);
        if (!handle)
            return;

        handle.WriteLine("1");
        handle.Close();
    }

    //------------------------------------------------------------------------------------------------
    static bool IsBaking()
    {
        return s_Active;
    }

    //------------------------------------------------------------------------------------------------
    static float GetProgress01()
    {
        if (!s_Active)
            return 0;

        int total = s_TileCountX * s_TileCountZ;
        if (total <= 0)
            return 0;

        return (s_TilesWritten + s_TilesSkipped) / (float)total;
    }

    //------------------------------------------------------------------------------------------------
    static void TryStartBake()
    {
        if (!IsBakeRequested())
            return;

        World world = GetGame().GetWorld();
        if (!world)
            return;

        string worldKey = RDF_DemWorldKey.FromPath(GetGame().GetWorldFile());
        if (worldKey.IsEmpty())
        {
            Print("[RDF DEM Bake] Could not resolve world key.", LogLevel.ERROR);
            return;
        }

        if (s_Active)
        {
            if (s_WorldKey == worldKey)
            {
                EnsureOutputDirs(worldKey);
                return;
            }

            s_Active = false;
        }

        vector boundsMin;
        vector boundsMax;
        if (!TryGetWorldBoundsXZ(boundsMin, boundsMax))
        {
            Print("[RDF DEM Bake] Could not resolve world bounds.", LogLevel.ERROR);
            return;
        }

        if (!EnsureOutputDirs(worldKey))
        {
            Print("[RDF DEM Bake] Could not create output directories under $profile:RDF/DemData/.", LogLevel.ERROR);
            return;
        }

        s_WorldKey = worldKey;
        s_BoundsMin = boundsMin;
        s_BoundsMax = boundsMax;
        s_CellM = RDF_DemBakeConstants.CELL_M;
        s_TileCells = RDF_DemBakeConstants.TILE_CELLS;
        s_OutputDir = RDF_DemBakeConstants.DEM_DATA_DIR + worldKey + "/tiles/";

        float spanX = boundsMax[0] - boundsMin[0];
        float spanZ = boundsMax[2] - boundsMin[2];
        float tileSpanM = s_CellM * s_TileCells;

        s_TileCountX = Math.Ceil(spanX / tileSpanM);
        s_TileCountZ = Math.Ceil(spanZ / tileSpanM);
        if (s_TileCountX < 1)
            s_TileCountX = 1;
        if (s_TileCountZ < 1)
            s_TileCountZ = 1;

        s_NextTileX = 0;
        s_NextTileZ = 0;
        s_TilesWritten = 0;
        s_TilesSkipped = 0;
        s_AccumS = 0;
        s_TileWriteFails = 0;
        s_LastProgressPct = -1;
        s_Active = true;

        Print(string.Format(
            "[RDF DEM Bake] Started world=%1 tiles=%2x%3 cell=%4m out=%5",
            worldKey,
            s_TileCountX.ToString(),
            s_TileCountZ.ToString(),
            s_CellM.ToString(),
            s_OutputDir), LogLevel.NORMAL);
        Print("[RDF DEM Bake] Keep Workbench Play running; watch Script log for progress.", LogLevel.NORMAL);
    }

    //------------------------------------------------------------------------------------------------
    static void OnFrame(float timeSlice)
    {
        if (!IsBakeRequested() && !s_Active)
            return;

        if (!s_Active)
            TryStartBake();

        if (!s_Active)
            return;

        if (!FileIO.FileExists(s_OutputDir))
            EnsureOutputDirs(s_WorldKey);

        s_AccumS = s_AccumS + timeSlice;
        if (s_AccumS < RDF_DemBakeConstants.TILE_INTERVAL_S)
            return;

        s_AccumS = 0;

        int budget = RDF_DemBakeConstants.TILES_PER_FRAME;
        if (budget < 1)
            budget = 1;

        for (int i = 0; i < budget; i++)
        {
            if (!s_Active)
                return;

            BakeNextTile();
        }
    }

    //------------------------------------------------------------------------------------------------
    protected static void BakeNextTile()
    {
        if (s_NextTileX >= s_TileCountX)
        {
            FinishBake();
            return;
        }

        World world = GetGame().GetWorld();
        if (!world)
            return;

        string fileName = string.Format("tile_%1_%2.csv", s_NextTileX.ToString(), s_NextTileZ.ToString());
        string filePath = s_OutputDir + fileName;

        if (FileIO.FileExists(filePath))
        {
            s_TilesSkipped = s_TilesSkipped + 1;
            s_TileWriteFails = 0;
            AdvanceTileCursor();
            MaybeLogProgress();
            return;
        }

        Print(string.Format(
            "[RDF DEM Bake] baking tile_%1_%2 ...",
            s_NextTileX.ToString(),
            s_NextTileZ.ToString()), LogLevel.NORMAL);

        float tileSpanM = s_CellM * s_TileCells;
        float originX = s_BoundsMin[0] + s_NextTileX * tileSpanM;
        float originZ = s_BoundsMin[2] + s_NextTileZ * tileSpanM;

        // Absolute cell origin for stitching (matches ANNA origin_ix/origin_iz convention).
        int originIx = Math.Round(s_NextTileX * s_TileCells);
        int originIz = Math.Round(s_NextTileZ * s_TileCells);

        array<string> lines = {};
        lines.Insert("RDF_DEM_TILE_V3");
        lines.Insert("world " + s_WorldKey);
        lines.Insert("tile_ix " + s_NextTileX.ToString());
        lines.Insert("tile_iz " + s_NextTileZ.ToString());
        lines.Insert("origin_x " + originX.ToString());
        lines.Insert("origin_z " + originZ.ToString());
        lines.Insert("origin_ix " + originIx.ToString());
        lines.Insert("origin_iz " + originIz.ToString());
        lines.Insert("cell_m " + s_CellM.ToString());
        lines.Insert("size " + s_TileCells.ToString());
        lines.Insert("max_spans " + RDF_DemBakeConstants.MAX_SPANS.ToString());
        lines.Insert("cols local_ix local_iz terrain_y slope_deg water_depth_m water_type occ_ground entity_hits density_gcm3 surface_class n_spans s0_lo s0_hi s1_lo s1_hi s2_lo s2_hi s3_lo s3_hi s4_lo s4_hi s5_lo s5_hi s6_lo s6_hi s7_lo s7_hi");

        array<float> spanLo = new array<float>();
        array<float> spanHi = new array<float>();

        for (int localIz = 0; localIz < s_TileCells; localIz++)
        {
            for (int localIx = 0; localIx < s_TileCells; localIx++)
            {
                float cx = originX + (localIx + 0.5) * s_CellM;
                float cz = originZ + (localIz + 0.5) * s_CellM;

                if (cx > s_BoundsMax[0] || cz > s_BoundsMax[2])
                    continue;

                float seabedY = world.GetSurfaceY(cx, cz);
                float oceanBaseY = world.GetOceanBaseHeight();

                EWaterSurfaceType waterType;
                float lakeArea;
                float waterSurfaceY = SCR_WorldTools.GetWaterSurfaceY(
                    world, Vector(cx, Math.Max(seabedY, oceanBaseY), cz), waterType, lakeArea);
                float waterDepthM = 0;
                int waterTypeInt = (int)waterType;
                if (waterType != EWaterSurfaceType.WST_NONE)
                {
                    if (seabedY < waterSurfaceY - 0.05)
                        waterDepthM = waterSurfaceY - seabedY;
                }

                // Treat as deep water when:
                // 1) classified water with depth >= threshold, or
                // 2) seabed is clearly below ocean base (GetWaterSurfaceY sometimes
                //    returns WST_NONE offshore — TraceMove there heap-corrupts).
                bool belowOcean = seabedY < (oceanBaseY - 0.5);
                bool deepWater = belowOcean;
                if (!deepWater)
                {
                    if (waterType != EWaterSurfaceType.WST_NONE
                        && waterDepthM >= RDF_DemBakeConstants.DEEP_WATER_SURFACE_ONLY_M)
                    {
                        deepWater = true;
                    }
                }

                if (deepWater)
                {
                    float surfaceY = waterSurfaceY;
                    if (waterType == EWaterSurfaceType.WST_NONE)
                        surfaceY = oceanBaseY;
                    if (waterDepthM <= 0.0)
                        waterDepthM = oceanBaseY - seabedY;
                    if (waterTypeInt == 0)
                        waterTypeInt = (int)EWaterSurfaceType.WST_OCEAN;

                    lines.Insert(BuildSeaSurfaceRow(
                        localIx, localIz, surfaceY, waterDepthM, waterTypeInt));
                    continue;
                }

                float terrainY = seabedY;
                float slopeDeg = EstimateSlopeDeg(world, cx, cz, terrainY, s_CellM);

                int entityHits = 0;
                int occGround = 0;
                bool queryColumnEntities = RDF_DemBakeConstants.ENABLE_COLUMN_ENTITY_QUERY;
                int spanCount = RDF_DemColumnSpans.SampleColumn(
                    world,
                    cx,
                    cz,
                    terrainY,
                    s_CellM,
                    queryColumnEntities,
                    spanLo,
                    spanHi,
                    entityHits,
                    occGround);

                float density = 0.5;
                int surfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
                string materialBasename;
                // enableTrace=true only on dry land; underwater TraceMove crashes (0xc0000374).
                RDF_DemMaterialTable.SampleAt(
                    world,
                    cx,
                    cz,
                    terrainY,
                    waterTypeInt,
                    true,
                    density,
                    surfaceClass,
                    materialBasename);

                string spanFields = RDF_DemColumnSpans.FormatSpanFields(spanLo, spanHi, spanCount);
                string row = string.Format(
                    "%1 %2 %3 %4 %5 %6 %7 %8",
                    localIx.ToString(),
                    localIz.ToString(),
                    terrainY.ToString(),
                    slopeDeg.ToString(),
                    waterDepthM.ToString(),
                    waterTypeInt.ToString(),
                    occGround.ToString(),
                    entityHits.ToString());
                row = row + " " + density.ToString();
                row = row + " " + surfaceClass.ToString();
                row = row + " " + spanFields;
                lines.Insert(row);
            }
        }

        if (!TryWriteTile(filePath, lines))
        {
            s_TileWriteFails = s_TileWriteFails + 1;
            if (s_TileWriteFails < 3)
                return;

            Print(string.Format(
                "[RDF DEM Bake] Skipping tile_%1_%2 after repeated write failures.",
                s_NextTileX.ToString(),
                s_NextTileZ.ToString()), LogLevel.ERROR);
            s_TileWriteFails = 0;
            AdvanceTileCursor();
            return;
        }

        s_TileWriteFails = 0;
        s_TilesWritten = s_TilesWritten + 1;
        AdvanceTileCursor();
        MaybeLogProgress();
    }

    //------------------------------------------------------------------------------------------------
    protected static bool EnsureOutputDirs(string worldKey)
    {
        string profileRoot = RDF_DemBakeConstants.PROFILE_ROOT;
        if (!FileIO.FileExists(profileRoot))
        {
            if (!FileIO.MakeDirectory(profileRoot))
                return false;
        }

        string demRoot = RDF_DemBakeConstants.DEM_DATA_DIR;
        if (!FileIO.FileExists(demRoot))
        {
            if (!FileIO.MakeDirectory(demRoot))
                return false;
        }

        string worldDir = demRoot + worldKey + "/";
        if (!FileIO.FileExists(worldDir))
        {
            if (!FileIO.MakeDirectory(worldDir))
                return false;
        }

        string tilesDir = worldDir + "tiles/";
        if (!FileIO.FileExists(tilesDir))
        {
            if (!FileIO.MakeDirectory(tilesDir))
                return false;
        }

        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool TryWriteTile(string filePath, notnull array<string> lines)
    {
        if (SCR_FileIOHelper.WriteFileContent(filePath, lines))
            return true;

        EnsureOutputDirs(s_WorldKey);

        if (SCR_FileIOHelper.WriteFileContent(filePath, lines))
            return true;

        Print("[RDF DEM Bake] Failed to write " + filePath, LogLevel.ERROR);
        return false;
    }

    //------------------------------------------------------------------------------------------------
    protected static void MaybeLogProgress()
    {
        int doneCount = s_TilesWritten + s_TilesSkipped;
        int total = s_TileCountX * s_TileCountZ;
        if (total <= 0)
            return;

        int pct = Math.Round(GetProgress01() * 100);
        if (doneCount == 1 || doneCount % 32 == 0 || pct >= s_LastProgressPct + 5)
        {
            s_LastProgressPct = pct;
            Print(string.Format(
                "[RDF DEM Bake] progress=%1% done=%2/%3 written=%4 skipped=%5",
                pct.ToString(),
                doneCount.ToString(),
                total.ToString(),
                s_TilesWritten.ToString(),
                s_TilesSkipped.ToString()), LogLevel.NORMAL);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected static void AdvanceTileCursor()
    {
        s_NextTileZ = s_NextTileZ + 1;
        if (s_NextTileZ >= s_TileCountZ)
        {
            s_NextTileZ = 0;
            s_NextTileX = s_NextTileX + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected static void FinishBake()
    {
        WriteManifest();
        WriteCompleteMarker();
        ClearBakeRequest();
        s_Active = false;

        Print(string.Format(
            "[RDF DEM Bake] Done. world=%1 written=%2 skipped=%3 dir=%4",
            s_WorldKey,
            s_TilesWritten.ToString(),
            s_TilesSkipped.ToString(),
            s_OutputDir), LogLevel.NORMAL);
    }

    //------------------------------------------------------------------------------------------------
    protected static void WriteManifest()
    {
        array<string> lines = {};
        lines.Insert("RDF_DEM_MANIFEST_V3");
        lines.Insert("world " + s_WorldKey);
        lines.Insert("bounds_min_x " + s_BoundsMin[0].ToString());
        lines.Insert("bounds_min_z " + s_BoundsMin[2].ToString());
        lines.Insert("bounds_max_x " + s_BoundsMax[0].ToString());
        lines.Insert("bounds_max_z " + s_BoundsMax[2].ToString());
        lines.Insert("cell_m " + s_CellM.ToString());
        lines.Insert("tile_cells " + s_TileCells.ToString());
        lines.Insert("tile_count_x " + s_TileCountX.ToString());
        lines.Insert("tile_count_z " + s_TileCountZ.ToString());
        lines.Insert("tiles_written " + s_TilesWritten.ToString());
        lines.Insert("tiles_skipped " + s_TilesSkipped.ToString());
        lines.Insert("max_spans " + RDF_DemBakeConstants.MAX_SPANS.ToString());
        lines.Insert("column_y_min_offset " + RDF_DemBakeConstants.COLUMN_Y_MIN_OFFSET.ToString());
        lines.Insert("column_y_max_offset " + RDF_DemBakeConstants.COLUMN_Y_MAX_OFFSET.ToString());
        lines.Insert("channels terrain_y slope_deg water_depth_m water_type occ_ground entity_hits density_gcm3 surface_class n_spans spans");

        string manifestPath = RDF_DemBakeConstants.DEM_DATA_DIR + s_WorldKey + "/manifest.csv";
        SCR_FileIOHelper.WriteFileContent(manifestPath, lines);
    }

    //------------------------------------------------------------------------------------------------
    protected static void WriteCompleteMarker()
    {
        array<string> lines = {};
        lines.Insert("RDF_DEM_BAKE_COMPLETE");
        lines.Insert("world " + s_WorldKey);
        lines.Insert("tiles_written " + s_TilesWritten.ToString());
        lines.Insert("tiles_skipped " + s_TilesSkipped.ToString());

        string path = RDF_DemBakeConstants.DEM_DATA_DIR + s_WorldKey + "/bake_complete.txt";
        SCR_FileIOHelper.WriteFileContent(path, lines);
    }

    //------------------------------------------------------------------------------------------------
    protected static bool HasGroundObstacle(
        notnull World world, float cx, float cz, float terrainY, out int outHits)
    {
        outHits = 0;
        float half = s_CellM * 0.5;
        float yMin = terrainY;
        float yMax = terrainY + RDF_DemBakeConstants.OCC_CLEARANCE_M;

        vector aabbMin = Vector(cx - half, yMin, cz - half);
        vector aabbMax = Vector(cx + half, yMax, cz + half);

        s_QueryHit = false;
        s_QueryHits = 0;
        world.QueryEntitiesByAABB(aabbMin, aabbMax, QueryObstacleCallback, null, EQueryEntitiesFlags.STATIC);

        outHits = s_QueryHits;
        return s_QueryHit;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool QueryObstacleCallback(IEntity entity)
    {
        if (!entity)
            return true;

        s_QueryHit = true;
        s_QueryHits = s_QueryHits + 1;
        return false;
    }

    //------------------------------------------------------------------------------------------------
    // Deep-ocean cell: sea surface only. No seabed slope, material Trace, or entity query.
    protected static string BuildSeaSurfaceRow(
        int localIx, int localIz, float waterSurfaceY, float waterDepthM, int waterTypeInt)
    {
        float lo = waterSurfaceY - 0.05;
        float hi = waterSurfaceY + 0.15;
        int waterClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_WATER;

        string row = string.Format(
            "%1 %2 %3 %4 %5 %6 %7 %8",
            localIx.ToString(),
            localIz.ToString(),
            waterSurfaceY.ToString(),
            "0",
            waterDepthM.ToString(),
            waterTypeInt.ToString(),
            "0",
            "0");
        row = row + " 1 " + waterClass.ToString();
        // n_spans=1, first span = sea surface slab, remaining padded with 0.
        row = row + " 1 " + lo.ToString() + " " + hi.ToString();
        row = row + " 0 0 0 0 0 0 0 0 0 0 0 0 0 0";
        return row;
    }

    //------------------------------------------------------------------------------------------------
    // Finite-difference slope from neighboring GetSurfaceY samples (no TraceParam alloc).
    protected static float EstimateSlopeDeg(
        notnull World world, float cx, float cz, float centerY, float cellM)
    {
        float step = cellM;
        float yxp = world.GetSurfaceY(cx + step, cz);
        float yxm = world.GetSurfaceY(cx - step, cz);
        float yzp = world.GetSurfaceY(cx, cz + step);
        float yzm = world.GetSurfaceY(cx, cz - step);

        float gx = (yxp - yxm) / (2.0 * step);
        float gz = (yzp - yzm) / (2.0 * step);
        float horiz = Math.Sqrt(gx * gx + gz * gz);
        return Math.RAD2DEG * Math.Atan2(horiz, 1.0);
    }

    //------------------------------------------------------------------------------------------------
    protected static float NormalToSlopeDeg(vector normal)
    {
        if (normal == vector.Zero)
            return 0;

        float ny = normal[1];
        if (ny > 1.0)
            ny = 1.0;
        if (ny < -1.0)
            ny = -1.0;

        float horiz = Math.Sqrt(normal[0] * normal[0] + normal[2] * normal[2]);
        return Math.RAD2DEG * Math.Atan2(horiz, Math.AbsFloat(ny));
    }

    //------------------------------------------------------------------------------------------------
    protected static bool TryGetWorldBoundsXZ(out vector outMin, out vector outMax)
    {
        outMin = Vector(-4096, 0, -4096);
        outMax = Vector(4096, 0, 4096);

        IEntity worldEntity = GetGame().GetWorldEntity();
        if (!worldEntity)
            return true;

        vector boundsMin;
        vector boundsMax;
        worldEntity.GetWorldBounds(boundsMin, boundsMax);
        if (boundsMax[0] <= boundsMin[0] || boundsMax[2] <= boundsMin[2])
            return true;

        outMin = Vector(boundsMin[0], 0, boundsMin[2]);
        outMax = Vector(boundsMax[0], 0, boundsMax[2]);
        return true;
    }
}
