# Radar Development Framework — LiDAR 模块

轻量、模块化的 LiDAR（激光雷达）开发框架，用于在 **Arma Reforger** 中实现射线点云扫描、可视化渲染与调试。扫描核心、可视化与演示相互隔离，便于扩展与复用。

- **Repository**: [Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **Contact**: 747384120@qq.com
- **License**: Apache-2.0（见根目录 `LICENSE`）

---

## 特性

- **扫描核心**：可配置射线数、半径、间隔；支持多种采样策略（均匀、半球、锥形、分层、扫描线、雷达式扫掠等）
- **可视化**：点云 + 渐变射线、可插拔颜色策略、可选「仅点云」模式（纯色背景）
- **演示与调试**：统一开关、预设配置、策略轮换、Bootstrap 可选开局自启（默认关闭）
- **工具与扩展**：CSV 导出、样本统计/过滤、扫描完成回调；支持仅逻辑不渲染（无 Visualizer）

---

## 项目结构

| 目录 | 说明 |
|------|------|
| `scripts/Game/RDF/Lidar/Core/` | 扫描核心：设置、类型、扫描器、采样策略 |
| `scripts/Game/RDF/Lidar/Visual/` | 可视化：点云与射线渲染、颜色策略、视觉参数 |
| `scripts/Game/RDF/Lidar/Util/` | 工具：主体解析、CSV 导出、统计/过滤、扫描完成回调 |
| `scripts/Game/RDF/Lidar/Demo/` | 演示：统一入口、配置预设、策略轮换、Bootstrap |
| `scripts/Game/RDF/Lidar/Network/` | 网络：服务器权威同步 API 与组件 |

详细模块说明与扩展点见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)，完整 API 见 [docs/API.md](docs/API.md)。

---

## 快速上手

框架默认**不自动运行**，演示需显式开启，避免干扰其他模组。

### 统一开关（唯一入口）

```c
// 开启演示（需先通过 SetDemoConfig 或 StartWithConfig 设定策略等）
RDF_LidarAutoRunner.SetDemoEnabled(true);

// 关闭演示
RDF_LidarAutoRunner.SetDemoEnabled(false);

// 查询是否已开启
RDF_LidarAutoRunner.IsDemoEnabled();
```

### 通过预设启动（推荐）

所有演示均通过 `RDF_LidarDemoConfig` 预设 + `RDF_LidarAutoRunner` 完成：

```c
// 一条调用完成配置并启动
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefaultDebug(512)); // 原点轴 + 控制台统计
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateHemisphere(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateConical(25.0, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateStratified(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateScanline(32, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateSweep(30.0, 20.0, 45.0, 512)); // 雷达扫描动画

// 或分步：先设置配置再开启
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateConical(25.0, 256);
RDF_LidarAutoRunner.SetDemoConfig(cfg);
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

### 自定义配置

```c
RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
cfg.m_Enable = true;
cfg.m_SampleStrategy = new RDF_ConicalSampleStrategy(25.0);
cfg.m_RayCount = 256;
cfg.m_MinTickInterval = 0.25;
cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
RDF_LidarAutoRunner.SetDemoConfig(cfg);
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

### 策略轮换（Cycler）

```c
RDF_LidarDemoCycler.Cycle(256);                    // 切换到下一策略并开启演示
RDF_LidarDemoCycler.StartIndex(2, 256);            // 按索引启动（如 2=锥形）
RDF_LidarDemoCycler.StartAutoCycle(10.0);          // 每 10 秒自动轮换
RDF_LidarDemoCycler.StopAutoCycle();
RDF_LidarDemoCycler.SetAutoCycleInterval(5.0);
```

### 统一 Bootstrap（游戏开局可选自启）

由本模组通过 **modded `SCR_BaseGameMode`** 提供，加载 RDF 后可用。默认**关闭**。

```c
SCR_BaseGameMode.SetBootstrapEnabled(true);        // 开局自动开演示（默认策略）
SCR_BaseGameMode.SetBootstrapAutoCycle(true);      // 开局自动轮换策略
SCR_BaseGameMode.SetBootstrapAutoCycleInterval(10.0);
```

