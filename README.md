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

### 自检（控制台/脚本）

```c
RDF_RunAllSampleChecks();
```

### 导出与逻辑

- **导出 CSV**：`RDF_LidarExport.ExportLastScanToConsole(visualizer)` 或 `RDF_LidarExport.PrintCSVToConsole(samples)`，从控制台复制到外部文件。
- **扫描完成回调**：继承 `RDF_LidarScanCompleteHandler` 并重写 `OnScanComplete(samples)`，再调用 `RDF_LidarAutoRunner.SetScanCompleteHandler(handler)`。
- **统计/过滤**：`RDF_LidarSampleUtils.GetClosestHit(samples)`、`GetHitCount(samples)`、`GetHitsInRange(...)`、`GetAverageDistance(...)` 等，详见 [docs/API.md](docs/API.md)。
- **仅逻辑不渲染**：直接使用 `RDF_LidarScanner.Scan(subject, outSamples)`，不创建 Visualizer，见 [docs/API.md](docs/API.md)「仅逻辑不渲染」一节。
- 获取上次扫描：`visualizer.GetLastSamples()` 后可用 `RDF_LidarExport.PrintCSVToConsole(samples)` 导出或自行处理。

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

## 文档与贡献

- 内部架构、扩展接口与开发约定：[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)
- 完整 API 与字段说明：[docs/API.md](docs/API.md)

欢迎提交 PR、Issue 或讨论扩展点。PR 请说明变更目的与性能影响（如有）。直接联系：747384120@qq.com。
