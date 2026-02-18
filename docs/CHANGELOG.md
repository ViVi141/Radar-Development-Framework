# CHANGELOG

## 2026-02-18 — 电磁波雷达系统：完整实现 + HUD PPI 扫描图

### 概述

完成了以电磁波为载体的雷达仿真系统全部模块，包括物理模型、工作模式、可视化、演示系统与片上 HUD（`RDF_RadarHUD`）。同时修复了多项运行时 Bug 并完善了 CanvasWidget PPI 圆形扫描图显示。

### 新增模块（24 个脚本文件）

**Core（核心）**
- `RDF_EMWaveParameters.c` — 电磁波物理参数容器（频率、波长、波段、发射功率、天线增益等）
- `RDF_RadarSample.c` — 雷达回波数据（继承 `RDF_LidarSample`，增加 SNR、RCS、多普勒、相位等字段）
- `RDF_RadarSettings.c` — 雷达扫描器配置（量程、距离门限 `m_MinRange`、系统损耗、检测门限等）
- `RDF_RadarScanner.c` — 雷达扫描核心（完整 `ApplyRadarPhysics()` 10 步物理管线）

**Physics（物理）**
- `RDF_RadarPropagation.c` — 电磁波传播：FSPL、大气衰减、雨衰
- `RDF_RCSModel.c` — RCS 模型：解析球/板/柱、材质反射率查表、实体边界框估算、地物漫反射
- `RDF_RadarEquation.c` — 雷达方程：接收功率、噪声功率 kTBF、SNR
- `RDF_DopplerProcessor.c` — 多普勒：径向速度、频移 `fd = 2vr f/c`、MTI 杂波抑制

**Modes（工作模式）**
- `RDF_RadarMode.c` — 四种模式基类 + 具体实现：Pulse / CW / FMCW / PhasedArray

**Visual（可视化）**
- `RDF_RadarColorStrategy.c` — 四种颜色策略：SNR 映射、RCS 映射、多普勒色、复合色
- `RDF_RadarDisplay.c` — PPI 平面显示（`RDF_PPIDisplay`）与 A-Scope 距离幅度图（`RDF_AScopeDisplay`）
- `RDF_RadarSimpleDisplay.c` — 世界立柱标记（`RDF_RadarWorldMarkerDisplay`）+ ASCII 控制台地图（`RDF_RadarTextDisplay`）

**Advanced / ECM / Classification**
- `RDF_SARProcessor.c` — 合成孔径雷达处理器（SAR 图像积累）
- `RDF_JammingModel.c` — 电子对抗干扰模型（噪声干扰、欺骗干扰、自卫干扰）
- `RDF_TargetClassifier.c` — 目标自动分类（5 类：飞机/车辆/人员/建筑/未知）

**Demo（演示系统）**
- `RDF_RadarDemoConfig.c` — 五种预设工厂：`CreateXBandSearch` / `CreateAutomotiveRadar` / `CreateWeatherRadar` / `CreatePhasedArrayRadar` / `CreateLBandSurveillance`
- `RDF_RadarAutoRunner.c` — 演示主驱动器（单例）：扫描 tick、回调派发、世界标记显示
- `RDF_RadarDemoStatsHandler.c` — 控制台统计报告（SNR / RCS / 速度 / 多普勒 / 量程 / ASCII 地图）
- `RDF_RadarDemoCycler.c` — 五预设自动轮换（手动 / 定时两种模式）
- `RDF_RadarAutoBootstrap.c` — `modded SCR_BaseGameMode`：游戏启动自动运行雷达演示 + HUD

**UI（片上 HUD）**
- `RDF_RadarHUD.c` — 完全基于 `WorkspaceWidget.CreateWidgetInWorkspace` 的动态 HUD，含：
  - 标题栏 + 当前工作模式
  - `CanvasWidget` PPI 圆形扫描图（背景圆盘、50%/100% 距离环、N/S/E/W 罗盘轴、目标光点）
  - 三行数据面板（SNR / RCS / 速度 / 命中数 / 量程）
  - 0.5 秒节流防止闪烁

