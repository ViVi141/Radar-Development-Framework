// Runtime DEM / surface-class tile cache with optional whole-world RAM preload.
// SURF packs use a compact flat surface-class grid; optional HEIGHT JSON fills Y.
// Prefer baked height RAM when RDF_HEIGHT_JSON_V1 is attached; else GetSurfaceY.
class RDF_DemRuntimeCache
{
    protected static bool s_LoggedClientPreloadSkip;
    // Shared SURF/HEIGHT RAM so game-start warm preload is reused by every scanner.
    protected static string s_SharedWorldKey;
    protected static bool s_SharedSurfReady;
    protected static ref array<int> s_SharedSurfClass;
    protected static bool s_SharedHeightReady;
    protected static ref array<float> s_SharedWorldY;
    protected static int s_SharedCellsX;
    protected static int s_SharedCellsZ;
    // Async warm-preload job (authority-only, frame-budgeted).
    // Phase 0 = SURF decode; phase 1 = optional HEIGHT decode.
    protected static bool s_AsyncRunning;
    protected static bool s_AsyncPumpScheduled;
    protected static int s_AsyncPhase;
    protected static string s_AsyncWorldKey;
    protected static ref RDF_DemRuntimeManifest s_AsyncManifest;
    protected static ref array<int> s_AsyncWorldSurf;
    protected static ref array<float> s_AsyncWorldY;
    protected static int s_AsyncCellsX;
    protected static int s_AsyncCellsZ;
    protected static int s_AsyncTileIz;
    protected static int s_AsyncEntryIdx;
    protected static int s_AsyncLoaded;
    protected static int s_AsyncFailed;
    protected static int s_AsyncWall0;

    protected ref RDF_DemRuntimeManifest m_Manifest;
    protected string m_WorldKey;
    protected bool m_Ready;
    protected bool m_LastInitFailed;
    protected bool m_LiveHeightOnly;

    protected ref map<string, ref RDF_DemRuntimeTile> m_TilesByKey;
    protected ref map<string, int> m_TileTouchOrder;
    protected int m_TouchCounter;

    protected int m_MaxTiles = 16;
    protected int m_LoadBudgetRemaining = 2;
    protected bool m_PreloadAll = RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ALL;
    protected bool m_FullyResident;
    protected bool m_WorldSurfReady;
    protected ref array<int> m_WorldSurfClass;
    protected bool m_WorldHeightReady;
    protected ref array<float> m_WorldHeightY;
    protected int m_WorldCellsX;
    protected int m_WorldCellsZ;

    // Transient sample returned by TrySampleAt. Callers must copy fields before
    // the next TrySampleAt on this cache (do not store the reference).
    protected ref RDF_DemRuntimeCellSample m_ScratchSample;

    protected int m_StatHits;
    protected int m_StatMisses;
    protected int m_StatLoads;
    protected int m_StatLoadFails;
    protected int m_StatEvictions;
    protected int m_StatBudgetBlocks;
    protected int m_StatLiveHeight;
    protected int m_StatBakedHeight;

    void RDF_DemRuntimeCache()
    {
        m_TilesByKey = new map<string, ref RDF_DemRuntimeTile>();
        m_TileTouchOrder = new map<string, int>();
    }

    //--------------------------------------------------------------------------------------------
    // Drop instance state only. Shared SURF RAM survives so later scanners reuse it.
    void Clear()
    {
        m_Manifest = null;
        m_WorldKey = string.Empty;
        m_Ready = false;
        m_LastInitFailed = false;
        m_LiveHeightOnly = false;
        m_FullyResident = false;
        m_WorldSurfReady = false;
        m_WorldSurfClass = null;
        m_WorldHeightReady = false;
        m_WorldHeightY = null;
        m_WorldCellsX = 0;
        m_WorldCellsZ = 0;
        m_TilesByKey.Clear();
        m_TileTouchOrder.Clear();
        m_TouchCounter = 0;
        ResetStats();
    }

    //--------------------------------------------------------------------------------------------
    static void ClearSharedPreload()
    {
        CancelAsyncWarmPreload();
        s_SharedWorldKey = string.Empty;
        s_SharedSurfReady = false;
        s_SharedSurfClass = null;
        s_SharedHeightReady = false;
        s_SharedWorldY = null;
        s_SharedCellsX = 0;
        s_SharedCellsZ = 0;
    }

    //--------------------------------------------------------------------------------------------
    static bool IsSharedSurfReady()
    {
        return s_SharedSurfReady;
    }

    //--------------------------------------------------------------------------------------------
    static bool IsSharedHeightReady()
    {
        return s_SharedHeightReady;
    }

