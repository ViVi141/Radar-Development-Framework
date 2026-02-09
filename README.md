# Radar Development Framework — LiDAR 模块 📡

**简介**

本仓库新增了一个轻量的激光雷达（LiDAR）开发框架，用于在 Arma Reforger 中快速做射线点云扫描、可视化与调试。该框架独立于示例模块，可直接在工程中启用作为开发工具或原型演示。

---

## 包含内容 ✅
- `scripts/Game/RDF/Lidar/Core/` — 扫描核心（设置、采样、扫描器）
- `scripts/Game/RDF/Lidar/Visual/` — 可视化渲染（点云 + 渐变射线）
- `scripts/Game/RDF/Lidar/Util/` — 主体解析（本地玩家 / 载具切换）
- `scripts/Game/RDF/Lidar/Demo/` — 演示控制（可选自动运行）

---

## 快速上手 ⚡
1. 框架默认不自动运行（避免影响其他模组）。
2. 需要演示时：在代码中调用 `RDF_LidarAutoRunner.SetDemoEnabled(true/false)`。
3. 调整参数：扫描参数在 `RDF_LidarSettings`，可视化参数在 `RDF_LidarVisualSettings`。

示例代码：
```c
// Enable demo visualization
RDF_LidarAutoRunner.SetDemoEnabled(true);

// Disable demo visualization
RDF_LidarAutoRunner.SetDemoEnabled(false);

// Configure minimum tick interval (seconds) to reduce per-frame overhead
RDF_LidarAutoRunner.SetMinTickInterval(0.2);

// Replace sampling strategy (example: custom strategy that implements RDF_LidarSampleStrategy)
RDF_LidarScanner scanner = new RDF_LidarScanner();
scanner.SetSampleStrategy(new RDF_UniformSampleStrategy()); // default

// Get last scan data for custom export (e.g. CSV/JSON via external tooling)
RDF_LidarVisualizer visual = new RDF_LidarVisualizer();
ref array<ref RDF_LidarSample> samples = visual.GetLastSamples();

```

全局启动开关示例（仅在加载 `RDF_LidarAutoBootstrap.c` 时生效）：
```c
// Enable global auto-start
SCR_BaseGameMode.SetBootstrapEnabled(true);

// Disable global auto-start
SCR_BaseGameMode.SetBootstrapEnabled(false);

Note: The bootstrap default is now disabled to avoid unexpected demo activation when this file is included. To enable global auto-start, call the API above or set it explicitly at runtime.
```

参数配置示例：
```c
RDF_LidarScanner scanner = new RDF_LidarScanner();
RDF_LidarSettings scan = scanner.GetSettings();
scan.m_Range = 30.0;
scan.m_RayCount = 256;
scan.m_UpdateInterval = 2.0;

RDF_LidarVisualizer visual = new RDF_LidarVisualizer();
RDF_LidarVisualSettings vis = visual.GetSettings();
vis.m_PointSize = 0.05;
vis.m_RaySegments = 4;

// Export removed: the project no longer provides in-engine export helpers. Use `visual.GetLastSamples()` to obtain the samples and export externally if required.

```

常用扫描参数（`scripts/Game/RDF/Lidar/Core/RDF_LidarSettings.c`）：
- `m_Range`：扫描半径（默认 50.0 米）
- `m_RayCount`：射线数量（默认 512，密集点云建议 256–1024）
- `m_UpdateInterval`：扫描间隔（秒，默认 5.0）
- `m_UseBoundsCenter`：是否使用包围盒中心作为球心（默认：true）
- `m_OriginOffset`：偏移（本地偏移或世界偏移由 `m_UseLocalOffset` 控制）

常用可视化参数（`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualSettings.c`）：
- `m_PointSize`：点体积大小（可视化）
- `m_RaySegments`：每条射线的分段数（用于渐变显示）
- `m_RayAlpha`：射线透明度

---

## 可视化说明 🎨
- 使用点云 + 分段线的方式显示射线（红→绿渐变）。
- 命中点绘制为球体（大小 `m_PointSize`），射线用半透明线段绘制（段数=`m_RaySegments`）并按距离渐变颜色。
- 每次扫描会清空上次绘制（如需历史轨迹/淡出，可扩展实现）。

---

## 性能建议 ⚠️
- 射线数量与段数会显著影响性能。建议调试时使用低于 512 的数量；如果需要高密度点云（>512），请降低 `m_UpdateInterval` 的频率或仅在调试时启用。

---

## 扩展点 / 可改进项 💡
- 导出点云数据（CSV / 二进制）以便外部处理。
- 添加 HUD/调试界面控制参数（开/关、密度、范围、颜色映射）。
- 使用不同的采样策略（分层网格、随机采样、优先命中方向）以提高代表性与效率。

---

更多架构与扩展说明请见：`docs/DEVELOPMENT.md`