**Tests / Util**
- `RDF_RadarTests.c` — 单元测试（传播损耗、多普勒、RCS、检测距离）
- `RDF_RadarBenchmark.c` — 性能基准
- `RDF_RadarExport.c` — CSV 数据导出

### 关键 Bug 修复

| 问题 | 修复方案 |
|------|---------|
| `RDF_RadarScanner` 的雷达方程双重计入 FSPL | 系统损耗 L 改为只含 `m_SystemLossDB` + 大气/雨衰；`R⁴` 项已包含传播衰减 |
| 近场 SNR 饱和（range min=1m，SNR=130dB+）| 增加 `m_MinRange` 距离门限，拒绝盲区内回波 |
| 杂波过滤器不触发 | 地物判断从 `m_Surface != null` 修正为 `m_Entity == null` |
| 距离显示全为 0 | 改用 `(m_HitPos - m_Start).Length()` 计算真实几何距离 |
| 地物 RCS 返回 0 导致无法计算功率 | 引入漫反射面积散射模型 `sigma0 * patchArea` |
| `.layout` 文件报错 `Unknown keyword 'FrameWidgetClass'` | `.layout` 是二进制格式；改用纯脚本 `CreateWidgetInWorkspace` 动态创建所有控件 |
| HUD 数据闪烁 | 增加 0.5 秒节流（`System.GetTickCount()` 计时） |

### Enfusion Script 兼容性修复

- 替换所有非 ASCII Unicode 字符
- 去除三目运算符 `? :`，改为 `if/else`
- 去除函数式类型转换 `float(x)`，改为 C 风格 `(float)x`
- 修复 `ToString()` 不能在临时表达式上调用的问题
- 修复 `out` 作变量名与关键字冲突
- 去除 `string.ToLower()`、`IEntity.GetMaterial()`、`IEntity.GetClassName()` 等不存在的 API
- 修复 `enum` 直接整数转换报错，改用预填数组查表
- `string.Format` 改为字符串拼接（Enfusion 用 `%1/%2` 位置说明符，不支持 `%.1f`）

---

## 2026-02-18 — 网络：单片扫描载荷改为可靠 RPC（小修复）

概述：将单片扫描载荷的 RPC 通道由不可靠（Unreliable）改为可靠（Reliable），以降低小型（不分片）CSV 载荷在传输层丢失的概率。此变更仅更改传输通道；RPC 签名与分片逻辑保持不变。

主要改动：

- 修正：`RpcDo_ScanCompleteWithPayload(string csv)` 从 `RplChannel.Unreliable` 改为 `RplChannel.Reliable`。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`
  - 兼容性：签名未变；对外调用者无影响。

- 修正（可视化）：`DrawPointCloudOnlyBackground` 中背景四边形现在包含 `ShapeFlags.NOZBUFFER`，确保“仅点云”模式下背景遮挡行为可靠。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`
  - 测试建议：将 `m_RenderWorld = false` 并确认点云始终在场景之上（移动相机、近/远物体验证）。

测试建议：
- 触发 `RequestScan()` 并在客户端确认 `HasSyncedSamples()` 返回 true；在模拟丢包的环境下验证单片载荷的接收率提升.

---

## 2026-02-17 — CSV/导出与网络传输改进（小范围性能与健壮性提升）

概述：本次更新集中在服务器端的 CSV 序列化、网络传输和导出路径，目标是在处理大型点云时减少峰值内存/CPU 和降低网络分片的失败率。改动均为向后兼容的实现层优化（未修改公开 API 签名）。

主要改动（实际实现）：

