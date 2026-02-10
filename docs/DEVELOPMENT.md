# LiDAR Framework — Developer Guide

Repository: https://github.com/ViVi141/Radar-Development-Framework
Contact: 747384120@qq.com
License: Apache-2.0

## Goals
- 框架默认**静默**，只有在明确启用时才会运行。
- 将扫描核心、可视化与演示隔离，便于替换与测试。
- 对外暴露稳定的扩展点以便第三方复用与扩展。

## 模块布局
```
scripts/Game/RDF/Lidar/
  Core/
    RDF_LidarSettings.c        // 参数与校验
    RDF_LidarTypes.c           // 数据结构（RDF_LidarSample 等）
    RDF_LidarScanner.c         // 扫描与射线构建
    RDF_LidarSampleStrategy.c  // 采样策略接口与默认均匀策略
    RDF_HemisphereSampleStrategy.c
    RDF_ConicalSampleStrategy.c
    RDF_StratifiedSampleStrategy.c
    RDF_ScanlineSampleStrategy.c
    RDF_SweepSampleStrategy.c  // 雷达式扇区旋转扫描
  Visual/
    RDF_LidarVisualSettings.c  // 可视化参数（含 m_RenderWorld 仅点云开关）
    RDF_LidarVisualizer.c      // 渲染与数据获取（仅点云时在相机前绘制黑色四边形）
    RDF_LidarColorStrategy.c   // 颜色策略接口与默认实现
    RDF_IndexColorStrategy.c
  Util/
    RDF_LidarSubjectResolver.c // 解析扫描主体（玩家/载具）
    RDF_LidarExport.c          // CSV 导出
    RDF_LidarSampleUtils.c     // 统计与过滤
    RDF_LidarScanCompleteHandler.c // 扫描完成回调基类
  Demo/
    RDF_LidarAutoBootstrap.c   // 统一 bootstrap（默认关闭）
    RDF_LidarAutoRunner.c      // 演示唯一入口与统一开关
    RDF_LidarDemoConfig.c      // 配置与预设工厂 Create*()
    RDF_LidarDemo_Cycler.c     // 策略轮换（仅调用 API）
    RDF_LidarDemoStatsHandler.c // 内置统计回调（命中数、最近距离）
```

## 数据流
1. `RDF_LidarScanner` 根据 `RDF_LidarSettings` 生成 `RDF_LidarSample` 列表。
2. `RDF_LidarVisualizer` 使用 `RDF_LidarVisualSettings` 绘制点云与射线。
3. 演示工具（`RDF_LidarAutoRunner`）可选地驱动定期扫描循环。

## 扩展点（示例）
- 采样策略：实现 `RDF_LidarSampleStrategy::BuildDirection()`，并通过 `RDF_LidarScanner.SetSampleStrategy()` 注入。
  - 内置与示例实现：`RDF_UniformSampleStrategy`（默认），`RDF_HemisphereSampleStrategy`（上半球），`RDF_ConicalSampleStrategy`（锥形/面向前方），`RDF_StratifiedSampleStrategy`（分层网格），`RDF_ScanlineSampleStrategy`（扇区/扫描线）。
- 可视化：实现或扩展 `RDF_LidarColorStrategy` 以支持按样本着色或按索引/角度着色；当前已添加样本级 hook：
  - `BuildPointColorFromSample(ref RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)`
  - `BuildPointSizeFromSample(ref RDF_LidarSample sample, float lastRange, RDF_LidarVisualSettings settings)`
- 输出：通过 `RDF_LidarVisualizer.GetLastSamples()` 获取数据并在外部导出（CSV/JSON）。

新增：`scripts/tests/lidar_sample_checks.c` 提供了基础自检函数以验证采样方向的单位长度与锥体边界。

## Demo 与隔离（统一 API + 统一开关）
- **统一开关**：`RDF_LidarAutoRunner.SetDemoEnabled(true/false)` 为演示的唯一起停入口。
- **统一配置入口**：所有演示通过 `RDF_LidarDemoConfig` 预设或自定义 config + `SetDemoConfig()` / `StartWithConfig()` 完成，不再使用独立 Demo 类（如原 `RDF_HemisphereDemo`、`RDF_ConicalDemo` 等已移除）。
- 预设启动示例：
  - `RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateHemisphere(256));`
  - `RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateConical(25.0, 256));`
- 自定义配置：`RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();` 设置各字段后 `RDF_LidarAutoRunner.SetDemoConfig(cfg); SetDemoEnabled(true);`。若 demo 已开启，再次调用 `SetDemoConfig(cfg)` 会立即应用新配置。
- **统一 Bootstrap**：仅一个文件 `RDF_LidarAutoBootstrap.c`，`SCR_BaseGameMode.SetBootstrapEnabled(true)` 在游戏启动时开演示（默认策略）；`SetBootstrapAutoCycle(true)` 可改为开局自动轮换策略，轮换间隔由 `SetBootstrapAutoCycleInterval(seconds)` 设置。默认 bootstrap 为 **关闭**。
- **RDF_LidarDemoCycler**：仅调用 `RDF_LidarAutoRunner.SetDemoConfig` + `SetDemoEnabled`，用于策略轮换与自动轮换。
- **仅点云模式**：`SetDemoRenderWorld(false)` 或 config 中 `m_RenderWorld = false` 时，在相机前绘制黑色四边形遮挡场景，并调用 `PlayerController.SetCharacterCameraRenderActive(false)` 关闭场景渲染；点云因 NOZBUFFER 绘制在上层。

## 性能建议
- 将 `m_RayCount` 限制在合理范围内（默认 512），并在运行时降低以减小开销。
- 使用较大的 `m_UpdateInterval` 来降低频率开销，最小值受限于实现（≥ 0.01s）。
- 减少 `m_RaySegments` 和点绘制数量以控制渲染负载。

## 开发与贡献指南
- 代码风格：遵循项目现有约定与注释风格，文件放在相应模块目录下。
- 提交 PR：在 PR 描述中说明变更动机、兼容性影响与性能影响（如有）。
- 测试：尽量提供自检脚本或简单场景验证（例如小场景下的扫描结果、可视化无明显抖动）。
- CI 建议：添加 GitHub Actions 工作流以自动运行基础验证（lint、构建检查、文档链接检查）。

---

如需帮助实现示例演示、CI 配置或增加更多示例策略，请告诉我你希望优先的项。
