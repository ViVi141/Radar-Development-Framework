# CHANGELOG

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

如需我继续，接下来我会按优先级实现「批量绘制原型」或「CSV 序列化缓冲化」。

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

### 2026-02-15 (回退)

已回退“分段退化”改动，恢复默认按 `m_RaySegments` 的分段绘制行为（原因：保持视觉一致性与细节）。

变更影响：

- 性能：回退后在极高密度场景中会保留更多 `Shape` 的创建，可能导致比退化时更高的渲染与 GC 开销；但之前的其他优化（TraceParam 重用、数组预分配、网络序列化/分片优化）仍然有效并减少了整体开销。
- 视觉：恢复了射线上的连续渐变效果，保留原始视觉表现。
- 后续建议保持不变：建议将退化阈值作为可配置项并考虑更温和的自适应分段或两段近似渐变替代方案。
