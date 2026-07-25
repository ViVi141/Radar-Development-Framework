# Radar Development Framework (RDF)

Arma Reforger 用的模块化传感器开发框架：**LiDAR**、**雷达**、**DEM 烘焙/运行时**。

- **Repository**: [Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **Contact**: 747384120@qq.com
- **License**: Apache-2.0

---

## 模块概览

| 模块 | 状态 | 说明 |
|------|------|------|
| LiDAR | 维护中 | 射线点云、可视化、HUD、CSV、联机同步 |
| Radar | 已落地 | 实体扫描、物理检测、DEM 杂波、PPI、自动化测试 |
| DEM | 已落地 | Workbench 烘焙 V3 CSV + 游戏内 LRU 加载 |

---

## 项目结构

```
scripts/Game/RDF/
  Lidar/     Core / Visual / Util / Demo / Network / UI
  Radar/     Core / Physics / EW / Visual / UI / Demo / Util
  DEM/       烘焙 + Runtime/（manifest/tile 解析与缓存）
scripts/WorkbenchGame/RDF/
  RDF_DemBakePlugin.c
tools/dem/   离线打包、雷达物理原型、框架 Demo
docs/        文档
```

---

## 快速上手 — LiDAR

```c
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

完整 API：[docs/API.md](docs/API.md)

---

## 快速上手 — 雷达

进图后任选其一：

1. `SCR_BaseGameMode.SetRadarBootstrapEnabled(true);`
2. 场景实体挂 `RDF_RadarBootstrap`
3. 脚本手动启动：

```c
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
RDF_RadarAutoRunner.SetDemoEnabled(true);
RDF_RadarAutoRunner.SetHudEnabled(true);   // PPI HUD（品红=辐射源 / 绿=载具 / 橙=弹丸）
```

自动化测试（Script Debugger）：

```c
RDF_RadarAutoTest.Start();                 // DEM 杂波回归
RDF_RadarAirborneScanTest.StartKeepTarget(); // 空中目标 + PPI 全程开启
```

游戏内框架说明：[docs/RADAR_GAME_FRAMEWORK.md](docs/RADAR_GAME_FRAMEWORK.md)  
离线 Python 原型：[tools/dem/RADAR_FRAMEWORK.md](tools/dem/RADAR_FRAMEWORK.md)

---

## 快速上手 — DEM

1. Workbench 插件：`RDF Bake DEM Heightfield`（或写 `BakeDemFull.flag`）
2. Play 保持运行直到 `[RDF DEM Bake] Done`
3. 输出：`$profile:RDF/DemData/<world>/`

详情：[docs/DEM.md](docs/DEM.md)

```powershell
python tools\dem\rdf_dem_bake_help.py
python tools\dem\rdf_dem_pack.py --world GM_Eden
```

---

## 文档索引

| 文件 | 内容 |
|------|------|
| [docs/API.md](docs/API.md) | LiDAR API |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 架构与扩展 |
| [docs/RADAR_GAME_FRAMEWORK.md](docs/RADAR_GAME_FRAMEWORK.md) | 游戏内雷达框架 |
| [docs/DEM.md](docs/DEM.md) | DEM 烘焙与运行时 |
| [docs/RADAR_REQUIRED_APIS.md](docs/RADAR_REQUIRED_APIS.md) | 引擎 API 对照 |
| [docs/VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) | 载具锁定/打击思路 |
| [docs/OPTIMIZATION_AND_MEMORY.md](docs/OPTIMIZATION_AND_MEMORY.md) | 性能与内存 |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 变更历史 |
| [TODO.md](TODO.md) | 已完成 / 待办 |

---

## 贡献

欢迎 PR / Issue。联系：747384120@qq.com