- 优化：在 `RDF_LidarNetworkComponent` 中**缓存并复用** `RDF_LidarScanner`（替代每次 `new RDF_LidarScanner()`），减少短生命周期对象分配并降低 GC 压力。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 优化：引入按样本构建的 CSV "parts"（新增 `RDF_LidarExport.SamplesToCSVParts`），并在服务器端**流式/分块**发送 RPC payload。对较大载荷仍支持将 parts 合并后进行 RLE 压缩再发送（保留原有 `RLE:` 协议前缀）。此改动显著减少了为大扫描一次性构建巨型字符串的内存峰值。
  - 文件：`scripts/Game/RDF/Lidar/Util/RDF_LidarExport.c`, `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 改进：`ExportToFile` 采用 **best-effort 原子写入**（先写入 `.tmp` 临时文件，再写目标文件）以降低导出时产生截断文件的概率（注意：引擎脚本层面无法保证完全原子重命名）。
  - 文件：`scripts/Game/RDF/Lidar/Util/RDF_LidarExport.c`

- 健壮性增强：在 RPC 接收与分片组装路径增加对解析失败/空结果的可选日志（由 `m_Verbose` 控制），便于诊断网络损坏或压缩/解压问题。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

兼容性说明：
- 未修改任一公共方法的签名或外部可见行为；CSV/RLE 的字符串格式与客户端解析兼容（服务端仅改变序列化/分片实现）。

测试建议：
- 在本地或服务器上触发大射线计数扫描（例如 m_RayCount >= 4096），验证内存/GC 峰值、网络分片与客户端解析是否正常；开启 `m_Verbose` 以查看 RPC 解析警告日志。
- 使用 `ExportToFile` 验证临时文件写入路径与最终目标文件内容一致（注意脚本层面的重命名/删除限制）。

---

## 2026-02-14 — 修复与性能优化

概述：本次提交修复了几处功能不一致和性能瓶颈，并做了若干内部实现优化以降低运行时分配与网络开销。所有改动均保持对外 API 的兼容性（未修改公开方法签名或行为契约）。

改动摘要：

- 修复：`RDF_LidarScanner::Scan` 使用统一的本地 `range` 变量进行 `param.End`、命中距离与 `m_HitPos` 的计算，避免因混用 `m_Settings.m_Range` 导致距离不一致的问题。
  - 文件：`scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- 优化：在 `RDF_LidarScanner::Scan` 中复用单个 `TraceParam` 实例，减少每条射线的临时对象分配（降低 GC 压力）。
  - 文件：`scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- 优化：网络序列化去重。`RDF_LidarNetworkComponent::PerformScanInternal` 现在只构建一次 CSV 字符串，必要时在该字符串上执行压缩，而不是重新对样本进行二次序列化。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 修复/优化：分片接收组装逻辑改为按索引直接写入缓冲槽并记录已接收计数，避免原先的 O(n^2) 查找，提升不可靠 RPC 分片重组效率与鲁棒性。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 优化：`RDF_LidarNetworkScanner` 的异步 poll 调度改为单次计划并由 tick 自行重调度，以在无待处理 poller 时停止调度，避免无用空转。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkScanner.c`

- 低风险改进（渲染预分配）：在 `RDF_LidarVisualizer` 的 `Render` / `RenderWithSamples` 中增加对 `m_Samples` 与 `m_DebugShapes` 的容量预分配，减少每帧数组重分配与复制。此改动不改变渲染 API 或外部可见行为，仅降低分配与抖动。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

兼容性说明：
- 未修改任何公共方法签名或属性名称；演示、网络、导出、以及已有脚本保持向后兼容。

性能影响（预期）：
- 减少了射线扫描时的短生命对象分配（TraceParam），在高 `m_RayCount` 场景可明显减轻 GC 压力。
- 网络端在大载荷场景中避免重复序列化，降低 CPU 与内存峰值开销。
- 改进的分片重组算法在不可靠网络中更健壮且更快。
- 预分配渲染数组减少了每帧内存复制与临时分配，能降低抖动并略微提升帧率。

