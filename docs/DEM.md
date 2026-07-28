# RDF DEM — 烘焙与运行时

DEM（数字高程/表面网格）为雷达杂波与离线电磁仿真提供地形基底。

## 数据流

```
Workbench Play + BakeDemFull.flag
    → RDF_DemTileBake 写出 V3 CSV
    → $profile:RDF/DemData/<world>/manifest.csv
    → $profile:RDF/DemData/<world>/tiles/tile_<ix>_<iz>.csv
         ├─ 游戏内 RDF_DemRuntimeCache（开发优先读 CSV）
         ├─ python tools/dem/rdf_dem_pack_bin.py → DemData/<world>/<world>.dem.data（工坊）
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

## V3 CSV 要点

- 每格：`terrain_y`、坡度、水深、密度、`surface_class`、`n_spans`、最多 8 组绝对世界 Y 区间
- 深水格走海面捷径（不做海底 Trace）
- 列实体查询可开关：`ENABLE_COLUMN_ENTITY_QUERY`
- **游戏内杂波当前只用** `terrain_y` + `surface_class`；span 供离线仿真

## 二进制包 `RDF_DEM_BIN_V1`（工坊推荐）

CSV 通常**不会**随创意工坊发布。发布用单个 `.dem.data`：

| 项 | 说明 |
|----|------|
| 路径 | `DemData/<world>/<world>.dem.data` |
| 内容 | 杂波核心字段；**无 span**；高程量化 **0.1 m** |
| 体积 | Eden/Cain ~60 MB；Arland ~7 MB（相对 CSV 约 1/13） |

打包：

```powershell
python tools\dem\rdf_dem_pack_bin.py --world GM_Arland
python tools\dem\rdf_dem_pack_bin.py --world GM_Eden
python tools\dem\rdf_dem_pack_bin.py --world GM_Cain
```

默认读 `$profile:.../DemData/<world>`，写出到模组根 `DemData/<world>/<world>.dem.data`。

### Workbench 注册（必做，否则工坊仍可能丢掉）

1. Resource Browser 选中 `DemData/**/*.dem.data`
2. **Register**（生成 `.meta`，Build Presence 含 Runtime）
3. 再 **Publish Project**

地图作者：烘焙自己的世界 → `rdf_dem_pack_bin.py --world <世界名>` → 把 `.dem.data` 放进地图模组 `DemData/<世界名>/` 并 Register。

## 运行时查找顺序

1. `$profile:RDF/DemData/<world>/manifest.csv`（开发）
2. `$profile:.../<world>.dem.data`
3. 模组 `DemData/<world>/<world>.dem.data`（发布）
4. 模组 `DemData/<world>/manifest.csv`（旧 CSV 兼容）

日志：`[RDF DEM Runtime] ready ... mode=CSV|BIN`。  
HUD：`DEM OK` / `DEM OFF`。  
雷达：`m_EnableDemClutter`（默认开；无 DEM 安全退化）。

`DemData/` 默认 **不进 Git**；本机保留 `.dem.data` 即可。  
模组内当前仅有二进制包（已删 CSV tiles / manifest）：

- `GM_Arland/GM_Arland.dem.data`（~7 MB）
- `GM_Eden/GM_Eden.dem.data`（~60 MB）
- `GM_Cain/GM_Cain.dem.data`（~60 MB）

工坊发布 Register 上述 `.dem.data` 即可。

## 离线 npz 仿真

```powershell
python tools\dem\rdf_dem_pack.py --world GM_Eden
python tools\dem\rdf_radar_framework_demo.py --preset shorad
```

说明：[tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)

## 联机注意

DEM 文件本机加载；检测结果可通过 `RDF_RadarNetworkComponent` 服务器权威同步（不能替代 DEM 文件）。
