> **Languages / 语言**: [中文](#中文) · [English](#english)

## 中文

# RDF DEM — 烘焙与运行时

DEM / 地表类数据为雷达杂波与离线电磁仿真提供地形基底。

### 仓库卫生

- 离线工具目录索引：[tools/README.md](../tools/README.md)；Windows 入口 `tools/dem/run_tools.ps1`。
- 离线生成物写在 `tools/dem/out/`（png / json / csv / npz）与 `tools/dem/TrainData/`，
  已在根 `.gitignore` 中忽略，**不要提交**。
- 运行时工坊数据目录 `DemData/` 同样 gitignore（体积大，本地/发布另管）。

### 游戏内杂波（推荐）

**地表类**用自建 `RDF_SURF_JSON_V1`（只发 JSON，体积远小于完整 DEM）。  
**高度**优先可选 `RDF_HEIGHT_JSON_V1`：离线从**官方** `worlds/<Name>/Terrain`（`.ttile` HGHT，与 `GetSurfaceY` 同源）打包进模组并进 RAM；缺包或缺 RAM 时回退 `BaseWorld.GetSurfaceY`。打包**不**依赖 Workbench 插件，也**不**依赖 RDF 已烘焙 CSV/`.dem.data`。

```
官方 Terrain.terr + .Data/*.ttile
  → rdf_ttile_unpack.py（可选中间 npz）
  → rdf_dem_pack_height_json.py  → height_manifest.json + height_chunks/
烘焙 CSV / .dem.data（仅地表类）
  → rdf_dem_pack_surface_json.py → surf_manifest.json + surf_chunks/
运行时 TrySampleAt:
  surface_class ← SURF JSON
  terrain_y     ← HEIGHT RAM（有则优先）否则 GetSurfaceY
```

打包：

```powershell
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Arland --from-bin
python tools\dem\rdf_dem_pack_height_json.py --world GM_Arland --terrain "D:\arma_reforger_full\worlds\Arland\Terrain"
# 或已解包：
python tools\dem\rdf_dem_pack_height_json.py --world GM_Eden --from-ttile-npz
```

体积大约：2 m 网格下 SURF 约为 HEIGHT 的一半（Arland ~8 MB / ~16 MB；Eden ~79 MB / ~157 MB）。  
无地表类烘焙源时可用 `--match-height` 先写出与 HEIGHT 同网格的 UNKNOWN SURF（保证运行时可附着高度）；有 `.dem.data` 后再加 `--from-bin` 重采样填类。

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
4. 皆无 → `mode=LIVE`（仅 `GetSurfaceY`，地表类 = UNKNOWN）

采样时：若 HEIGHT 包已进 RAM，**优先烘焙 Y**；否则用 `GetSurfaceY`（含全量 DEM 回退路径上的 live 覆盖，取决于 `m_PreferLiveTerrainY` / `RUNTIME_DEM_PREFER_BAKED_HEIGHT`）。

日志：`mode=SURF|LIVE|CSV`，`liveY=`，`heightPack=`。  
HUD：`SURF` / `SURF RAM` / `SURF+H` / `LIVE` / `DEM OK` / `DEM RAM` / `DEM OFF`。  
默认 `m_DemPreloadAll=true`：权威端把整图 SURF（及可选 HEIGHT）解码进扁平 RAM。  
开局约 2s 后自动异步预热（约 6ms/帧；SURF 完成后接 HEIGHT）；纯客户端跳过。  
扫图时若尚未完成则暂用 LRU / live Y。关：`RUNTIME_DEM_PRELOAD_AT_GAME_START` / `RUNTIME_DEM_PRELOAD_ASYNC`。

### 数据流（开发烘焙）

```
Workbench Play + BakeDemFull.flag
    → RDF_DemTileBake 写出 V3 CSV
    → $profile:RDF/DemData/<world>/manifest.csv + tiles/*.csv
         ├─ 游戏内优先 SURF JSON；否则可读 CSV
         ├─ python tools/dem/rdf_dem_pack_surface_json.py --from-bin → surf_*.json（工坊）
         ├─ python tools/dem/rdf_dem_pack_height_json.py --terrain … → height_*.json（官方 .ttile）
         └─ python tools/dem/rdf_dem_pack.py → TrainData/*.npz（离线仿真）
```

### 游戏内烘焙

1. World Editor：`RDF Bake DEM Heightfield`  
   （或 `$profile:RDF/BakeDemFull.flag` / `SCR_BaseGameMode.RequestDemBake()`）
2. Play 中看 `[RDF DEM Bake] progress=...`
3. 产出 `manifest.csv`、`tiles/tile_*.csv`、`bake_complete.txt`

常量：`RDF_DemBakeConstants`（默认 `CELL_M=2`，`TILE_CELLS=32`，与官方 `.ttile` / HEIGHT 包对齐）。

```powershell
python tools\dem\rdf_dem_bake_help.py
```

### V3 CSV 要点

- 每格：`terrain_y`、坡度、水深、密度、`surface_class`、`n_spans`…
- **游戏内杂波**：SURF + 可选 HEIGHT RAM（否则 `GetSurfaceY`）；span 主要给离线仿真

### 发布格式

| 格式 | 路径 | 说明 |
|------|------|------|
| `RDF_SURF_JSON_V1` | `surf_manifest.json` + `surf_chunks/` | **推荐发布**（地表类） |
| `RDF_HEIGHT_JSON_V1` | `height_manifest.json` + `height_chunks/` | 可选（烘焙高度进 RAM） |

`DemData/` 默认不进 Git；模组内保留 SURF JSON，并可附带 HEIGHT JSON（已无运行时 `.dem.data`）。
`.dem.data` 仅作离线中间格式（`--from-bin`），游戏运行时不再加载。

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

### Repo hygiene

- Tools catalog: [tools/README.md](../tools/README.md); Windows entry `tools/dem/run_tools.ps1`.
- Offline outputs go under `tools/dem/out/` (png / json / csv / npz) and
  `tools/dem/TrainData/`; both are root-`.gitignore`d — **do not commit**.
- Runtime workshop folder `DemData/` is also gitignored (large; manage locally / for publish).

### In-game clutter (recommended)

**Surface class** uses custom `RDF_SURF_JSON_V1` (JSON only; much smaller than a full DEM).  
**Height** prefers optional `RDF_HEIGHT_JSON_V1` packed offline from **official** `worlds/<Name>/Terrain` (`.ttile` HGHT — same source as `GetSurfaceY`); missing pack/RAM falls back to `BaseWorld.GetSurfaceY`. No Workbench plugin; **not** re-baked from RDF V3 CSV / `.dem.data`.

```
Official Terrain.terr + .Data/*.ttile
  → rdf_ttile_unpack.py (optional intermediate npz)
  → rdf_dem_pack_height_json.py  → height_manifest.json + height_chunks/
Bake CSV / .dem.data (surface class only)
  → rdf_dem_pack_surface_json.py → surf_manifest.json + surf_chunks/
Runtime TrySampleAt:
  surface_class ← SURF JSON
  terrain_y     ← HEIGHT RAM (if present) else GetSurfaceY
```

Pack:

```powershell
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Arland --from-bin
python tools\dem\rdf_dem_pack_height_json.py --world GM_Arland --terrain "D:\arma_reforger_full\worlds\Arland\Terrain"
python tools\dem\rdf_dem_pack_height_json.py --world GM_Eden --from-ttile-npz
```

Approx. size: SURF Arland ~2.3 MB, Eden/Cain ~20 MB; HEIGHT ≈ 2× SURF (int16 vs uint8).  
Workshop: Register `surf_*` and optional `height_manifest.json` + `height_chunks/*.json` → Publish.

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
4. None of the above → `mode=LIVE` (`GetSurfaceY` only; surface class = UNKNOWN)

When sampling: if a HEIGHT pack is resident in RAM, **prefer baked Y**; otherwise use `GetSurfaceY` (live override on CSV paths still follows `m_PreferLiveTerrainY` / `RUNTIME_DEM_PREFER_BAKED_HEIGHT`).

Logs: `mode=SURF|LIVE|CSV`, `liveY=`, `heightPack=`.  
HUD: `SURF` / `SURF RAM` / `SURF+H` / `LIVE` / `DEM OK` / `DEM RAM` / `DEM OFF`.  
Default `m_DemPreloadAll=true`: authority decodes whole-world SURF (and optional HEIGHT) into flat RAM.  
~2s after start, async warm (~6ms/frame; HEIGHT follows SURF); pure clients skip.

### Data flow (dev bake)

```
Workbench Play + BakeDemFull.flag
    → RDF_DemTileBake 写出 V3 CSV
    → $profile:RDF/DemData/<world>/manifest.csv + tiles/*.csv
         ├─ 游戏内优先 SURF JSON；否则可读 CSV
         ├─ python tools/dem/rdf_dem_pack_surface_json.py --from-bin → surf_*.json（工坊）
         ├─ python tools/dem/rdf_dem_pack_height_json.py --terrain … → height_*.json（官方 .ttile）
         └─ python tools/dem/rdf_dem_pack.py → TrainData/*.npz（离线仿真）
```

### In-game bake

1. World Editor: `RDF Bake DEM Heightfield`  
   (or `$profile:RDF/BakeDemFull.flag` / `SCR_BaseGameMode.RequestDemBake()`)
2. In Play watch `[RDF DEM Bake] progress=...`
3. Outputs `manifest.csv`, `tiles/tile_*.csv`, `bake_complete.txt`

Constants: `RDF_DemBakeConstants` (defaults `CELL_M=2`, `TILE_CELLS=32`, aligned with official `.ttile` / HEIGHT packs).

```powershell
python tools\dem\rdf_dem_bake_help.py
```

### V3 CSV essentials

- Per cell: `terrain_y`, slope, water depth, density, `surface_class`, `n_spans`, …
- **In-game clutter**: SURF + optional HEIGHT RAM (else `GetSurfaceY`); spans mainly for offline simulation

### Publish format

| Format | Path | Notes |
|--------|------|-------|
| `RDF_SURF_JSON_V1` | `surf_manifest.json` + `surf_chunks/` | **Recommended** (surface class) |
| `RDF_HEIGHT_JSON_V1` | `height_manifest.json` + `height_chunks/` | Optional (baked height RAM) |

`DemData/` is not in Git by default; ship SURF JSON and optionally HEIGHT JSON (no runtime `.dem.data`).
`.dem.data` remains only as an offline intermediate (`--from-bin`); the game runtime no longer loads it.

### Offline npz simulation

```powershell
python tools\dem\rdf_dem_pack.py --world GM_Eden
python tools\dem\rdf_radar_framework_demo.py --preset shorad
```

See: [tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)

### Multiplayer note

DEM/SURF load locally; detection results can sync via `RDF_RadarNetworkComponent` (this does not replace surface files).
