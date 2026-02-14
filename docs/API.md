# API Reference — Radar Development Framework (LiDAR)

此文档为第一版 API 摘要，覆盖主要公开类与要点。

## Core

### RDF_LidarSettings
字段：
- `m_Enabled` (bool): 是否启用扫描（默认 true）
- `m_Range` (float): 扫描半径（默认 50.0）。在运行时会被 clamp 到 [0.1, 1000.0]
- `m_RayCount` (int): 射线数量（默认 512）。在运行时保证至少为 1，无上限
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
- `m_DrawOriginAxis` (bool): 是否绘制扫描原点与局部 X/Y/Z 三轴（默认 false，调试用）
- `m_OriginAxisLength` (float): 三轴长度（默认 0.8）
- `m_RenderWorld` (bool): 为 true 时渲染游戏画面+点云；为 false 时仅渲染点云（在相机前绘制黑色四边形遮挡场景，并通过 `SetCharacterCameraRenderActive(false)` 关闭场景渲染，默认 true）

### RDF_LidarVisualizer
主要方法：
- `RDF_LidarVisualizer(RDF_LidarVisualSettings settings = null)` — 构造函数
- `void Render(IEntity subject, RDF_LidarScanner scanner)` — 执行扫描并绘制点云。
- `RDF_LidarVisualSettings GetSettings()`
- `ref array<ref RDF_LidarSample> GetLastSamples()` — 返回最近一次渲染样本的防御性副本（对返回数组的修改不会影响 visualizer 内部状态）

颜色策略：可用 `RDF_LidarColorStrategy` 替换默认色彩映射。需自定义导出时，使用 `GetLastSamples()` 取得数据后自行处理（如 CSV/JSON）。

## Strategies

### RDF_LidarSampleStrategy (接口)
- `vector BuildDirection(int index, int count)` — 返回单位方向向量

实现：`RDF_UniformSampleStrategy`、`RDF_HemisphereSampleStrategy`、`RDF_ConicalSampleStrategy`、`RDF_StratifiedSampleStrategy`、`RDF_ScanlineSampleStrategy`、`RDF_SweepSampleStrategy`（随时间旋转的扇区扫描，雷达风格）。

### RDF_SweepSampleStrategy
- 构造：`RDF_SweepSampleStrategy(float halfAngleDeg = 30.0, float sweepWidthDeg = 20.0, float sweepSpeedDegPerSec = 45.0)` — 锥半角、扇区宽度（度）、旋转速度（度/秒）。
- 射线方向在扇区内沿弧线均匀分布，扇区随世界时间旋转。

### RDF_LidarColorStrategy (接口)
- `int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)`
- `int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)`

实现：`RDF_DefaultColorStrategy`、`RDF_IndexColorStrategy`、`RDF_ThreeColorStrategy`（近/中/远三段渐变，默认绿→黄→红）。

## Util

### RDF_LidarSubjectResolver
用于解析扫描主体（本地玩家或当前载具）的静态工具类。
- `static IEntity ResolveLocalSubject(bool preferVehicle = true)` — 解析本地玩家控制的实体；若在载具内且 preferVehicle 为 true 则返回载具根实体
- `static IEntity ResolveSubject(IEntity player, bool preferVehicle = true)` — 对给定玩家实体解析主体（载具或玩家）
- `static vector ResolveOrigin(IEntity player = null, bool preferVehicle = true)` — 解析主体原点；player 为 null 时使用本地玩家

### RDF_LidarExport
将扫描结果导出为 CSV 格式（输出到控制台或写入磁盘）。
- `static string GetCSVHeader()` — 返回 CSV 表头行
- `static string SampleToCSVRow(RDF_LidarSample sample)` — 将单条样本格式化为 CSV 行
- `static void PrintCSVToConsole(array<ref RDF_LidarSample> samples)` — 将整次扫描以 CSV 打印到控制台
- `static void ExportLastScanToConsole(RDF_LidarVisualizer visualizer)` — 从 visualizer 取上次扫描并打印 CSV
- `static bool ExportToFile(array<ref RDF_LidarSample> samples, string path)` — 将样本写入 CSV 文件（覆盖），成功返回 true
- `static bool AppendToFile(array<ref RDF_LidarSample> samples, string path, bool writeHeaderIfNew = true)` — 追加样本到 CSV 文件，文件不存在时创建并可选写入表头
- `static string GetExtendedCSVHeader()` — 扩展 CSV 表头（含 time、origin、elevation、azimuth、subjectVel、subjectYaw、subjectPitch、scanId、frameIndex、entityClass，适用于 AI 训练）
- `static string SampleToExtendedCSVRow(sample, currentTime, maxRange, subjectVel, subjectYaw, subjectPitch, scanId, frameIndex)` — 将单条样本格式化为扩展 CSV 行

