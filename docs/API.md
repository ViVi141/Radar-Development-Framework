# API Reference — Radar Development Framework (LiDAR)

此文档为第一版 API 摘要，覆盖主要公开类与要点（无迁移/发布说明）。

## Core

### RDF_LidarSettings
字段（常用）：
- `m_Enabled` (bool): 是否启用扫描（默认 true）
- `m_Range` (float): 扫描半径（默认 50.0）。在运行时会被 clamp 到 [0.1, 1000.0]
- `m_RayCount` (int): 射线数量（默认 512）。在运行时会被 clamp 到 [1, 4096]
- `m_UpdateInterval` (float): 扫描间隔（秒，默认 5.0），最小值 0.01

方法：
- `void Validate()` — 对设置进行防护性校验与 clamp（在扫描前会被自动调用）。

### RDF_LidarSample
数据结构，包含每条射线的采样信息：`m_Index`, `m_Hit`, `m_Start`, `m_End`, `m_Dir`, `m_HitPos`, `m_Distance`, `m_Entity`, `m_ColliderName`, `m_Surface`。

### RDF_LidarScanner
主要方法：
- `RDF_LidarScanner(RDF_LidarSettings settings = null)` — 构造器
- `RDF_LidarSettings GetSettings()` — 获取设置引用
- `void Scan(IEntity subject, array<ref RDF_LidarSample> outSamples)` — 对给定实体执行扫描并填充 samples（会在入口处调用 `Validate()`）。
- `void SetSampleStrategy(RDF_LidarSampleStrategy strategy)` — 注入自定义采样策略

备注：默认采样策略为 `RDF_UniformSampleStrategy`。

## Visual

### RDF_LidarVisualSettings
字段（常用）：
- `m_DrawPoints`, `m_DrawRays`, `m_ShowHitsOnly`
- `m_PointSize`, `m_RayAlpha`, `m_RaySegments`
- `m_MaxShapes`（int）：单次渲染创建的 Debug Shape 上限，用于防止资源过度创建（默认 2048）。

### RDF_LidarVisualizer
主要方法：
- `RDF_LidarVisualizer(RDF_LidarVisualSettings settings = null)` — 构造函数
- `void Render(IEntity subject, RDF_LidarScanner scanner)` — 执行扫描并绘制点云（请注意形状上限）。
- `RDF_LidarVisualSettings GetSettings()`
- `ref array<ref RDF_LidarSample> GetLastSamples()` — 获取最近一次渲染的 sample 数组
- `string ExportLastScanCSV()` / `string ExportLastScanJSON()` — 以字符串形式返回上次扫描的 CSV/JSON（不保证自动写入磁盘）

颜色策略：可用 `RDF_LidarColorStrategy` 替换默认色彩映射。

(Export functionality removed)

Note: Export helpers were removed by project owner request. Use `RDF_LidarVisualizer.GetLastSamples()` to retrieve scan samples and export them externally if needed.

## Strategies

### RDF_LidarSampleStrategy (接口)
- `vector BuildDirection(int index, int count)` — 返回单位方向向量

实现：`RDF_UniformSampleStrategy`（基于黄金角近似的球面均匀采样）

### RDF_LidarColorStrategy (接口)
- `int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)`
- `int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)`

默认实现：`RDF_DefaultColorStrategy`。

## Demo

### RDF_LidarAutoRunner
- `static void SetDemoEnabled(bool enabled)` — 全局控制 demo
- `static bool IsDemoEnabled()`
- `static void SetMinTickInterval(float interval)` — 设置最小 tick 间隔（秒，推荐默认 0.2）以减少 per-frame 调度开销

### RDF_LidarAutoEntity
- 场景实体，可以在编辑器中通过属性启用/禁用 demo。

### SCR_BaseGameMode (modded bootstrap)
- `static void SetBootstrapEnabled(bool enabled)` — 若启用，将在 `OnGameStart` 时自动 `SetDemoEnabled(true)`；默认**禁用**以避免意外激活。

---

使用示例和注意事项在 `README.md` 中，有关参数安全性、性能建议（限制射线数、降低分段）及导出使用说明也在 docs 中说明。