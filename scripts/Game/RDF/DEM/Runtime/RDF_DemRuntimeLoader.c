// Runtime parser for RDF DEM V3 manifest/tile CSV files.
class RDF_DemRuntimeLoader
{
    protected static int s_WarnCount;
    protected static bool s_WarnSuppressed;
    protected static const int MAX_WARNINGS = 20;

    //--------------------------------------------------------------------------------------------
    static bool LoadManifestForCurrentWorld(out RDF_DemRuntimeManifest outManifest)
    {
        string worldPath = GetGame().GetWorldFile();
        string worldKey = RDF_DemWorldKey.FromPath(worldPath);
        if (worldKey.IsEmpty())
        {
            Warn("Cannot resolve world key from path: " + worldPath);
            return false;
        }
        return LoadManifest(worldKey, outManifest);
    }

    //--------------------------------------------------------------------------------------------
    static bool LoadManifest(string worldKey, out RDF_DemRuntimeManifest outManifest)
    {
        outManifest = null;
        if (worldKey.IsEmpty())
        {
            Warn("Manifest load failed: empty world key.");
            return false;
        }

        string rootDir = RDF_DemBakeConstants.DEM_DATA_DIR + worldKey + "/";
        string manifestPath = rootDir + "manifest.csv";
        if (!FileIO.FileExists(manifestPath))
        {
            Warn("Manifest missing: " + manifestPath);
            return false;
        }

        array<string> lines = SCR_FileIOHelper.ReadFileContent(manifestPath, false);
        if (!lines || lines.Count() < 2)
        {
            Warn("Manifest unreadable or empty: " + manifestPath);
            return false;
        }

        if (lines.Get(0) != "RDF_DEM_MANIFEST_V3")
        {
            Warn("Manifest magic mismatch: " + manifestPath);
            return false;
        }

        map<string, string> kv = new map<string, string>();
        for (int i = 1; i < lines.Count(); i++)
        {
            string key;
            string value;
            if (!ParseKeyValue(lines.Get(i), key, value))
                continue;
            kv.Set(key, value);
        }

        RDF_DemRuntimeManifest manifest = new RDF_DemRuntimeManifest();
        if (!RequireString(kv, "world", manifest.m_WorldKey))
            return false;
        if (!RequireFloat(kv, "bounds_min_x", manifest.m_BoundsMinX))
            return false;
        if (!RequireFloat(kv, "bounds_min_z", manifest.m_BoundsMinZ))
            return false;
        if (!RequireFloat(kv, "bounds_max_x", manifest.m_BoundsMaxX))
            return false;
        if (!RequireFloat(kv, "bounds_max_z", manifest.m_BoundsMaxZ))
            return false;
        if (!RequireFloat(kv, "cell_m", manifest.m_CellM))
            return false;
        if (!RequireInt(kv, "tile_cells", manifest.m_TileCells))
            return false;
        if (!RequireInt(kv, "tile_count_x", manifest.m_TileCountX))
            return false;
        if (!RequireInt(kv, "tile_count_z", manifest.m_TileCountZ))
            return false;
        if (!RequireInt(kv, "max_spans", manifest.m_MaxSpans))
            return false;
        string channels;
        if (!RequireString(kv, "channels", channels))
            return false;

        if (manifest.m_WorldKey != worldKey)
        {
            Warn("Manifest world mismatch: expected " + worldKey + " got " + manifest.m_WorldKey);
            return false;
        }

        if (manifest.m_CellM <= 0.0)
        {
            Warn("Manifest invalid cell_m.");
            return false;
        }

        if (manifest.m_TileCells < 1 || manifest.m_TileCells > 4096)
        {
            Warn("Manifest invalid tile_cells.");
            return false;
        }

        if (manifest.m_TileCountX < 1 || manifest.m_TileCountZ < 1)
        {
            Warn("Manifest invalid tile counts.");
            return false;
        }

        if (manifest.m_MaxSpans < 1 || manifest.m_MaxSpans > RDF_DemBakeConstants.MAX_SPANS)
        {
            Warn("Manifest invalid max_spans.");
            return false;
        }

        if (manifest.m_BoundsMaxX <= manifest.m_BoundsMinX
            || manifest.m_BoundsMaxZ <= manifest.m_BoundsMinZ)
        {
            Warn("Manifest invalid world bounds.");
            return false;
        }
        if (!channels.Contains("terrain_y")
            || !channels.Contains("surface_class")
            || !channels.Contains("n_spans"))
        {
            Warn("Manifest channels missing required fields.");
            return false;
        }

        manifest.m_RootDir = rootDir;
        manifest.m_TilesDir = rootDir + "tiles/";
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
        if (tileIx < 0 || tileIz < 0
            || tileIx >= manifest.m_TileCountX
            || tileIz >= manifest.m_TileCountZ)
        {
            Warn("Tile index out of range: " + tileIx.ToString() + "," + tileIz.ToString());
            return false;
        }

        string tilePath = manifest.m_TilesDir
            + "tile_" + tileIx.ToString()
            + "_" + tileIz.ToString() + ".csv";
        if (!FileIO.FileExists(tilePath))
        {
            Warn("Tile missing: " + tilePath);
            return false;
        }

        array<string> lines = SCR_FileIOHelper.ReadFileContent(tilePath, false);
        if (!lines || lines.Count() < 12)
        {
            Warn("Tile unreadable: " + tilePath);
            return false;
        }

        if (lines.Get(0) != "RDF_DEM_TILE_V3")
        {
            Warn("Tile magic mismatch: " + tilePath);
            return false;
        }

        map<string, string> kv = new map<string, string>();
        int dataStartLine = -1;
        for (int i = 1; i < lines.Count(); i++)
        {
            string line = lines.Get(i);
            if (line.StartsWith("cols "))
            {
                dataStartLine = i + 1;
                break;
            }

            string key;
            string value;
            if (!ParseKeyValue(line, key, value))
                continue;
            kv.Set(key, value);
        }

        if (dataStartLine < 0 || dataStartLine >= lines.Count())
        {
            Warn("Tile missing cols header: " + tilePath);
            return false;
        }

        RDF_DemRuntimeTile tile = new RDF_DemRuntimeTile();
        if (!RequireInt(kv, "tile_ix", tile.m_TileIx))
            return false;
        if (!RequireInt(kv, "tile_iz", tile.m_TileIz))
            return false;
        if (!RequireFloat(kv, "origin_x", tile.m_OriginX))
            return false;
        if (!RequireFloat(kv, "origin_z", tile.m_OriginZ))
            return false;
        if (!RequireInt(kv, "origin_ix", tile.m_OriginIx))
            return false;
        if (!RequireInt(kv, "origin_iz", tile.m_OriginIz))
            return false;
        if (!RequireFloat(kv, "cell_m", tile.m_CellM))
            return false;
        if (!RequireInt(kv, "size", tile.m_Size))
            return false;
        if (!RequireInt(kv, "max_spans", tile.m_MaxSpans))
            return false;

        if (tile.m_TileIx != tileIx || tile.m_TileIz != tileIz)
        {
            Warn("Tile header index mismatch: " + tilePath);
            return false;
        }

        if (tile.m_CellM != manifest.m_CellM)
        {
            Warn("Tile cell_m mismatch: " + tilePath);
            return false;
        }

        if (tile.m_Size != manifest.m_TileCells)
        {
            Warn("Tile size mismatch: " + tilePath);
            return false;
        }

        if (tile.m_MaxSpans != manifest.m_MaxSpans)
        {
            Warn("Tile max_spans mismatch: " + tilePath);
            return false;
        }

        tile.InitializeStorage(tile.m_Size, tile.m_MaxSpans);

        int expectedFields = 11 + tile.m_MaxSpans * 2;
        int loadedRows = 0;
        int badRows = 0;
        for (int row = dataStartLine; row < lines.Count(); row++)
        {
            string rowLine = lines.Get(row);
            if (rowLine.IsEmpty())
                continue;

            array<string> f = new array<string>();
            rowLine.Split(" ", f, true);
            if (f.Count() != expectedFields)
            {
                badRows = badRows + 1;
                Warn("Tile row field count mismatch: " + tilePath + " line " + row.ToString());
                continue;
            }

            int localIx = f.Get(0).ToInt();
            int localIz = f.Get(1).ToInt();
            float terrainY = f.Get(2).ToFloat();
            float slopeDeg = f.Get(3).ToFloat();
            float waterDepthM = f.Get(4).ToFloat();
            int waterType = f.Get(5).ToInt();
            int occGround = f.Get(6).ToInt();
            int entityHits = f.Get(7).ToInt();
            float densityGcm3 = f.Get(8).ToFloat();
            int surfaceClass = f.Get(9).ToInt();
            int nSpans = f.Get(10).ToInt();

            array<float> spanLo = new array<float>();
            array<float> spanHi = new array<float>();
            int cursor = 11;
            for (int s = 0; s < tile.m_MaxSpans; s++)
            {
                spanLo.Insert(f.Get(cursor).ToFloat());
                spanHi.Insert(f.Get(cursor + 1).ToFloat());
                cursor = cursor + 2;
            }

            if (!tile.SetCell(
                localIx,
                localIz,
                terrainY,
                slopeDeg,
                waterDepthM,
                waterType,
                occGround,
                entityHits,
                densityGcm3,
                surfaceClass,
                nSpans,
                spanLo,
                spanHi))
            {
                badRows = badRows + 1;
                Warn("Tile row value out of range: " + tilePath + " line " + row.ToString());
                continue;
            }

            loadedRows = loadedRows + 1;
        }

        if (loadedRows <= 0)
        {
            Warn("Tile contains no data rows: " + tilePath);
            return false;
        }
        if (badRows > 0)
        {
            Warn("Tile parse failed due to bad rows: " + tilePath + " bad=" + badRows.ToString());
            return false;
        }

        outTile = tile;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool ParseKeyValue(string line, out string outKey, out string outValue)
    {
        outKey = string.Empty;
        outValue = string.Empty;
        if (line.IsEmpty())
            return false;

        int separator = line.IndexOf(" ");
        if (separator <= 0)
            return false;
        if (separator >= line.Length() - 1)
            return false;

        outKey = line.Substring(0, separator);
        outValue = line.Substring(separator + 1, line.Length() - separator - 1);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool RequireString(map<string, string> kv, string key, out string outValue)
    {
        outValue = string.Empty;
        if (!kv.Contains(key))
        {
            Warn("Manifest key missing: " + key);
            return false;
        }

        outValue = kv.Get(key);
        if (outValue.IsEmpty())
        {
            Warn("Manifest key empty: " + key);
            return false;
        }

        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool RequireInt(map<string, string> kv, string key, out int outValue)
    {
        outValue = 0;
        string raw;
        if (!RequireString(kv, key, raw))
            return false;
        outValue = raw.ToInt();
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool RequireFloat(map<string, string> kv, string key, out float outValue)
    {
        outValue = 0.0;
        string raw;
        if (!RequireString(kv, key, raw))
            return false;
        outValue = raw.ToFloat();
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static void Warn(string message)
    {
        if (s_WarnCount < MAX_WARNINGS)
        {
            Print("[RDF DEM Runtime] " + message, LogLevel.WARNING);
            s_WarnCount = s_WarnCount + 1;
            return;
        }

        if (!s_WarnSuppressed)
        {
            s_WarnSuppressed = true;
            Print("[RDF DEM Runtime] further warnings suppressed", LogLevel.WARNING);
        }
    }
}
