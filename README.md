# Radar Development Framework — LiDAR 模块 📡

轻量且模块化的 LiDAR（激光雷达）开发框架，用于在 Arma Reforger 中快速实现射线点云扫描、渲染与调试。该框架将扫描核心、可视化和演示隔离开，便于扩展与复用。

Repository: https://github.com/ViVi141/Radar-Development-Framework
Contact: 747384120@qq.com

---

## 主要内容 ✅
- `scripts/Game/RDF/Lidar/Core/` — 扫描核心（设置、采样策略、扫描器）
- `scripts/Game/RDF/Lidar/Visual/` — 可视化渲染（点云 + 渐变射线）
- `scripts/Game/RDF/Lidar/Util/` — 主体解析（玩家 / 载具）
- `scripts/Game/RDF/Lidar/Demo/` — 演示控制（可选自动运行）

许可证：Apache-2.0（见仓库根目录 `LICENSE`）。

---

## 快速上手 ⚡
- 默认：**不自动运行**（演示为可选，避免干扰其他模组）。
- 启用演示：
```c
// 开启演示
RDF_LidarAutoRunner.SetDemoEnabled(true);

// 关闭演示
RDF_LidarAutoRunner.SetDemoEnabled(false);
```
- 更改最小调度间隔：
```c
RDF_LidarAutoRunner.SetMinTickInterval(0.2);
```
- 替换采样策略示例：
```c
RDF_LidarScanner scanner = new RDF_LidarScanner();
// 默认策略
scanner.SetSampleStrategy(new RDF_UniformSampleStrategy());
// 示例：仅采样上半球（新增示例策略）
scanner.SetSampleStrategy(new RDF_HemisphereSampleStrategy());

// 其它采样策略示例：
// Conical sampling (cone half-angle 30 degrees)
scanner.SetSampleStrategy(new RDF_ConicalSampleStrategy(30.0));

// Stratified sampling (near-regular grid on sphere)
scanner.SetSampleStrategy(new RDF_StratifiedSampleStrategy());

// Scanline / sector sampling (useful for sweep scans)
scanner.SetSampleStrategy(new RDF_ScanlineSampleStrategy(64));

// Convenience: start hemisphere demo (sets strategy and starts auto-run)
RDF_LidarAutoRunner.StartHemisphereDemo();
// Start conical demo (half-angle 25°, 256 rays)
RDF_ConicalDemo.Start(25.0, 256);
// Start stratified demo
RDF_StratifiedDemo.Start(256);
// Start scanline demo (32 sectors)
RDF_ScanlineDemo.Start(32, 256);
// Start conical demo with index coloring (forward-facing cone)
RDF_ConicalDemo.Start(25.0, 256);
// Stop demo
RDF_LidarAutoRunner.SetDemoEnabled(false);

// Demo cycler: call multiple times to rotate strategies
RDF_LidarDemoCycler.Cycle(256);
// or start a specific strategy by index
RDF_LidarDemoCycler.StartIndex(2, 256); // 2 = conical in default cycle list

// Auto-cycle: switch strategy automatically every 10 seconds
RDF_LidarDemoCycler.StartAutoCycle(10.0);
// Stop auto-cycle
RDF_LidarDemoCycler.StopAutoCycle();
// Query status
RDF_LidarDemoCycler.IsAutoCycling();
// Change interval while stopped (or restart after set)
RDF_LidarDemoCycler.SetAutoCycleInterval(5.0);

// Optional bootstrap (opt-in): enable auto-cycle at game start
// By default the bootstrap is disabled to avoid surprising behavior. To enable at runtime:
//   SCR_BaseGameMode.SetAutoCycleBootstrapEnabled(true);
// To disable:
//   SCR_BaseGameMode.SetAutoCycleBootstrapEnabled(false)

// Demo configuration example:
RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
cfg.m_Enable = true;
cfg.m_SampleStrategy = new RDF_ConicalSampleStrategy(25.0);
cfg.m_RayCount = 256;
cfg.m_MinTickInterval = 0.25;
cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
RDF_LidarAutoRunner.SetDemoConfig(cfg);
RDF_LidarAutoRunner.SetDemoEnabled(true);

// Quick self-checks (run from dev console / init script):
RDF_RunAllSampleChecks(); // prints basic verification for strategies
```
- 获取上次扫描数据以便导出：
```c
RDF_LidarVisualizer visual = new RDF_LidarVisualizer();
// Optional: set an index-based color strategy for debugging sample order
visual.SetColorStrategy(new RDF_IndexColorStrategy());
ref array<ref RDF_LidarSample> samples = visual.GetLastSamples();
// 导出由外部工具负责（CSV/JSON 等）
```

---

## 常见设置
- 扫描半径、射线数量与更新频率：在 `RDF_LidarSettings` 中配置（clamp 与校验已实现）。
- 可视化细节：在 `RDF_LidarVisualSettings` 中控制点大小、分段数、透明度等。

---

## 性能建议 ⚠️
- 降低 `m_RayCount` 或 `m_RaySegments` 可显著减少开销。
- 如果需要高密度点云，请增大 `m_UpdateInterval` 或仅在调试时启用渲染。

---

## 贡献与联系方式 🤝
欢迎提交 PR、Issue 或讨论扩展点。请在 PR 描述中添加变更目的与性能影响说明。若需直接联系：747384120@qq.com。

---

有关内部架构、扩展接口与开发约定，请参阅 `docs/DEVELOPMENT.md`。