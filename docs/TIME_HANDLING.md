> **Languages / 语言**: [中文](#中文) · [English](#english)

# RDF 时间处理与基本类型规范 / Time Handling & Primitive-Type Conventions

> 适用：`scripts/Game/RDF/**`（Radar / LiDAR / DEM / 网络 / UI）。从 2026-08-14「GetWorldTime 毫秒/秒混用 9 处」、2026-08-15「帧预算分母错误 + 时间审计 7 项 + 类型审计 31 项」中沉淀的规则。目标：让时间/类型处理可审、可测、可预测。

---

## 中文

### 1. 时钟选择矩阵（最重要的规则）

项目里有**三种时间源**，语义不同、**不可混用**：

| 时间源 | 返回 | 用途 | 何时用 |
|---|---|---|---|
| `BaseWorld.GetWorldTime()` | **毫秒** (int) | 模拟/世界时间 | 扫描时间戳、航迹/锁定/数据链的**逻辑时间**（暂停时冻结、时间缩放时缩放） |
| `System.GetTickCount()` | **毫秒** (int) | 墙钟时间 | 超时、节拍门控、性能计时、RWR/假点 TTL、DEM 预载分片——**任何"必须按真实时间流逝"的场景** |
| `System.GetFPS()` | 帧/秒 | 服务器真实帧率 | 预算 governor 的分母（`frameMs = 1000/GetFPS()`），**不是**扫描间隔 |

**硬规则**：
1. `GetWorldTime()` 返回 **ms**，转秒必须 `* 0.001`；变量名带 `*S` 的必须是秒，带 `*Ms` 的必须是毫秒，**不允许无单位后缀的裸时间字段**（2026-08-15 修复了 `m_LastScanTime` → `m_LastScanTimeMs`）。
2. `CallLater(func, delay, repeat)` 的 delay 是 **int 毫秒**。秒值必须 `(int)(seconds * 1000.0)`；常量直接声明 `const int XXX_MS = N`，**不要用 float 常量**（2026-08-15 修复 `TICK_MS` / `HUD_AFTERGLOW_TICK_MS`）。
3. **超时/门控/TTL 一律墙钟**：`GetWorldTime` 在暂停/时间缩放/进图前会冻结，用它的超时永远不触发（2026-08-15 修复 `RDF_LidarNetworkScanner` poller 死等）。
4. **扫描节拍门控墙钟，扫描时间戳世界钟**：`RDF_RadarSensor.Tick` / `RDF_LidarSensor.Tick` 用 `GetTickCount` 门控 `m_UpdateInterval`（GM 编辑器暂停也能推进扫描序号）；扫描产生的样本/航迹时间戳用 `GetWorldTime()*0.001`（逻辑世界时间）。产品路径与 demo 路径必须一致（2026-08-15 修复 `RDF_LidarAutoRunner` 双时钟分歧）。
5. **预算分母 = 帧时间**：`frameMs = 1000/GetFPS()`，不是扫描间隔（`m_UpdateInterval` 是 200ms 的 CallLater 节拍，用它是错的——2026-08-15 governor 修复）。
6. **相对时间必须从起点减起**：`elapsed = worldTimeS - startTimeS`，绝不能用绝对世界时间当相对时间（2026-08-15 修复 RGPO 拖距——`range + rate × worldTimeS` 会让假点飞出场被门滤掉）。

### 2. 字段命名约定

| 后缀 | 单位/语义 |
|---|---|
| `*Ms` | 毫秒 |
| `*S` / `*Sec` | 秒 |
| `*WallS` | 墙钟秒（`GetTickCount()*0.001`） |
| `*TimeS` | 世界时间秒（`GetWorldTime()*0.001`） |
| `*DurationMs` | 耗时毫秒（墙钟差值） |

无后缀时间字段 = **缺陷**，评审直接打回。

### 3. 基本类型（int / float / string）规则

**int/float**：
1. **int 运算用 int API**：`Math.MaxInt` / `Math.MinInt` / `Math.ClampInt`。`Math.Max/Min/Clamp` 只有 float 重载，int 参数会隐式 float 往返——整数值无害，但热路径（每射线/每样本/每扫描循环）里不要（2026-08-15 批量修复 22 处）。
2. **int/int 除法**：Enforce 整数除法**截断**。要比例必须显式 `(float)`：`a / (float)b`。`float x = a / b` 得到 0.0 是 bug（2026-08-15 修复 `MtdBinPowerGain` bin 中心恒 0，MTD 全失效）。
3. **哈希/累加器保持 int 域**：`h = h * 31 + (int)Math.Round(v)`，不要 `h * 31 + Math.Round(v)`（float 提升后 h 超 2²⁴ 丢精度 → 哈希碰撞，2026-08-15 修复指纹）。
4. **浮点相等用 epsilon**：`==` / `!=` 对 float 不可靠。比较 `Math.AbsFloat(a - b) > eps`（距离比较用 `LengthSq() < eps`）。已修复：`DwellScheduler.TaskLess`、`DemRuntimeLoader.CellM`、`LockAutoTest.IsTargetAlive`。
5. **Floor/Ceil 语义**：`Math.Floor` 返回 float。索引计算在整数边界附近加 epsilon 再 floor：`Math.Floor(x * 0.95 + 0.0001)`（P95 取样的 ULP 陷阱，2026-08-15 修复 3 个 AutoTest）。
6. **无用 round-trip 去掉**：`int x = Math.Round(a * b)`（a、b 都是 int）→ `int x = a * b`。

**string**：
1. **热路径禁止字符串拼接/ToString**：每扫描网格键（`GridKey`）、每样本 CSV 行、每字符 Substring——已知可优化项，见 §4。
2. **map 键优先 int**：网格/瓦片/缓存键用打包整数（`key = iz * countX + ix`），避免 `ToString() + ":" + ToString()` 每查一次分配。
3. **大小写规范化**：类名/材质名等用于匹配的字符串统一 `ToLower()` 一次再扫描（`RDF_RadarEntityClassifier` 已做），避免大小写差异导致误判。
4. **Prepend 重复获取**：同一实体的 prefab 类名只取一次（`GetPrefabData → GetPrefab → GetClassName` 不便宜），复用局部变量。
5. 非热路径（报告、Print、一次性状态行）的字符串拼接**可接受**，不追求极端优化。

### 4. 已知可优化项（未改，低风险低收益或改动面大）

| 位置 | 问题 | 建议 |
|---|---|---|
| ~~`RDF_RadarScattererRegistry.GridKey` / `s_CellBuckets`~~ | ~~每扫描每格 `ToString()+":"+ToString()` 建 string 键（~289 个/扫描/雷达，5Hz）~~ | **✅ 已修复（2026-08-15）**：`map<string,...>` → `map<int,...>`，打包键 `((ix+32768)<<16) \| (iz+32768)`，无每格字符串分配 |
| ~~`RDF_RadarProjectileTracker.UpdateWithOrigin`~~ | ~~每次扫描 new 6 个并行数组（plots/plotUsed/trackAssigned/pairCosts/pairTrackIdx/pairPlotIdx）~~ | **✅ 已修复（2026-08-15）**：成员 `m_Scratch*` 复用缓冲，Clear 后复用 |
| ~~`RDF_RadarNetCodec.PackScanRpc/PackScanPlotsRpc/WriteScanBits`~~ | ~~每次广播 new 12 个临时数组~~ | **✅ 已修复（2026-08-15）**：静态 `s_Scratch*` 复用缓冲（单线程顺序调用安全） |
| ~~`RDF_RadarScanner.ScanFromRegistry`~~ | ~~每候选 new `RDF_RadarTruthSample`~~ | **✅ 已修复（2026-08-15）**：`m_TruthScratch` 复用 + 关键字段显式重置（防残留）；`RDF_RadarTarget`/`LidarSample` 逃逸对象不池化 |
| `RDF_LidarExport.SamplesToCSV` | 每样本 ~13 次 `(Math.Round(v*mul)/mul).ToString()` + `csv += part` O(n²) | 单次 `string.Format` 整行 + 预分配缓冲拼接 |
| `RDF_LidarExport.RLECompress/Decompress` | 每字符 `Substring` + `+=`（仅 >3KB 载荷触发） | 按 run 整段 Substring，或字节缓冲 |
| `RDF_DemTileBake` 行构建 | 每格 ~28 次 ToString（烘焙专用，非运行时） | 单次 Format；可选 |
| `RDF_RadarAutoRunner.RadarTick` | 每扫描重建 HUD modeText 字符串 | 缓存，仅状态变化时重建 |
| `RDF_RadarEwModel.CollectFalsePlots` dst | 每扫描每假点 new `RDF_RadarFalsePlot`（RGPO） | 假点数量小、跨 effect 聚合有覆盖风险，**不池化**；如需可改为调用方传入输出缓冲 |

### 5. 变更清单（2026-08-15 已落地）

- 时间审计 7 项：RGPO 拖距相对时间、LiDAR 网络超时墙钟、HEIGHT 阶段计时、2×CallLater 常量 int、Lidar demo 门控时钟统一、`m_LastScanTimeMs` 命名
- 类型审计：MTD bin 中心 int/int 除法（高危）、指纹哈希 int 域（中危）、22 处 `Math.*Int` 热路径、3 处 P95 epsilon、DwellScheduler/CellM/IsTargetAlive epsilon、DemTileBake round-trip、EntityClassifier 单次取类名+小写
- 性能：跨帧 LOS 队列、WLR 解算预算、自适应预算 governor（帧时间分母）

---

## English

### 1. Clock selection matrix (most important)

Three time sources with **different semantics — never mix them**:

| Source | Returns | Purpose | When |
|---|---|---|---|
| `BaseWorld.GetWorldTime()` | **milliseconds** (int) | world/sim time | scan timestamps, track/lock/datalink **logical time** (freezes on pause, scales with time scale) |
| `System.GetTickCount()` | **milliseconds** (int) | wall clock | timeouts, cadence gating, perf timing, RWR/false-plot TTL, DEM preload slices — anything that **must elapse in real time** |
| `System.GetFPS()` | frames/sec | server real frame rate | governor denominator (`frameMs = 1000/GetFPS()`), **not** the scan interval |

**Hard rules**:
1. `GetWorldTime()` returns **ms**; convert with `* 0.001`. Variables named `*S` must be seconds, `*Ms` milliseconds; **no unit-less bare time fields** (fixed 2026-08-15: `m_LastScanTime` → `m_LastScanTimeMs`).
2. `CallLater(func, delay, repeat)` delay is **int milliseconds**. Seconds → `(int)(seconds * 1000.0)`; declare constants `const int XXX_MS = N`, never float (fixed `TICK_MS` / `HUD_AFTERGLOW_TICK_MS`).
3. **Timeouts / gates / TTLs always wall clock**: `GetWorldTime` freezes on pause / time-scale / pre-mission; timeouts built on it never fire (fixed `RDF_LidarNetworkScanner` poller).
4. **Scan cadence gates on wall clock; scan timestamps on world clock**: `*_Sensor.Tick` gates `m_UpdateInterval` with `GetTickCount` (scan serial advances even in paused GM editor); sample/track timestamps use `GetWorldTime()*0.001`. Product and demo paths must agree (fixed `RDF_LidarAutoRunner`).
5. **Budget denominator = frame time**: `frameMs = 1000/GetFPS()`, not the 200 ms CallLater interval (governor fix).
6. **Relative time must subtract its start**: `elapsed = worldTimeS - startTimeS`; never use absolute world time as elapsed (fixed RGPO range-walk — `range + rate × worldTimeS` flies plots out of the gate).

### 2. Field naming

| Suffix | Meaning |
|---|---|
| `*Ms` | milliseconds |
| `*S` / `*Sec` | seconds |
| `*WallS` | wall-clock seconds (`GetTickCount()*0.001`) |
| `*TimeS` | world-time seconds (`GetWorldTime()*0.001`) |
| `*DurationMs` | elapsed ms (wall-clock delta) |

A bare time field without a suffix = defect; reject in review.

### 3. Primitive types (int / float / string)

**int/float**:
1. int ops use int APIs: `Math.MaxInt` / `Math.MinInt` / `Math.ClampInt`. `Math.Max/Min/Clamp` are float-only; int args round-trip through float — harmless for integral values but avoid in hot paths (22 sites fixed).
2. int/int division **truncates**; ratios must cast: `a / (float)b`. `float x = a / b` yielding 0.0 is a bug (fixed `MtdBinPowerGain`, MTD was fully broken).
3. Hashes/accumulators stay in int: `h = h * 31 + (int)Math.Round(v)`; float promotion loses precision past 2^24 → collisions (fixed fingerprint).
4. Float equality needs epsilon: `Math.AbsFloat(a - b) > eps`; for positions `LengthSq() < eps` (fixed DwellScheduler, CellM, IsTargetAlive).
5. Floor near integer boundaries: add epsilon `Math.Floor(x * 0.95 + 0.0001)` (P95 ULP trap, fixed 3 AutoTests).
6. Drop no-op round-trips: `int x = Math.Round(a * b)` → `int x = a * b`.

**string**:
1. No string building / ToString in hot paths (grid keys, per-sample CSV, per-char Substring — see §4).
2. Prefer int map keys (`key = iz * countX + ix`) over `ToString()+":"+ToString()`.
3. Normalize case once (`ToLower()`) before token matching (done in `RDF_RadarEntityClassifier`).
4. Fetch prefab class name once per entity and reuse.
5. Non-hot strings (reports, Print, one-shot status) may concatenate freely.

### 4. Known optimizable (not changed: low value/risk or large blast radius)

| Location | Issue | Suggestion |
|---|---|---|
| ~~`RDF_RadarScattererRegistry.GridKey` / `s_CellBuckets`~~ | ~~per-scan per-cell `ToString()+":"+ToString()` string key (~289/scan/radar @ 5 Hz)~~ | **✅ Fixed (2026-08-15)**: `map<string,...>` → `map<int,...>`, packed key `((ix+32768)<<16) \| (iz+32768)`, zero per-cell string allocation |
| ~~`RDF_RadarProjectileTracker.UpdateWithOrigin`~~ | ~~6 parallel scratch arrays allocated per scan~~ | **✅ Fixed (2026-08-15)**: member `m_Scratch*` buffers, clear-and-reuse |
| ~~`RDF_RadarNetCodec.PackScanRpc/PackScanPlotsRpc/WriteScanBits`~~ | ~~12 temp arrays per broadcast~~ | **✅ Fixed (2026-08-15)**: static `s_Scratch*` buffers (safe: single-threaded sequential pack calls) |
| ~~`RDF_RadarScanner.ScanFromRegistry`~~ | ~~`new RDF_RadarTruthSample` per candidate~~ | **✅ Fixed (2026-08-15)**: `m_TruthScratch` reuse with explicit reset of conditionally-set fields; escaping `RDF_RadarTarget`/`LidarSample` stay un-pooled |
| `RDF_LidarExport.SamplesToCSV` | ~13 `ToString()` per sample + `csv += part` O(n²) | single `string.Format` row + preallocated buffer |
| `RDF_LidarExport.RLECompress/Decompress` | per-char `Substring` + `+=` (>3 KB payload only) | substring whole runs / byte buffer |
| `RDF_DemTileBake` row build | ~28 `ToString()` per cell (bake-only) | single Format; optional |
| `RDF_RadarAutoRunner.RadarTick` | rebuilds HUD modeText every scan | cache, rebuild on change |
| `RDF_RadarEwModel.CollectFalsePlots` dst | per-scan per-plot `new RDF_RadarFalsePlot` (RGPO) | plot count small + cross-effect aggregate overwrite risk — **not pooled**; caller-supplied out buffer if needed |

### 5. Changelog (2026-08-15)

- Time audit 7 items: RGPO relative time, LiDAR net timeout wall clock, HEIGHT phase timing, 2× CallLater const int, Lidar demo clock unification, `m_LastScanTimeMs` rename
- Type audit: MTD bin-centre int/int division (high), fingerprint hash int domain (medium), 22× `Math.*Int` hot paths, 3× P95 epsilon, DwellScheduler/CellM/IsTargetAlive epsilon, DemTileBake round-trip, EntityClassifier single class-name fetch + lowercase
- Perf: cross-frame LOS queue, WLR solve budget, adaptive budget governor (frame-time denominator)

*维护：随审计与修复持续更新。* / *Maintained as audits and fixes land.*
