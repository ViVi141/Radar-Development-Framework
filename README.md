> **Languages / 语言**: [English](#english) · [中文](#中文)

# Radar Development Framework (RDF)

## English

Modular sensor framework for Arma Reforger: **LiDAR**, **Radar**, and **DEM bake/runtime**.

- **Repository**: [Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **Contact**: 747384120@qq.com
- **License**: Apache-2.0

---

### Module overview

| Module | Status | Notes |
|--------|--------|-------|
| LiDAR | Maintaining | Ray point cloud, visualization, HUD, CSV, multiplayer sync |
| Radar | Shipped | Entity scan, physical detection, DEM clutter, PPI, ESM/RWR, authoritative multiplayer, automated tests |
| DEM | Shipped | Bake V3 CSV; publish SURF JSON; runtime GetSurfaceY + SURF (fallback CSV/BIN/LIVE) |

---

### Project structure

```
scripts/Game/RDF/
  Common/    RDF_DebugShapeManager (world-space Shape lifetime)
  Lidar/     Core / Visual / Util / Demo / Network / UI
  Radar/     Core / Physics / EW / Network / Visual / UI / Demo / Util
  DEM/       Bake + Runtime/ (SURF JSON / BIN / CSV parse + LRU)
scripts/WorkbenchGame/RDF/
  RDF_DemBakePlugin.c / RDF_RadarSignatureBakePlugin.c
RadarData/   SurfaceTable.conf (surface-class EM params)
Signatures/  rdf_radar_signatures.conf (target signatures)
UI/layouts/RDF/  RadarPPI.layout / LidarPPI.layout
tools/dem/   Offline pack, radar physics prototypes, framework demos
docs/        Documentation (bilingual — see docs/I18N.md)
```

---

### Quick start — LiDAR

```c
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

Full API: [docs/API.md](docs/API.md)

---

### Quick start — Radar

Preferred facade for mod authors (see [docs/RADAR_API.md](docs/RADAR_API.md)):

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
// 模式：SEARCH / STARE / WLR / ESM
sensor.Tick(subject, null);
array<ref RDF_RadarTarget> plots = sensor.GetPlots();
array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
```

In-mission demo — pick one:

1. `SCR_BaseGameMode.SetRadarBootstrapEnabled(true);`
2. Attach `RDF_RadarBootstrap` to a scene entity
3. Start manually from script:

```c
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
RDF_RadarAutoRunner.SetDemoEnabled(true);
RDF_RadarAutoRunner.SetHudEnabled(true);   // PPI HUD（品红=辐射源 / 绿=载具 / 橙=弹丸）
// PPI 为北向上：绿色扫线 = 实体朝向(transform[2])，不是自由视角相机方向
```

Automated tests (Script Debugger, **run only one at a time**; they share AutoRunner as Tick/HUD host and will overwrite each other's config if parallel):

Tests configure and read via the **`RDF_RadarSensor` public API** (`Configure` / `GetPlots` / `GetTracks` / `GetLockedTarget` / `ResetSession` / `ClearDemCache`),
not through demo-shell indirection like `SetDemoConfig` / `GetLastTargets`. Presets start from
`RDF_RadarSensor.CreateSearchSettings` / `CreateStareSettings` / `CreateWlrSettings`.

```c
// 推荐：顺序跑完全部（理想档）— 仅下列 5 项
RDF_RadarAutoTestSuite.StartAll();
// 逼真档（测量噪声 / 热填空 / 大气等加压，误差带验收）
RDF_RadarAutoTestSuite.StartAllRealistic();

// 手动摆车看 PPI（物理检测，默认关 MTI）
RDF_RadarManualDemo.Start();
RDF_RadarManualDemo.Probe();   // 诊断 Det 0/0
RDF_RadarManualDemo.Stop();

// 套件内单项：
RDF_RadarAutoTest.Start();                 // DEM 杂波回归
RDF_RadarBallisticsAutoTest.Start();       // 弹道/WLR 数学
RDF_RadarShellFireAutoTest.Start();        // 实弹放炮 + WLR
RDF_RadarAirborneScanTest.StartKeepTarget(); // 空中目标 + PPI
RDF_RadarLockAutoTest.Start();             // 空中目标锁定状态机

// 独立回归（不进 StartAll；一次只跑一个）：
RDF_RadarRwrAutoTest.Start();              // RWR 被照射/锁定告警
RDF_RadarEsmArmAutoTest.Start();           // ESM + GetArmAim / 静默丢锁
RDF_RadarRocketLockFireAutoTest.Start();   // 锁定 → Hydra70 制导打击
```

Suite scenarios and radar config:

| Test | Target | Sensor preset | Checks |
|---|---|---|---|
| Ballistics | Pure math, no entities | — | Ballistic integration, back-solve, multi-point vacuum-fit WLR |
| DEM AutoTest | 3× Mi-8 hovering on boresight at 400/700/1000 m | `CreateSearchSettings` + 90° wide beam | Clutter changes correctly with DEM on/off, scale, budget |
| Lock | 1× Mi-8 flying a 120 m-radius racetrack at 800 m on boresight (±8.5° azimuth swing) | `CreateStareSettings` + 20° fire-control narrow beam | SEARCH→ACQUIRING→TRACKING→COAST state machine |
| Airborne | 1× Mi-8 orbiting ~345 m around the field | `CreateSearchSettings` + 40° beam | Physical detection + vehicle classification (emitter count must be 0) |
| ShellFire | Live 82 mm mortar shell (RCS 0.01 m²) | `CreateWlrSettings` | Shell discover/detect/track + gun-position back-solve (ideal ≤250 m / realistic ≤800 m) |

Standalone regressions:

| Test | Checks |
|---|---|
| RWR | SEARCH/TRACK/LOCK warnings on the target entity |
| ESM/ARM | ESM intercept + `GetArmAim`; drop lock after emitter goes silent |
| Rocket Lock-Fire | Lock Mi-8 → Hydra70 BOOST/MIDCOURSE/TERMINAL guidance |

Test rule: **do not give detection assists to objects under test** — no emitter tags, no RCS edits, no pre-writing into the scatterer table.
Targets must be found by discovery sweep and detected from their own returns. Each test's `discovered_unaided` check enforces this line.

Two hard-won lessons (see CHANGELOG 2026-07-27):

- **Wide beam + DEM clutter buries airborne targets**: a 90° beam at 300 m has a clutter cell ~471 m across; clutter RCS is an order of magnitude above a Mi-8
  (measured SNR delta ~70 dB with clutter on vs off on the same hardware). To keep the target in the main lobe, narrow the beam and fly the target along boresight
  (Lock test racetrack), instead of opening an unrealistic beam width.
- **Tests that share AutoRunner must clear session state**: leftover tracks from the previous step make the next test report TRACKING from nowhere.
  Call `sensor.ResetSession()` on test start; DEM tests should warm up until all targets are in the scatterer table before phase one.

Public API: [docs/RADAR_API.md](docs/RADAR_API.md)  
In-game framework notes: [docs/RADAR_GAME_FRAMEWORK.md](docs/RADAR_GAME_FRAMEWORK.md)  
Offline Python prototype: [tools/dem/RADAR_FRAMEWORK.md](tools/dem/RADAR_FRAMEWORK.md)

---

### Quick start — DEM

1. Workbench plugin: `RDF Bake DEM Heightfield` (or write `BakeDemFull.flag`)
2. Keep Play running until `[RDF DEM Bake] Done`
3. Dev output: `$profile:RDF/DemData/<world>/` (V3 CSV)
4. Workshop publish: pack SURF JSON (height at runtime via `GetSurfaceY`)

Details: [docs/DEM.md](docs/DEM.md)

```powershell
python tools\dem\rdf_dem_bake_help.py
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Eden --from-bin
python tools\dem\rdf_dem_pack.py --world GM_Eden
```

---

### Doc index

| File | Content |
|------|---------|
| [docs/API.md](docs/API.md) | LiDAR / Radar API summary (radar contract is RADAR_API) |
| [docs/RADAR_API.md](docs/RADAR_API.md) | Radar public Sensor facade |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Architecture and extension |
| [docs/RADAR_GAME_FRAMEWORK.md](docs/RADAR_GAME_FRAMEWORK.md) | In-game radar framework |
| [docs/RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) | Capability bounds: have / missing vs reality / next |
| [docs/DEM.md](docs/DEM.md) | DEM bake and runtime |
| [docs/RADAR_REQUIRED_APIS.md](docs/RADAR_REQUIRED_APIS.md) | Engine API mapping |
| [docs/VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) | Vehicle lock / engage patterns |
| [docs/OPTIMIZATION_AND_MEMORY.md](docs/OPTIMIZATION_AND_MEMORY.md) | Performance and memory |
| [docs/LESSONS_FROM_ENGINE.md](docs/LESSONS_FROM_ENGINE.md) | Engine pitfalls and anti-patterns |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | Change history |
| [docs/I18N.md](docs/I18N.md) | Language policy / bilingual docs |
| [TODO.md](TODO.md) | Done / todo |

---

### Contributing

PRs and Issues welcome. Contact: 747384120@qq.com

---

## 中文

> 语言约定见 [docs/I18N.md](docs/I18N.md)（双语文档 / Language policy）。

Arma Reforger 用的模块化传感器开发框架：**LiDAR**、**雷达**、**DEM 烘焙/运行时**。

- **Repository**: [Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **Contact**: 747384120@qq.com
- **License**: Apache-2.0

---

### 模块概览

| 模块 | 状态 | 说明 |
|------|------|------|
| LiDAR | 维护中 | 射线点云、可视化、HUD、CSV、联机同步 |
| Radar | 已落地 | 实体扫描、物理检测、DEM 杂波、PPI、ESM/RWR、联机权威、自动化测试 |
| DEM | 已落地 | 烘焙 V3 CSV；发布用 SURF JSON；运行时 GetSurfaceY + SURF（回退 CSV/BIN/LIVE） |

---

### 项目结构

```
scripts/Game/RDF/
  Common/    RDF_DebugShapeManager（世界 Shape 托管）
  Lidar/     Core / Visual / Util / Demo / Network / UI
  Radar/     Core / Physics / EW / Network / Visual / UI / Demo / Util
  DEM/       烘焙 + Runtime/（SURF JSON / BIN / CSV 解析与 LRU）
scripts/WorkbenchGame/RDF/
  RDF_DemBakePlugin.c / RDF_RadarSignatureBakePlugin.c
RadarData/   SurfaceTable.conf（地表类电磁参数）
Signatures/  rdf_radar_signatures.conf（目标特征）
UI/layouts/RDF/  RadarPPI.layout / LidarPPI.layout
tools/dem/   离线打包、雷达物理原型、框架 Demo
docs/        文档（双语，见 docs/I18N.md）
```

---

### 快速上手 — LiDAR

```c
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

完整 API：[docs/API.md](docs/API.md)

---

### 快速上手 — 雷达

模组作者首选门面（详见 [docs/RADAR_API.md](docs/RADAR_API.md)）：

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
// 模式：SEARCH / STARE / WLR / ESM
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
// 推荐：顺序跑完全部（理想档）— 仅下列 5 项
RDF_RadarAutoTestSuite.StartAll();
// 逼真档（测量噪声 / 热填空 / 大气等加压，误差带验收）
RDF_RadarAutoTestSuite.StartAllRealistic();

// 手动摆车看 PPI（物理检测，默认关 MTI）
RDF_RadarManualDemo.Start();
RDF_RadarManualDemo.Probe();   // 诊断 Det 0/0
RDF_RadarManualDemo.Stop();

// 套件内单项：
RDF_RadarAutoTest.Start();                 // DEM 杂波回归
RDF_RadarBallisticsAutoTest.Start();       // 弹道/WLR 数学
RDF_RadarShellFireAutoTest.Start();        // 实弹放炮 + WLR
RDF_RadarAirborneScanTest.StartKeepTarget(); // 空中目标 + PPI
RDF_RadarLockAutoTest.Start();             // 空中目标锁定状态机

// 独立回归（不进 StartAll；一次只跑一个）：
RDF_RadarRwrAutoTest.Start();              // RWR 被照射/锁定告警
RDF_RadarEsmArmAutoTest.Start();           // ESM + GetArmAim / 静默丢锁
RDF_RadarRocketLockFireAutoTest.Start();   // 锁定 → Hydra70 制导打击
```

套件内场景与雷达配置：

| 测试 | 目标 | Sensor 预设 | 验证点 |
|---|---|---|---|
| Ballistics | 纯数学，无实体 | — | 弹道积分、反推、多点真空拟合 WLR |
| DEM AutoTest | 3 架 Mi-8 悬停在视轴 400/700/1000 m | `CreateSearchSettings` + 90° 宽波束 | 杂波随 DEM 开关/scale/budget 正确变化 |
| Lock | 1 架 Mi-8 沿视轴 800 m 处飞半径 120 m 跑道（方位摆动 ±8.5°） | `CreateStareSettings` + 20° 火控窄波束 | SEARCH→ACQUIRING→TRACKING→COAST 状态机 |
| Airborne | 1 架 Mi-8 绕场约 345 m 盘旋 | `CreateSearchSettings` + 40° 波束 | 物理检测 + 按载具分类（emitter 计数必须为 0） |
| ShellFire | 实发 82 mm 迫击炮弹（RCS 0.01 m²） | `CreateWlrSettings` | 弹丸发现/检测/跟踪 + 反推炮位（理想 ≤250 m / 逼真 ≤800 m） |

独立回归：

| 测试 | 验证点 |
|---|---|
| RWR | 目标实体上 SEARCH/TRACK/LOCK 告警 |
| ESM/ARM | ESM 侦收 + `GetArmAim`；辐射源静默后丢锁 |
| Rocket Lock-Fire | 锁定 Mi-8 → Hydra70 BOOST/MIDCOURSE/TERMINAL 制导 |

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

### 快速上手 — DEM

1. Workbench 插件：`RDF Bake DEM Heightfield`（或写 `BakeDemFull.flag`）
2. Play 保持运行直到 `[RDF DEM Bake] Done`
3. 开发输出：`$profile:RDF/DemData/<world>/`（V3 CSV）
4. 工坊发布：打包 SURF JSON（高度运行时用 `GetSurfaceY`）

详情：[docs/DEM.md](docs/DEM.md)

```powershell
python tools\dem\rdf_dem_bake_help.py
python tools\dem\rdf_dem_pack_surface_json.py --world GM_Eden --from-bin
python tools\dem\rdf_dem_pack.py --world GM_Eden
```

---

### 文档索引

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
| [docs/I18N.md](docs/I18N.md) | 语言约定 / 双语文档 |
| [TODO.md](TODO.md) | 已完成 / 待办 |

---

### 贡献

欢迎 PR / Issue。联系：747384120@qq.com
