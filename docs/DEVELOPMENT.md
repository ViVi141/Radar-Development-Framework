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
    RDF_ThreeColorStrategy.c   // 近/中/远三段渐变（绿→黄→红）
  Network/
    RDF_LidarNetworkAPI.c      // 网络同步 API（基类）
    RDF_LidarNetworkComponent.c // Rpl 实现
    RDF_LidarNetworkUtils.c    // 网络辅助工具（自动绑定）
    RDF_LidarNetworkScanner.c  // 网络扫描适配器

  // === 网络模块注意事项（分片 / 压缩 / 异步） ===
  // 说明：近期对网络同步做了重构以支持服务器权威扫描与高射线计数场景下的可靠传输。
  // - RequestScan() 变为无参：组件的 owner 作为扫描主体；请更新旧调用（RequestScan(subject) -> RequestScan()）。
  // - 序列化：服务器端使用 RDF_LidarExport.SamplesToCSV() 将样本序列化为紧凑字符串，客户端使用 ParseCSVToSamples() 解析。
  // - 分片：若载荷较大，服务器会将 CSV 按块（默认每块约 1000 字节）分片并通过不可靠 RPC 广播；客户端支持乱序组装与超时清理。
  // - 压缩（可选）：为进一步减小带宽，支持一个简单的 RLE 压缩层（序列前缀 `RLE:` 表示压缩），在超长载荷下自动启用或可作为选项开启。
  // - 异步扫描：RequestScan() 为异步请求，新增 `RDF_LidarNetworkScanner.ScanAsync()` / `ScanWithAutoRunnerAPIAsync()` 帮助器，支持超时回退到本地扫描并通过回调（RDF_LidarScanCompleteHandler）通知结果。
  // - 兼容性：Rpl 不再直接复制 `RDF_LidarDemoConfig` 对象，配置以原子字段形式（m_RayCount / m_UpdateInterval / m_RenderWorld / m_DrawOriginAxis / m_Verbose）传输并由服务器验证。

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
- 输出：通过 `RDF_LidarVisualizer.GetLastSamples()` 获取**防御性副本**并在外部导出（CSV/JSON）。

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

---

## English translation

# LiDAR Framework — Developer Guide

Repository: https://github.com/ViVi141/Radar-Development-Framework
Contact: 747384120@qq.com
License: Apache-2.0

## Goals
- The framework is **silent by default** and only runs when explicitly enabled.
- Keep scanning core, visualization and demo logic isolated to simplify replacement and testing.
- Expose stable extension points for third-party reuse.

## Module layout

```
scripts/Game/RDF/Lidar/
  Core/
    RDF_LidarSettings.c        // parameters & validation
    RDF_LidarTypes.c           // data structs (RDF_LidarSample etc.)
    RDF_LidarScanner.c         // scanning & ray construction
    RDF_LidarSampleStrategy.c  // sampling strategy interface & default uniform strategy
    RDF_HemisphereSampleStrategy.c
    RDF_ConicalSampleStrategy.c
    RDF_StratifiedSampleStrategy.c
    RDF_ScanlineSampleStrategy.c
    RDF_SweepSampleStrategy.c  // radar-style rotating sector
  Visual/
    RDF_LidarVisualSettings.c  // visual params (including m_RenderWorld)
    RDF_LidarVisualizer.c      // rendering & data retrieval
    RDF_LidarColorStrategy.c   // color strategy interface & defaults
    RDF_IndexColorStrategy.c
    RDF_ThreeColorStrategy.c   // 3-range gradient (green→yellow→red)
  Network/
    RDF_LidarNetworkAPI.c
    RDF_LidarNetworkComponent.c
    RDF_LidarNetworkUtils.c
    RDF_LidarNetworkScanner.c
  Util/
    RDF_LidarSubjectResolver.c
    RDF_LidarExport.c
    RDF_LidarSampleUtils.c
    RDF_LidarScanCompleteHandler.c
  Demo/
    RDF_LidarAutoBootstrap.c
    RDF_LidarAutoRunner.c
    RDF_LidarDemoConfig.c
    RDF_LidarDemo_Cycler.c
    RDF_LidarDemoStatsHandler.c
```

## Data flow
1. `RDF_LidarScanner` produces an array of `RDF_LidarSample` from `RDF_LidarSettings`.
2. `RDF_LidarVisualizer` renders samples according to `RDF_LidarVisualSettings`.
3. The demo driver (`RDF_LidarAutoRunner`) optionally drives periodic scans.

## Extension points (examples)
- Sampling strategy: implement `RDF_LidarSampleStrategy::BuildDirection()` and inject via `RDF_LidarScanner.SetSampleStrategy()`.
  - Built-in examples: `RDF_UniformSampleStrategy`, `RDF_HemisphereSampleStrategy`, `RDF_ConicalSampleStrategy`, `RDF_StratifiedSampleStrategy`, `RDF_ScanlineSampleStrategy`.
- Visualization: implement/extend `RDF_LidarColorStrategy` to color by sample attributes or by index/angle.
- Output: use `RDF_LidarVisualizer.GetLastSamples()` (defensive copy) and export externally (CSV/JSON).

New: `scripts/tests/lidar_sample_checks.c` contains basic self-checks for sampling direction unit-length and conical bounds.

## Demo & isolation (unified API)
- **Unified switch**: `RDF_LidarAutoRunner.SetDemoEnabled(true/false)` is the single start/stop entry for demos.
- **Unified config entry**: use `RDF_LidarDemoConfig` presets + `SetDemoConfig()` / `StartWithConfig()` instead of separate demo classes.
- **Bootstrap**: `RDF_LidarAutoBootstrap.c` provides optional auto-start at `OnGameStart` (default disabled).
- **Point-cloud-only**: `SetDemoRenderWorld(false)` or `m_RenderWorld = false` draws a black quad and disables scene rendering so only the point cloud is visible.

## Performance recommendations
- Limit `m_RayCount` to reasonable values (default 512) and reduce at runtime when possible.
- Increase `m_UpdateInterval` to reduce frequency; the minimum is implementation-limited (≥ 0.01s).
- Reduce `m_RaySegments` and point draw counts to control rendering load.

## Development & contribution
- Code style: follow existing project conventions and place files in the appropriate module directories.
- PRs: describe motivation, compatibility and performance impact.
- Testing: provide simple scenario validations (scan results, visual stability).
- CI: consider adding GitHub Actions for lint/build/docs checks.

---

If you want me to implement example demos, add CI, or create more sample strategies, tell me which item to prioritize.
