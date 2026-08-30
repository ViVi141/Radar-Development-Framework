> **Languages / 语言**: [中文](#中文) · [English](#english)

## 中文

# RDF DEM — 烘焙与运行时

DEM / 地表类数据为雷达杂波与离线电磁仿真提供地形基底。

**新图操作示例（HEIGHT+SURF 打包与识别）**：[DEVELOPER_RECIPES.md](DEVELOPER_RECIPES.md) §3。

### 仓库卫生

- 离线工具目录索引：[tools/README.md](../tools/README.md)；Windows 入口 `tools/dem/run_tools.ps1`。
- 离线生成物写在 `tools/dem/out/`（png / json / csv / npz）与 `tools/dem/TrainData/`，
  已在根 `.gitignore` 中忽略，**不要提交**。
- 运行时工坊数据目录 `DemData/`（SURF/HEIGHT JSON）**纳入 Git**；profile 下 V3 CSV / `.dem.data` 仍仅本地。

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

运行时 σ⁰ 按**该雷达载频**选 VHF / UHF / L / S / C / X / Ku / K / Ka（`RDF_RadarHardware.GetBand()`）。工坊表的 `band` 字段只覆盖对应那一张；换 P-18 不会继续用 X 波段 σ⁰。介电 / 粗糙度 / 指数仍共用一份。植被体衰减在 VHF/UHF 更弱，Ku/Ka 更强；Ku+ 雨衰与晴空损耗也更陡。

每类字段：`sigma0_ref_db`、`gamma_k`、`clutter_scale`、`dielectric`、`roughness`、`attenuation_db_per_km`  
（conf 中为 `m_fSigma0RefDb` 等 Attribute 名）

Workbench 中可直接编辑 `.conf`；改完后确保 Resource Browser 仍已 Register。

### 运行时查找顺序

1. `$profile:.../DemData/<world>/surf_manifest.json`
2. 模组 `DemData/<world>/surf_manifest.json`（**工坊推荐**）
3. `$profile` V3 CSV（开发全量 DEM）
4. 皆无 → `mode=LIVE`（仅 `GetSurfaceY`，地表类 = UNKNOWN）

### 支持的世界（官方三图）

仓库已内置三张官方地图的 SURF+HEIGHT 包（`DemData/<world>/`）：

| 世界 | SURF | HEIGHT |
|------|------|--------|
| `GM_Eden` | ✅ | ✅ |
| `GM_Cain` | ✅ | ✅ |
| `GM_Arland` | ✅ | ✅ |

### 无烘焙地图（其他模组图）的回退表现

用其他模组地图但没有对应 `DemData/<world>/` 烘焙文件时：

1. 运行时查找走到底 → `mode=LIVE`，HUD 显示 `LIVE`
2. **高度仍可用**：回退 `BaseWorld.GetSurfaceY`（live 地形），通视遮挡 / WLR 地面采样照常
3. **地表类 = UNKNOWN** → 无 DEM σ⁰ 杂波：
   - 雷达**仍能检测目标**（物理检测 + Trace 通视 + SNR 门限都正常）
   - 但**没有地面杂波压制**——杂波不会掩埋/压掉目标，检测偏「容易」、SNR 更高
   - 噪声分母只剩热噪声 + EW 噪声
4. **DEM-LOS 预检空操作**：无 HEIGHT RAM，回退纯 `TraceMove`（稍慢但正确）
5. **无烘焙 HEIGHT RAM**：高度走 live `GetSurfaceY`（无 RAM 优化）

一句话：雷达功能完整，但少了「地面杂波」这一层真实度——检测更干净也更容易，通视优化退化为纯 Trace。

采样时：HEIGHT+SURF 常驻后**只读 RAM**（不再 `GetSurfaceY`）。预热未完成或仅有 SURF、无 HEIGHT 包时，才允许 live Y。  
HUD：`SURF+H` = 两者皆 RAM；`SURF RAM` = 仅地表类（高度仍可能 live）。

**雷达 LOS（给集成者）**：带 `SURF+H` 时默认先用 HEIGHT 挡地形再 Trace；你**不用改 Sensor API**。说明与旋钮表：[RADAR_API.md](RADAR_API.md) § 扫描通视。

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

`DemData/` 的 SURF/HEIGHT JSON **进 Git**；profile 烘焙 CSV / `.dem.data` 仍本地另管。
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
- Runtime workshop folder `DemData/` (SURF/HEIGHT JSON) is **tracked in Git**; profile V3 CSV / `.dem.data` stay local-only.

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

Runtime σ⁰ is selected from **that radar's carrier** (VHF / UHF / L / S / C / X / Ku / K / Ka via `RDF_RadarHardware.GetBand()`). The workshop table's `band` tag overlays only that band; a P-18-like radar no longer keeps the X-band σ⁰ table. Dielectric / roughness / gamma exponents stay shared. Foliage volume attenuation is weaker at VHF/UHF and stronger at Ku/Ka; clear-air and rain loss also steepen above X.

Per-class fields: `sigma0_ref_db`, `gamma_k`, `clutter_scale`, `dielectric`, `roughness`, `attenuation_db_per_km`  
(in conf these are Attribute names such as `m_fSigma0RefDb`)

You can edit `.conf` directly in Workbench; after changes, ensure Resource Browser still has it Registered.

### Runtime lookup order

1. `$profile:.../DemData/<world>/surf_manifest.json`
2. Mod `DemData/<world>/surf_manifest.json` (**workshop recommended**)
3. `$profile` V3 CSV (full DEM for development)
4. None of the above → `mode=LIVE` (`GetSurfaceY` only; surface class = UNKNOWN)

### Supported worlds (official)

The repo ships SURF+HEIGHT packs for the three official maps under `DemData/<world>/`:

| World | SURF | HEIGHT |
|-------|------|--------|
| `GM_Eden` | ✅ | ✅ |
| `GM_Cain` | ✅ | ✅ |
| `GM_Arland` | ✅ | ✅ |

### Fallback on unbaked maps (other mod maps)

On a mod map with no matching `DemData/<world>/` bake files:

1. Runtime lookup bottoms out → `mode=LIVE`, HUD shows `LIVE`
2. **Height still works**: falls back to `BaseWorld.GetSurfaceY` (live terrain); LOS occlusion / WLR ground sampling are unaffected
3. **Surface class = UNKNOWN** → no DEM σ⁰ clutter:
   - Radar **still detects targets** (physical detection + Trace LOS + SNR gate all work)
   - But **no ground-clutter suppression** — clutter never buries/suppresses targets; detection is "easier" with higher SNR
   - The noise denominator is thermal + EW noise only
4. **DEM-LOS precheck is a no-op**: no HEIGHT RAM, falls back to pure `TraceMove` (slightly slower but correct)
5. **No baked HEIGHT RAM**: height uses live `GetSurfaceY` (no RAM optimization)

In short: radar is fully functional, but the "ground clutter" fidelity layer is missing — detection is cleaner/easier, and the LOS optimization degrades to pure Trace.

When sampling: once SURF+HEIGHT are resident, **read RAM only** (no `GetSurfaceY`). Live Y is allowed only while warming or when no HEIGHT pack ships.

**Radar LOS (integrators)**: with `SURF+H`, HEIGHT terrain precheck runs before Trace by default; **no Sensor API change**. Knobs: [RADAR_API.md](RADAR_API.md) § Scan LOS.

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

`DemData/` SURF/HEIGHT JSON is **in Git**; profile bake CSV / `.dem.data` stay local.
`.dem.data` remains only as an offline intermediate (`--from-bin`); the game runtime no longer loads it.

### Offline npz simulation

```powershell
python tools\dem\rdf_dem_pack.py --world GM_Eden
python tools\dem\rdf_radar_framework_demo.py --preset shorad
```

See: [tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)

### Multiplayer note

DEM/SURF load locally; detection results can sync via `RDF_RadarNetworkComponent` (this does not replace surface files).
