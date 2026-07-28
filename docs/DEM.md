> **Languages / 语言**: [中文](#中文) · [English](#english)

## 中文

# RDF DEM — 烘焙与运行时

DEM / 地表类数据为雷达杂波与离线电磁仿真提供地形基底。

### 游戏内杂波（推荐）

**高度**用引擎实时 `BaseWorld.GetSurfaceY`（官方 `.ttile` 背后数据，精度更好）。  
**地表类**用自建 `RDF_SURF_JSON_V1`（只发 JSON，体积远小于完整 DEM）。

```
烘焙 CSV → rdf_dem_pack_surface_json.py
  → DemData/<world>/surf_manifest.json
  → DemData/<world>/surf_chunks/row_<iz>.json
运行时 TrySampleAt:
  surface_class ← SURF JSON
  terrain_y     ← GetSurfaceY（优先）
```

打包：

```powershell
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Arland --from-bin
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Eden --from-bin
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Cain --from-bin
```

体积大约：Arland ~2.3 MB；Eden/Cain ~20 MB（相对 `.dem.data` ~60 MB）。  
工坊：Resource Browser 选中 `surf_manifest.json` 与 `surf_chunks/*.json` → **Register** → Publish。

### SurfaceTable（类 → 电磁参数）

**工坊推荐**：`RadarData/SurfaceTable.conf`（Enfusion 原生，已带 `.meta`）

| 资源 | 用途 |
|------|------|
| `RadarData/SurfaceTable.conf` | 游戏内主表（`RDF_RadarSurfaceTableConf`） |
| `RadarData/SurfaceTable.json` | Python / profile 覆盖 / JSON 回退 |

加载顺序：`$profile:.../SurfaceTable.json` → 模组 `.conf` → 模组 `.json` → 内置。

每类字段：`sigma0_ref_db`、`gamma_k`、`clutter_scale`、`dielectric`、`roughness`、`attenuation_db_per_km`  
（conf 中为 `m_fSigma0RefDb` 等 Attribute 名）

Workbench 中可直接编辑 `.conf`；改完后确保 Resource Browser 仍已 Register。

### 运行时查找顺序

1. `$profile:.../DemData/<world>/surf_manifest.json`
2. 模组 `DemData/<world>/surf_manifest.json`（**工坊推荐**）
3. `$profile` V3 CSV（开发全量 DEM）
4. `$profile` 全量 DEM JSON / `.dem.data`
5. 模组全量 DEM JSON / `.dem.data` / CSV
6. 皆无 → `mode=LIVE`（仅 `GetSurfaceY`，地表类 = UNKNOWN）

采样时：只要世界已加载，**一律优先用 `GetSurfaceY` 覆盖**烘焙高度（含全量 DEM 回退路径）。

日志：`mode=SURF|LIVE|CSV|JSON|BIN`，`liveY=1`。  
HUD：`SURF` / `LIVE` / `DEM OK` / `DEM OFF`。

### 数据流（开发烘焙）

```
Workbench Play + BakeDemFull.flag
    → RDF_DemTileBake 写出 V3 CSV
    → $profile:RDF/DemData/<world>/manifest.csv + tiles/*.csv
         ├─ 游戏内优先 SURF JSON；否则可读 CSV
         ├─ python tools/dem/rdf_dem_pack_surface_json.py → surf_*.json（工坊）
         ├─ python tools/dem/rdf_dem_pack_bin.py → .dem.data（遗留/离线）
         └─ python tools/dem/rdf_dem_pack.py → TrainData/*.npz（离线仿真）
```

### 游戏内烘焙

1. World Editor：`RDF Bake DEM Heightfield`  
   （或 `$profile:RDF/BakeDemFull.flag` / `SCR_BaseGameMode.RequestDemBake()`）
2. Play 中看 `[RDF DEM Bake] progress=...`
3. 产出 `manifest.csv`、`tiles/tile_*.csv`、`bake_complete.txt`

常量：`RDF_DemBakeConstants`（默认 `CELL_M=4`，`TILE_CELLS=32`）。

```powershell
python tools\dem\rdf_dem_bake_help.py
```

### V3 CSV 要点

- 每格：`terrain_y`、坡度、水深、密度、`surface_class`、`n_spans`…
- **游戏内杂波**：实时高度 + 地表类；span 主要给离线仿真

### 遗留：全量 DEM 包

| 格式 | 路径 | 说明 |
|------|------|------|
| `RDF_DEM_BIN_V1` | `<world>.dem.data` | 含量化高度；工坊曾不稳定 |
| `RDF_DEM_JSON_V1` | `manifest.json` + `jchunks/` | 全量 hex；体积大 |
| `RDF_SURF_JSON_V1` | `surf_manifest.json` + `surf_chunks/` | **推荐发布** |

`DemData/` 默认不进 Git；模组内仅保留 SURF JSON（已无 `.dem.data`）。
离线仿真仍用 `$profile` CSV 或自行 `rdf_dem_pack_bin.py`。

### 离线 npz 仿真

```powershell
python tools\dem\rdf_dem_pack.py --world GM_Eden
python tools\dem\rdf_radar_framework_demo.py --preset shorad
```

说明：[tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)

### 联机注意

DEM/SURF 本机加载；检测结果可通过 `RDF_RadarNetworkComponent` 同步（不能替代地表文件）。

---

## English

# RDF DEM — Bake & Runtime

DEM / surface-class data provides the terrain base for radar clutter and offline EM simulation.

### In-game clutter (recommended)

**Height** uses the engine live `BaseWorld.GetSurfaceY` (data behind official `.ttile`, better precision).  
**Surface class** uses custom `RDF_SURF_JSON_V1` (JSON only; much smaller than a full DEM).

```
烘焙 CSV → rdf_dem_pack_surface_json.py
  → DemData/<world>/surf_manifest.json
  → DemData/<world>/surf_chunks/row_<iz>.json
运行时 TrySampleAt:
  surface_class ← SURF JSON
  terrain_y     ← GetSurfaceY（优先）
```

Pack:

```powershell
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Arland --from-bin
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Eden --from-bin
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Cain --from-bin
```

Approx. size: Arland ~2.3 MB; Eden/Cain ~20 MB (vs `.dem.data` ~60 MB).  
Workshop: in Resource Browser select `surf_manifest.json` and `surf_chunks/*.json` → **Register** → Publish.

### SurfaceTable (class → EM parameters)

**Workshop recommended**: `RadarData/SurfaceTable.conf` (native Enfusion, already has `.meta`)

| Resource | Purpose |
|----------|---------|
| `RadarData/SurfaceTable.conf` | In-game primary table (`RDF_RadarSurfaceTableConf`) |
| `RadarData/SurfaceTable.json` | Python / profile override / JSON fallback |

Load order: `$profile:.../SurfaceTable.json` → mod `.conf` → mod `.json` → built-in.

Per-class fields: `sigma0_ref_db`, `gamma_k`, `clutter_scale`, `dielectric`, `roughness`, `attenuation_db_per_km`  
(in conf these are Attribute names such as `m_fSigma0RefDb`)

You can edit `.conf` directly in Workbench; after changes, ensure Resource Browser still has it Registered.

### Runtime lookup order

1. `$profile:.../DemData/<world>/surf_manifest.json`
2. Mod `DemData/<world>/surf_manifest.json` (**workshop recommended**)
3. `$profile` V3 CSV (full DEM for development)
4. `$profile` full DEM JSON / `.dem.data`
5. Mod full DEM JSON / `.dem.data` / CSV
6. None of the above → `mode=LIVE` (`GetSurfaceY` only; surface class = UNKNOWN)

When sampling: once the world is loaded, **always prefer `GetSurfaceY` to override** baked height (including full-DEM fallback paths).

Logs: `mode=SURF|LIVE|CSV|JSON|BIN`, `liveY=1`.  
HUD: `SURF` / `LIVE` / `DEM OK` / `DEM OFF`.

### Data flow (dev bake)

```
Workbench Play + BakeDemFull.flag
    → RDF_DemTileBake 写出 V3 CSV
    → $profile:RDF/DemData/<world>/manifest.csv + tiles/*.csv
         ├─ 游戏内优先 SURF JSON；否则可读 CSV
         ├─ python tools/dem/rdf_dem_pack_surface_json.py → surf_*.json（工坊）
         ├─ python tools/dem/rdf_dem_pack_bin.py → .dem.data（遗留/离线）
         └─ python tools/dem/rdf_dem_pack.py → TrainData/*.npz（离线仿真）
```

### In-game bake

1. World Editor: `RDF Bake DEM Heightfield`  
   (or `$profile:RDF/BakeDemFull.flag` / `SCR_BaseGameMode.RequestDemBake()`)
2. In Play watch `[RDF DEM Bake] progress=...`
3. Outputs `manifest.csv`, `tiles/tile_*.csv`, `bake_complete.txt`

Constants: `RDF_DemBakeConstants` (defaults `CELL_M=4`, `TILE_CELLS=32`).

```powershell
python tools\dem\rdf_dem_bake_help.py
```

### V3 CSV essentials

- Per cell: `terrain_y`, slope, water depth, density, `surface_class`, `n_spans`, …
- **In-game clutter**: live height + surface class; spans are mainly for offline simulation

### Legacy: full DEM packs

| Format | Path | Notes |
|--------|------|-------|
| `RDF_DEM_BIN_V1` | `<world>.dem.data` | Quantized height; workshop historically unstable |
| `RDF_DEM_JSON_V1` | `manifest.json` + `jchunks/` | Full hex; large |
| `RDF_SURF_JSON_V1` | `surf_manifest.json` + `surf_chunks/` | **Recommended for publish** |

`DemData/` is not in Git by default; the mod keeps only SURF JSON (no `.dem.data`).  
Offline simulation still uses `$profile` CSV or run `rdf_dem_pack_bin.py` yourself.

### Offline npz simulation

```powershell
python tools\dem\rdf_dem_pack.py --world GM_Eden
python tools\dem\rdf_radar_framework_demo.py --preset shorad
```

See: [tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)

### Multiplayer note

DEM/SURF load locally; detection results can sync via `RDF_RadarNetworkComponent` (this does not replace surface files).
