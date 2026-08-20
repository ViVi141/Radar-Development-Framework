// Workshop-friendly baked height map from official game .ttile (FORM/TERR HGHT).
// Layout: DemData/<world>/height_manifest.json + height_chunks/row_<iz>.json
// Cell payload: little-endian int16 quanta (y / y_scale), hex-encoded (4 chars/cell).
// Aligns with BaseWorld.GetSurfaceY(x,z) semantics; pack is a snapshot of game terrain.
class RDF_DemHeightManifestDoc : JsonApiStruct
{
    string magic;
    string world;
    float bounds_min_x;
    float bounds_min_z;
    float bounds_max_x;
    float bounds_max_z;
    float cell_m;
    int tile_cells;
    int tile_count_x;
    int tile_count_z;
    float y_scale;
    string chunks_dir;

    void RDF_DemHeightManifestDoc()
    {
        RegV("magic");
        RegV("world");
        RegV("bounds_min_x");
        RegV("bounds_min_z");
        RegV("bounds_max_x");
        RegV("bounds_max_z");
        RegV("cell_m");
        RegV("tile_cells");
        RegV("tile_count_x");
        RegV("tile_count_z");
        RegV("y_scale");
        RegV("chunks_dir");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_DemHeightTileEntry : JsonApiStruct
{
    int ix;
    string hex;

    void RDF_DemHeightTileEntry()
    {
        RegV("ix");
        RegV("hex");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_DemHeightRowDoc : JsonApiStruct
{
    int iz;
    ref array<ref RDF_DemHeightTileEntry> tiles;

    void RDF_DemHeightRowDoc()
    {
        RegV("iz");
        RegV("tiles");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_DemHeightJsonPack
{
    static const string MANIFEST_NAME = "height_manifest.json";
    static const string CHUNKS_DIR_NAME = "height_chunks/";
    static const string MAGIC = "RDF_HEIGHT_JSON_V1";

    protected static string s_CachedRoot;
    protected static int s_CachedIz;
    protected static ref RDF_DemHeightRowDoc s_CachedRow;

    //--------------------------------------------------------------------------------------------
    static string BuildManifestPath(string rootDir)
    {
        return rootDir + MANIFEST_NAME;
    }

    //--------------------------------------------------------------------------------------------
    static string BuildRowPath(string rootDir, string chunksDir, int tileIz)
    {
        string dir = chunksDir;
        if (dir.IsEmpty())
            dir = CHUNKS_DIR_NAME;
        return rootDir + dir + "row_" + tileIz.ToString() + ".json";
    }

    //--------------------------------------------------------------------------------------------
    // Attach height pack metadata onto an already-loaded SURF/CSV manifest (same root).
    static bool TryAttachHeightPack(notnull RDF_DemRuntimeManifest manifest)
    {
        if (!manifest)
            return false;
        if (manifest.m_RootDir.IsEmpty())
            return false;

        string path = BuildManifestPath(manifest.m_RootDir);
        RDF_DemHeightManifestDoc doc = new RDF_DemHeightManifestDoc();
        if (!RDF_DemJsonIo.TryLoad(doc, path))
            return false;
        if (doc.magic != MAGIC)
            return false;
        if (doc.world.IsEmpty())
            return false;
        if (doc.world != manifest.m_WorldKey)
            return false;
        if (doc.cell_m <= 0.0 || doc.tile_cells < 1)
            return false;
        if (doc.tile_count_x != manifest.m_TileCountX)
            return false;
        if (doc.tile_count_z != manifest.m_TileCountZ)
            return false;
        if (doc.tile_cells != manifest.m_TileCells)
            return false;
        if (Math.AbsFloat(doc.cell_m - manifest.m_CellM) > 0.001)
            return false;

        float yScale = doc.y_scale;
        if (yScale <= 0.0)
            yScale = 0.1;

        string chunks = doc.chunks_dir;
        if (chunks.IsEmpty())
            chunks = CHUNKS_DIR_NAME;

        manifest.m_HasHeightPack = true;
        manifest.m_HeightYScale = yScale;
        manifest.m_HeightChunksDir = manifest.m_RootDir + chunks;
        // Prefer baked Y when pack is present (still fall back to GetSurfaceY on miss).
        if (RDF_DemBakeConstants.RUNTIME_DEM_PREFER_BAKED_HEIGHT)
            manifest.m_PreferLiveTerrainY = false;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    static bool BeginWorldHeightGrid(
        notnull RDF_DemRuntimeManifest manifest,
        out array<float> outWorldY,
        out int outCellsX,
        out int outCellsZ)
    {
        outWorldY = null;
        outCellsX = 0;
        outCellsZ = 0;
        if (!manifest.m_HasHeightPack)
            return false;
        if (manifest.m_TileCountX <= 0 || manifest.m_TileCountZ <= 0)
            return false;
        if (manifest.m_TileCells <= 0)
            return false;

        int cellsX = manifest.m_TileCountX * manifest.m_TileCells;
        int cellsZ = manifest.m_TileCountZ * manifest.m_TileCells;
        int cellCount = cellsX * cellsZ;
        if (cellCount <= 0)
            return false;

        array<float> worldY = new array<float>();
        worldY.Resize(cellCount);
        for (int i = 0; i < cellCount; i++)
            worldY.Set(i, 0.0);

        outWorldY = worldY;
        outCellsX = cellsX;
        outCellsZ = cellsZ;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    static bool PreloadWorldHeights(
        notnull RDF_DemRuntimeManifest manifest,
        out array<float> outWorldY,
        out int outCellsX,
        out int outCellsZ,
        out int outTilesLoaded,
        out int outTilesFailed)
    {
        outWorldY = null;
        outCellsX = 0;
        outCellsZ = 0;
        outTilesLoaded = 0;
        outTilesFailed = 0;

        array<float> worldY;
        int cellsX;
        int cellsZ;
        if (!BeginWorldHeightGrid(manifest, worldY, cellsX, cellsZ))
            return false;

        int loaded = 0;
        int failed = 0;
        for (int tileIz = 0; tileIz < manifest.m_TileCountZ; tileIz++)
        {
            int rowLoaded;
            int rowFailed;
            AppendHeightRow(manifest, worldY, cellsX, tileIz, rowLoaded, rowFailed);
            loaded = loaded + rowLoaded;
            failed = failed + rowFailed;
        }

        ClearRowCache();
        outWorldY = worldY;
        outCellsX = cellsX;
        outCellsZ = cellsZ;
        outTilesLoaded = loaded;
        outTilesFailed = failed;
        return loaded > 0;
    }

    //--------------------------------------------------------------------------------------------
    static void AppendHeightRow(
        notnull RDF_DemRuntimeManifest manifest,
        notnull array<float> worldY,
        int cellsX,
        int tileIz,
        out int outTilesLoaded,
        out int outTilesFailed)
    {
        outTilesLoaded = 0;
        outTilesFailed = 0;
        if (tileIz < 0 || tileIz >= manifest.m_TileCountZ)
            return;

        RDF_DemHeightRowDoc row = LoadRowCached(manifest, tileIz);
        if (!row || !row.tiles)
        {
            outTilesFailed = manifest.m_TileCountX;
            return;
        }

        int loaded = 0;
        int failed = 0;
        for (int t = 0; t < row.tiles.Count(); t++)
        {
            int oneLoaded;
            int oneFailed;
            AppendHeightTileEntry(manifest, worldY, cellsX, tileIz, t, oneLoaded, oneFailed);
            loaded = loaded + oneLoaded;
            failed = failed + oneFailed;
        }
        outTilesLoaded = loaded;
        outTilesFailed = failed;
    }

    //--------------------------------------------------------------------------------------------
    static void AppendHeightTileEntry(
        notnull RDF_DemRuntimeManifest manifest,
        notnull array<float> worldY,
        int cellsX,
        int tileIz,
        int entryIndex,
        out int outTilesLoaded,
        out int outTilesFailed)
    {
        outTilesLoaded = 0;
        outTilesFailed = 0;
        RDF_DemHeightRowDoc row = LoadRowCached(manifest, tileIz);
        if (!row || !row.tiles)
        {
            outTilesFailed = 1;
            return;
        }
        if (entryIndex < 0 || entryIndex >= row.tiles.Count())
            return;

        RDF_DemHeightTileEntry entry = row.tiles.Get(entryIndex);
        if (!entry)
        {
            outTilesFailed = 1;
            return;
        }

        int size = manifest.m_TileCells;
        int expectedHex = size * size * 4;
        int tileIx = entry.ix;
        if (tileIx < 0 || tileIx >= manifest.m_TileCountX)
        {
            outTilesFailed = 1;
            return;
        }

        string hex = entry.hex;
        if (hex.Length() != expectedHex)
        {
            outTilesFailed = 1;
            return;
        }

        array<int> bytes = new array<int>();
        if (!HexDecode(hex, bytes))
        {
            outTilesFailed = 1;
            return;
        }
        if (bytes.Count() < size * size * 2)
        {
            outTilesFailed = 1;
            return;
        }

        float yScale = manifest.m_HeightYScale;
        if (yScale <= 0.0)
            yScale = 0.1;

        int originIx = tileIx * size;
        int originIz = tileIz * size;
        for (int localIz = 0; localIz < size; localIz++)
        {
            int globalIz = originIz + localIz;
            int rowBase = globalIz * cellsX + originIx;
            int localBase = localIz * size * 2;
            for (int localIx = 0; localIx < size; localIx++)
            {
                int bo = localBase + localIx * 2;
                int lo = bytes.Get(bo);
                int hi = bytes.Get(bo + 1);
                int q = lo | (hi << 8);
                if (q >= 32768)
                    q = q - 65536;
                worldY.Set(rowBase + localIx, q * yScale);
            }
        }
        outTilesLoaded = 1;
    }

    //--------------------------------------------------------------------------------------------
    static int GetHeightRowEntryCount(notnull RDF_DemRuntimeManifest manifest, int tileIz)
    {
        RDF_DemHeightRowDoc row = LoadRowCached(manifest, tileIz);
        if (!row || !row.tiles)
            return 0;
        return row.tiles.Count();
    }

    //--------------------------------------------------------------------------------------------
    static void ClearRowCache()
    {
        s_CachedRoot = string.Empty;
        s_CachedIz = -1;
        s_CachedRow = null;
    }

    //--------------------------------------------------------------------------------------------
    protected static RDF_DemHeightRowDoc LoadRowCached(
        notnull RDF_DemRuntimeManifest manifest,
        int tileIz)
    {
        if (s_CachedRow && s_CachedRoot == manifest.m_RootDir && s_CachedIz == tileIz)
            return s_CachedRow;

        string chunksDir = manifest.m_HeightChunksDir;
        if (chunksDir.IsEmpty())
            chunksDir = manifest.m_RootDir + CHUNKS_DIR_NAME;
        string path = chunksDir + "row_" + tileIz.ToString() + ".json";
        RDF_DemHeightRowDoc doc = new RDF_DemHeightRowDoc();
        if (!RDF_DemJsonIo.TryLoad(doc, path))
            return null;
        if (doc.iz != tileIz)
            return null;

        s_CachedRoot = manifest.m_RootDir;
        s_CachedIz = tileIz;
        s_CachedRow = doc;
        return doc;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool HexDecode(string hex, notnull array<int> outBytes)
    {
        outBytes.Clear();
        int len = hex.Length();
        if ((len % 2) != 0)
            return false;

        for (int i = 0; i < len; i = i + 2)
        {
            int hi = HexNibble(hex.Get(i));
            int lo = HexNibble(hex.Get(i + 1));
            if (hi < 0 || lo < 0)
                return false;
            outBytes.Insert((hi << 4) | lo);
        }
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static int HexNibble(string ch)
    {
        if (ch.IsEmpty())
            return -1;
        int a = ch.ToAscii();
        if (a >= 48 && a <= 57)
            return a - 48;
        if (a >= 97 && a <= 102)
            return a - 87;
        if (a >= 65 && a <= 70)
            return a - 55;
        return -1;
    }
}