### RDF_LidarSampleUtils
对样本数组的统计与过滤（静态方法）。
- `static RDF_LidarSample GetClosestHit(array<ref RDF_LidarSample> samples)` — 最近命中样本，无则 null
- `static RDF_LidarSample GetFurthestHit(array<ref RDF_LidarSample> samples)` — 最远命中样本，无则 null
- `static int GetHitCount(array<ref RDF_LidarSample> samples)` — 命中数量
- `static void GetHitsInRange(samples, minDist, maxDist, outSamples)` — 将距离在 [minDist, maxDist] 的命中填入 outSamples
- `static float GetAverageDistance(samples, bool hitsOnly = true)` — 平均距离（hitsOnly 为 true 时仅统计命中）

### RDF_LidarNetworkAPI
网络同步 API 基类（默认空实现），用于解耦 Demo 与网络逻辑。

主要方法：
- `bool IsNetworkAvailable()` — 网络层是否可用
- `void SetDemoEnabled(bool enabled)` — 设置演示启用状态（服务器权威）
- `void SetDemoConfig(RDF_LidarDemoConfig config)` — 设置演示配置（服务器权威）
- `void RequestScan()` — 请求服务器执行扫描（客户端调用，无参数，服务器使用组件所属实体作为扫描主体）
- `bool HasSyncedSamples()` — 是否已有同步样本
- `array<ref RDF_LidarSample> GetLastScanResults()` — 获取最后扫描结果

### RDF_LidarNetworkComponent
Network 模块内置实现，基于 Rpl 同步状态与扫描结果。

同步属性（原子字段）：
- `[RplProp] m_DemoEnabled` — 演示启用状态（bool）
- `[RplProp] m_RayCount` — 射线计数（int）
- `[RplProp] m_UpdateInterval` — 扫描更新间隔（float）
- `[RplProp] m_RenderWorld` — 是否同时渲染世界（bool）
- `[RplProp] m_DrawOriginAxis` — 是否绘制原点轴（bool）
- `[RplProp] m_Verbose` — 是否输出统计（bool）

备注：不再直接 Rpl 同步 `RDF_LidarDemoConfig` 对象；配置通过 RPC 以原子字段形式传输以避免复杂对象复制。

扫描结果：服务器通过 `RDF_LidarExport.SamplesToCSV()` 将样本序列化为紧凑 CSV 字符串，并通过不可靠 RPC `RpcDo_ScanCompleteWithPayload(string csv)` 广播到客户端；客户端使用 `RDF_LidarExport.ParseCSVToSamples(csv)` 解析回 `RDF_LidarSample` 列表。
### RDF_LidarAutoRunner 网络集成
- `static void SetNetworkAPI(RDF_LidarNetworkAPI networkAPI)` — 设置网络 API 引用
- `static RDF_LidarNetworkAPI GetNetworkAPI()` — 获取网络 API

当设置网络 API 后，AutoRunner 会使用服务器权威的扫描与同步状态变化。

### RDF_LidarNetworkUtils
网络辅助工具：
- `static RDF_LidarNetworkAPI FindNetworkAPI(IEntity entity)` — 从实体或其父链上查找网络 API
- `static bool BindAutoRunnerToLocalSubject(bool preferVehicle = true)` — 自动绑定本地玩家主体上的网络 API

### RDF_LidarNetworkScanner
网络扫描适配器（非 Demo）：
- `static bool Scan(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, RDF_LidarNetworkAPI api)` — 有网络则用同步结果，否则本地扫描（同步，若无可用同步样本则返回 false）
- `static bool ScanWithAutoRunnerAPI(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples)` — 使用 AutoRunner 已绑定的网络 API（同步）
- `static void ScanAsync(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, RDF_LidarNetworkAPI api, float timeoutSeconds, RDF_LidarScanCompleteHandler handler)` — 异步请求服务器扫描，并通过 `handler.OnScanComplete(outSamples)` 在完成或超时后回调（非阻塞）。参数 `timeoutSeconds` 可用于指定超时回退时限；若想更细粒度控制轮询间隔与重试，可联系维护者扩展 `ScanAsync`。
- `static void ScanWithAutoRunnerAPIAsync(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, float timeoutSeconds, RDF_LidarScanCompleteHandler handler)` — 使用 AutoRunner 已绑定的网络 API 的异步便捷方式