    //--------------------------------------------------------------------------------------------
    // True when authority shared RAM can serve surface + height without GetSurfaceY.
    static bool IsSharedTerrainRamReady()
    {
        if (!s_SharedSurfReady)
            return false;
        // Height pack is optional; when absent, live Y is still used until pack ships.
        if (s_AsyncRunning)
            return false;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    static bool IsAsyncWarmPreloadRunning()
    {
        return s_AsyncRunning;
    }

    //--------------------------------------------------------------------------------------------
    // Authority-only warm load at game start. Prefers async frame slices (no 10s hitch).
    static bool WarmPreloadCurrentWorld()
    {
        if (!RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ALL)
            return false;
        if (!IsAuthorityPeer())
        {
            if (!s_LoggedClientPreloadSkip)
            {
                s_LoggedClientPreloadSkip = true;
                Print("[RDF DEM Runtime] SURF/DEM RAM preload skipped on client (authority-only)");
            }
            return false;
        }
        if (s_SharedSurfReady)
        {
            if (s_AsyncRunning)
                return true;
            if (RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ASYNC)
                return StartAsyncWarmPreload();
            return true;
        }
        if (RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ASYNC)
            return StartAsyncWarmPreload();

        RDF_DemRuntimeCache warm = new RDF_DemRuntimeCache();
        warm.SetPreloadAll(true);
        if (!warm.InitializeForCurrentWorld())
        {
            Print("[RDF DEM Runtime] warm preload deferred: world not ready", LogLevel.WARNING);
            return false;
        }
        return warm.EnsurePreloaded();
    }

    //--------------------------------------------------------------------------------------------
    static bool StartAsyncWarmPreload()
    {
        if (s_AsyncRunning)
            return true;
        if (!IsAuthorityPeer())
            return false;

        RDF_DemRuntimeCache warm = new RDF_DemRuntimeCache();
        warm.SetPreloadAll(true);
        if (!warm.InitializeForCurrentWorld())
        {
            Print("[RDF DEM Runtime] async warm preload deferred: world not ready", LogLevel.WARNING);
            return false;
        }
        if (!warm.m_Manifest || !warm.m_Manifest.m_IsSurfacePack)
        {
            // Non-SURF packs: fall back to one-shot tile preload on first Ensure.
            Print("[RDF DEM Runtime] async warm skipped (not SURF pack); will preload on first use");
            return false;
        }

        // SURF already shared: only warm HEIGHT if the pack is present and missing.
        if (s_SharedSurfReady)
        {
            if (!warm.m_Manifest.m_HasHeightPack)
                return true;
            if (s_SharedHeightReady)
                return true;
            s_AsyncManifest = warm.m_Manifest;
            s_AsyncWorldKey = warm.m_WorldKey;
            s_AsyncWorldSurf = null;
            s_AsyncWall0 = System.GetTickCount();
            s_AsyncRunning = true;
            if (!BeginAsyncHeightPhase())
            {
                FinishAsyncWarmJob();
                return false;
            }
            ScheduleAsyncPump();
            return true;
        }

        array<int> worldSurf;
        int cellsX;
        int cellsZ;
        if (!RDF_DemSurfaceJsonPack.BeginWorldSurfaceGrid(
            warm.m_Manifest, worldSurf, cellsX, cellsZ))
        {
            Print("[RDF DEM Runtime] async warm failed to allocate SURF grid", LogLevel.WARNING);
            return false;
        }

        s_AsyncManifest = warm.m_Manifest;
        s_AsyncWorldKey = warm.m_WorldKey;
        s_AsyncWorldSurf = worldSurf;
        s_AsyncWorldY = null;
        s_AsyncCellsX = cellsX;
        s_AsyncCellsZ = cellsZ;
        s_AsyncPhase = 0;
        s_AsyncTileIz = 0;
        s_AsyncEntryIdx = 0;
        s_AsyncLoaded = 0;
        s_AsyncFailed = 0;
        s_AsyncWall0 = System.GetTickCount();
        s_AsyncRunning = true;
        Print(string.Format(
            "[RDF DEM Runtime] async SURF warm start world=%1 rows=%2 budgetMs=%3 heightPack=%4",
            s_AsyncWorldKey,
            s_AsyncManifest.m_TileCountZ.ToString(),
            RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_FRAME_BUDGET_MS.ToString(),
            BoolToStaticFlag(s_AsyncManifest.m_HasHeightPack)));
        ScheduleAsyncPump();
        return true;
    }

    //--------------------------------------------------------------------------------------------
    static void ScheduleAsyncPump()
    {
        if (s_AsyncPumpScheduled)
            return;
        if (!GetGame() || !GetGame().GetCallqueue())
            return;
        s_AsyncPumpScheduled = true;
        GetGame().GetCallqueue().CallLater(StaticPumpAsyncWarmPreload, 0, false);
    }

    //--------------------------------------------------------------------------------------------
    static void StaticPumpAsyncWarmPreload()
    {
        s_AsyncPumpScheduled = false;
        if (!PumpAsyncWarmPreloadSlice())
            return;
        ScheduleAsyncPump();
    }

    //--------------------------------------------------------------------------------------------
    // Returns true if more work remains.
    static bool PumpAsyncWarmPreloadSlice()
    {
        if (!s_AsyncRunning || !s_AsyncManifest)
            return false;
        if (s_AsyncPhase == 0 && !s_AsyncWorldSurf)
            return false;
        if (s_AsyncPhase == 1 && !s_AsyncWorldY)
            return false;

        int budgetMs = RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_FRAME_BUDGET_MS;
        if (budgetMs < 1)
            budgetMs = 1;
        int wall0 = System.GetTickCount();
        int rowCount = s_AsyncManifest.m_TileCountZ;

        while (s_AsyncTileIz < rowCount)
        {
            int entryCount;
            if (s_AsyncPhase == 0)
                entryCount = RDF_DemSurfaceJsonPack.GetSurfRowEntryCount(
                    s_AsyncManifest, s_AsyncTileIz);
            else
                entryCount = RDF_DemHeightJsonPack.GetHeightRowEntryCount(
                    s_AsyncManifest, s_AsyncTileIz);

            if (entryCount <= 0)
            {
                s_AsyncFailed = s_AsyncFailed + s_AsyncManifest.m_TileCountX;
                s_AsyncTileIz = s_AsyncTileIz + 1;
                s_AsyncEntryIdx = 0;
            }
            else
            {
                while (s_AsyncEntryIdx < entryCount)
                {
                    int oneLoaded;
                    int oneFailed;
                    if (s_AsyncPhase == 0)
                    {
                        RDF_DemSurfaceJsonPack.AppendSurfTileEntry(
                            s_AsyncManifest,
                            s_AsyncWorldSurf,
                            s_AsyncCellsX,
                            s_AsyncTileIz,
                            s_AsyncEntryIdx,
                            oneLoaded,
                            oneFailed);
                    }
                    else
                    {
                        RDF_DemHeightJsonPack.AppendHeightTileEntry(
                            s_AsyncManifest,
                            s_AsyncWorldY,
                            s_AsyncCellsX,
                            s_AsyncTileIz,
                            s_AsyncEntryIdx,
                            oneLoaded,
                            oneFailed);
                    }
                    s_AsyncLoaded = s_AsyncLoaded + oneLoaded;
                    s_AsyncFailed = s_AsyncFailed + oneFailed;
                    s_AsyncEntryIdx = s_AsyncEntryIdx + 1;

                    int elapsed = System.GetTickCount() - wall0;
                    if (elapsed >= budgetMs)
                        return true;
                }
                s_AsyncTileIz = s_AsyncTileIz + 1;
                s_AsyncEntryIdx = 0;
            }

            int rowElapsed = System.GetTickCount() - wall0;
            if (rowElapsed >= budgetMs)
                return true;
        }

        if (s_AsyncPhase == 0)
        {
            RDF_DemSurfaceJsonPack.ClearRowCache();
            PublishSharedSurf(s_AsyncWorldKey, s_AsyncWorldSurf, s_AsyncCellsX, s_AsyncCellsZ);

            float surfMs = System.GetTickCount() - s_AsyncWall0;
            if (surfMs < 0.0)
                surfMs = 0.0;
            Print(string.Format(
                "[RDF DEM Runtime] async SURF warm done world=%1 cells=%2x%3 tilesOk=%4 fail=%5 ms=%6",
                s_AsyncWorldKey,
                s_AsyncCellsX.ToString(),
                s_AsyncCellsZ.ToString(),
                s_AsyncLoaded.ToString(),
                s_AsyncFailed.ToString(),
                (Math.Round(surfMs * 10.0) * 0.1).ToString()), LogLevel.NORMAL);

            if (BeginAsyncHeightPhase())
                return true;

            FinishAsyncWarmJob();
            return false;
        }

        RDF_DemHeightJsonPack.ClearRowCache();
        PublishSharedHeight(s_AsyncWorldKey, s_AsyncWorldY, s_AsyncCellsX, s_AsyncCellsZ);

        float heightMs = System.GetTickCount() - s_AsyncWall0;
        if (heightMs < 0.0)
            heightMs = 0.0;
        Print(string.Format(
            "[RDF DEM Runtime] async HEIGHT warm done world=%1 cells=%2x%3 tilesOk=%4 fail=%5 ms=%6",
            s_AsyncWorldKey,
            s_AsyncCellsX.ToString(),
            s_AsyncCellsZ.ToString(),
            s_AsyncLoaded.ToString(),
            s_AsyncFailed.ToString(),
            (Math.Round(heightMs * 10.0) * 0.1).ToString()), LogLevel.NORMAL);

        FinishAsyncWarmJob();
        return false;
    }

    //--------------------------------------------------------------------------------------------
    protected static bool BeginAsyncHeightPhase()
    {
        if (!s_AsyncManifest || !s_AsyncManifest.m_HasHeightPack)
            return false;

        array<float> worldY;
        int cellsX;
        int cellsZ;
        if (!RDF_DemHeightJsonPack.BeginWorldHeightGrid(
            s_AsyncManifest, worldY, cellsX, cellsZ))
        {
            Print("[RDF DEM Runtime] async HEIGHT allocate failed", LogLevel.WARNING);
            return false;
        }

        s_AsyncWorldY = worldY;
        s_AsyncCellsX = cellsX;
        s_AsyncCellsZ = cellsZ;
        s_AsyncPhase = 1;
        s_AsyncTileIz = 0;
        s_AsyncEntryIdx = 0;
        s_AsyncLoaded = 0;
        s_AsyncFailed = 0;
        // Reset the phase timer so the "async HEIGHT warm done ... ms=" log
        // measures only the HEIGHT decode, not SURF + HEIGHT total.
        s_AsyncWall0 = System.GetTickCount();
        Print(string.Format(
            "[RDF DEM Runtime] async HEIGHT warm start world=%1 rows=%2",
            s_AsyncWorldKey,
            s_AsyncManifest.m_TileCountZ.ToString()), LogLevel.NORMAL);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static void FinishAsyncWarmJob()
    {
        s_AsyncRunning = false;
        s_AsyncManifest = null;
        s_AsyncWorldSurf = null;
        s_AsyncWorldY = null;
        s_AsyncWorldKey = string.Empty;
        s_AsyncPhase = 0;
        s_AsyncEntryIdx = 0;
    }

    //--------------------------------------------------------------------------------------------
    static void CancelAsyncWarmPreload()
    {
        s_AsyncRunning = false;
        s_AsyncPumpScheduled = false;
        s_AsyncManifest = null;
        s_AsyncWorldSurf = null;
        s_AsyncWorldY = null;
        s_AsyncWorldKey = string.Empty;
        s_AsyncPhase = 0;
        s_AsyncTileIz = 0;
        s_AsyncEntryIdx = 0;
        s_AsyncLoaded = 0;
        s_AsyncFailed = 0;
        RDF_DemSurfaceJsonPack.ClearRowCache();
        RDF_DemHeightJsonPack.ClearRowCache();
    }

    //--------------------------------------------------------------------------------------------
    // Finish remaining async work synchronously (tests that need resident SURF/HEIGHT now).
    static bool FlushAsyncWarmPreload()
    {
        if (!s_AsyncRunning)
            return s_SharedSurfReady;
        DrainAsyncPhaseFully();
        return s_SharedSurfReady;
    }

    //--------------------------------------------------------------------------------------------
    protected static void DrainAsyncPhaseFully()
    {
        if (!s_AsyncRunning || !s_AsyncManifest)
            return;

        int safety = 0;
        while (s_AsyncRunning && safety < 1000000)
        {
            safety = safety + 1;
            int rowCount = s_AsyncManifest.m_TileCountZ;

            while (s_AsyncTileIz < rowCount)
            {
                int entryCount;
                if (s_AsyncPhase == 0)
                    entryCount = RDF_DemSurfaceJsonPack.GetSurfRowEntryCount(
                        s_AsyncManifest, s_AsyncTileIz);
                else
                    entryCount = RDF_DemHeightJsonPack.GetHeightRowEntryCount(
                        s_AsyncManifest, s_AsyncTileIz);

                if (entryCount <= 0)
                {
                    s_AsyncFailed = s_AsyncFailed + s_AsyncManifest.m_TileCountX;
                    s_AsyncTileIz = s_AsyncTileIz + 1;
                    s_AsyncEntryIdx = 0;
                    continue;
                }

                while (s_AsyncEntryIdx < entryCount)
                {
                    int oneLoaded;
                    int oneFailed;
                    if (s_AsyncPhase == 0)
                    {
                        RDF_DemSurfaceJsonPack.AppendSurfTileEntry(
                            s_AsyncManifest,
                            s_AsyncWorldSurf,
                            s_AsyncCellsX,
                            s_AsyncTileIz,
                            s_AsyncEntryIdx,
                            oneLoaded,
                            oneFailed);
                    }
                    else
                    {
                        RDF_DemHeightJsonPack.AppendHeightTileEntry(
                            s_AsyncManifest,
                            s_AsyncWorldY,
                            s_AsyncCellsX,
                            s_AsyncTileIz,
                            s_AsyncEntryIdx,
                            oneLoaded,
                            oneFailed);
                    }
                    s_AsyncLoaded = s_AsyncLoaded + oneLoaded;
                    s_AsyncFailed = s_AsyncFailed + oneFailed;
                    s_AsyncEntryIdx = s_AsyncEntryIdx + 1;
                }
                s_AsyncTileIz = s_AsyncTileIz + 1;
                s_AsyncEntryIdx = 0;
            }

            if (s_AsyncPhase == 0)
            {
                RDF_DemSurfaceJsonPack.ClearRowCache();
                PublishSharedSurf(
                    s_AsyncWorldKey, s_AsyncWorldSurf, s_AsyncCellsX, s_AsyncCellsZ);
                Print(string.Format(
                    "[RDF DEM Runtime] async SURF warm flushed world=%1 tilesOk=%2 fail=%3",
                    s_AsyncWorldKey,
                    s_AsyncLoaded.ToString(),
                    s_AsyncFailed.ToString()), LogLevel.NORMAL);
                if (!BeginAsyncHeightPhase())
                {
                    FinishAsyncWarmJob();
                    return;
                }
                continue;
            }

            RDF_DemHeightJsonPack.ClearRowCache();
            PublishSharedHeight(
                s_AsyncWorldKey, s_AsyncWorldY, s_AsyncCellsX, s_AsyncCellsZ);
            Print(string.Format(
                "[RDF DEM Runtime] async HEIGHT warm flushed world=%1 tilesOk=%2 fail=%3",
                s_AsyncWorldKey,
                s_AsyncLoaded.ToString(),
                s_AsyncFailed.ToString()), LogLevel.NORMAL);
            FinishAsyncWarmJob();
            return;
        }
    }

    //--------------------------------------------------------------------------------------------
    void SetPreloadAll(bool preloadAll)
    {
        m_PreloadAll = preloadAll;
    }

    //--------------------------------------------------------------------------------------------
    bool IsFullyResident()
    {
        if (m_WorldSurfReady)
        {
            if (NeedsHeightRam() && !m_WorldHeightReady)
                return false;
            return true;
        }
        return m_FullyResident;
    }

    //--------------------------------------------------------------------------------------------
    protected bool NeedsHeightRam()
    {
        if (!m_Manifest)
            return false;
        return m_Manifest.m_HasHeightPack;
    }

    //--------------------------------------------------------------------------------------------
    void SetMaxTiles(int maxTiles)
    {
        // Do not shrink a resident world preload (scanner may still pass LRU=16).
        if (m_FullyResident || m_WorldSurfReady)
            return;

        m_MaxTiles = Math.Max(1, maxTiles);
        while (m_TilesByKey.Count() > m_MaxTiles)
            EvictOldestTile();
    }

    //--------------------------------------------------------------------------------------------
    void BeginScan(int loadBudget)
    {
        if (m_FullyResident || m_WorldSurfReady)
        {
            m_LoadBudgetRemaining = 2147483647;
            return;
        }
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
            m_LiveHeightOnly = false;
            m_WorldKey = string.Empty;
            return false;
        }

        if (m_Ready && m_WorldKey == worldKey && (m_Manifest || m_LiveHeightOnly))
            return true;

        // Avoid reopening missing/unbaked DEM files every scan (disk hitch).
        if (m_LastInitFailed && m_WorldKey == worldKey)
            return false;

        if (m_WorldKey != worldKey)
        {
            m_TilesByKey.Clear();
            m_TileTouchOrder.Clear();
            m_TouchCounter = 0;
            m_WorldKey = worldKey;
            m_LastInitFailed = false;
            m_Ready = false;
            m_LiveHeightOnly = false;
            m_Manifest = null;
            m_FullyResident = false;
            m_WorldSurfReady = false;
            m_WorldSurfClass = null;
            m_WorldHeightReady = false;
            m_WorldHeightY = null;
            m_WorldCellsX = 0;
            m_WorldCellsZ = 0;
        }

        RDF_DemRuntimeManifest manifest;
        if (RDF_DemRuntimeLoader.LoadManifest(worldKey, manifest))
        {
            m_Manifest = manifest;
            m_LiveHeightOnly = false;
            m_Ready = true;
            m_LastInitFailed = false;
            string mode = "CSV";
            if (manifest.m_IsSurfacePack)
                mode = "SURF";
            Print(string.Format(
                "[RDF DEM Runtime] ready world=%1 cell=%2 tile=%3 count=%4x%5 cache=%6 mode=%7 liveY=%8 heightPack=%9",
                worldKey,
                manifest.m_CellM.ToString(),
                manifest.m_TileCells.ToString(),
                manifest.m_TileCountX.ToString(),
                manifest.m_TileCountZ.ToString(),
                m_MaxTiles.ToString(),
                mode,
                BoolToLiveFlag(manifest.m_PreferLiveTerrainY),
                BoolToLiveFlag(manifest.m_HasHeightPack)), LogLevel.NORMAL);
            AdoptSharedSurfIfMatching();
            return true;
        }

        // No packaged surface/DEM: still allow clutter grazing via live surface height.
        BaseWorld world = GetGame().GetWorld();
        if (world)
        {
            m_Manifest = null;
            m_LiveHeightOnly = true;
            m_Ready = true;
            m_LastInitFailed = false;
            Print("[RDF DEM Runtime] ready world=" + worldKey
                + " mode=LIVE (GetSurfaceY, surface=UNKNOWN)", LogLevel.NORMAL);
            return true;
        }

        // World not ready yet — soft fail so a later scan can become LIVE/SURF.
        m_Manifest = null;
        m_Ready = false;
        m_LiveHeightOnly = false;
        return false;
    }

    //--------------------------------------------------------------------------------------------
    // True on dedicated / listen host / single-player. Clients must not preload SURF/DEM.
    static bool IsAuthorityPeer()
    {
        if (RplSession.Mode() == RplMode.Client)
            return false;
        if (!Replication.IsServer())
            return false;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    // Load the whole pack into RAM once. Safe to call every scan; no-ops when resident.
    // Authority-only: pure clients skip (network path does not sample DEM locally).
    bool EnsurePreloaded()
    {
        if (!m_PreloadAll)
            return false;
        if (!IsAuthorityPeer())
        {
            if (!s_LoggedClientPreloadSkip)
            {
                s_LoggedClientPreloadSkip = true;
                Print("[RDF DEM Runtime] SURF/DEM RAM preload skipped on client (authority-only)");
            }
            return false;
        }
        if (!InitializeForCurrentWorld())
            return false;
        if (m_LiveHeightOnly)
            return false;
        if (!m_Manifest)
            return false;
        if (AdoptSharedSurfIfMatching())
        {
            if (IsTerrainRamResident())
                return true;
        }
        if (m_WorldSurfReady || m_FullyResident)
        {
            if (IsTerrainRamResident())
                return true;
            // SURF resident but HEIGHT pack still decoding — keep warming, avoid live Y.
            if (NeedsHeightRam() && !m_WorldHeightReady)
            {
                if (!s_AsyncRunning && m_Manifest.m_IsSurfacePack)
                {
                    if (RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ASYNC)
                        StartAsyncWarmPreload();
                    else
                        PreloadHeightWorldIfPresent();
                }
                return false;
            }
            return true;
        }
        if (s_AsyncRunning)
            return false;

        if (m_Manifest.m_IsSurfacePack)
        {
            // Prefer async: never hitch a scan for a full-world decode.
            if (RDF_DemBakeConstants.RUNTIME_DEM_PRELOAD_ASYNC)
            {
                StartAsyncWarmPreload();
                return false;
            }
            return PreloadSurfaceWorld();
        }

        return PreloadAllTiles();
    }

    //--------------------------------------------------------------------------------------------
    // SURF RAM (+ HEIGHT RAM when pack present) — hot path must not call GetSurfaceY.
    protected bool IsTerrainRamResident()
    {
        if (!m_WorldSurfReady && !m_FullyResident)
            return false;
        if (NeedsHeightRam() && !m_WorldHeightReady)
            return false;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    bool IsReady()
    {
        return m_Ready && (m_Manifest != null || m_LiveHeightOnly);
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
        if (m_LiveHeightOnly)
            return "LIVE";
        if (m_WorldSurfReady && m_WorldHeightReady)
            return "SURF+H";
        if (m_WorldSurfReady)
            return "SURF RAM";
        if (m_FullyResident)
            return "DEM RAM";
        if (m_Manifest && m_Manifest.m_IsSurfacePack)
            return "SURF";
        return "DEM OK";
    }

    //--------------------------------------------------------------------------------------------
    string GetStatsLine()
    {
        return string.Format(
            "hit=%1 miss=%2 load=%3 fail=%4 evict=%5 budget=%6 liveY=%7 bakeY=%8 ram=%9",
            m_StatHits.ToString(),
            m_StatMisses.ToString(),
            m_StatLoads.ToString(),
            m_StatLoadFails.ToString(),
            m_StatEvictions.ToString(),
            m_StatBudgetBlocks.ToString(),
            m_StatLiveHeight.ToString(),
            m_StatBakedHeight.ToString(),
            BoolToLiveFlag(IsFullyResident()));
    }

    //--------------------------------------------------------------------------------------------
    bool TrySampleAt(float worldX, float worldZ, out RDF_DemRuntimeCellSample outSample)
    {
        outSample = null;
        if (!InitializeForCurrentWorld())
            return false;

        if (m_LiveHeightOnly)
            return TrySampleLiveOnly(worldX, worldZ, outSample);

        if (!m_Manifest)
            return false;

        if (worldX < m_Manifest.m_BoundsMinX
            || worldZ < m_Manifest.m_BoundsMinZ
            || worldX >= m_Manifest.m_BoundsMaxX
            || worldZ >= m_Manifest.m_BoundsMaxZ)
        {
            // Outside surface/DEM bounds: still try live height for grazing.
            if (TrySampleLiveOnly(worldX, worldZ, outSample))
                return true;
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

        if (m_WorldSurfReady)
            return TrySampleWorldSurf(globalIx, globalIz, worldX, worldZ, outSample);

        int tileIx = globalIx / m_Manifest.m_TileCells;
        int tileIz = globalIz / m_Manifest.m_TileCells;
        int localIx = globalIx - tileIx * m_Manifest.m_TileCells;
        int localIz = globalIz - tileIz * m_Manifest.m_TileCells;

        RDF_DemRuntimeTile tile;
        if (!TryGetTile(tileIx, tileIz, tile))
        {
            // Tile not loaded yet (budget): fall back to live height + unknown surface.
            if (TrySampleLiveOnly(worldX, worldZ, outSample))
                return true;
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        if (!tile.TryFillCell(localIx, localIz, BorrowScratchSample()))
        {
            if (TrySampleLiveOnly(worldX, worldZ, outSample))
                return true;
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        outSample = m_ScratchSample;
        ApplyLiveTerrainY(worldX, worldZ, outSample);
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
    protected bool PreloadSurfaceWorld()
    {
        if (!m_Manifest || !m_Manifest.m_IsSurfacePack)
            return false;

        int wall0 = System.GetTickCount();
        array<int> worldSurf;
        int cellsX;
        int cellsZ;
        int loaded;
        int failed;
        if (!RDF_DemSurfaceJsonPack.PreloadWorldSurfaceClasses(
            m_Manifest, worldSurf, cellsX, cellsZ, loaded, failed))
        {
            Print("[RDF DEM Runtime] SURF RAM preload failed", LogLevel.WARNING);
            return false;
        }

        m_WorldSurfClass = worldSurf;
        m_WorldCellsX = cellsX;
        m_WorldCellsZ = cellsZ;
        m_WorldSurfReady = true;
        m_FullyResident = true;
        m_TilesByKey.Clear();
        m_TileTouchOrder.Clear();

        PublishSharedSurf(m_WorldKey, worldSurf, cellsX, cellsZ);
        PreloadHeightWorldIfPresent();

        float ms = System.GetTickCount() - wall0;
        if (ms < 0.0)
            ms = 0.0;
        Print(string.Format(
            "[RDF DEM Runtime] SURF RAM preload world=%1 cells=%2x%3 tilesOk=%4 fail=%5 ms=%6 heightRam=%7",
            m_WorldKey,
            cellsX.ToString(),
            cellsZ.ToString(),
            loaded.ToString(),
            failed.ToString(),
            (Math.Round(ms * 10.0) * 0.1).ToString(),
            BoolToLiveFlag(m_WorldHeightReady)), LogLevel.NORMAL);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected bool PreloadHeightWorldIfPresent()
    {
        if (!m_Manifest || !m_Manifest.m_HasHeightPack)
            return false;
        if (m_WorldHeightReady)
            return true;
        if (AdoptSharedHeightIfMatching())
            return true;

        int wall0 = System.GetTickCount();
        array<float> worldY;
        int cellsX;
        int cellsZ;
        int loaded;
        int failed;
        if (!RDF_DemHeightJsonPack.PreloadWorldHeights(
            m_Manifest, worldY, cellsX, cellsZ, loaded, failed))
        {
            Print("[RDF DEM Runtime] HEIGHT RAM preload failed", LogLevel.WARNING);
            return false;
        }

        m_WorldHeightY = worldY;
        m_WorldCellsX = cellsX;
        m_WorldCellsZ = cellsZ;
        m_WorldHeightReady = true;
        PublishSharedHeight(m_WorldKey, worldY, cellsX, cellsZ);

        float ms = System.GetTickCount() - wall0;
        if (ms < 0.0)
            ms = 0.0;
        Print(string.Format(
            "[RDF DEM Runtime] HEIGHT RAM preload world=%1 cells=%2x%3 tilesOk=%4 fail=%5 ms=%6",
            m_WorldKey,
            cellsX.ToString(),
            cellsZ.ToString(),
            loaded.ToString(),
            failed.ToString(),
            (Math.Round(ms * 10.0) * 0.1).ToString()), LogLevel.NORMAL);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected bool AdoptSharedSurfIfMatching()
    {
        if (!s_SharedSurfReady || !s_SharedSurfClass)
            return false;
        if (m_WorldKey.IsEmpty() || s_SharedWorldKey != m_WorldKey)
            return false;
        if (!m_Manifest || !m_Manifest.m_IsSurfacePack)
            return false;

        m_WorldSurfClass = s_SharedSurfClass;
        m_WorldCellsX = s_SharedCellsX;
        m_WorldCellsZ = s_SharedCellsZ;
        m_WorldSurfReady = true;
        m_FullyResident = true;
        m_TilesByKey.Clear();
        m_TileTouchOrder.Clear();
        AdoptSharedHeightIfMatching();
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected bool AdoptSharedHeightIfMatching()
    {
        if (!s_SharedHeightReady || !s_SharedWorldY)
            return false;
        if (m_WorldKey.IsEmpty() || s_SharedWorldKey != m_WorldKey)
            return false;
        if (!m_Manifest || !m_Manifest.m_HasHeightPack)
            return false;

        m_WorldHeightY = s_SharedWorldY;
        m_WorldCellsX = s_SharedCellsX;
        m_WorldCellsZ = s_SharedCellsZ;
        m_WorldHeightReady = true;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected static void PublishSharedSurf(
        string worldKey,
        notnull array<int> worldSurf,
        int cellsX,
        int cellsZ)
    {
        s_SharedWorldKey = worldKey;
        s_SharedSurfClass = worldSurf;
        s_SharedCellsX = cellsX;
        s_SharedCellsZ = cellsZ;
        s_SharedSurfReady = true;
    }

    //--------------------------------------------------------------------------------------------
    protected static void PublishSharedHeight(
        string worldKey,
        notnull array<float> worldY,
        int cellsX,
        int cellsZ)
    {
        s_SharedWorldKey = worldKey;
        s_SharedWorldY = worldY;
        s_SharedCellsX = cellsX;
        s_SharedCellsZ = cellsZ;
        s_SharedHeightReady = true;
    }

    //--------------------------------------------------------------------------------------------
    protected bool PreloadAllTiles()
    {
        if (!m_Manifest)
            return false;

        int total = m_Manifest.m_TileCountX * m_Manifest.m_TileCountZ;
        if (total <= 0)
            return false;

        int wall0 = System.GetTickCount();
        m_MaxTiles = Math.Max(m_MaxTiles, total);
        m_LoadBudgetRemaining = 2147483647;

        int loaded = 0;
        int failed = 0;
        for (int tileIz = 0; tileIz < m_Manifest.m_TileCountZ; tileIz++)
        {
            for (int tileIx = 0; tileIx < m_Manifest.m_TileCountX; tileIx++)
            {
                RDF_DemRuntimeTile tile;
                if (TryGetTile(tileIx, tileIz, tile))
                    loaded = loaded + 1;
                else
                    failed = failed + 1;
            }
        }

        m_FullyResident = loaded > 0;
        PreloadHeightWorldIfPresent();
        float ms = System.GetTickCount() - wall0;
        if (ms < 0.0)
            ms = 0.0;
        Print(string.Format(
            "[RDF DEM Runtime] DEM RAM preload world=%1 tilesOk=%2 fail=%3 resident=%4 ms=%5 heightRam=%6",
            m_WorldKey,
            loaded.ToString(),
            failed.ToString(),
            m_TilesByKey.Count().ToString(),
            (Math.Round(ms * 10.0) * 0.1).ToString(),
            BoolToLiveFlag(m_WorldHeightReady)), LogLevel.NORMAL);
        return m_FullyResident;
    }

    //--------------------------------------------------------------------------------------------
    protected bool TrySampleWorldSurf(
        int globalIx,
        int globalIz,
        float worldX,
        float worldZ,
        out RDF_DemRuntimeCellSample outSample)
    {
        outSample = null;
        if (!m_WorldSurfClass)
            return false;
        if (globalIx < 0 || globalIz < 0
            || globalIx >= m_WorldCellsX
            || globalIz >= m_WorldCellsZ)
        {
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        int idx = globalIz * m_WorldCellsX + globalIx;
        if (idx < 0 || idx >= m_WorldSurfClass.Count())
        {
            m_StatMisses = m_StatMisses + 1;
            return false;
        }

        RDF_DemRuntimeCellSample sample = BorrowScratchSample();
        sample.m_Valid = true;
        sample.m_TerrainY = 0.0;
        sample.m_SurfaceClass = m_WorldSurfClass.Get(idx);
        sample.m_DensityGcm3 = 0.5;
        sample.m_NSpans = 1;

        float y = 0.0;
        bool haveY = false;
        if (TryGetBakedHeightAt(globalIx, globalIz, y))
        {
            haveY = true;
            m_StatBakedHeight = m_StatBakedHeight + 1;
        }
        else if (AllowLiveTerrainQuery())
        {
            if (TryGetLiveTerrainY(worldX, worldZ, y))
            {
                haveY = true;
                m_StatLiveHeight = m_StatLiveHeight + 1;
            }
        }

        if (haveY)
            sample.m_TerrainY = y;
        sample.m_SpanLo.Insert(sample.m_TerrainY);
        sample.m_SpanHi.Insert(sample.m_TerrainY);
        outSample = sample;
        m_StatHits = m_StatHits + 1;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected bool TrySampleLiveOnly(
        float worldX,
        float worldZ,
        out RDF_DemRuntimeCellSample outSample)
    {
        outSample = null;
        float liveY;
        if (!TryGetLiveTerrainY(worldX, worldZ, liveY))
            return false;

        RDF_DemRuntimeCellSample sample = BorrowScratchSample();
        sample.m_Valid = true;
        sample.m_TerrainY = liveY;
        sample.m_SurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        sample.m_DensityGcm3 = 0.5;
        sample.m_NSpans = 1;
        sample.m_SpanLo.Insert(liveY);
        sample.m_SpanHi.Insert(liveY);
        outSample = sample;
        m_StatLiveHeight = m_StatLiveHeight + 1;
        m_StatHits = m_StatHits + 1;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    // Hot-path scratch. Valid only until the next BorrowScratchSample / TrySampleAt.
    protected RDF_DemRuntimeCellSample BorrowScratchSample()
    {
        if (!m_ScratchSample)
            m_ScratchSample = new RDF_DemRuntimeCellSample();
        m_ScratchSample.m_Valid = false;
        m_ScratchSample.m_TerrainY = 0.0;
        m_ScratchSample.m_SlopeDeg = 0.0;
        m_ScratchSample.m_WaterDepthM = 0.0;
        m_ScratchSample.m_WaterType = 0;
        m_ScratchSample.m_OccGround = 0;
        m_ScratchSample.m_EntityHits = 0;
        m_ScratchSample.m_DensityGcm3 = 0.0;
        m_ScratchSample.m_SurfaceClass = ERDF_DemSurfaceClass.RDF_DEM_SURF_UNKNOWN;
        m_ScratchSample.m_NSpans = 0;
        if (m_ScratchSample.m_SpanLo)
            m_ScratchSample.m_SpanLo.Clear();
        if (m_ScratchSample.m_SpanHi)
            m_ScratchSample.m_SpanHi.Clear();
        return m_ScratchSample;
    }

    //--------------------------------------------------------------------------------------------
    // GetSurfaceY only when HEIGHT RAM is unavailable and live Y is allowed.
    protected bool AllowLiveTerrainQuery()
    {
        if (m_WorldHeightReady)
            return false;
        if (!m_Manifest)
            return true;
        if (m_Manifest.m_HasHeightPack && !m_Manifest.m_PreferLiveTerrainY)
            return false;
        if (!m_Manifest.m_PreferLiveTerrainY)
            return false;
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected void ApplyLiveTerrainY(
        float worldX,
        float worldZ,
        notnull RDF_DemRuntimeCellSample sample)
    {
        if (!sample)
            return;

        // Prefer packed HEIGHT RAM when resident (grid-aligned with SURF/CSV).
        if (m_Manifest && m_WorldHeightReady)
        {
            float relX = worldX - m_Manifest.m_BoundsMinX;
            float relZ = worldZ - m_Manifest.m_BoundsMinZ;
            int globalIx = Math.Floor(relX / m_Manifest.m_CellM);
            int globalIz = Math.Floor(relZ / m_Manifest.m_CellM);
            float bakedY;
            if (TryGetBakedHeightAt(globalIx, globalIz, bakedY))
            {
                sample.m_TerrainY = bakedY;
                if (sample.m_NSpans >= 1 && sample.m_SpanLo && sample.m_SpanHi)
                {
                    if (sample.m_SpanLo.Count() > 0)
                        sample.m_SpanLo.Set(0, bakedY);
                    if (sample.m_SpanHi.Count() > 0)
                        sample.m_SpanHi.Set(0, bakedY);
                }
                m_StatBakedHeight = m_StatBakedHeight + 1;
                return;
            }
            // HEIGHT RAM resident but cell miss: do not fall back to engine.
            return;
        }

        if (!AllowLiveTerrainQuery())
            return;

        float liveY;
        if (!TryGetLiveTerrainY(worldX, worldZ, liveY))
            return;

        sample.m_TerrainY = liveY;
        if (sample.m_NSpans >= 1 && sample.m_SpanLo && sample.m_SpanHi)
        {
            if (sample.m_SpanLo.Count() > 0)
                sample.m_SpanLo.Set(0, liveY);
            if (sample.m_SpanHi.Count() > 0)
                sample.m_SpanHi.Set(0, liveY);
        }
        m_StatLiveHeight = m_StatLiveHeight + 1;
    }

    //--------------------------------------------------------------------------------------------
    protected bool TryGetBakedHeightAt(int globalIx, int globalIz, out float outY)
    {
        outY = 0.0;
        if (!m_WorldHeightReady || !m_WorldHeightY)
            return false;
        if (globalIx < 0 || globalIz < 0
            || globalIx >= m_WorldCellsX
            || globalIz >= m_WorldCellsZ)
        {
            return false;
        }

        int idx = globalIz * m_WorldCellsX + globalIx;
        if (idx < 0 || idx >= m_WorldHeightY.Count())
            return false;
        outY = m_WorldHeightY.Get(idx);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected bool TryGetLiveTerrainY(float worldX, float worldZ, out float outY)
    {
        outY = 0.0;
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;
        outY = world.GetSurfaceY(worldX, worldZ);
        return true;
    }

    //--------------------------------------------------------------------------------------------
    protected string BoolToLiveFlag(bool value)
    {
        if (value)
            return "1";
        return "0";
    }

    //--------------------------------------------------------------------------------------------
    protected static string BoolToStaticFlag(bool value)
    {
        if (value)
            return "1";
        return "0";
    }

    //--------------------------------------------------------------------------------------------
    protected void EnsureCapacityForInsert()
    {
        if (m_FullyResident)
            return;
        while (m_TilesByKey.Count() >= m_MaxTiles)
            EvictOldestTile();
    }

    //--------------------------------------------------------------------------------------------
    protected void EvictOldestTile()
    {
        if (m_FullyResident)
            return;

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
        m_StatLiveHeight = 0;
        m_StatBakedHeight = 0;
    }
}
