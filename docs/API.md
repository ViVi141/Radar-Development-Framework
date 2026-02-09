# API Reference — Radar Development Framework (LiDAR)

此文档为第一版 API 摘要，覆盖主要公开类与要点。

## Core

### RDF_LidarSettings
字段：
- `m_Enabled` (bool): 是否启用扫描（默认 true）
- `m_Range` (float): 扫描半径（默认 50.0）。在运行时会被 clamp 到 [0.1, 1000.0]
- `m_RayCount` (int): 射线数量（默认 512）。在运行时会被 clamp 到 [1, 4096]
- `m_UpdateInterval` (float): 扫描间隔（秒，默认 5.0），最小值 0.01
- `m_OriginOffset` (vector): 扫描原点偏移（默认 "0 0 0"），与 `m_UseLocalOffset` 配合使用
- `m_TraceFlags` (int): 射线检测标志（默认 `TraceFlags.WORLD | TraceFlags.ENTS`）
- `m_LayerMask` (int): 物理层掩码（默认 `EPhysicsLayerPresets.Projectile`）
- `m_UseBoundsCenter` (bool): 是否使用实体包围盒中心作为扫描原点（默认 true）
- `m_UseLocalOffset` (bool): 是否将 `m_OriginOffset` 视为实体局部空间偏移（默认 true）

方法：
- `void Validate()` — 对设置进行防护性校验与 clamp（在扫描前会被自动调用）。

### RDF_LidarSample
单条射线的采样结果数据结构。字段：
- `m_Index` (int): 射线索引
- `m_Hit` (bool): 是否命中
- `m_Start` (vector): 射线起点
- `m_End` (vector): 射线终点（最大距离处）
- `m_Dir` (vector): 单位方向向量
- `m_HitPos` (vector): 命中点位置（未命中时为射线终点）
- `m_Distance` (float): 命中距离（未命中时为最大距离）
- `m_Entity` (IEntity): 命中的实体（若有）
- `m_ColliderName` (string): 命中的碰撞体名称
- `m_Surface` (GameMaterial): 命中表面材质（若有）

### RDF_LidarScanner
主要方法：
- `RDF_LidarScanner(RDF_LidarSettings settings = null)` — 构造器
- `RDF_LidarSettings GetSettings()` — 获取设置引用
- `void Scan(IEntity subject, array<ref RDF_LidarSample> outSamples)` — 对给定实体执行扫描并填充 samples（会在入口处调用 `Validate()`）。
- `void SetSampleStrategy(RDF_LidarSampleStrategy strategy)` — 注入自定义采样策略
- `RDF_LidarSampleStrategy GetSampleStrategy()` — 获取当前采样策略

备注：默认采样策略为 `RDF_UniformSampleStrategy`。

## Visual

### RDF_LidarVisualSettings
字段：
- `m_DrawPoints` (bool): 是否绘制命中点（默认 true）
- `m_DrawRays` (bool): 是否绘制射线（默认 true）
- `m_ShowHitsOnly` (bool): 是否仅绘制命中的点/射线（默认 false）
- `m_PointSize` (float): 点球体半径（默认 0.08）
- `m_RayAlpha` (float): 射线透明度（默认 0.25）
- `m_RaySegments` (int): 每条射线的分段数，用于渐变显示（默认 6）
- `m_UseDistanceGradient` (bool): 是否按距离渐变着色（默认 true）；为 false 时命中为红、未命中为橙

### RDF_LidarVisualizer
主要方法：
- `RDF_LidarVisualizer(RDF_LidarVisualSettings settings = null)` — 构造函数
- `void Render(IEntity subject, RDF_LidarScanner scanner)` — 执行扫描并绘制点云。
- `RDF_LidarVisualSettings GetSettings()`
- `ref array<ref RDF_LidarSample> GetLastSamples()` — 获取最近一次渲染的 sample 数组