### RDF_LidarExport 网络序列化
- `static string SamplesToCSV(array<ref RDF_LidarSample> samples, bool compress = false, int decimalPlaces = 3)` — 将样本序列化为紧凑字符串；当 `compress=true` 时会在序列化后应用简单 RLE 压缩并在结果前缀 `RLE:` 以便解析方识别并解压。

### RDF_LidarVisualizer 网络支持
- `void RenderWithSamples(IEntity subject, array<ref RDF_LidarSample> samples)` — 使用预计算样本渲染（用于网络同步）

## Demo（统一 API）

### 统一开关
- **RDF_LidarAutoRunner** 为演示的唯一起动入口，所有演示通过 **SetDemoEnabled** 与 **SetDemoConfig / StartWithConfig** 控制。

### RDF_LidarAutoRunner
- `static void SetDemoEnabled(bool enabled)` — **统一开关**：开启/关闭 demo
- `static bool IsDemoEnabled()` — 是否已启用 demo
- `static void StartWithConfig(RDF_LidarDemoConfig cfg)` — 应用配置并开启 demo（推荐与 `RDF_LidarDemoConfig.Create*()` 预设搭配）
- `static void SetDemoConfig(RDF_LidarDemoConfig cfg)` — 设置当前 demo 配置（不自动开启）；若 demo 已在运行则立即应用新配置（如 `m_RenderWorld` 等）
- `static RDF_LidarDemoConfig GetDemoConfig()` — 获取当前配置
- `static void SetMinTickInterval(float interval)` — 最小 tick 间隔（秒）
- `static float GetMinTickInterval()` — 获取最小 tick 间隔
- `static void SetDemoSampleStrategy(RDF_LidarSampleStrategy strategy)` — 设置演示用采样策略
- `static void SetDemoRayCount(int rays)` — 设置演示射线数（clamp）
- `static void SetDemoColorStrategy(RDF_LidarColorStrategy strategy)` — 设置演示颜色策略
- `static void SetDemoUpdateInterval(float interval)` — 设置演示扫描更新间隔（秒）
- `static void SetScanCompleteHandler(RDF_LidarScanCompleteHandler handler)` — 设置扫描完成回调（传 null 清除）
- `static RDF_LidarScanCompleteHandler GetScanCompleteHandler()` — 获取当前回调
- `static void SetDemoDrawOriginAxis(bool draw)` — 是否在 demo 中绘制扫描原点与三轴（对应 VisualSettings.m_DrawOriginAxis）
- `static void SetDemoRenderWorld(bool renderWorld)` — 为 true 时渲染游戏画面+点云，为 false 时仅渲染点云；内部会同步调用 `PlayerController.SetCharacterCameraRenderActive(renderWorld)` 以关闭/恢复场景渲染
- `static bool GetDemoRenderWorld()` — 获取当前是否渲染游戏画面（默认 true）
- `static void SetDemoVerbose(bool verbose)` — 为 true 时使用内置回调每帧打印命中数与最近距离（使用 RDF_LidarSampleUtils）
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
- `bool m_DrawOriginAxis` — 为 true 时 demo 绘制扫描原点与三轴（ApplyTo 时通过 SetDemoDrawOriginAxis 应用）
- `bool m_Verbose` — 为 true 时 demo 每帧打印命中数与最近距离（ApplyTo 时通过 SetDemoVerbose 应用）
- `bool m_RenderWorld` — 为 true 时渲染游戏画面+点云，为 false 时仅渲染点云（ApplyTo 时通过 SetDemoRenderWorld 应用）

**预设工厂（替代原 RDF_*Demo.Start）：**
- `static RDF_LidarDemoConfig CreateDefault(int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateThreeColor(int rayCount = 512)` — 同 CreateDefault，三色距离渐变（近绿→中黄→远红）
- `static RDF_LidarDemoConfig CreateDefaultDebug(int rayCount = 512)` — 同 CreateDefault，并开启原点轴与统计输出（展示新功能）
- `static RDF_LidarDemoConfig CreateHemisphere(int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateConical(float halfAngleDeg = 30.0, int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateStratified(int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateScanline(int sectors = 32, int rayCount = 256)`
- `static RDF_LidarDemoConfig CreateSweep(float halfAngleDeg = 30.0, float sweepWidthDeg = 20.0, float sweepSpeedDegPerSec = 45.0, int rayCount = 512)` — 雷达扫描动画预设

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

