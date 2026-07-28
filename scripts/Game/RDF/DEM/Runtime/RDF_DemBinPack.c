// RDF_DEM_BIN_V1: one seekable .dem.data pack per world (workshop-friendly).
// Header 120 B + tile table + 6 B/cell payloads. No column spans.
class RDF_DemBinPack
{
    //--------------------------------------------------------------------------------------------
    static string BuildPackPath(string rootDir, string worldKey)
    {
        return rootDir + worldKey + ".dem.data";
    }

    //--------------------------------------------------------------------------------------------
    static bool TryLoadManifest(string packPath, string expectWorldKey, out RDF_DemRuntimeManifest outManifest)
    {
        outManifest = null;
        if (packPath.IsEmpty() || !FileIO.FileExists(packPath))
            return false;

        FileHandle file = FileIO.OpenFile(packPath, FileMode.READ);
        if (!file)
            return false;

        string magic;
        file.Read(magic, 7);
        int nul;
        file.Read(nul, 1);
        if (magic != RDF_DemBakeConstants.DEM_BIN_MAGIC)
        {
            file.Close();
            return false;
        }

        int version;
        file.Read(version, 4);
        if (version != RDF_DemBakeConstants.DEM_BIN_VERSION)
        {
            file.Close();
            return false;
        }

        int flags;
        file.Read(flags, 4);

        float boundsMinX;
        float boundsMinZ;
        float boundsMaxX;
        float boundsMaxZ;
        float cellM;
        file.Read(boundsMinX, 4);
        file.Read(boundsMinZ, 4);
        file.Read(boundsMaxX, 4);
        file.Read(boundsMaxZ, 4);
        file.Read(cellM, 4);

        int tileCells;
        int tileCountX;
        int tileCountZ;
        float yScale;
        int tableCount;
        file.Read(tileCells, 4);
        file.Read(tileCountX, 4);
        file.Read(tileCountZ, 4);
        file.Read(yScale, 4);
        file.Read(tableCount, 4);

        string worldKey = ReadFixedAscii(file, RDF_DemBakeConstants.DEM_BIN_WORLD_KEY_BYTES);
        if (worldKey.IsEmpty())
        {
            file.Close();
            return false;
        }
        if (!expectWorldKey.IsEmpty() && worldKey != expectWorldKey)
        {
            file.Close();
            return false;
        }

        if (cellM <= 0.0 || tileCells < 1 || tileCountX < 1 || tileCountZ < 1)
        {
            file.Close();
            return false;
        }
        if (yScale <= 0.0)
            yScale = RDF_DemBakeConstants.DEM_BIN_Y_SCALE;
        if (tableCount < 0 || tableCount > tileCountX * tileCountZ)
        {
            file.Close();
            return false;
        }

        RDF_DemRuntimeManifest manifest = new RDF_DemRuntimeManifest();
        manifest.m_WorldKey = worldKey;
        manifest.m_BoundsMinX = boundsMinX;
        manifest.m_BoundsMinZ = boundsMinZ;
        manifest.m_BoundsMaxX = boundsMaxX;
        manifest.m_BoundsMaxZ = boundsMaxZ;
        manifest.m_CellM = cellM;
        manifest.m_TileCells = tileCells;
        manifest.m_TileCountX = tileCountX;
        manifest.m_TileCountZ = tileCountZ;
        manifest.m_MaxSpans = 1;
        manifest.m_IsBinaryPack = true;
        manifest.m_IsJsonPack = false;
        manifest.m_IsSurfacePack = false;
        manifest.m_PreferLiveTerrainY = true;
        manifest.m_BinPackPath = packPath;
        manifest.m_BinYScale = yScale;
        manifest.m_BinTileByteOffset = new map<string, int>();
        manifest.m_BinTileByteLength = new map<string, int>();

        for (int i = 0; i < tableCount; i++)
        {
            int tileIx;
            int tileIz;
            int byteOffset;
            int byteLength;
            file.Read(tileIx, 4);
            file.Read(tileIz, 4);
            file.Read(byteOffset, 4);
            file.Read(byteLength, 4);
            string key = tileIx.ToString() + "_" + tileIz.ToString();
            manifest.m_BinTileByteOffset.Set(key, byteOffset);
            manifest.m_BinTileByteLength.Set(key, byteLength);
        }

        file.Close();

        int slash = packPath.LastIndexOf("/");
        if (slash < 0)
            slash = packPath.LastIndexOf("\\");
        if (slash >= 0)
            manifest.m_RootDir = packPath.Substring(0, slash + 1);
        else
            manifest.m_RootDir = string.Empty;
        manifest.m_TilesDir = manifest.m_RootDir + "tiles/";

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
        if (!manifest.m_IsBinaryPack || manifest.m_BinPackPath.IsEmpty())
            return false;

        string key = tileIx.ToString() + "_" + tileIz.ToString();
        if (!manifest.m_BinTileByteOffset || !manifest.m_BinTileByteOffset.Contains(key))
            return false;

        int byteOffset = manifest.m_BinTileByteOffset.Get(key);
        int byteLength = manifest.m_BinTileByteLength.Get(key);
        int size = manifest.m_TileCells;
        int cellCount = size * size;
        int expected = cellCount * RDF_DemBakeConstants.DEM_BIN_CELL_BYTES;
        if (byteLength != expected || byteOffset < RDF_DemBakeConstants.DEM_BIN_HEADER_BYTES)
            return false;

        FileHandle file = FileIO.OpenFile(manifest.m_BinPackPath, FileMode.READ);
        if (!file)
            return false;

        file.Seek(byteOffset);
        array<int> bytes = new array<int>();
        bytes.Resize(byteLength);
        int got = file.ReadArray(bytes, 1, byteLength);
        file.Close();
        if (got != byteLength)
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

        float yScale = manifest.m_BinYScale;
        if (yScale <= 0.0)
            yScale = RDF_DemBakeConstants.DEM_BIN_Y_SCALE;

        array<float> spanLo = new array<float>();
        array<float> spanHi = new array<float>();
        spanLo.Insert(0.0);
        spanHi.Insert(0.0);

        for (int localIz = 0; localIz < size; localIz++)
        {
            for (int localIx = 0; localIx < size; localIx++)
            {
                int cellIdx = localIz * size + localIx;
                int base = cellIdx * RDF_DemBakeConstants.DEM_BIN_CELL_BYTES;
                int b0 = bytes.Get(base);
                int b1 = bytes.Get(base + 1);
                int b2 = bytes.Get(base + 2);
                int b3 = bytes.Get(base + 3);
                int b4 = bytes.Get(base + 4);
                int b5 = bytes.Get(base + 5);

                int yRaw = b0 | (b1 << 8);
                if (yRaw >= 32768)
                    yRaw = yRaw - 65536;
                float terrainY = yRaw * yScale;
                float slopeDeg = b2;
                float waterDepthM = b3;
                int waterType = b4 & 3;
                int occGround = (b4 >> 2) & 1;
                int densityQ = (b4 >> 3) & 31;
                float densityGcm3 = densityQ * 0.1;
                int surfaceClass = b5;

                spanLo.Set(0, terrainY);
                spanHi.Set(0, terrainY);
                tile.SetCell(
                    localIx,
                    localIz,
                    terrainY,
                    slopeDeg,
                    waterDepthM,
                    waterType,
                    occGround,
                    0,
                    densityGcm3,
                    surfaceClass,
                    1,
                    spanLo,
                    spanHi);
            }
        }

        outTile = tile;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static string ReadFixedAscii(FileHandle file, int byteCount)
    {
        string outStr = string.Empty;
        for (int i = 0; i < byteCount; i++)
        {
            int b;
            file.Read(b, 1);
            if (b == 0)
            {
                // Consume remaining padding.
                for (int j = i + 1; j < byteCount; j++)
                {
                    int pad;
                    file.Read(pad, 1);
                }
                return outStr;
            }
            outStr = outStr + b.AsciiToString();
        }
        return outStr;
    }
}