测试与注意事项：
- 已做静态代码修改与人工审查；建议在目标平台（Arma Reforger）上用以下配置做快速验证：
  - 单人场景：开启高 `m_RayCount`（例如 4096）并观察内存/GC 行为与渲染帧率。
  - 多人场景：在服务器上触发 `RequestScan()` 并观察分片/重组在丢包（模拟）下是否正常。

后续优化建议（不在本次提交中）：
- 批量/合并绘制 `Shape`（减少每帧 Shape 数量）——效果显著但需在渲染 API 层面小心处理颜色渐变。
- 使用复用/池化策略管理 `RDF_LidarSample` 对象（需保证外部不会长时间持有引用）。
- 将 CSV 序列化改为可重用缓冲写入器（StringBuilder 风格）以进一步降低临时字符串分配。


## 2026-02-15 — 可视化批量绘制原型与进一步优化

概述：本次提交在 `RDF_LidarVisualizer` 中引入低风险的批量绘制/退化策略原型，并做了若干渲染层面的预分配优化以显著减小高密度扫描场景下的 Shape 数量与内存抖动。

改动摘要：

- 在 `RDF_LidarVisualizer` 中添加预分配逻辑，调用 `Reserve()` 减少 `m_Samples` 与 `m_DebugShapes` 的每帧重分配。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

- 添加“分段退化”原型：当 (射线数 × 分段数) 超过阈值（当前实现阈值为 2000）时，射线从多段渐变退化为单段直线，以减少每帧 `Shape` 的创建量。
  - 实现细节：新增 `DrawRay(..., segmented)` 支持按需绘制单段或多段线；视觉颜色在退化模式下使用 t=1.0 处颜色。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

- 修复（网络健壮性）：修正 `RDF_LidarAutoRunner` 对静态 `s_NetworkAPI` 的潜在悬挂引用 — 现在在绑定与使用前验证 `IsNetworkAvailable()` 并在检测到失效时自动清理引用；`RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject` 亦在绑定前验证 API 可用性。
  - 文件：`scripts/Game/RDF/Lidar/Demo/RDF_LidarAutoRunner.c`, `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkUtils.c`

- 修复（API 硬化）：`RDF_LidarVisualizer::GetLastSamples()` 现在返回 **防御性副本**（外部修改不会影响 visualizer 内部状态）；同时更新了 API 文档与 README 的相关说明。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`, `docs/API.md`, `README.md`, `docs/DEVELOPMENT.md`

兼容性说明：
- `GetLastSamples()` 的返回语义由“内部引用”变为“防御性副本”——方法签名未变，但如果已有外部代码依赖修改返回数组以影响 visualizer 内部状态，需要改为显式调用并处理复制后的结果。其余改动向后兼容。

视觉与功能影响：

- 性能：在高密度点云（高 `m_RayCount` 与 `m_RaySegments`）场景中，Shape 数量显著下降，GC 与渲染开销预期大幅减少，帧率更稳定。
- 视觉：退化模式会丢失每条射线上连续渐变的细节（分段渐变 → 单色线），但命中点、距离、点色和点大小保持不变，整体场景可读性保持良好。
- API 兼容性：未修改任何公共 API 或公开方法签名；`DrawRay` 为可见范围内的内部渲染方法更改，不影响外部使用者的调用契约。

建议与后续工作：

- 将阈值（当前硬编码为 2000）暴露到 `RDF_LidarVisualSettings` 作为可配置项，便于按场景调整（建议优先项）。
- 实现自适应分段（基于投影长度或距离动态减少分段数）或“两段近似渐变”以在更低开销下保留部分渐变效果。
- 若需要顶级质量，考虑实现真正的批量顶点合并与顶点色插值（更复杂但效果最佳）。

测试建议：

- 单人模式：在本地使用 `RDF_LidarAutoRunner` 以高 `m_RayCount`（如 4096）与不同 `m_RaySegments` 值对比帧率、GC 活动与视觉差异。
- 多人模式：在服务器上触发 `RequestScan()` 并在客户端验证退化与正常分段在网络同步下的显示一致性。

回滚：若视觉退化不可接受，可将阈值调高或恢复旧的 `m_RaySegments` 行为（一次性替换代码即可）。

---

## English translation

# CHANGELOG

## 2026-02-14 — Fixes & Performance Improvements

Summary: This update fixes several functional inconsistencies and performance bottlenecks and includes internal optimizations to reduce runtime allocations and network overhead. All changes maintain API compatibility (no public method signatures were changed).

Highlights:

- Fix: `RDF_LidarScanner::Scan` now uses a local `range` variable consistently for `param.End`, hit distance and `m_HitPos` calculations to avoid inconsistent distances caused by mixing with `m_Settings.m_Range`.
  - File: `scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- Optimization: Reuse a single `TraceParam` instance inside `RDF_LidarScanner::Scan` to reduce per-ray temporary allocations (lower GC pressure).
  - File: `scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- Optimization: Network serialization de-duplication. `RDF_LidarNetworkComponent::PerformScanInternal` now constructs the CSV string once and compresses that string when needed instead of re-serializing samples.
  - File: `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- Fix/Optimization: Fragment reassembly now writes directly to buffer slots by index and tracks received counts to avoid the previous O(n^2) behavior, improving unreliable-RPC fragment reassembly robustness and performance.
  - File: `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- Optimization: `RDF_LidarNetworkScanner` async poll scheduling changed to a single scheduled job that re-schedules itself only when needed, avoiding idle polling.
  - File: `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkScanner.c`

- Low-risk rendering preallocation: `RDF_LidarVisualizer` now preallocates capacity for `m_Samples` and `m_DebugShapes`, reducing per-frame array reallocations and copies.
  - File: `scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

