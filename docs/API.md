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

## Demo（统一 API）

### 统一开关
- **RDF_LidarAutoRunner** 为演示的唯一起动入口，所有演示通过 **SetDemoEnabled** 与 **SetDemoConfig / StartWithConfig** 控制。

### RDF_LidarAutoRunner
- `static void SetDemoEnabled(bool enabled)` — **统一开关**：开启/关闭 demo
- `static bool IsDemoEnabled()` — 是否已启用 demo
- `static void StartWithConfig(RDF_LidarDemoConfig cfg)` — 应用配置并开启 demo（推荐与 `RDF_LidarDemoConfig.Create*()` 预设搭配）
- `static void SetDemoConfig(RDF_LidarDemoConfig cfg)` — 设置当前 demo 配置（不自动开启）
- `static RDF_LidarDemoConfig GetDemoConfig()` — 获取当前配置
- `static void SetMinTickInterval(float interval)` — 最小 tick 间隔（秒）
- `static float GetMinTickInterval()` — 获取最小 tick 间隔
- `static void SetDemoSampleStrategy(RDF_LidarSampleStrategy strategy)` — 设置演示用采样策略
- `static void SetDemoRayCount(int rays)` — 设置演示射线数（clamp）
- `static void SetDemoColorStrategy(RDF_LidarColorStrategy strategy)` — 设置演示颜色策略
- `static void SetDemoUpdateInterval(float interval)` — 设置演示扫描更新间隔（秒）
- `static void StartAutoRun()` / `static void StopAutoRun()` — 内部启停（一般通过 SetDemoEnabled 即可）
- `static bool IsRunning()` — 当前是否正在自动运行

---

### RDF_LidarDemoConfig
配置对象，**推荐通过静态预设工厂创建**，再配合 `SetDemoConfig` / `StartWithConfig` 使用：
- `bool m_Enable` — 是否启用 demo（默认 false）
- `RDF_LidarSampleStrategy m_SampleStrategy` — 采样策略
- `RDF_LidarColorStrategy m_ColorStrategy` — 颜色策略（可选）
- `int m_RayCount` — 射线数量（-1 表示不覆盖）
- `float m_MinTickInterval` — 最小 tick 间隔（秒，-1 表示不覆盖）
- `float m_UpdateInterval` — 扫描更新间隔（秒，-1 表示不覆盖）

**预设工厂（替代原 RDF_*Demo.Start）：**
- `static RDF_LidarDemoConfig CreateDefault(int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateHemisphere(int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateConical(float halfAngleDeg = 30.0, int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateStratified(int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateScanline(int sectors = 32, int rayCount = 256)`

方法：
- `void ApplyTo(RDF_LidarAutoRunner runner)` — 将配置应用到 runner（内部通过 AutoRunner 的 SetDemo* API 实现）。

### RDF_LidarDemoCycler
策略轮换，仅调用 `RDF_LidarAutoRunner.SetDemoConfig` + `SetDemoEnabled`：
- `static void Cycle(int rayCount = 256)` — 下一策略并开启 demo
- `static void StartIndex(int index, int rayCount = 256)` — 按索引启动
- `static void StartAutoCycle(float interval = 10.0)` / `StopAutoCycle()` / `SetAutoCycleInterval(float)` / `IsAutoCycling()`

### SCR_BaseGameMode（统一 Bootstrap）
- `static void SetBootstrapEnabled(bool enabled)` — **统一开关**：为 true 时在 `OnGameStart` 自动开演示；默认 **false**。
- `static bool IsBootstrapEnabled()` — 是否启用 bootstrap
- `static void SetBootstrapAutoCycle(bool enabled)` — 开局是否以自动轮换模式启动
- `static void SetBootstrapAutoCycleInterval(float intervalSeconds)` — 轮换间隔（秒）

---

使用示例和注意事项在 `README.md` 中，有关参数安全性、性能建议（限制射线数、降低分段）及导出使用说明也在 docs 中说明.