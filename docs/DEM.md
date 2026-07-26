# RDF DEM — 烘焙与运行时

DEM（数字高程/表面网格）为雷达杂波与离线电磁仿真提供地形基底。

## 数据流

```
Workbench Play + BakeDemFull.flag
    → RDF_DemTileBake 写出 V3 CSV
    → $profile:RDF/DemData/<world>/manifest.csv
    → $profile:RDF/DemData/<world>/tiles/tile_<ix>_<iz>.csv
         ├─ 游戏内 RDF_DemRuntimeCache 按需加载（雷达杂波）
         └─ python tools/dem/rdf_dem_pack.py → TrainData/*.npz（离线仿真）
```

## 游戏内烘焙

1. World Editor 插件菜单：`RDF Bake DEM Heightfield`  
   （或创建 `$profile:RDF/BakeDemFull.flag` / `SCR_BaseGameMode.RequestDemBake()`）
2. 打开世界后保持 **Play**，看 Script 日志 `[RDF DEM Bake] progress=...`
3. 完成后存在：
   - `manifest.csv`
   - `tiles/tile_*.csv`
   - `bake_complete.txt`

常量见 `scripts/Game/RDF/DEM/RDF_DemBakeConstants.c`（默认 `CELL_M=4`，`TILE_CELLS=32`）。

帮助脚本：

```powershell
python tools\dem\rdf_dem_bake_help.py
```

## V3 要点

- 每格：`terrain_y`、坡度、水深、密度、`surface_class`、`n_spans`、最多 8 组绝对世界 Y 区间
- 深水格走海面捷径（不做海底 Trace）
- 列实体查询可开关：`ENABLE_COLUMN_ENTITY_QUERY`

## 运行时加载（游戏内）

- `RDF_DemRuntimeLoader`：严格解析 manifest / tile，按顺序尝试 `$profile:RDF/DemData/<world>/` → `DemData/<world>/`
- `RDF_DemRuntimeCache`：世界坐标采样 + 默认 16 tile LRU + 每扫描加载预算
- 雷达侧：`RDF_RadarSettings.m_EnableDemClutter`（默认开；无 DEM 时安全退化）

状态：HUD 模式行显示 `DEM OK` / `DEM OFF`。

发布包回退约定：

1. 先在 Workbench 烘焙到 `$profile:RDF/DemData/<world>/...`。
2. 将同目录结构复制到模组根 `DemData/<world>/...`。
3. 发布后运行时仍优先读 `$profile`，缺失时自动回退到模组内只读 DEM。

## 离线打包与仿真

```powershell
python tools\dem\rdf_dem_pack.py --world GM_Eden
python tools\dem\rdf_radar_framework_demo.py --preset shorad
```

离线框架说明：[tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)

## 联机注意

`$profile` DEM 是本机文件；各端自行扫描时需各自具备同一套烘焙数据。雷达检测结果目前不跨机复制。