### 其它常用 API

```c
RDF_LidarAutoRunner.SetMinTickInterval(0.2);
RDF_LidarAutoRunner.SetDemoRayCount(128);
RDF_LidarAutoRunner.SetDemoSampleStrategy(new RDF_HemisphereSampleStrategy());
RDF_LidarAutoRunner.SetDemoColorStrategy(new RDF_IndexColorStrategy());
// true = 游戏画面+点云，false = 仅点云（纯色背景）
RDF_LidarAutoRunner.SetDemoRenderWorld(true);
RDF_LidarAutoRunner.GetDemoRenderWorld();
```

### 多人游戏网络同步

框架提供独立的网络模块（Network），用于服务器权威的 LiDAR 扫描与配置同步。

#### 设置网络 API

1. 在玩家实体预制件中添加 `RDF_LidarNetworkComponent`
2. 将组件绑定到 AutoRunner（或使用自动绑定）：

```c
RDF_LidarAutoRunner.SetNetworkAPI(networkComponent);
```

或在游戏开始时自动绑定本地玩家主体：

```c
RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject(true);
```

#### 服务器权威扫描

- 客户端请求扫描 → 服务器执行 → 结果广播到所有客户端
- 可视化使用同步的扫描结果，确保所有玩家看到相同画面
- 配置更改需要服务器验证，防止恶意参数

#### 仅点云模式推荐

在多人游戏中推荐使用仅点云模式：

```c
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(128);
cfg.m_RenderWorld = false; // 仅点云，隐藏游戏世界
RDF_LidarAutoRunner.SetDemoConfig(cfg);
```

#### 非 Demo 扫描（网络适配）

```c
RDF_LidarScanner scanner = new RDF_LidarScanner();
array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);

// 自动使用网络 API（如已绑定），否则本地扫描
bool updated = RDF_LidarNetworkScanner.ScanWithAutoRunnerAPI(subject, scanner, samples);
```

### 导出与逻辑

- **导出 CSV**：`RDF_LidarExport.ExportLastScanToConsole(visualizer)` 或 `RDF_LidarExport.PrintCSVToConsole(samples)`，从控制台复制到外部文件。
- **扫描完成回调**：继承 `RDF_LidarScanCompleteHandler` 并重写 `OnScanComplete(samples)`，再调用 `RDF_LidarAutoRunner.SetScanCompleteHandler(handler)`。
- **统计/过滤**：`RDF_LidarSampleUtils.GetClosestHit(samples)`、`GetHitCount(samples)`、`GetHitsInRange(...)`、`GetAverageDistance(...)` 等，详见 [docs/API.md](docs/API.md)。
- **仅逻辑不渲染**：直接使用 `RDF_LidarScanner.Scan(subject, outSamples)`，不创建 Visualizer，见 [docs/API.md](docs/API.md)「仅逻辑不渲染」一节。
- 获取上次扫描：`visualizer.GetLastSamples()`（返回防御性副本）后可用 `RDF_LidarExport.PrintCSVToConsole(samples)` 导出或自行处理。

---

## 常见设置

- **扫描参数**：半径、射线数、更新间隔等在 `RDF_LidarSettings` 中配置（含 clamp 与校验）。
- **可视化**：点大小、分段数、透明度等在 `RDF_LidarVisualSettings` 中配置；调试可设 `m_DrawOriginAxis = true` 绘制扫描原点与三轴。
- **游戏+点云 / 仅点云**：`m_RenderWorld = true`（默认）为游戏画面+点云；`false` 为仅点云（相机前黑色四边形 + 关闭场景渲染）。演示下可用 `SetDemoRenderWorld(false)` 或 config 的 `m_RenderWorld = false`。

---

## 性能建议

- 降低 `m_RayCount` 或 `m_RaySegments` 可显著减少开销。
- 高密度点云场景建议增大 `m_UpdateInterval` 或仅在调试时开启渲染。

---

## 文档、迁移与贡献