Compatibility: No public method signatures or property names were changed; demos, network, export and existing scripts remain backward compatible.

Performance impact (expected):
- Reduced short-lived allocations during ray scanning (TraceParam reuse) — noticeable GC reduction under high `m_RayCount`.
- Reduced CPU/memory peaks on the network side by avoiding duplicate serialization for large payloads.
- Faster and more robust fragment reassembly under lossy networks.
- Preallocated render arrays reduce frame jitter and slightly improve frame rate.

Testing notes: run scenarios with high `m_RayCount` and in multiplayer to validate fragment/assembly robustness.

## 2026-02-15 — Visual batching prototype & follow-up

Summary: Introduced a low-risk prototype for batch/degenerate drawing in `RDF_LidarVisualizer` plus additional preallocation optimizations to reduce Shape counts and memory churn in dense scans.

- Added preallocation/reserve logic in `RDF_LidarVisualizer` to reduce array reallocations.
- Added a segmented-degeneration prototype that falls back to single-segment rays when (rayCount × segments) exceeds a threshold (prototype later reverted).
- Fixed a potential dangling reference to `s_NetworkAPI` in `RDF_LidarAutoRunner` by validating `IsNetworkAvailable()` before use and auto-cleaning invalid references.
- Hardened `GetLastSamples()` to return a defensive copy (API signature unchanged).

Compatibility: No public API signature changes; note `GetLastSamples()` now returns a defensive copy (behavioral change for code that previously modified the returned array to affect internal state).

Performance/visual impact: See original changelog for details.


### 2026-02-15 (回退)

已回退“分段退化”改动，恢复默认按 `m_RaySegments` 的分段绘制行为（原因：保持视觉一致性与细节）。

变更影响：

- 性能：回退后在极高密度场景中会保留更多 `Shape` 的创建，可能导致比退化时更高的渲染与 GC 开销；但之前的其他优化（TraceParam 重用、数组预分配、网络序列化/分片优化）仍然有效并减少了整体开销。
- 视觉：恢复了射线上的连续渐变效果，保留原始视觉表现。
- 后续建议保持不变：建议将退化阈值作为可配置项并考虑更温和的自适应分段或两段近似渐变替代方案。