## 仅逻辑不渲染（无可视化）

若只需要扫描数据做逻辑（威胁检测、距离判断等），**不必使用 RDF_LidarVisualizer**，直接使用 Scanner 即可，避免创建 debug shapes：

```c
// 仅扫描，不绘制
RDF_LidarScanner scanner = new RDF_LidarScanner();
RDF_LidarSettings settings = scanner.GetSettings();
settings.m_RayCount = 128;
settings.m_Range = 80.0;

array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
scanner.Scan(subject, samples);

// 用工具类做统计
int hits = RDF_LidarSampleUtils.GetHitCount(samples);
RDF_LidarSample closest = RDF_LidarSampleUtils.GetClosestHit(samples);
if (closest)
    Print("Closest hit distance: " + closest.m_Distance);
```

若使用 AutoRunner 但希望每次扫描后做处理，可设置 `SetScanCompleteHandler`，在回调里用 `RDF_LidarSampleUtils` 或 `RDF_LidarExport.PrintCSVToConsole(samples)` 导出。

---

使用示例和注意事项在 `README.md` 中，有关参数安全性、性能建议（限制射线数、降低分段）及导出使用说明也在 docs 中说明。

---

## English translation

# API Reference — Radar Development Framework (LiDAR)

This document is the first-version API summary covering the main public classes and highlights.

## Core

### RDF_LidarSettings
Fields:
- `m_Enabled` (bool): enable scanning (default true)
- `m_Range` (float): scan radius (default 50.0). Clamped at runtime to [0.1, 1000.0]
- `m_RayCount` (int): number of rays (default 512). At runtime guaranteed to be at least 1.
- `m_UpdateInterval` (float): scan interval in seconds (default 5.0), minimum 0.01
- `m_OriginOffset` (vector): origin offset (default "0 0 0"), used with `m_UseLocalOffset`
- `m_TraceFlags` (int): trace flags (default `TraceFlags.WORLD | TraceFlags.ENTS`)
- `m_LayerMask` (int): physics layer mask (default `EPhysicsLayerPresets.Projectile`)
- `m_UseBoundsCenter` (bool): use bounding-box center as origin (default true)
- `m_UseLocalOffset` (bool): treat `m_OriginOffset` as local-space offset (default true)

Methods:
- `void Validate()` — defensive validation and clamping (called automatically before scanning).

### RDF_LidarSample
Single-ray sample structure. Fields:
- `m_Index` (int): ray index
- `m_Hit` (bool): whether it hit
- `m_Start` (vector): ray start
- `m_End` (vector): ray end (max distance)
- `m_Dir` (vector): unit direction vector
- `m_HitPos` (vector): hit position (or ray end if no hit)
- `m_Distance` (float): hit distance (or max distance if no hit)
- `m_Entity` (IEntity): hit entity (if any)
- `m_ColliderName` (string): collider name
- `m_Surface` (GameMaterial): hit surface material (if available)

### RDF_LidarScanner
Key methods:
- `RDF_LidarScanner(RDF_LidarSettings settings = null)` — constructor
- `RDF_LidarSettings GetSettings()` — get settings reference
- `void Scan(IEntity subject, array<ref RDF_LidarSample> outSamples)` — scan given entity and fill samples (calls `Validate()` at entry)
- `void SetSampleStrategy(RDF_LidarSampleStrategy strategy)` — inject a sampling strategy
- `RDF_LidarSampleStrategy GetSampleStrategy()` — get current strategy

Notes: default sampling strategy is `RDF_UniformSampleStrategy`.

## Visual

### RDF_LidarVisualSettings
Fields:
- `m_DrawPoints` (bool): draw hit points (default true)
- `m_DrawRays` (bool): draw rays (default true)
- `m_ShowHitsOnly` (bool): draw only hits (default false)
- `m_PointSize` (float): point sphere radius (default 0.08)
- `m_RayAlpha` (float): ray alpha (default 0.25)
- `m_RaySegments` (int): segments per ray for gradient rendering (default 6)
- `m_UseDistanceGradient` (bool): distance-based gradient coloring (default true)
- `m_DrawOriginAxis` (bool): draw origin axis (default false, debug)
- `m_OriginAxisLength` (float): axis length (default 0.8)
- `m_RenderWorld` (bool): when true render game world + point cloud; when false render point cloud only (draws a black quad and calls `SetCharacterCameraRenderActive(false)` to disable scene rendering; default true)

