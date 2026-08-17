> **Languages / 语言**: [中文](#中文) · [English](#english)

# RDF 游戏内性能评估与预测 / In-Game Performance Evaluation & Prediction

> 评估范围：`scripts/Game/RDF/**`（Radar / LiDAR / DEM / 网络 / UI）在 Arma Reforger 游戏循环中的 CPU、内存、网络与 GC 表现。
> 方法：代码热路径分析 + 项目自带 AutoTest 的 PASS 阈值（Perf / Stress / DEM-LOS Bench）+ Enforce Script 引擎成本常识。
> 结论性质：**预测**（predictive）。Workbench 未运行，未做本次会话内的实机 profile；下列数字为有依据的估算区间，不是实测。

---

## 中文

### 0. 结论速览 (TL;DR)

| 场景 | 扫描节拍 | 单次扫描预计耗时（中端机） | 帧影响 | 风险 |
|---|---|---|---|---|
| 轻载 SHORAD（2km、车辆 5–10、DEM 开） | 4–5 Hz | 1–5 ms | 无感 | 低 |
| 中载（Airborne 式：直升机+车辆、64 目标、48 条 LOS） | 4–5 Hz | 5–20 ms，偶发 30–50 ms（LOS 冷缓存） | 偶发轻微卡顿 | 中 |
| 重载（Stress 式：24×UAZ + 3×Mi-8、2.5km、90° 扇区、128 目标、DEM+CFAR+HUD） | 4–5 Hz | **实测 avg 4.4 ms，p95 5 ms，max 8 ms（优化后）** | 无卡顿（hitch=0） | 低 |
| WLR 弹道解算（每发确认航迹） | 随扫描 | 每发 +2–5 ms | 齐射时叠加 | 中 |
| LiDAR 256 射线 | 5 Hz | 2–6 ms | 无感 | 低 |
| LiDAR 4096 射线 | 5 Hz | 30–80 ms | 明显卡顿 | 高 |
| 多人客户端（纯 HUD/同步消费） | 5 Hz | HUD 重绘 <1 ms；网络解包有界 | 无感 | 低 |

**核心判断**：框架整体是**预算驱动**（budget-driven）设计——LOS 条数预算、fresh 更新预算、发现/分类/刷新摊销、LRU、网络上限、绘制上限都齐备；默认配置下单雷达对游戏帧率的威胁**低**。真正的风险点集中在：① 密集场景 LOS TraceMove 预算吃满；② DEM 全量预载内存（GM_Eden 约 334 MB）；③ 大规模齐射时 WLR 解算叠加；④ LiDAR 高射线数；⑤ 多雷达并存时各雷达 LOS 预算线性叠加。

> **实测更新（2026-08-15）**：Stress 重载场景在跨帧 LOS 队列 + 自适应 governor + 分配复用全部落地后，tickAvg 4.4 ms / p95 5 ms / max 8 ms / hitch=0，详见 §8。

---

### 1. 项目性能相关架构（代码事实）

- **雷达**：实体优先。全局静态 `RDF_RadarScattererRegistry`（空间网格 256m）维护散射体/辐射源表；`RDF_RadarScanner` 每次扫描从表里取候选（`CollectInSphere`），做扇区/距离门 → LOS（复用缓存 / DEM 预检 / `TraceMove`）→ `RDF_RadarPhysicalDetect`（雷达方程、波束、多普勒/MTI/MTD、DEM 杂波、NLOS/刃绕）→ 测量合成 → 跟踪器 → 锁定 →（可选）数据链/融合。网络走类型化 `array<int>/array<float>` RPC（Reliable 摘要 + Unreliable 点迹）。
- **LiDAR**：逐射线 `TraceMove`，`m_RayCount` 条射线；`RDF_LidarVisualizer` 分静态/动态/余晖三桶绘制；PPI HUD 磷光余晖；网络为 CSV 分片（上限 16 buffer、轮询器上限 8）。
- **DEM**：烘焙 V3 CSV / 打包 SURF+HEIGHT JSON；运行时 `RDF_DemRuntimeCache`：全量预载进 RAM（`RUNTIME_DEM_PRELOAD_ALL=true`，异步分帧 6ms/帧预算），或 LRU（16 tile，2 tile/扫描加载预算），或 `GetSurfaceY` 回退。
- **调度**：`RDF_RadarAutoRunner` 以 `CallLater(200ms)` 驱动 `Sensor.Tick`；`Sensor.Tick` 用墙钟按 `m_UpdateInterval`（SEARCH 0.25s / STARE 0.2s / WLR 0.15s）门控。扫描**同步**跑在主线程。`ScattererRegistry` 每帧只 `RegisterFocus` + 一次 `CallLater(0)` flush。

### 2. 每次扫描的耗时构成（估算模型）

单次扫描墙钟 ≈ 注册表摊销 + 候选门 + Σ(候选)［LOS 或复用 + 物理检测或复用］+ 欺骗假点 + CFAR + 测量合成 + 跟踪器 + WLR。

| 环节 | 每项成本 | 数量上限 | 说明 |
|---|---|---|---|
| 注册表 flush（每帧一次） | ~0.1–0.5 ms | 1/帧 | 发现扫掠 3s 一次（球查询 DYNAMIC）；分类 ≤24/帧（仅非 Vehicle/Projectile 走 prefab 字符串）；运动学刷新 ≤96/帧；DEM 重采样 ≤2s 或 25m |
| 候选收集+门控 | ~0.2–1 ms | maxTargets 64–128 | 网格收集 + 距离/方位门，纯向量数学 |
| LOS（`TraceMove`） | **0.05–1 ms/条**（物理查询） | `m_MaxLosTracesPerScan`=48（clamp 4–512） | 复用缓存（LOS 0.25s / 目标 0.6s / 物理 0.2s）优先；DEM 预检 8 采样可免 Trace；否则 1–2 次 TraceMove |
| 物理检测（fresh） | ~0.02–0.1 ms/目标 | freshBudget（默认 24–48/扫描） | 纯脚本浮点数学（雷达方程/波束/MTD 16 槽/杂波/刃绕 LUT） |
| 跟踪器 | ~0.05–0.5 ms | O(航迹×点迹) | GNN 最近邻 + 小 N 选择排序；JPDA 默认关 |
| WLR 解算 | **~2.5–5 ms/次** | 每确认弹道航迹 ≤1 次/扫描 | Perf 基准：40 次解算 ≈ 100–200 ms（Workbench 宿主） |
| 欺骗假点 / CFAR | ~0.05–0.3 ms | maxTargets | 有界 |

**关键结论**：单次扫描的“大头”只有两个——**LOS TraceMove 条数**与**WLR 解算次数**，其余都是有界的轻量脚本。预算旋钮（`m_MaxLosTracesPerScan`、`m_FreshUpdateBudget*`、`m_WeaponLocateMinHits/MinSpanS/FitWindow`）直接决定最坏情况。

### 3. 内建基准（项目自带 AutoTest PASS 阈值 = 软上限，非帧率目标）

- **Perf**（同步 ScanOnce，Workbench 开发机）：ballistics ≤250ms（40 解算，实测常 100–200ms）；light avg ≤120ms；heavy avg ≤350ms；heavy P95 ≤600ms。
- **Stress**（24 UAZ + 3 Mi-8 + DEM 杂波 + HUD + showcase，200ms 节拍）：tick avg ≤25ms、P95 ≤80ms、max ≤250ms；scan avg ≤22ms、P95 ≤70ms；统计 hitch ≥16/33ms。
- **DEM-LOS Bench**：比较 DemLos ON/OFF × flush/cache，度量 `demBlk`/`trace`/`losHit`/`reuse`；设计上验证“缓存开启时重复驻留墙钟收益最大”。

这些阈值是**回归护栏**（防病态退化），不是真实典型负载；典型负载应远低于阈值。Stress 的数字最接近“可玩但偏重”的上界。

### 4. 内存评估

| 项 | 估算 | 说明 |
|---|---|---|
| DEM 全量预载（GM_Eden，cell_m=2） | **约 334 MB**（surface `array<int>` 41.8M 格 ≈167MB + height `array<float>` ≈167MB） | 网格 6464×6464；异步 6ms/帧解码避免开局卡死，但常驻内存固定。8GB 机器 + 其他模组需注意；coarser cell（4m）可降到 ~84MB |
| 散射体注册表 | ≤512 条目 × ~200B ≈ 0.1 MB | `m_ScattererMaxEntries` |
| 航迹/点迹/锁定 | 有界（maxTargets、track 上限） | 扫描间缓存（LOS/reuse/物理复用）有 TTL 与数量上限 |
| LiDAR | 样本数 = 射线数；`MAX_DRAW_RAYS=50000`、`m_MaxRayCount=4096` 软封顶 | 高射线数下样本对象是主要 GC 源 |
| HUD | blip 上限 256（雷达）/ 512 降采样（LiDAR） | `m_AllCmds` 已复用（Clear 而非重分配） |

### 5. 网络带宽评估（每台雷达，5 Hz 扫描）

| 通道 | 频率 | 载荷上限 | 最坏速率 |
|---|---|---|---|
| Reliable 摘要（航迹+锁+meta） | ≥0.15s 间隔，指纹未变即跳过 | 32 航迹 ≈ 2KB | ~13 KB/s |
| Unreliable 点迹（HUD） | ≥0.05s 间隔 | 24 点迹 ≈ 1.7KB | ~34 KB/s |
| 数据链 Hub 广播 | ≥0.25s 间隔 | 48 航迹 + 32 融合 | ~15 KB/s |
| 合计 | — | — | **≤ ~50–60 KB/s / 雷达（~0.5 Mbps 量级）** |

单台/少数几台雷达：无压力。兴趣半径 12km + 指纹跳过 + 上限是主要护栏；10 台以上全速广播时才会形成可观占用。客户端成本 = HUD 重绘（0.15s 间隔、≤256 blip）+ 有界解包，很轻。

### 6. 风险清单（按优先级）

1. **【中高】密集场景 LOS TraceMove 预算吃满**：48 条 × 物理查询在 24 UAZ+3 Mi-8 级场景可贡献 scan P95 50–70ms。护栏：`m_MaxLosTracesPerScan`、DEM 预检（默认开）、LOS/reuse 缓存（默认开）。
2. **【中高】DEM 全量预载内存 ~334MB（Eden）**：功能安全（异步 6ms/帧），但常驻内存对低配机是负担；建议文档化并给出 `m_DemCacheMaxTiles`/预载开关或 4m 网格选项。
3. **【中】WLR 解算在大规模齐射时叠加**：每发 2.5–5ms/扫描；10 发齐射 ≈ 25–50ms 额外扫描耗时。护栏：`m_WeaponLocateMinHits=5`、`m_WeaponLocateMinSpanS=1.0`、`m_WeaponLocateFitWindow=20`；如需可加“每扫描解算预算”。
4. **【中】多雷达并存**：注册表全局合并（好），但各雷达 LOS 预算线性叠加（10 雷达 × 48 条 = 480 TraceMove/窗口）。建议全局 Trace 预算核算或共享预算。
5. **【高·条件触发】LiDAR 高射线数**：4096 射线 ≈ 30–80ms/扫描；烟雾遮挡双 Trace 加倍。护栏：`m_MaxRayCount=4096` 软封顶 + 文档建议 ≤1024 玩法 / 256–512 showcase。
6. **【低】PromoReel 等 Demo/AutoTest 脚本**（共 ~13k 行）不进产品帧路径；`RDF_RadarPromoReel` 33ms tick 仅用于录制，不随模组发布。

### 7. 建议（落地优先级）

1. **✅ 已实现 — LOS 预算跨帧队列**：`RDF_RadarSettings.m_EnableLosFrameQueue`（默认开）+ `m_LosTracesPerTick`（默认 16）+ `m_LosQueueMax`（默认 128）。`RDF_RadarScanner` 每 Tick 至多 `m_LosTracesPerTick` 条 TraceMove，超出入队（`m_LosQueue`），下一 Tick 先排空（`DrainLosQueue`）；队列排空与正常扫掠共享 `m_LosBudget`，单 Tick 总 TraceMove 有界；同 Tick 内 `PublishTruthToPlots` 按 scatterer id 去重，避免同实体重复出图。
2. DEM 内存：对低配机提供 `cell_m=4` 或“仅 SURF 不预载 HEIGHT（回退 GetSurfaceY）”的打包选项；README 写明 Eden 预载 ~334MB。
3. **✅ 已实现 — WLR 解算预算+队列**：`RDF_RadarSettings.m_WeaponLocateSolvesPerScan`（默认 2）+ `m_WeaponLocateQueueMax`（默认 16）。`RDF_RadarProjectileTracker.RefreshWeaponLocates` 每扫描至多 N 次解算，超出航迹先进先出排队、后续扫描排空；0 = 恢复旧行为（每扫描解算全部合格航迹）。
4. **✅ 已实现（自动，默认开）— 多雷达/负载自适应**：`RDF_RadarBudgetGovernor`
   （`m_EnableAdaptiveBudget`，默认 true）按实测扫描耗时与 Tick 节拍自动伸缩
   LOS/WLR 预算，服务器繁忙自动降载、空闲自动提档——比手动调预算更省心；
   固定预算用 min==max 或关闭；多雷达全局 LOS 预算核算仍未做（当前按雷达独立预算）。
5. LiDAR：文档写明射线数建议（≤1024 玩法）；考虑样本对象池（射线数极高时）。
6. 交付前验证：跑 `RDF_RadarPerfAutoTest` + `RDF_RadarStressAutoTest` + `RDF_RadarDemLosBenchAutoTest`，用生成的 `$profile:RDF/RadarTests/*.txt` 作为回归基线；多人环境验证 Reliable/Unreliable 节流与兴趣半径。

### 8. 实测验证（2026-08-15，全部优化叠加后）

**Stress AutoTest**（24×UAZ + 3×Mi-8、DEM 杂波、HUD ON、60s soak、200ms 节拍）——Workbench 实测：

| 指标 | 优化前（首轮基线） | 全部优化后 | 改善 |
|---|---|---|---|
| tickAvg | 9.4 ms | **4.4 ms** | **53% ↓** |
| p95 | 15 ms | **5 ms** | **67% ↓** |
| max | 23 ms（governor 棘轮版 84 ms） | **8 ms** | **65% ↓**（vs 84 ms 为 90% ↓） |
| hitch ≥16 / ≥33 ms | 10 / 0 | **0 / 0** | **归零** |
| scanMs 稳态 | 6–8 ms | **4–5 ms** | ~35% ↓ |
| 原 46 ms 外部负载尖峰（t≈45s） | 17–46 ms | **4 ms** | **尖峰消除** |

- 检测完整性不变：`maxScat=40 / hit=40`，签名全走烘焙表（`sig=759 baked=759 measured=0`）
- 网格查询行为不变：`cells=14 / gridQ=274 / gridHit=10506`（GridKey 整数键改造后统计一致）
- 报告：`$profile:RDF/RadarTests/radar_stress_autotest_5531806.txt`（scans=296 tickAvg=4.4ms p95=5ms max=8ms）

**换算到 60 tick 服务器**：单次扫描 4–5 ms ≈ 帧预算（16.7 ms）的 25–30%——仍是单项最重的脚本工作，但不再周期性吃掉半帧；P95/max 与首轮相比改善 2/3 以上，40 人混战场景下雷达不再引起可感知掉帧。

**生效机制**（三层叠加，缺一不可）：
1. 跨帧 LOS 队列（`m_EnableLosFrameQueue`）——把每 Tick TraceMove 从 48 条同步烧完摊平到 16 条/ Tick + 队列；
2. 自适应预算 governor（`m_EnableAdaptiveBudget`，默认开）——空闲提档/过载降档，配合队列把尖峰钳进预算；
3. GridKey 整数键 + Tracker/NetCodec/TruthSample 复用——消除每扫描字符串/数组/DTO 分配，降 GC 抖动。

**归因说明**：Tick 4.4 ms 已含 HUD 与可视化；客户端（纯网络消费）不受影响；TraceMove 物理查询本身未变快，改善来自摊平与分配消除。

---

## English

### 0. TL;DR

| Scene | Scan cadence | Est. scan wall time (mid-range PC) | Frame impact | Risk |
|---|---|---|---|---|
| Light SHORAD (2 km, 5–10 vehicles, DEM on) | 4–5 Hz | 1–5 ms | Negligible | Low |
| Medium (helo + vehicles, 64 targets, 48 LOS) | 4–5 Hz | 5–20 ms, occasional 30–50 ms (cold LOS cache) | Occasional micro-stutter | Medium |
| Heavy (Stress-like: 24 UAZ + 3 Mi-8, 2.5 km, 90°, 128 targets, DEM+CFAR+HUD) | 4–5 Hz | **measured avg 4.4 ms, p95 5 ms, max 8 ms (after optimization)** | No hitch (hitch=0) | Low |
| WLR solve per confirmed shell track | per scan | +2–5 ms each | Adds up during barrages | Medium |
| LiDAR 256 rays | 5 Hz | 2–6 ms | Negligible | Low |
| LiDAR 4096 rays | 5 Hz | 30–80 ms | Noticeable hitch | High |
| MP client (HUD / sync consumer only) | 5 Hz | HUD redraw <1 ms; bounded unpack | Negligible | Low |

**Bottom line**: the framework is budget-driven by design (LOS-trace budget, fresh-update budget, amortized discovery/classify/refresh, LRU, network caps, draw caps). A single radar at default settings is a **low** threat to frame rate. Real risks: ① LOS `TraceMove` budget saturation in dense scenes; ② DEM full preload RAM (~334 MB on GM_Eden); ③ WLR solves stacking under mass fire; ④ high LiDAR ray counts; ⑤ per-radar LOS budgets adding linearly when many radars coexist.

### 1. Performance-relevant architecture (code facts)

- **Radar**: entity-first. Global static `RDF_RadarScattererRegistry` (256 m spatial grid) holds scatterers/emitters; `RDF_RadarScanner` collects candidates per scan (`CollectInSphere`), gates by sector/range → LOS (reuse cache / DEM precheck / `TraceMove`) → `RDF_RadarPhysicalDetect` (radar equation, beams, Doppler/MTI/MTD, DEM clutter, NLOS/knife-edge) → measurement synthesis → tracker → lock → (optional) datalink/fusion. Network uses typed `array<int>/array<float>` RPCs (Reliable summary + Unreliable plots).
- **LiDAR**: per-ray `TraceMove`; `RDF_LidarVisualizer` splits static/dynamic/afterglow shape buckets; PPI HUD phosphor afterglow; network via CSV shards (16-buffer cap, 8 poller cap).
- **DEM**: baked V3 CSV / packaged SURF+HEIGHT JSON; runtime `RDF_DemRuntimeCache`: full RAM preload (`RUNTIME_DEM_PRELOAD_ALL=true`, async 6 ms/frame decode budget), or LRU (16 tiles, 2 tile loads/scan), or `GetSurfaceY` fallback.
- **Scheduling**: `RDF_RadarAutoRunner` drives `Sensor.Tick` via `CallLater(200 ms)`; `Sensor.Tick` gates on wall clock per `m_UpdateInterval` (SEARCH 0.25 / STARE 0.2 / WLR 0.15 s). Scans run **synchronously** on the main thread. `ScattererRegistry` does one `RegisterFocus` + one deferred `CallLater(0)` flush per frame.

### 2. Per-scan cost model (estimate)

Scan wall time ≈ registry amortization + candidate gating + Σ(candidate)［LOS-or-reuse + physical-or-reuse］+ deception plots + CFAR + measurement synthesis + tracker + WLR.

| Stage | Per-item cost | Cap | Notes |
|---|---|---|---|
| Registry flush (per frame) | ~0.1–0.5 ms | 1/frame | Discovery sweep every 3 s (sphere query DYNAMIC); classify ≤24/frame (prefab-string only for non-Vehicle/Projectile); kinematics refresh ≤96/frame; DEM resample ≤2 s or 25 m |
| Candidate collect + gate | ~0.2–1 ms | maxTargets 64–128 | Grid collect + range/azimuth gate, pure vector math |
| LOS (`TraceMove`) | **0.05–1 ms each** (physics query) | `m_MaxLosTracesPerScan`=48 (clamp 4–512) | Reuse caches (LOS 0.25 s / target 0.6 s / physics 0.2 s) first; DEM precheck (8 samples) can skip Trace; else 1–2 TraceMoves |
| Physical detect (fresh) | ~0.02–0.1 ms/target | freshBudget (~24–48/scan) | Pure script float math (radar equation/beams/MTD 16 bins/clutter/knife-edge LUT) |
| Tracker | ~0.05–0.5 ms | O(tracks×plots) | GNN nearest-neighbor + small-N selection sort; JPDA off by default |
| WLR solve | **~2.5–5 ms each** | ≤1 per confirmed projectile track per scan | Perf bench: 40 solves ≈ 100–200 ms (Workbench host) |
| Deception plots / CFAR | ~0.05–0.3 ms | maxTargets | Bounded |

**Key insight**: only two things dominate a scan — **LOS TraceMove count** and **WLR solve count**. The knobs (`m_MaxLosTracesPerScan`, `m_FreshUpdateBudget*`, `m_WeaponLocateMinHits/MinSpanS/FitWindow`) directly bound worst case.

### 3. Built-in baselines (project AutoTest PASS gates = soft ceilings, not FPS targets)

- **Perf** (synchronous ScanOnce, Workbench dev): ballistics ≤250 ms (40 solves, typically 100–200 ms); light avg ≤120 ms; heavy avg ≤350 ms; heavy P95 ≤600 ms.
- **Stress** (24 UAZ + 3 Mi-8 + DEM clutter + HUD + showcase, 200 ms cadence): tick avg ≤25 ms, P95 ≤80 ms, max ≤250 ms; scan avg ≤22 ms, P95 ≤70 ms; counts hitches ≥16/33 ms.
- **DEM-LOS Bench**: compares DemLos ON/OFF × flush/cache; verifies that with caches on, repeat dwell wall time improves most.

These gates are regression guardrails, not typical loads; typical runs should be far below them. Stress numbers are the closest proxy for a “playable but heavy” upper bound.

### 4. Memory

| Item | Estimate | Notes |
|---|---|---|
| DEM full preload (GM_Eden, cell_m=2) | **~334 MB** (surface `array<int>` 41.8 M cells ≈167 MB + height `array<float>` ≈167 MB) | Grid 6464×6464; async 6 ms/frame decode avoids startup hitch but resident RAM is fixed. 8 GB rigs + other mods: watch this; 4 m cells → ~84 MB |
| Scatterer registry | ≤512 × ~200 B ≈ 0.1 MB | `m_ScattererMaxEntries` |
| Tracks/plots/lock | Bounded | Scan-side caches (LOS/reuse/physical) have TTLs and caps |
| LiDAR | Samples = ray count; `MAX_DRAW_RAYS=50000`, `m_MaxRayCount=4096` soft cap | Sample objects are the main GC source at high ray counts |
| HUD | 256 blips (radar) / 512 downsample (LiDAR) | `m_AllCmds` is reused (Clear, not reallocated) |

### 5. Network per radar (5 Hz scans)

| Channel | Rate | Payload cap | Worst-case |
|---|---|---|---|
| Reliable summary (tracks+lock+meta) | ≥0.15 s, skipped on unchanged fingerprint | 32 tracks ≈ 2 KB | ~13 KB/s |
| Unreliable plots (HUD) | ≥0.05 s | 24 plots ≈ 1.7 KB | ~34 KB/s |
| Datalink hub broadcast | ≥0.25 s | 48 tracks + 32 fused | ~15 KB/s |
| Total | — | — | **≤ ~50–60 KB/s per radar (~0.5 Mbps)** |

Fine for one or a few radars. Interest radius 12 km + fingerprint skip + caps are the guards. Client cost = HUD redraw (0.15 s interval, ≤256 blips) + bounded unpack — very light.

### 6. Risk register (priority order)

1. **Med-High — LOS TraceMove budget saturation in dense scenes**: 48 physics queries can drive scan P95 to 50–70 ms at Stress scale. Guards: `m_MaxLosTracesPerScan`, DEM precheck (default on), LOS/reuse caches (default on).
2. **Med-High — DEM full preload ~334 MB (Eden)**: functionally safe (async 6 ms/frame) but resident RAM is a real cost on low-spec rigs; document and offer `m_DemCacheMaxTiles`/preload toggle or 4 m grid.
3. **Medium — WLR solves stack under mass fire**: 2.5–5 ms each; a 10-shell barrage ≈ 25–50 ms extra per scan. Guards exist (min-hits/span/window); a per-scan solve budget would harden it.
4. **Medium — many radars**: registry is globally merged (good), but per-radar LOS budgets add linearly (10 radars × 48 traces = 480 TraceMoves/window). Recommend global/merged trace budgeting.
5. **High-conditional — LiDAR high ray counts**: 4096 rays ≈ 30–80 ms/scan; smoke occlusion doubles traces. Guards: `m_MaxRayCount=4096` soft cap + documented ≤1024 gameplay / 256–512 showcase.
6. **Low — Demo/AutoTest scripts** (~13k lines incl. `RDF_RadarPromoReel` 33 ms tick) are dev/recording only, not in the shipped frame path.

### 7. Recommendations (landing order)

1. **✅ Implemented — cross-frame LOS queue**: `RDF_RadarSettings.m_EnableLosFrameQueue` (default on) + `m_LosTracesPerTick` (default 16) + `m_LosQueueMax` (default 128). `RDF_RadarScanner` spends at most `m_LosTracesPerTick` TraceMove calls per Tick; overflow is queued (`m_LosQueue`) and drained first on later scans (`DrainLosQueue`); drain + sweep share `m_LosBudget` within a Tick so total trace cost is bounded; `PublishTruthToPlots` dedups by scatterer id per scan so the same entity is not plotted twice in one tick.
2. DEM memory: ship a `cell_m=4` or “SURF-only, no HEIGHT preload (GetSurfaceY fallback)” option for low-spec rigs; note the ~334 MB Eden preload in README.
3. **✅ Implemented — WLR solve budget + queue**: `RDF_RadarSettings.m_WeaponLocateSolvesPerScan` (default 2) + `m_WeaponLocateQueueMax` (default 16). `RDF_RadarProjectileTracker.RefreshWeaponLocates` runs at most N ballistic solves per scan; overflow tracks are queued FIFO and drained on later scans; 0 restores legacy (solve every eligible track every scan).
4. **✅ Implemented (automatic, default on) — load-adaptive budgets**: `RDF_RadarBudgetGovernor` (`m_EnableAdaptiveBudget`, default true) auto-scales LOS/WLR budgets from measured scan wall time + tick cadence — busy servers shed load, idle servers refresh faster; pin with min==max or disable; global multi-radar LOS accounting still not done (per-radar budgets).
5. LiDAR: document ray-count guidance (≤1024 gameplay); consider a sample object pool for extreme ray counts.
6. Pre-release verification: run `RDF_RadarPerfAutoTest` + `RDF_RadarStressAutoTest` + `RDF_RadarDemLosBenchAutoTest` and treat the generated `$profile:RDF/RadarTests/*.txt` as the regression baseline; validate Reliable/Unreliable throttling + interest radius in MP.

### 8. Measured validation (2026-08-15, all optimizations applied)

**Stress AutoTest** (24 UAZ + 3 Mi-8, DEM clutter, HUD ON, 60 s soak, 200 ms cadence) — Workbench measured:

| Metric | Before (first baseline) | After all optimizations | Improvement |
|---|---|---|---|
| tickAvg | 9.4 ms | **4.4 ms** | **53% ↓** |
| p95 | 15 ms | **5 ms** | **67% ↓** |
| max | 23 ms (84 ms with the governor ratchet bug) | **8 ms** | **65% ↓** (90% vs 84 ms) |
| hitch ≥16 / ≥33 ms | 10 / 0 | **0 / 0** | **zeroed** |
| scanMs steady state | 6–8 ms | **4–5 ms** | ~35% ↓ |
| former external spike at t≈45 s (17–46 ms) | 17–46 ms | **4 ms** | **spike eliminated** |

- Detection intact: `maxScat=40 / hit=40`; all signatures from bake table (`sig=759 baked=759 measured=0`)
- Grid queries unchanged: `cells=14 / gridQ=274 / gridHit=10506` (identical after the int GridKey change)
- Report: `$profile:RDF/RadarTests/radar_stress_autotest_5531806.txt` (scans=296 tickAvg=4.4ms p95=5ms max=8ms)

**On a 60-tick server**: 4–5 ms per scan ≈ 25–30% of the 16.7 ms frame budget — still the single heaviest script item, but no longer eats half a frame periodically; P95/max improve by >⅔, so a 40-player match no longer sees radar-induced frame drops.

**Mechanisms (all three required)**:
1. Cross-frame LOS queue (`m_EnableLosFrameQueue`) — spreads 48 TraceMoves/tick into 16/tick + queue drain;
2. Adaptive budget governor (`m_EnableAdaptiveBudget`, default on) — idle-raise / overload-shed, clamping spikes into budget;
3. GridKey int keys + Tracker/NetCodec/TruthSample reuse — removes per-scan string/array/DTO allocations, cutting GC jitter.

**Attribution**: the 4.4 ms tick includes HUD and visualization; clients (network-only consumers) are unaffected; the TraceMove physics query itself is not faster — the gain is spreading + allocation removal.

---

*评估基于 2026 会话快照的代码树；Stress 重载场景已实测（§8），其余数字为估算区间。*  
*Based on the code tree at this session snapshot; the heavy Stress scene is measured (§8), other figures are estimate ranges.*
