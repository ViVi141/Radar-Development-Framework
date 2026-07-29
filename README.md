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
  Radar/     Core / Physics / EW / Network / Visual / UI / Demo / Util
  DEM/       烘焙 + Runtime/（manifest/tile 解析与缓存）
scripts/WorkbenchGame/RDF/
  RDF_DemBakePlugin.c / RDF_RadarSignatureBakePlugin.c
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

模组作者首选门面（详见 [docs/RADAR_API.md](docs/RADAR_API.md)）：

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
sensor.Tick(subject, null);
array<ref RDF_RadarTarget> plots = sensor.GetPlots();
array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
```

进图演示任选其一：

1. `SCR_BaseGameMode.SetRadarBootstrapEnabled(true);`
2. 场景实体挂 `RDF_RadarBootstrap`
3. 脚本手动启动：

```c
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
RDF_RadarAutoRunner.SetDemoEnabled(true);
RDF_RadarAutoRunner.SetHudEnabled(true);   // PPI HUD（品红=辐射源 / 绿=载具 / 橙=弹丸）
// PPI 为北向上：绿色扫线 = 实体朝向(transform[2])，不是自由视角相机方向
```

自动化测试（Script Debugger，**一次只跑一个**；共用 AutoRunner 作 Tick/HUD 宿主，并行会互相覆盖配置）：

测试通过 **`RDF_RadarSensor` 公共 API** 配置与读数（`Configure` / `GetPlots` / `GetTracks` / `GetLockedTarget` / `ResetSession` / `ClearDemCache`），
不再经 `SetDemoConfig` / `GetLastTargets` 等演示壳层间接调用。预设从
`RDF_RadarSensor.CreateSearchSettings` / `CreateStareSettings` / `CreateWlrSettings` 起步。

```c
// 推荐：顺序跑完全部（理想档）
RDF_RadarAutoTestSuite.StartAll();
// 逼真档（测量噪声 / 热填空 / 大气等加压，误差带验收）
RDF_RadarAutoTestSuite.StartAllRealistic();

// 手动摆车看 PPI（物理检测，默认关 MTI）
RDF_RadarManualDemo.Start();
RDF_RadarManualDemo.Probe();   // 诊断 Det 0/0
RDF_RadarManualDemo.Stop();

// 或单独跑：
RDF_RadarAutoTest.Start();                 // DEM 杂波回归
RDF_RadarBallisticsAutoTest.Start();       // 弹道/WLR 数学
RDF_RadarShellFireAutoTest.Start();        // 实弹放炮 + WLR
RDF_RadarAirborneScanTest.StartKeepTarget(); // 空中目标 + PPI
RDF_RadarLockAutoTest.Start();             // 空中目标锁定状态机
RDF_RadarPerfAutoTest.Start();             // 扫描/弹道性能开销
```

各测试的场景与雷达配置：

| 测试 | 目标 | Sensor 预设 | 验证点 |
|---|---|---|---|
| Ballistics | 纯数学，无实体 | — | 弹道积分、反推、WLR 解算 |
| DEM AutoTest | 3 架 Mi-8 悬停在视轴 400/700/1000 m | `CreateSearchSettings` + 90° 宽波束 | 杂波随 DEM 开关/scale/budget 正确变化 |
| Lock | 1 架 Mi-8 沿视轴 800 m 处飞半径 120 m 跑道（方位摆动 ±8.5°） | `CreateStareSettings` + 20° 火控窄波束 | SEARCH→ACQUIRING→TRACKING→COAST 状态机 |
| Airborne | 1 架 Mi-8 绕场约 345 m 盘旋 | `CreateSearchSettings` + 40° 波束 | 物理检测 + 按载具分类（emitter 计数必须为 0） |
| ShellFire | 实发 82 mm 迫击炮弹（RCS 0.01 m²） | `CreateWlrSettings` | 弹丸发现/检测/跟踪 + 反推炮位 |
| Perf | 轻载 vs 8 辆 UAZ 重载扫描 | light/heavy `CreateSearchSettings` | 弹道微基准 + 平均/P95 扫描墙钟 ms |

测试约定：**不得给被探测物体添加任何探测助攻** —— 不加辐射源标记、不改 RCS、不预先写进散射体表。
目标必须由发现扫掠自行找到，并靠自身回波被检测。各测试的 `discovered_unaided` 检查即用于守住这条线。

两条经验教训（详见 CHANGELOG 2026-07-27）：

- **宽波束 + DEM 杂波会埋掉空中目标**：90° 波束在 300 m 处的杂波单元横向 471 m，杂波截面比 Mi-8 大一个量级
  （实测同一硬件开/关杂波 SNR 相差约 70 dB）。想让目标一直在主瓣里，正确做法是收窄波束并让目标沿视轴运动
  （Lock 测试的跑道航线），而不是把波束开到不真实的宽度。
- **共用 AutoRunner 的测试必须清场**：上一步遗留的航迹会让下一个测试凭空报 TRACKING。
  测试启动时调用 `sensor.ResetSession()`；DEM 测试先预热等目标全部进散射体表再开第一阶段。

公共 API：[docs/RADAR_API.md](docs/RADAR_API.md)  
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
| [docs/API.md](docs/API.md) | LiDAR / Radar API 摘要（雷达契约以 RADAR_API 为准） |
| [docs/RADAR_API.md](docs/RADAR_API.md) | 雷达公共 Sensor 门面 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 架构与扩展 |
| [docs/RADAR_GAME_FRAMEWORK.md](docs/RADAR_GAME_FRAMEWORK.md) | 游戏内雷达框架 |
| [docs/RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) | 能力边界：已有 / 相对现实缺什么 / 还能做什么 |
| [docs/DEM.md](docs/DEM.md) | DEM 烘焙与运行时 |
| [docs/RADAR_REQUIRED_APIS.md](docs/RADAR_REQUIRED_APIS.md) | 引擎 API 对照 |
| [docs/VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) | 载具锁定/打击思路 |
| [docs/OPTIMIZATION_AND_MEMORY.md](docs/OPTIMIZATION_AND_MEMORY.md) | 性能与内存 |
| [docs/LESSONS_FROM_ENGINE.md](docs/LESSONS_FROM_ENGINE.md) | 引擎踩坑与反模式 |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 变更历史 |
| [TODO.md](TODO.md) | 已完成 / 待办 |

---

## 贡献

欢迎 PR / Issue。联系：747384120@qq.com