- 内部架构、扩展接口与开发约定：[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)
- 完整 API 与字段说明：[docs/API.md](docs/API.md)
- **迁移指南（重要）**：近期网络同步重构包含破坏性变更，请参阅 [docs/MIGRATION.md](docs/MIGRATION.md) 进行升级。

欢迎提交 PR、Issue 或讨论扩展点。PR 请说明变更目的与性能影响（如有）。直接联系：747384120@qq.com。

---

## English translation

# Radar Development Framework — LiDAR module

A lightweight, modular LiDAR (light detection and ranging) development framework for **Arma Reforger** that provides ray-based point-cloud scanning, visualization rendering and debugging utilities. The scanning core, visualizer and demo components are separated to make extension and reuse easy.

- **Repository**: [Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **Contact**: 747384120@qq.com
- **License**: Apache-2.0 (see `LICENSE` in repository root)

---

## Features

- **Scanning core**: configurable ray count, range and intervals; supports multiple sampling strategies (uniform, hemisphere, conical, stratified, scanline, sweep/radar-style, etc.)
- **Visualization**: point-cloud + gradient rays, pluggable color strategies, optional "point-cloud-only" mode (solid background)
- **Demos & debugging**: unified switch, preset configs, strategy cycler, optional bootstrap-on-start (disabled by default)
- **Tools & extensions**: CSV export, sampling statistics/filters, scan-complete callbacks; supports logic-only scans without rendering (no Visualizer)

---

## Project structure

| Path | Purpose |
|------|---------|
| `scripts/Game/RDF/Lidar/Core/` | Scanning core: settings, types, scanner and sampling strategies |
| `scripts/Game/RDF/Lidar/Visual/` | Visualization: point-cloud/ray rendering, color strategies, visual settings |
| `scripts/Game/RDF/Lidar/Util/` | Utilities: subject resolution, CSV export, statistics/filters, scan-complete callbacks |
| `scripts/Game/RDF/Lidar/Demo/` | Demos: single entry, preset configs, strategy cycler, bootstrap |
| `scripts/Game/RDF/Lidar/Network/` | Network: server-authoritative synchronization API & components |

See `docs/DEVELOPMENT.md` for module details and extension points, and `docs/API.md` for the full API reference.

---

## Quick start

The framework does **not auto-run** by default. Demos must be explicitly enabled to avoid interfering with other mods.

### Unified switch (single entry point)

```c
// Enable demo (after setting DemoConfig or using StartWithConfig)
RDF_LidarAutoRunner.SetDemoEnabled(true);

// Disable demo
RDF_LidarAutoRunner.SetDemoEnabled(false);

// Query
RDF_LidarAutoRunner.IsDemoEnabled();
```

### Start with a preset (recommended)

All demos are driven via `RDF_LidarDemoConfig` presets + `RDF_LidarAutoRunner`:

```c
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefaultDebug(512)); // origin axis + console stats
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateHemisphere(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateConical(25.0, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateStratified(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateScanline(32, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateSweep(30.0, 20.0, 45.0, 512)); // radar sweep animation

// Or set config then enable
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateConical(25.0, 256);
RDF_LidarAutoRunner.SetDemoConfig(cfg);
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

### Custom configuration

```c
RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
cfg.m_Enable = true;
cfg.m_SampleStrategy = new RDF_ConicalSampleStrategy(25.0);
cfg.m_RayCount = 256;
cfg.m_MinTickInterval = 0.25;
cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
RDF_LidarAutoRunner.SetDemoConfig(cfg);
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

### Strategy cycler (Cycler)

```c
RDF_LidarDemoCycler.Cycle(256);                    // switch to next strategy and enable demo
RDF_LidarDemoCycler.StartIndex(2, 256);            // start by index (e.g. 2 = conical)
RDF_LidarDemoCycler.StartAutoCycle(10.0);          // auto-cycle every 10s
RDF_LidarDemoCycler.StopAutoCycle();
RDF_LidarDemoCycler.SetAutoCycleInterval(5.0);
```

### Bootstrap (optional auto-start at game start)

Provided via a modded `SCR_BaseGameMode`. Disabled by default.

```c
SCR_BaseGameMode.SetBootstrapEnabled(true);
SCR_BaseGameMode.SetBootstrapAutoCycle(true);
SCR_BaseGameMode.SetBootstrapAutoCycleInterval(10.0);
```

### Common API examples

```c
RDF_LidarAutoRunner.SetMinTickInterval(0.2);
RDF_LidarAutoRunner.SetDemoRayCount(128);
RDF_LidarAutoRunner.SetDemoSampleStrategy(new RDF_HemisphereSampleStrategy());
RDF_LidarAutoRunner.SetDemoColorStrategy(new RDF_IndexColorStrategy());
// true = render game world + point cloud, false = point-cloud-only (solid background)
RDF_LidarAutoRunner.SetDemoRenderWorld(true);
RDF_LidarAutoRunner.GetDemoRenderWorld();
```

### Multiplayer network sync

The framework includes a Network module for server-authoritative LiDAR scans and configuration synchronization.

#### Setup
1. Add `RDF_LidarNetworkComponent` to the prefab for the player-controlled entity.
2. Bind the component to the AutoRunner (or rely on automatic binding):

```c
RDF_LidarAutoRunner.SetNetworkAPI(networkComponent);
```

or auto-bind to the local subject at game start:

```c
RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject(true);
```

#### Server-authoritative scans

- Client requests a scan → server executes → results broadcast to clients
- Visualizer renders the synchronized scan so all players see the same data
- Configuration changes should be validated server-side to prevent malicious parameters

#### Point-cloud-only recommended

For multiplayer, consider point-cloud-only mode:

```c
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(128);
cfg.m_RenderWorld = false; // point-cloud-only
RDF_LidarAutoRunner.SetDemoConfig(cfg);
```

#### Non-demo scans (network-aware)

```c
RDF_LidarScanner scanner = new RDF_LidarScanner();
array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);

// Automatically uses the bound network API (if set), otherwise performs a local scan
bool updated = RDF_LidarNetworkScanner.ScanWithAutoRunnerAPI(subject, scanner, samples);
```

### Export & logic-only scans

- **Export CSV**: `RDF_LidarExport.ExportLastScanToConsole(visualizer)` or `RDF_LidarExport.PrintCSVToConsole(samples)` — copy from console to external file.
- **Scan-complete callback**: subclass `RDF_LidarScanCompleteHandler`, override `OnScanComplete(samples)`, and call `RDF_LidarAutoRunner.SetScanCompleteHandler(handler)`.
- **Statistics/filters**: `RDF_LidarSampleUtils.GetClosestHit(samples)`, `GetHitCount(samples)`, `GetHitsInRange(...)`, `GetAverageDistance(...)`, see `docs/API.md`.
- **Logic-only scans**: call `RDF_LidarScanner.Scan(subject, outSamples)` without creating a Visualizer (see `docs/API.md` "logic-only" section).
- Retrieve last scan via `visualizer.GetLastSamples()` (returns a defensive copy), then export with `RDF_LidarExport.PrintCSVToConsole(samples)`.

---

## Common settings

- **Scan parameters** (range, ray count, update interval) are in `RDF_LidarSettings` (includes clamps and validation).
- **Visualization** settings (point size, segments, alpha) are in `RDF_LidarVisualSettings`; enable `m_DrawOriginAxis = true` for origin axis debug.
- **World+point-cloud vs point-cloud-only**: `m_RenderWorld = true` (default) renders the game world + point cloud; `false` renders point cloud only by drawing a black quad in front of the camera and disabling scene rendering.

---

## Performance recommendations

- Lower `m_RayCount` or `m_RaySegments` to reduce cost.
- For dense point-clouds increase `m_UpdateInterval` or disable rendering when not needed.

---

## Docs, migration & contribution

- Developer architecture & extension points: `docs/DEVELOPMENT.md`
- Full API & field documentation: `docs/API.md`
- **Migration guide (important)**: recent network sync refactor contains breaking changes — see `docs/MIGRATION.md` to upgrade.

Contributions welcome — please open PRs or Issues and describe purpose and performance impact. Contact: 747384120@qq.com.