颜色策略：可用 `RDF_LidarColorStrategy` 替换默认色彩映射。需自定义导出时，使用 `GetLastSamples()` 取得数据后自行处理（如 CSV/JSON）。

## Strategies

### RDF_LidarSampleStrategy (接口)
- `vector BuildDirection(int index, int count)` — 返回单位方向向量

实现：`RDF_UniformSampleStrategy`（基于黄金角近似的球面均匀采样）

### RDF_LidarColorStrategy (接口)
- `int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)`
- `int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)`

默认实现：`RDF_DefaultColorStrategy`。

## Util

### RDF_LidarSubjectResolver
用于解析扫描主体（本地玩家或当前载具）的静态工具类。
- `static IEntity ResolveLocalSubject(bool preferVehicle = true)` — 解析本地玩家控制的实体；若在载具内且 preferVehicle 为 true 则返回载具根实体
- `static IEntity ResolveSubject(IEntity player, bool preferVehicle = true)` — 对给定玩家实体解析主体（载具或玩家）
- `static vector ResolveOrigin(IEntity player = null, bool preferVehicle = true)` — 解析主体原点；player 为 null 时使用本地玩家

## Demo

### RDF_LidarAutoRunner
- `static void SetDemoEnabled(bool enabled)` — 全局开启/关闭 demo
- `static bool IsDemoEnabled()` — 是否已启用 demo
- `static void SetMinTickInterval(float interval)` — 设置最小 tick 间隔（秒，推荐默认 0.2）以减少 per-frame 调度开销
- `static float GetMinTickInterval()` — 获取当前最小 tick 间隔
- `static void StartAutoRun()` — 开始自动扫描循环
- `static void StopAutoRun()` — 停止自动扫描循环
- `static bool IsRunning()` — 当前是否正在自动运行

### SCR_BaseGameMode (modded bootstrap)
- `static void SetBootstrapEnabled(bool enabled)` — 若启用，将在 `OnGameStart` 时自动 `SetDemoEnabled(true)`；默认**禁用**以避免意外激活。

---

## Demo API (新增)

### RDF_LidarDemoConfig
配置对象用于在启动 demo 时一次性传入多个设置：
- `bool m_Enable` — 是否启用 demo（默认 false）
- `RDF_LidarSampleStrategy m_SampleStrategy` — 要使用的采样策略（示例：`RDF_ConicalSampleStrategy`）
- `RDF_LidarColorStrategy m_ColorStrategy` — 可选的颜色/大小策略（示例：`RDF_IndexColorStrategy`）
- `int m_RayCount` — 覆盖扫描的射线数量（-1 表示保持不变）
- `float m_MinTickInterval` — 覆盖 AutoRunner 的最小调度间隔（秒，-1 表示保持不变）
- `float m_UpdateInterval` — 覆盖扫描设置的更新间隔（秒，-1 表示保持不变）

方法：
- `void ApplyTo(RDF_LidarAutoRunner runner)` — 将配置安全地应用到指定的 `RDF_LidarAutoRunner`（注：也可通过 `RDF_LidarAutoRunner.SetDemoConfig()` 全局设置）。

### RDF_LidarAutoRunner（扩展）
新增方法：
- `static void SetDemoConfig(RDF_LidarDemoConfig cfg)` — 设置将被应用于下次启用 demo 的配置对象。
- `static RDF_LidarDemoConfig GetDemoConfig()` — 获取当前（最近设置的）demo 配置。
- `void ApplyDemoConfig()` — 实例方法：将 `m_DemoConfig` 应用到 scanner/visualizer/设置（在 `SetDemoEnabled(true)` 时自动调用）。
- `static void SetDemoRayCount(int rays)` — 安全地在 demo scanner 上设定 `m_RayCount`（clamp）。

示例：
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

---

使用示例和注意事项在 `README.md` 中，有关参数安全性、性能建议（限制射线数、降低分段）及导出使用说明也在 docs 中说明.