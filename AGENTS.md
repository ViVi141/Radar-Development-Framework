# AGENTS.md

## Cursor Cloud specific instructions

### 这是什么项目

本仓库是 **Arma Reforger 的游戏模组**（Radar Development Framework），核心代码是 Enforce Script（`scripts/**/*.c`），运行在 Bohemia 的 **Enfusion Engine / Enfusion Workbench**（仅 Windows 的专有 GUI 工具链）。

- **核心模组（LiDAR / Radar / DEM 运行时、Workbench 烘焙插件）无法在本 Linux 无头环境里编译、构建或运行。** 没有针对 `.c` 代码的 CLI 构建/测试；自动化测试只是游戏内脚本测试（README 里的 `RDF_RadarAutoTest.Start()` 等，需在 Workbench Script Debugger 内运行）。改这部分代码时，只能靠静态审查 + 遵守 `docs/DEVELOPMENT.md` 的 Enforce Script 约束（无三目运算符、纯 ASCII、`string.Format` 用 `%1 %2` 位置符等）。

### 本环境里唯一可运行的部分：离线 Python 工具链（`tools/dem/`）

- 依赖 `numpy` + `matplotlib`（无 `requirements.txt`，由 update script 安装）。
- 无 lint 配置、无 pytest 测试套件。可用的“lint/冒烟”手段是语法编译：`python3 -m py_compile tools/dem/*.py`。
- 旗舰 end-to-end 脚本是 `tools/dem/rdf_radar_framework_demo.py`（雷达时域仿真：扫描、多波束、Doppler/MTI/CFAR、alpha-beta 跟踪、可选 EW 干扰），输出到 gitignore 的 `tools/dem/out/`（PNG + detections/tracks CSV）。用法见 `tools/dem/RADAR_FRAMEWORK.md`。

### 关键坑：雷达 demo/preview 需要已烘焙的 DEM 瓦片

`rdf_radar_framework_demo.py`、`rdf_radar_sector_preview.py`、`rdf_dem_preview.py` 会调用 `load_dem()` 读取 `tools/dem/TrainData/<world>/tiles/dataset_*.npz`。这些瓦片正常由**游戏内 Workbench 烘焙**产出，仓库里没有；缺失时脚本直接 `SystemExit("No dataset tiles found ...")`。

- `TrainData/` 和 `*.npz` 都在 `.gitignore` 里。
- 在无头环境冒烟测试雷达链路时，可生成一个**合成 DEM 瓦片**：写一个 `dataset_0_0.npz`，字段为 `terrain_y[H,W]`、`slope_deg`、`water_type`、`occ_ground`、`surface_class`（合法类见 `rdf_radar_materials.py`，如 3=soil）、`density_gcm3`、`entity_hits`、标量 `origin_ix=0`、`origin_iz=0`、`cell_m`（例 4.0）。字段 schema 以 `tools/dem/rdf_dem_io.py: load_dem()` 读取的 key 为准。地形要足够大（默认场景目标最远在半径约 4.5 km 处），且让最高的内部格点（`choose_radar_site`，边距 128）落在低索引处，示例目标才不会越界被跳过。
- 生成瓦片后即可：`cd tools/dem && python3 rdf_radar_framework_demo.py --preset shorad`（加 `--ew` 演示干扰；`--azimuth-count` 可调运行时长）。