### RDF_LidarVisualizer
Key methods:
- `RDF_LidarVisualizer(RDF_LidarVisualSettings settings = null)` — constructor
- `void Render(IEntity subject, RDF_LidarScanner scanner)` — perform scan and render point-cloud
- `RDF_LidarVisualSettings GetSettings()`
- `ref array<ref RDF_LidarSample> GetLastSamples()` — returns a defensive copy of the last rendered samples

Color strategy: replaceable via `RDF_LidarColorStrategy`. For exports, use `GetLastSamples()` to obtain data and post-process (CSV/JSON).

## Strategies

### RDF_LidarSampleStrategy (interface)
- `vector BuildDirection(int index, int count)` — returns a unit direction vector

Implementations: `RDF_UniformSampleStrategy`, `RDF_HemisphereSampleStrategy`, `RDF_ConicalSampleStrategy`, `RDF_StratifiedSampleStrategy`, `RDF_ScanlineSampleStrategy`, `RDF_SweepSampleStrategy` (time-rotating sector — radar style).

### RDF_SweepSampleStrategy
- Constructor: `RDF_SweepSampleStrategy(float halfAngleDeg = 30.0, float sweepWidthDeg = 20.0, float sweepSpeedDegPerSec = 45.0)` — half-angle, sector width (deg), rotation speed (deg/sec).
- Directions are distributed across the sector and sector rotates with world time.

### RDF_LidarColorStrategy (interface)
- `int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)`
- `int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)`

Implementations: `RDF_DefaultColorStrategy`, `RDF_IndexColorStrategy`, `RDF_ThreeColorStrategy` (three-range gradient: near/mid/far — green→yellow→red).

## Util

### RDF_LidarSubjectResolver
Utility for resolving the scan subject (local player or vehicle):
- `static IEntity ResolveLocalSubject(bool preferVehicle = true)` — resolve the local subject; if in vehicle and preferVehicle==true returns the vehicle root entity
- `static IEntity ResolveSubject(IEntity player, bool preferVehicle = true)` — resolve subject for a given player entity
- `static vector ResolveOrigin(IEntity player = null, bool preferVehicle = true)` — resolve subject origin; player==null uses local player

### RDF_LidarExport
Export scan results to CSV (console or disk):
- `static string GetCSVHeader()`
- `static string SampleToCSVRow(RDF_LidarSample sample)`
- `static void PrintCSVToConsole(array<ref RDF_LidarSample> samples)`
- `static void ExportLastScanToConsole(RDF_LidarVisualizer visualizer)`
- `static bool ExportToFile(array<ref RDF_LidarSample> samples, string path)` — overwrite file
- `static bool AppendToFile(array<ref RDF_LidarSample> samples, string path, bool writeHeaderIfNew = true)` — append; create file if missing
- `static string GetExtendedCSVHeader()` — extended header (time, origin, elevation, azimuth, subjectVel, subjectYaw, subjectPitch, scanId, frameIndex, entityClass)
- `static string SampleToExtendedCSVRow(sample, currentTime, maxRange, subjectVel, subjectYaw, subjectPitch, scanId, frameIndex)`

### RDF_LidarSampleUtils
Static statistics & filters for sample arrays:
- `static RDF_LidarSample GetClosestHit(array<ref RDF_LidarSample> samples)`
- `static RDF_LidarSample GetFurthestHit(array<ref RDF_LidarSample> samples)`
- `static int GetHitCount(array<ref RDF_LidarSample> samples)`
- `static void GetHitsInRange(samples, minDist, maxDist, outSamples)`
- `static float GetAverageDistance(samples, bool hitsOnly = true)`

### RDF_LidarNetworkAPI
Network sync API base (default empty implementation) to decouple demo and networking:
- `bool IsNetworkAvailable()`
- `void SetDemoEnabled(bool enabled)`
- `void SetDemoConfig(RDF_LidarDemoConfig config)`
- `void RequestScan()`
- `bool HasSyncedSamples()`
- `array<ref RDF_LidarSample> GetLastScanResults()`

