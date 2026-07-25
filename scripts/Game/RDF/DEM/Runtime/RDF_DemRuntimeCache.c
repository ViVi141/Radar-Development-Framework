// Runtime DEM tile cache with bounded LRU + world-space sampling.
class RDF_DemRuntimeCache
{
    protected ref RDF_DemRuntimeManifest m_Manifest;
    protected string m_WorldKey;
    protected bool m_Ready;
    protected bool m_LastInitFailed;

    protected ref map<string, ref RDF_DemRuntimeTile> m_TilesByKey;
    protected ref map<string, int> m_TileTouchOrder;
    protected int m_TouchCounter;

    protected int m_MaxTiles = 16;
    protected int m_LoadBudgetRemaining = 2;

    protected int m_StatHits;
    protected int m_StatMisses;
    protected int m_StatLoads;
    protected int m_StatLoadFails;
    protected int m_StatEvictions;
    protected int m_StatBudgetBlocks;

    void RDF_DemRuntimeCache()
    {
        m_TilesByKey = new map<string, ref RDF_DemRuntimeTile>();
        m_TileTouchOrder = new map<string, int>();
    }

    //--------------------------------------------------------------------------------------------
    void Clear()
    {
        m_Manifest = null;
        m_WorldKey = string.Empty;
        m_Ready = false;
        m_LastInitFailed = false;
        m_TilesByKey.Clear();
        m_TileTouchOrder.Clear();
        m_TouchCounter = 0;
        ResetStats();
    }

    //--------------------------------------------------------------------------------------------
    void SetMaxTiles(int maxTiles)
    {
        m_MaxTiles = Math.Max(1, maxTiles);
        while (m_TilesByKey.Count() > m_MaxTiles)
            EvictOldestTile();
    }

    //--------------------------------------------------------------------------------------------
    void BeginScan(int loadBudget)
    {
        m_LoadBudgetRemaining = Math.Max(0, loadBudget);
    }

