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
    RDF_HemisphereSampleStrategy.c // 示例策略（仅采样上半球）
  Visual/
    RDF_LidarVisualSettings.c  // 可视化参数
    RDF_LidarVisualizer.c      // 渲染与数据获取
  Util/
    RDF_LidarSubjectResolver.c // 解析扫描主体（玩家/载具）
  Demo/
    RDF_LidarAutoBootstrap.c   // 可选 bootstrap（默认禁用）
    RDF_LidarAutoRunner.c      // 自动运行演示器
    RDF_LidarDemo_Hemisphere.c // 示例：半球采样演示 helper
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

## Demo 与隔离
- `RDF_LidarAutoRunner` 默认关闭，需显式调用 `SetDemoEnabled(true)` 才会运行。
- 若项目希望在游戏启动时自动启用 demo，可在 bootstrap 模块中调用 `SCR_BaseGameMode.SetBootstrapEnabled(true)`；默认不建议启用以避免影响其他模组。
- 示例：使用半球采样并启动 demo：
  - 直接设置策略并启动：
    - `RDF_LidarAutoRunner.SetDemoSampleStrategy(new RDF_HemisphereSampleStrategy());`
    - `RDF_LidarAutoRunner.SetDemoEnabled(true);`
  - 或使用便捷 helper：
    - `RDF_HemisphereDemo.Start();` / `RDF_HemisphereDemo.Stop();`
- 新增：**Demo 配置（`RDF_LidarDemoConfig`）**
  - 创建并设置配置：
    - `RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();`
    - `cfg.m_Enable = true;`
    - `cfg.m_SampleStrategy = new RDF_ConicalSampleStrategy(25.0);`
    - `cfg.m_RayCount = 256;`
    - `cfg.m_MinTickInterval = 0.25;`
    - `cfg.m_ColorStrategy = new RDF_IndexColorStrategy();`
    - `RDF_LidarAutoRunner.SetDemoConfig(cfg);`
    - `RDF_LidarAutoRunner.SetDemoEnabled(true);`
  - `RDF_LidarDemoConfig.ApplyTo()` 会把配置安全地应用到 demo（采样策略、射线数、可视化色彩、tick 间隔等）。
- 新增：**更多 demo helper**
  - `RDF_ScanlineDemo`：`Start(sectors = 32, rayCount = 256)` / `Stop()`。
  - `RDF_LidarDemoCycler`：`Cycle(rayCount = 256)`（循环策略进行对比），`StartIndex(index, rayCount)`。
  - 可选 bootstrap：`RDF_LidarAutoCycleBootstrap.c` 提供 `SCR_BaseGameMode.SetAutoCycleBootstrapEnabled(bool)`，用于在游戏启动时自动开启策略轮换（默认为禁用）。
  - 自动轮换：`RDF_LidarDemoCycler.StartAutoCycle(intervalSeconds = 10.0)` / `RDF_LidarDemoCycler.StopAutoCycle()` / `RDF_LidarDemoCycler.SetAutoCycleInterval(seconds)`。

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