### RDF_LidarNetworkComponent
Built-in network implementation using Rpl and sample serialization.
Synchronized properties: `[RplProp] m_DemoEnabled`, `m_RayCount`, `m_UpdateInterval`, `m_RenderWorld`, `m_DrawOriginAxis`, `m_Verbose`.
Note: `RDF_LidarDemoConfig` objects are not Rpl-synchronized; configs are transported via atomic fields/RPC.

### RDF_LidarNetworkScanner
Network-aware scanner adapter:
- `static bool Scan(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples, RDF_LidarNetworkAPI api)` — use network if available, otherwise local scan
- `static bool ScanWithAutoRunnerAPI(IEntity subject, RDF_LidarScanner scanner, array<ref RDF_LidarSample> outSamples)` — convenience
- `static void ScanAsync(..., RDF_LidarScanCompleteHandler handler)` — async request with timeout and callback

### RDF_LidarExport network serialization
- `static string SamplesToCSV(array<ref RDF_LidarSample> samples, bool compress = false, int decimalPlaces = 3)` — compact serialization; `compress=true` applies optional RLE compression with `RLE:` prefix.

### RDF_LidarVisualizer network support
- `void RenderWithSamples(IEntity subject, array<ref RDF_LidarSample> samples)` — render using precomputed samples (for network sync)

## Demo (unified API)

### Unified switch
`RDF_LidarAutoRunner` is the single entry for demos; controlled by `SetDemoEnabled` and `SetDemoConfig / StartWithConfig`.

### RDF_LidarAutoRunner
- `static void SetDemoEnabled(bool enabled)`
- `static bool IsDemoEnabled()`
- `static void StartWithConfig(RDF_LidarDemoConfig cfg)`
- `static void SetDemoConfig(RDF_LidarDemoConfig cfg)`
- `static RDF_LidarDemoConfig GetDemoConfig()`
- `static void SetMinTickInterval(float interval)`
- `static float GetMinTickInterval()`
- `static void SetDemoSampleStrategy(RDF_LidarSampleStrategy strategy)`
- `static void SetDemoRayCount(int rays)`
- `static void SetDemoColorStrategy(RDF_LidarColorStrategy strategy)`
- `static void SetDemoUpdateInterval(float interval)`
- `static void SetScanCompleteHandler(RDF_LidarScanCompleteHandler handler)`
- `static RDF_LidarScanCompleteHandler GetScanCompleteHandler()`
- `static void SetDemoDrawOriginAxis(bool draw)`
- `static void SetDemoRenderWorld(bool renderWorld)`
- `static bool GetDemoRenderWorld()`
- `static void SetDemoVerbose(bool verbose)`
- `static void StartAutoRun()` / `static void StopAutoRun()`
- `static bool IsRunning()`

### RDF_LidarDemoConfig
Configuration object with factory presets (use `Create*()`):
- `bool m_Enable`
- `RDF_LidarSampleStrategy m_SampleStrategy`
- `RDF_LidarColorStrategy m_ColorStrategy`
- `int m_RayCount`
- `float m_MinTickInterval`
- `float m_UpdateInterval`
- `bool m_DrawOriginAxis`
- `bool m_Verbose`
- `bool m_RenderWorld`

Factory presets: `CreateDefault`, `CreateThreeColor`, `CreateDefaultDebug`, `CreateHemisphere`, `CreateConical`, `CreateStratified`, `CreateScanline`, `CreateSweep`.

Methods: `void ApplyTo(RDF_LidarAutoRunner runner)` — apply config to runner.

### RDF_LidarDemoCycler
- `static void Cycle(int rayCount = 256)`
- `static void StartIndex(int index, int rayCount = 256)`
- `static void StartAutoCycle(float interval = 10.0)` / `StopAutoCycle()` / `SetAutoCycleInterval(float)` / `IsAutoCycling()`

### SCR_BaseGameMode (bootstrap)
- `static void SetBootstrapEnabled(bool enabled)`
- `static bool IsBootstrapEnabled()`
- `static void SetBootstrapAutoCycle(bool enabled)`
- `static void SetBootstrapAutoCycleInterval(float intervalSeconds)`

## Logic-only (no visualizer)

Use `RDF_LidarScanner` directly for logic-only scans; avoid creating a Visualizer to skip debug shapes. Example usage is provided in the README and above.

---

If you want example demos, CI configuration, or additional sample strategies implemented, tell me which item to prioritize.