    //--------------------------------------------------------------------------------------------
    bool InitializeForCurrentWorld()
    {
        string worldPath = GetGame().GetWorldFile();
        string worldKey = RDF_DemWorldKey.FromPath(worldPath);
        if (worldKey.IsEmpty())
        {
            if (!m_LastInitFailed)
                Print("[RDF DEM Runtime] init failed: world key empty", LogLevel.WARNING);
            m_LastInitFailed = true;
            m_Ready = false;
            return false;
        }

        if (m_Ready && m_WorldKey == worldKey && m_Manifest)
            return true;

        if (m_WorldKey != worldKey)
        {
            m_TilesByKey.Clear();
            m_TileTouchOrder.Clear();
            m_TouchCounter = 0;
            m_WorldKey = worldKey;
        }

        RDF_DemRuntimeManifest manifest;
        if (!RDF_DemRuntimeLoader.LoadManifest(worldKey, manifest))
        {
            if (!m_LastInitFailed)
                Print("[RDF DEM Runtime] init failed: manifest not ready for " + worldKey, LogLevel.WARNING);
            m_Manifest = null;
            m_Ready = false;
            m_LastInitFailed = true;
            return false;
        }

        m_Manifest = manifest;
        m_Ready = true;
        m_LastInitFailed = false;
        Print(string.Format(
            "[RDF DEM Runtime] ready world=%1 cell=%2 tile=%3 count=%4x%5 cache=%6",
            worldKey,
            manifest.m_CellM.ToString(),
            manifest.m_TileCells.ToString(),
            manifest.m_TileCountX.ToString(),
            manifest.m_TileCountZ.ToString(),
            m_MaxTiles.ToString()), LogLevel.NORMAL);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    bool IsReady()
    {
        return m_Ready && m_Manifest != null;
    }

    //--------------------------------------------------------------------------------------------
    float GetCellSizeM()
    {
        if (!m_Manifest)
            return RDF_DemBakeConstants.CELL_M;
        return m_Manifest.m_CellM;
    }

    //--------------------------------------------------------------------------------------------
    string GetStatusShort()
    {
        if (!m_Ready)
            return "DEM OFF";
        return "DEM OK";
    }

    //--------------------------------------------------------------------------------------------
    string GetStatsLine()
    {
        return string.Format(
            "hit=%1 miss=%2 load=%3 fail=%4 evict=%5 budget=%6",
            m_StatHits.ToString(),
            m_StatMisses.ToString(),
            m_StatLoads.ToString(),
            m_StatLoadFails.ToString(),
            m_StatEvictions.ToString(),
            m_StatBudgetBlocks.ToString());
    }

    //--------------------------------------------------------------------------------------------
    bool TrySampleAt(float worldX, float worldZ, out RDF_DemRuntimeCellSample outSample)
    {
        outSample = null;
        if (!InitializeForCurrentWorld())
            return false;
        if (!m_Manifest)
            return false;

        if (worldX < m_Manifest.m_BoundsMinX
            || worldZ < m_Manifest.m_BoundsMinZ
            || worldX >= m_Manifest.m_BoundsMaxX
            || worldZ >= m_Manifest.m_BoundsMaxZ)
        {
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        float relX = worldX - m_Manifest.m_BoundsMinX;
        float relZ = worldZ - m_Manifest.m_BoundsMinZ;
        int globalIx = Math.Floor(relX / m_Manifest.m_CellM);
        int globalIz = Math.Floor(relZ / m_Manifest.m_CellM);
        if (globalIx < 0 || globalIz < 0)
        {
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        int tileIx = globalIx / m_Manifest.m_TileCells;
        int tileIz = globalIz / m_Manifest.m_TileCells;
        int localIx = globalIx - tileIx * m_Manifest.m_TileCells;
        int localIz = globalIz - tileIz * m_Manifest.m_TileCells;

        RDF_DemRuntimeTile tile;
        if (!TryGetTile(tileIx, tileIz, tile))
        {
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        if (!tile.TryGetCell(localIx, localIz, outSample))
        {
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        m_StatHits = m_StatHits + 1;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    bool TryGetTile(int tileIx, int tileIz, out RDF_DemRuntimeTile outTile)
    {
        outTile = null;
        if (!InitializeForCurrentWorld())
            return false;
        if (!m_Manifest)
            return false;

        if (tileIx < 0 || tileIz < 0
            || tileIx >= m_Manifest.m_TileCountX
            || tileIz >= m_Manifest.m_TileCountZ)
        {
            return false;
        }

        string key = BuildTileKey(tileIx, tileIz);
        if (m_TilesByKey.Contains(key))
        {
            TouchTile(key);
            outTile = m_TilesByKey.Get(key);
            return outTile != null;
        }

        if (m_LoadBudgetRemaining <= 0)
        {
            m_StatBudgetBlocks = m_StatBudgetBlocks + 1;
            return false;
        }

        RDF_DemRuntimeTile loaded;
        if (!RDF_DemRuntimeLoader.LoadTile(m_Manifest, tileIx, tileIz, loaded))
        {
            m_StatLoadFails = m_StatLoadFails + 1;
            return false;
        }

        m_LoadBudgetRemaining = m_LoadBudgetRemaining - 1;
        m_StatLoads = m_StatLoads + 1;
        EnsureCapacityForInsert();
        m_TilesByKey.Set(key, loaded);
        TouchTile(key);
        outTile = loaded;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected void EnsureCapacityForInsert()
    {
        while (m_TilesByKey.Count() >= m_MaxTiles)
            EvictOldestTile();
    }

    //--------------------------------------------------------------------------------------------
    protected void EvictOldestTile()
    {
        string oldestKey = string.Empty;
        int oldestTouch = 2147483647;
        bool found = false;

        foreach (string key, int touch : m_TileTouchOrder)
        {
            if (!m_TilesByKey.Contains(key))
                continue;
            if (!found || touch < oldestTouch)
            {
                found = true;
                oldestTouch = touch;
                oldestKey = key;
            }
        }

        if (!found)
            return;

        m_TilesByKey.Remove(oldestKey);
        m_TileTouchOrder.Remove(oldestKey);
        m_StatEvictions = m_StatEvictions + 1;
    }

    //--------------------------------------------------------------------------------------------
    protected void TouchTile(string key)
    {
        m_TouchCounter = m_TouchCounter + 1;
        m_TileTouchOrder.Set(key, m_TouchCounter);
    }

    //--------------------------------------------------------------------------------------------
    protected string BuildTileKey(int tileIx, int tileIz)
    {
        return tileIx.ToString() + "_" + tileIz.ToString();
    }

    //--------------------------------------------------------------------------------------------
    protected void ResetStats()
    {
        m_StatHits = 0;
        m_StatMisses = 0;
        m_StatLoads = 0;
        m_StatLoadFails = 0;
        m_StatEvictions = 0;
        m_StatBudgetBlocks = 0;
    }
}
