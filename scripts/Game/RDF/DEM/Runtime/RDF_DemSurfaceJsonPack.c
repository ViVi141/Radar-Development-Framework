// Workshop-friendly surface-class map: height comes from BaseWorld.GetSurfaceY at runtime.
// Layout: DemData/<world>/surf_manifest.json + surf_chunks/row_<iz>.json (1 byte/cell hex).
class RDF_DemSurfManifestDoc : JsonApiStruct
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
    string chunks_dir;

    void RDF_DemSurfManifestDoc()
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
        RegV("chunks_dir");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_DemSurfTileEntry : JsonApiStruct
{
    int ix;
    string hex;

    void RDF_DemSurfTileEntry()
    {
        RegV("ix");
        RegV("hex");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_DemSurfRowDoc : JsonApiStruct
{
    int iz;
    ref array<ref RDF_DemSurfTileEntry> tiles;

    void RDF_DemSurfRowDoc()
    {
        RegV("iz");
        RegV("tiles");
    }
}

//------------------------------------------------------------------------------------------------
class RDF_DemSurfaceJsonPack
{
    static const string MANIFEST_NAME = "surf_manifest.json";
    static const string CHUNKS_DIR_NAME = "surf_chunks/";
    static const string MAGIC = "RDF_SURF_JSON_V1";

    protected static string s_CachedRoot;
    protected static int s_CachedIz;
    protected static ref RDF_DemSurfRowDoc s_CachedRow;

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
    static bool TryLoadManifest(string rootDir, string expectWorldKey, out RDF_DemRuntimeManifest outManifest)
    {
        outManifest = null;
        string path = BuildManifestPath(rootDir);
        if (!FileIO.FileExists(path))
            return false;

        RDF_DemSurfManifestDoc doc = new RDF_DemSurfManifestDoc();
        if (!doc.LoadFromFile(path))
            return false;
        if (doc.magic != MAGIC)
            return false;
        if (doc.world.IsEmpty())
            return false;
        if (!expectWorldKey.IsEmpty() && doc.world != expectWorldKey)
            return false;
        if (doc.cell_m <= 0.0 || doc.tile_cells < 1)
            return false;
        if (doc.tile_count_x < 1 || doc.tile_count_z < 1)
            return false;

        RDF_DemRuntimeManifest manifest = new RDF_DemRuntimeManifest();
        manifest.m_WorldKey = doc.world;
        manifest.m_BoundsMinX = doc.bounds_min_x;
        manifest.m_BoundsMinZ = doc.bounds_min_z;
        manifest.m_BoundsMaxX = doc.bounds_max_x;
        manifest.m_BoundsMaxZ = doc.bounds_max_z;
        manifest.m_CellM = doc.cell_m;
        manifest.m_TileCells = doc.tile_cells;
        manifest.m_TileCountX = doc.tile_count_x;
        manifest.m_TileCountZ = doc.tile_count_z;
        manifest.m_MaxSpans = 1;
        manifest.m_IsSurfacePack = true;
        manifest.m_PreferLiveTerrainY = true;
        manifest.m_RootDir = rootDir;
        string chunks = doc.chunks_dir;
        if (chunks.IsEmpty())
            chunks = CHUNKS_DIR_NAME;
        manifest.m_TilesDir = rootDir + chunks;
        outManifest = manifest;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    static bool LoadTile(
        notnull RDF_DemRuntimeManifest manifest,
        int tileIx,
        int tileIz,
        out RDF_DemRuntimeTile outTile)
    {
        outTile = null;
        if (!manifest.m_IsSurfacePack)
            return false;

        RDF_DemSurfRowDoc row = LoadRowCached(manifest, tileIz);
        if (!row)
            return false;
        if (!row.tiles)
            return false;

        string hex = string.Empty;
        bool found = false;
        int n = row.tiles.Count();
        for (int i = 0; i < n; i++)
        {
            RDF_DemSurfTileEntry entry = row.tiles.Get(i);
            if (!entry)
                continue;
            if (entry.ix != tileIx)
                continue;
            hex = entry.hex;
            found = true;
            break;
        }
        if (!found)
            return false;

        int size = manifest.m_TileCells;
        int expectedHex = size * size * 2;
        if (hex.Length() != expectedHex)
            return false;

        array<int> bytes = new array<int>();
        if (!HexDecode(hex, bytes))
            return false;

        RDF_DemRuntimeTile tile = new RDF_DemRuntimeTile();
        tile.m_TileIx = tileIx;
        tile.m_TileIz = tileIz;
        tile.m_CellM = manifest.m_CellM;
        tile.m_Size = size;
        tile.m_MaxSpans = 1;
        tile.m_OriginIx = tileIx * size;
        tile.m_OriginIz = tileIz * size;
        tile.m_OriginX = manifest.m_BoundsMinX + tile.m_OriginIx * manifest.m_CellM;
        tile.m_OriginZ = manifest.m_BoundsMinZ + tile.m_OriginIz * manifest.m_CellM;
        tile.InitializeStorage(size, 1);

        array<float> spanLo = new array<float>();
        array<float> spanHi = new array<float>();
        spanLo.Insert(0.0);
        spanHi.Insert(0.0);

        for (int localIz = 0; localIz < size; localIz++)
        {
            for (int localIx = 0; localIx < size; localIx++)
            {
                int cellIdx = localIz * size + localIx;
                int surfaceClass = bytes.Get(cellIdx);
                tile.SetCell(
                    localIx,
                    localIz,
                    0.0,
                    0.0,
                    0.0,
                    0,
                    0,
                    0,
                    0.5,
                    surfaceClass,
                    1,
                    spanLo,
                    spanHi);
            }
        }

        outTile = tile;
        return true;
    }

    // Decode every surf chunk into a flat world-sized surface-class grid (1 int/cell).
    // Prefer this over caching thousands of RDF_DemRuntimeTile objects.
    static bool PreloadWorldSurfaceClasses(
        notnull RDF_DemRuntimeManifest manifest,
        out array<int> outWorldSurf,
        out int outCellsX,
        out int outCellsZ,
        out int outTilesLoaded,
        out int outTilesFailed)
    {
        outWorldSurf = null;
        outCellsX = 0;
        outCellsZ = 0;
        outTilesLoaded = 0;
        outTilesFailed = 0;

        array<int> worldSurf;
        int cellsX;
        int cellsZ;
        if (!BeginWorldSurfaceGrid(manifest, worldSurf, cellsX, cellsZ))
            return false;

        int loaded = 0;
        int failed = 0;
        for (int tileIz = 0; tileIz < manifest.m_TileCountZ; tileIz++)
        {
            int rowLoaded;
            int rowFailed;
            AppendSurfRow(manifest, worldSurf, cellsX, tileIz, rowLoaded, rowFailed);
            loaded = loaded + rowLoaded;
            failed = failed + rowFailed;
        }

        ClearRowCache();

        outWorldSurf = worldSurf;
        outCellsX = cellsX;
        outCellsZ = cellsZ;
        outTilesLoaded = loaded;
        outTilesFailed = failed;
        return loaded > 0;
    }

    //--------------------------------------------------------------------------------------------
    static bool BeginWorldSurfaceGrid(
        notnull RDF_DemRuntimeManifest manifest,
        out array<int> outWorldSurf,
        out int outCellsX,
        out int outCellsZ)
    {
        outWorldSurf = null;
        outCellsX = 0;
        outCellsZ = 0;
        if (!manifest.m_IsSurfacePack)
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

        array<int> worldSurf = new array<int>();
        worldSurf.Resize(cellCount);
        int unknown = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        for (int i = 0; i < cellCount; i++)
            worldSurf.Set(i, unknown);

        outWorldSurf = worldSurf;
        outCellsX = cellsX;
        outCellsZ = cellsZ;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    // Decode one surf_chunks row into the flat grid. Used by sync preload.
    static void AppendSurfRow(
        notnull RDF_DemRuntimeManifest manifest,
        notnull array<int> worldSurf,
        int cellsX,
        int tileIz,
        out int outTilesLoaded,
        out int outTilesFailed)
    {
        outTilesLoaded = 0;
        outTilesFailed = 0;
        if (tileIz < 0 || tileIz >= manifest.m_TileCountZ)
            return;

        RDF_DemSurfRowDoc row = LoadRowCached(manifest, tileIz);
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
            AppendSurfTileEntry(manifest, worldSurf, cellsX, tileIz, t, oneLoaded, oneFailed);
            loaded = loaded + oneLoaded;
            failed = failed + oneFailed;
        }

        outTilesLoaded = loaded;
        outTilesFailed = failed;
    }

    //--------------------------------------------------------------------------------------------
    // Decode a single tile entry inside an already-cached row (async frame slices).
    static void AppendSurfTileEntry(
        notnull RDF_DemRuntimeManifest manifest,
        notnull array<int> worldSurf,
        int cellsX,
        int tileIz,
        int entryIndex,
        out int outTilesLoaded,
        out int outTilesFailed)
    {
        outTilesLoaded = 0;
        outTilesFailed = 0;
        RDF_DemSurfRowDoc row = LoadRowCached(manifest, tileIz);
        if (!row || !row.tiles)
        {
            outTilesFailed = 1;
            return;
        }
        if (entryIndex < 0 || entryIndex >= row.tiles.Count())
            return;

        RDF_DemSurfTileEntry entry = row.tiles.Get(entryIndex);
        if (!entry)
        {
            outTilesFailed = 1;
            return;
        }

        int size = manifest.m_TileCells;
        int expectedHex = size * size * 2;
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
        if (bytes.Count() < size * size)
        {
            outTilesFailed = 1;
            return;
        }

        int originIx = tileIx * size;
        int originIz = tileIz * size;
        for (int localIz = 0; localIz < size; localIz++)
        {
            int globalIz = originIz + localIz;
            int rowBase = globalIz * cellsX + originIx;
            int localBase = localIz * size;
            for (int localIx = 0; localIx < size; localIx++)
                worldSurf.Set(rowBase + localIx, bytes.Get(localBase + localIx));
        }
        outTilesLoaded = 1;
    }

    //--------------------------------------------------------------------------------------------
    static int GetSurfRowEntryCount(notnull RDF_DemRuntimeManifest manifest, int tileIz)
    {
        RDF_DemSurfRowDoc row = LoadRowCached(manifest, tileIz);
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
    protected static RDF_DemSurfRowDoc LoadRowCached(
        notnull RDF_DemRuntimeManifest manifest,
        int tileIz)
    {
        if (s_CachedRow && s_CachedRoot == manifest.m_RootDir && s_CachedIz == tileIz)
            return s_CachedRow;

        string chunksName = CHUNKS_DIR_NAME;
        if (manifest.m_TilesDir.Length() > manifest.m_RootDir.Length())
        {
            chunksName = manifest.m_TilesDir.Substring(
                manifest.m_RootDir.Length(),
                manifest.m_TilesDir.Length() - manifest.m_RootDir.Length());
        }

        string path = BuildRowPath(manifest.m_RootDir, chunksName, tileIz);
        if (!FileIO.FileExists(path))
            return null;

        RDF_DemSurfRowDoc doc = new RDF_DemSurfRowDoc();
        if (!doc.LoadFromFile(path))
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
