// Constants for RDF DEM tile bake (ANNA-style offline heightfield scan).
class RDF_DemBakeConstants
{
    // Profile paths (Workbench profile, not standalone game profile).
    static const string PROFILE_ROOT = "$profile:RDF/";
    static const string BAKE_FLAG_FILE = "$profile:RDF/BakeDemFull.flag";
    static const string DEM_DATA_DIR = "$profile:RDF/DemData/";
    static const string PACKAGED_DEM_DATA_DIR = "DemData/";

    // Surface-class JSON (RDF_SURF_JSON_V1): live GetSurfaceY + packaged surface map.
    static const string DEM_SURF_MANIFEST_NAME = "surf_manifest.json";
    static const string DEM_SURF_CHUNKS_DIR = "surf_chunks/";
    static const string DEM_SURF_MAGIC = "RDF_SURF_JSON_V1";

    // Optional height JSON (RDF_HEIGHT_JSON_V1): offline pack from official .ttile.
    // Same DemData/<world>/ root as SURF; no Workbench plugin; not from RDF bake CSV.
    static const string DEM_HEIGHT_MANIFEST_NAME = "height_manifest.json";
    static const string DEM_HEIGHT_CHUNKS_DIR = "height_chunks/";
    static const string DEM_HEIGHT_MAGIC = "RDF_HEIGHT_JSON_V1";
    // When a height pack is attached, prefer packed Y over BaseWorld.GetSurfaceY.
    static const bool RUNTIME_DEM_PREFER_BAKED_HEIGHT = true;

    // When true, runtime loads the whole world surface/DEM into RAM
    // (SURF uses a compact flat class map; other packs fill the tile cache).
    static const bool RUNTIME_DEM_PRELOAD_ALL = true;
    // Authority peers warm-preload at game start (before radar is used).
    static const bool RUNTIME_DEM_PRELOAD_AT_GAME_START = true;
    // Spread SURF decode across frames (avoids a 10s+ hitch).
    static const bool RUNTIME_DEM_PRELOAD_ASYNC = true;
    // Max wall time spent decoding SURF per pump slice (ms).
    static const int RUNTIME_DEM_PRELOAD_FRAME_BUDGET_MS = 6;
    // Delay so world file / replication mode are ready (ms).
    static const int RUNTIME_DEM_PRELOAD_START_DELAY_MS = 2000;

    // Grid: change these then delete tiles/ and re-bake for finer DEM.
    // Default 2 m matches official .ttile / packaged HEIGHT+SURF workshop packs.
    // Coarser: CELL_M=4; finer: CELL_M=1, TILE_CELLS=128.
    static const float CELL_M = 2.0;
    static const int TILE_CELLS = 32;

    // Throughput: bake every frame, multiple tiles per frame.
    static const float TILE_INTERVAL_S = 0.0;
    static const int TILES_PER_FRAME = 2;

    // Near-ground band used for occ_ground flag (compat with V1/V2).
    static const float OCC_CLEARANCE_M = 25.0;

    // Deep water: RF radar only sees the sea surface. Skip seabed slope/material/
    // entity sampling when depth >= this (meters). terrain_y is written as the
    // water surface height for these cells.
    static const float DEEP_WATER_SURFACE_ONLY_M = 5.0;

    // Per-column height intervals (V3 volumetric profile).
    // Entity spans capture canopy / roofs / bridge decks above the ground slab.
    // Deep-water cells short-circuit before the column sampler runs, so the
    // offshore heap corruption path (tile_6_91) is no longer reachable.
    static const bool ENABLE_COLUMN_ENTITY_QUERY = true;
    static const float COLUMN_Y_MIN_OFFSET = -2.0;
    static const float COLUMN_Y_MAX_OFFSET = 60.0;
    static const int MAX_SPANS = 8;
    static const float MAX_ENTITY_XZ_EXTENT_M = 48.0;
    static const float MAX_ENTITY_Y_EXTENT_M = 80.0;
    static const float SPAN_MERGE_GAP_M = 0.5;
    static const float SPAN_MIN_THICKNESS_M = 0.25;

    // Runtime DEM cache defaults (scanner-side, read-only from $profile DEM V3 CSV).
    static const int RUNTIME_DEM_CACHE_MAX_TILES = 16;
    static const int RUNTIME_DEM_LOADS_PER_SCAN = 2;

    // Delay after game start before trying to begin bake (ms).
    static const int START_DELAY_MS = 5000;
}
