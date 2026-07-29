> **Languages / 语言**: [English](#english) · [中文](#中文)

# TODO — RDF

## 中文

按 **收益 ÷ 成本** 排期；标签只作分类，不决定顺序。  
相关：[RADAR_API.md](docs/RADAR_API.md) · [RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) · [VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) · [CHANGELOG.md](docs/CHANGELOG.md)

---

### 定位

框架已复现功能链：**搜索 → 点迹 → 跟踪 → 锁定/火控 → WLR**。  
物理噪声 / 大气 / 信号处理高度理想化——回归“过准”是在无噪声数学世界验证闭环，不是战场精度。

| 当前强项 | 当前弱项 |
|----------|----------|
| 功能链、Sensor 门面、双档回归 | 精细多径、绕射 |
| 网格、LOS/物理复用、分片预算 + 统计 | 多雷达融合、IFF |
| 测量噪声/偏差、GO/SO-CFAR、Network 权威同步 | 远程算力、golden/CI 卫生 |

入口：[README.md](README.md)

---

### 实施顺序（按收益）

收益：玩法体感 / 可信度 / 可维护性。成本：改动面、回归风险、联调量。  
原则：**先让误差合常理，再加深模型；先可观测，再可调参；远程与组网最后。**

#### 1 — 立刻做（高收益 · 低成本）

| 项 | 收益 | 成本 | 为何现在做 |
|----|------|------|------------|
| CHANGELOG 补扫描优化条目 | 文档与现状对齐 | 极低 | 欠账，几分钟 |
| `GetStatusShort` / stats：`reuseHits` `freshUpdates` `budgetSkips` | 能看见优化是否生效 | 低 | 无观测就无法调预算 |
| 回归双档：理想档仍可“准”；逼真档用误差带验收 | 防止 P2 把套件卡死 | 低 | 物理项的前置护栏 |

- [x] CHANGELOG：LOS 缓存 / 分片预算 / 物理复用 / 优先级扫描
- [x] 复用与预算命中统计写入状态行
- [x] AutoTest：`ideal` vs `realistic`（或 settings 开关）；逼真档断言改为误差带

#### 2 — 下一波（高收益 · 中成本）← 已落地核心

目标：玩法上出现“发现不稳定 / 虚警 / 测距晃动”，而不是永远 0.3 m。

| 项 | 收益 | 成本 | 依赖 |
|----|------|------|------|
| 测量噪声与偏差（距离 / 方位 / 多普勒） | 跟踪与 WLR 立刻“合常理” | 中 | 双档回归 |
| 热噪声填空距离单元 | CFAR 有真实虚警率 | 中 | 无 |
| 大气 / 降雨简化衰减 | 远距探测变难，体感强 | 中 | 测量噪声后更自然 |

- [x] 测量噪声与系统性偏差（合成测量路径）
- [x] 热噪声填充空距离单元 → CA-CFAR 虚警可测
- [x] 大气衰减 / 降雨损耗（简化模型，可关）
- [x] 逼真档套件真正加压：CFAR+热填空保持开启；`MeasNoiseScale≈3.5`；WLR 误差带 800 m
- [x] 测量误差扩展接口：`RDF_RadarMeasurementModel`（CFAR 后 / Tracker 前，下游可 override）
- [x] 天气驱动雨/雾衰：`SampleWorldWeather` + `m_EnableWeatherDrivenRainLoss`（逼真开 / 理想关）
- [x] Showcase 可视化：扇面/锁定锥/余晖/距离环；PPI 压缩贴右下角

#### 3 — 再加深（中收益 · 中高成本）← 已落地

| 项 | 收益 | 成本 | 说明 |
|----|------|------|------|
| GO-CFAR / SO-CFAR | 杂波边缘更稳 | 中 | `m_CfarMode` |
| 扫描阈值进 `RDF_RadarSettings` | 不同雷达可调 | 低中 | LOS/复用/优先级 |
| 欺骗扩展：拖距 / 角闪烁 / 间歇假点 | EW 玩法厚度 | 中 | 三个 Effect 类 |
| 反炮兵 HUD（发射/落点圈） | WLR 产品化 | 中 | PPI + 世界环 |

- [x] 可配置 GO-CFAR / SO-CFAR
- [x] 扫描优化阈值下沉 `RDF_RadarSettings`
- [x] 欺骗库扩展（拖距、角闪烁、间歇假点）
- [x] 反炮兵专用 HUD（WLR 告警圈）

#### 4 — 重评估后排期（2026-07-29）

原则不变：**误差合常理 → 加深模型；可观测 → 可调参；远程/组网最后。**  
现状：逼真通道与 Network 单雷达权威路径已落地；§4 旧「一律延后」改为分档。

| 档 | 项 | 收益 | 成本 | 为何现在这样排 |
|----|----|------|------|----------------|
| **P1 已完成** | Python CFAR / track 单测 + 可选 golden | 防逼真模型漂移 | 低 | `test_rdf_radar_cfar.py` / `test_rdf_radar_track.py` |
| **P1 已完成** | 最小 CI（Python unittest） | 工程卫生 | 低 | `.github/workflows/python-dem-tests.yml` |
| **产品缺口** | 锁定层对接模组武器 | 玩法闭环 | 中 | 不在旧勾选表内，但 CAPABILITIES 仍列为首选；示例制导已有 |
| **P2 场景驱动** | 刀刃绕射 / 更精细多径 | 山地体感 | 高 | 已有弱 NLOS 反射因子；真绕射需几何模型 + 场景需求 |
| **P2 场景驱动** | DEM span 遮挡 / 多径 | 城区/林冠 | 高 | 离线/烘焙有 span；游戏 SURF 路径故意不带 — 勿轻易重开 |
| **P2 单雷达打磨后** | 多雷达 plots 融合 / 交叉定位 | 战役级 | 很高 | Network Sprint 已铺权威同步；融合是新产品层，非小补丁 |
| **P2 空窗** | LiDAR：DiagMenu、Sensor 门面 | LiDAR UX | 中 | ~~PPI + Showcase~~；**Sensor + DiagMenu + RECT FOV 已落地** |
| **P2 离线大改时** | 拆分 `rdf_radar_mass_battle_sim.py`（~1.8k LOC） | 可维护 | 中 | 不挡游戏内路线 |
| **P2→P3** | IFF / 数据链抽象 | 友军识别 | 高 | 无现成钩子；有明确友军玩法再启 |
| **P3** | 远程计算全链路 | 卸算力 | 很高 | Stress/Perf 证伪瓶颈前不做；禁止远程替代 Trace/实体查询 |
| **P3** | 游戏内 AutoTest 无 Debugger 批跑 | CI 完整 | 中高 | 依赖 Workbench 自动化；P1 用 Python CI 顶住 |

- [x] Network：权威结果（plots + K/W/L）+ 关键 Settings RplProp + 发射态（见 CHANGELOG Network Sprint）
- [x] **P1** Python CFAR / track 单测 + 可选 Enforce golden（`test_rdf_radar_cfar.py` / `test_rdf_radar_track.py`；CA 与 `RDF_RadarCfarGate` 对齐）
- [x] **P1** 最小 CI（`.github/workflows/python-dem-tests.yml` 跑 `test_rdf_*.py`；游戏内批跑仍见 P3）
- [ ] **产品** 锁定层对接模组武器（见 VEHICLE_RADAR_LOCK_GUIDE）
- [ ] **P2** 刀刃绕射 / 更精细多径（明确山地场景时）
- [ ] **P2** DEM span 遮挡 / 多径（明确城区/林冠需求且接受发布包变重时）
- [ ] **P2** 多雷达 plots 融合 / 交叉定位（单雷达玩法打磨完）
- [x] **P2** LiDAR：PPI 动画 + Showcase（余晖/环/扇面）
- [x] **P2** LiDAR：DiagMenu、Sensor 门面对齐（`RDF_LidarSensor` + `RDF_LidarDiagMenu` + `RDF_RectangularFOVSampleStrategy`）
- [ ] **P2** 拆分 `rdf_radar_mass_battle_sim.py`
- [ ] **P2/P3** IFF / 数据链抽象
- [ ] **P3** 远程计算：设计文档 → Backend 接口 → Rest 异步 → 服务端对齐（失败回退 Local）
- [ ] **P3** 游戏内 AutoTest 无 Debugger 批跑 / 完整 CI

#### 5 — 停车场（低收益或高风险，默认不做）

- [ ] `LICENSE` / `LICENSE.txt` 合并（内容已一致；工具链路径未确认前不动）
- 全场 FDTD / 训练级 RD 图每帧 / 完整 DRFM
- 远程替代本地 Trace / 实体查询

---

### 建议冲刺切片

**Sprint A+B（已完成）**：§1 + §2 — 观测、双档、测量噪声、热噪声填空、大气衰减。  
**Sprint C（已完成）**：GO/SO-CFAR、Settings 阈值、欺骗扩展、WLR HUD。  
**Network Sprint（已完成）**：关键 Settings RplProp、plots 加厚、K/W/L 权威摘要、发射态。  
**Sprint D（已完成核心）**：Python CFAR/track golden + 最小 CI。  
**LiDAR UX（已完成）**：Showcase / PPI 动画 + `RDF_LidarSensor` + DiagMenu + 矩形 FOV。  
**下一步**：锁定层对接武器（模组侧）；场景驱动绕射 / DEM span / 组网融合。

Ideal：`RDF_RadarAutoTestSuite.StartAll()`  
Realistic：`RDF_RadarAutoTestSuite.StartAllRealistic()`

---

### 已落地（摘要）

#### 功能链

- [x] 扫描 + Trace LOS / NLOS · 分类跟踪 · 雷达方程 / 多普勒 / MTI / SNR
- [x] PPI · DEM 杂波 · CA/GO/SO-CFAR · EW / 欺骗 · 测量合成
- [x] 弹道 + WLR（多点真空拟合）· 散射体表 / Signature / Swerling
- [x] `RDF_RadarSensor`（SEARCH / STARE / WLR / ESM）· `RDF_RadarLockManager` · `GetLockedTarget` / `GetArmAim`
- [x] `RDF_LidarSensor`（FULL_SPHERE / CONE / RECT / SWEEP / ENTS）· DiagMenu · 矩形 FOV
- [x] RWR（`RDF_RadarRwr`）· ESM 侦收 · 反辐射瞄点
- [x] 火箭制导示例（`RDF_RadarRocketGuidance` + Lock-Fire 回归）
- [x] Network 权威端挂 Sensor
- [x] Network：关键配置 RplProp + 扫描结果 Broadcast（plots / tracks / WLR / lock）+ 发射态

#### 工程 / 性能

- [x] 散射体 XZ 网格 · Scanner 编排拆分（Geometry + PhysicalDetect）· DEM `$profile`→`DemData/`
- [x] LOS 缓存 · 多帧预算 · 优先级扫描 · 低速物理复用 · `m_FairScanCursor`
- [x] SurfaceTable / Signature `.conf`（工坊）+ JSON/CSV 回退

#### 测试

- [x] `AutoTestSuite` 7/7（Ballistics / DEM / Lock / Airborne / ShellFire / Perf / Play）；双档 ideal / realistic
- [x] 独立回归：Stress / RWR / ESM-ARM / Rocket Lock-Fire；地图叠加 `RDF_RadarAutoTestMapOverlay`
- [x] 测试不加辐射源 / 抬 RCS / 强制写表
- [x] 离线 Python golden：ballistics / CFAR / track + GitHub Actions CI

#### DEM / 仓库

- [x] V3 烘焙 · SURF JSON 发布包 · 运行时 GetSurfaceY + LRU · 杂波 + WLR 地面
- [x] `.gitignore` · 空目录清理 · P1 CHANGELOG 摘要

#### LiDAR（维护）

- [x] 点云 / HUD（`LidarPPI.layout`）/ CSV / 网络 / Bootstrap  
- 体验项见上方 §4

---

### 明确不做（实时主路径）

- 全场电磁波 / FDTD · 训练级 RD 图每帧 · 完整 DRFM
- 远程替代本地 Trace / 实体查询

产品目标：**可信的军武传感器玩法**（发现 / 丢失 / 压制 / 欺骗 / 锁定），不是电磁仿真器，也不用理想世界冒充战场精度。

---

## English

Prioritize by **benefit ÷ cost**; tags are categories only, not ranking.  
Related: [RADAR_API.md](docs/RADAR_API.md) · [RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) · [VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) · [CHANGELOG.md](docs/CHANGELOG.md)

---

### Positioning

The framework already reproduces the chain: **search → plots → track → lock/fire-control → WLR**.  
Physical noise / atmosphere / signal processing are highly idealized — regressions that look “too accurate” are closed-loop checks in a noise-free math world, not battlefield precision.

| Current strengths | Current weaknesses |
|----------|----------|
| Feature chain, Sensor facade, dual-tier regression | Fine multipath, diffraction |
| Grid, LOS/physics reuse, shard budget + stats | Multi-radar fusion, IFF |
| Measurement noise/bias, GO/SO-CFAR, Network authority sync | Remote compute, golden/CI hygiene |

Entry: [README.md](README.md)

---

### Implementation order (by benefit)

Benefit: gameplay feel / credibility / maintainability. Cost: change surface, regression risk, integration effort.  
Principle: **make errors look sane first, then deepen models; observability before knobs; remote and networking last.**

#### 1 — Do now (high benefit · low cost)

| Item | Benefit | Cost | Why now |
|----|------|------|------------|
| CHANGELOG: backfill scan-optimization entries | Docs match reality | Very low | Debt; minutes |
| `GetStatusShort` / stats: `reuseHits` `freshUpdates` `budgetSkips` | See whether opts actually fire | Low | No observability → cannot tune budget |
| Dual-tier regression: ideal stays “accurate”; realistic uses error bands | Prevent P2 from locking the suite | Low | Guardrail before physics items |

- [x] CHANGELOG：LOS 缓存 / 分片预算 / 物理复用 / 优先级扫描
- [x] 复用与预算命中统计写入状态行
- [x] AutoTest：`ideal` vs `realistic`（或 settings 开关）；逼真档断言改为误差带

#### 2 — Next wave (high benefit · medium cost) ← core shipped

Goal: gameplay shows “unstable discovery / false alarms / range jitter”, not forever 0.3 m.

| Item | Benefit | Cost | Depends on |
|----|------|------|------|
| Measurement noise & bias (range / az / Doppler) | Track & WLR feel sane immediately | Medium | Dual-tier regression |
| Thermal fill of empty range cells | CFAR has real false-alarm rate | Medium | None |
| Simplified atmosphere / rain attenuation | Long-range detection gets harder; strong feel | Medium | More natural after measurement noise |

- [x] 测量噪声与系统性偏差（合成测量路径）
- [x] 热噪声填充空距离单元 → CA-CFAR 虚警可测
- [x] 大气衰减 / 降雨损耗（简化模型，可关）
- [x] 逼真档套件真正加压：CFAR+热填空保持开启；`MeasNoiseScale≈3.5`；WLR 误差带 800 m
- [x] 测量误差扩展接口：`RDF_RadarMeasurementModel`（CFAR 后 / Tracker 前，下游可 override）
- [x] 天气驱动雨/雾衰：`SampleWorldWeather` + `m_EnableWeatherDrivenRainLoss`（逼真开 / 理想关）
- [x] Showcase 可视化：扇面/锁定锥/余晖/距离环；PPI 压缩贴右下角

#### 3 — Deepen further (medium benefit · medium–high cost) ← shipped

| Item | Benefit | Cost | Notes |
|----|------|------|------|
| GO-CFAR / SO-CFAR | Stabler clutter edges | Medium | `m_CfarMode` |
| Scan thresholds into `RDF_RadarSettings` | Per-radar tuning | Low–medium | LOS/reuse/priority |
| Deception extensions: range gate pull-off / angle scintillation / intermittent false plots | EW gameplay depth | Medium | Three Effect classes |
| Counter-battery HUD (launch/impact rings) | Productize WLR | Medium | PPI + world rings |

- [x] 可配置 GO-CFAR / SO-CFAR
- [x] 扫描优化阈值下沉 `RDF_RadarSettings`
- [x] 欺骗库扩展（拖距、角闪烁、间歇假点）
- [x] 反炮兵专用 HUD（WLR 告警圈）

#### 4 — Reprioritized (2026-07-29)

Same principles: **sane errors → deeper models; observability → knobs; remote/networking last.**  
Context: realistic channel + single-radar authoritative Network path shipped; old “all deferred” list is now tiered.

| Tier | Item | Benefit | Cost | Why this order |
|----|------|------|------|----------------|
| **P1 done** | Python CFAR / track unit tests + optional golden | Drift guard | Low | `test_rdf_radar_cfar.py` / `test_rdf_radar_track.py` |
| **P1 done** | Minimal CI (Python unittest) | Eng hygiene | Low | `.github/workflows/python-dem-tests.yml` |
| **Product gap** | Lock layer → mod weapons | Gameplay loop | Medium | Outside old checklist; still CAPABILITIES #1; sample guidance exists |
| **P2 scenario** | Knife-edge diffraction / fine multipath | Mountain feel | High | Weak NLOS factor exists; real diffraction needs geometry + demand |
| **P2 scenario** | DEM span occlusion / multipath | Urban / canopy | High | Offline/bake has spans; game SURF path deliberately omits — don’t reopen lightly |
| **P2 after polish** | Multi-radar plots fusion / cross-fix | Campaign | Very high | Network Sprint paved sync; fusion is a new product layer |
| **P2 idle** | LiDAR PPI / DiagMenu / Sensor facade | LiDAR UX | Medium | Showcase + **`RDF_LidarSensor` + DiagMenu + RECT FOV shipped** |
| **P2 offline rewrite** | Split `rdf_radar_mass_battle_sim.py` (~1.8k LOC) | Maintainability | Medium | Does not block in-game roadmap |
| **P2→P3** | IFF / datalink abstraction | Friendly ID | High | No hooks; start only with explicit friend/foe gameplay |
| **P3** | Remote compute full chain | Offload CPU | Very high | Wait for Stress/Perf proof; never replace Trace/entity query remotely |
| **P3** | In-game AutoTest without Debugger | Full CI | Med–high | Needs Workbench automation; P1 Python CI covers interim |

- [x] Network: authoritative results (plots + K/W/L) + key Settings RplProp + emit state (see CHANGELOG Network Sprint)
- [x] **P1** Python CFAR / track unit tests + optional Enforce golden (`test_rdf_radar_cfar.py` / `test_rdf_radar_track.py`; CA aligned with `RDF_RadarCfarGate`)
- [x] **P1** Minimal CI (`.github/workflows/python-dem-tests.yml` runs `test_rdf_*.py`; in-game batch still P3)
- [ ] **Product** Lock layer → mod weapons (see VEHICLE_RADAR_LOCK_GUIDE)
- [ ] **P2** Knife-edge diffraction / fine multipath (clear mountain scenario)
- [ ] **P2** DEM span occlusion / multipath (urban/canopy need + accept heavier packs)
- [ ] **P2** Multi-radar plots fusion / cross-fix (after single-radar polish)
- [x] **P2** LiDAR: PPI animation + Showcase
- [x] **P2** LiDAR: DiagMenu + Sensor facade (`RDF_LidarSensor` + `RDF_LidarDiagMenu` + `RDF_RectangularFOVSampleStrategy`)
- [ ] **P2** Split `rdf_radar_mass_battle_sim.py`
- [ ] **P2/P3** IFF / datalink abstraction
- [ ] **P3** Remote compute: design doc → Backend → Rest async → server align (fallback Local)
- [ ] **P3** In-game AutoTest without Debugger / full CI

#### 5 — Parking lot (low benefit or high risk; default skip)

- [ ] `LICENSE` / `LICENSE.txt` merge (content identical; wait for toolchain path confirmation)
- Full-field FDTD / training-grade RD map every frame / full DRFM
- Remote replacing local Trace / entity queries

---

### Suggested sprint slices

**Sprint A+B (done)**: §1 + §2 — observability, dual-tier, measurement noise, thermal fill, atmospheric attenuation.  
**Sprint C (done)**: GO/SO-CFAR, Settings thresholds, deception extensions, WLR HUD.  
**Network Sprint (done)**: key Settings RplProp, thicker plots, K/W/L authoritative summary, emit state.  
**Sprint D (core done)**: Python CFAR/track golden + minimal CI.  
**LiDAR UX (done)**: Showcase / PPI anim + `RDF_LidarSensor` + DiagMenu + rectangular FOV.  
**Next**: lock→weapon integration (mod side); scenario-driven diffraction / DEM span / fusion.

Ideal: `RDF_RadarAutoTestSuite.StartAll()`  
Realistic: `RDF_RadarAutoTestSuite.StartAllRealistic()`

---

### Shipped (summary)

#### Feature chain

- [x] 扫描 + Trace LOS / NLOS · 分类跟踪 · 雷达方程 / 多普勒 / MTI / SNR
- [x] PPI · DEM 杂波 · CA/GO/SO-CFAR · EW / 欺骗 · 测量合成
- [x] 弹道 + WLR（多点真空拟合）· 散射体表 / Signature / Swerling
- [x] `RDF_RadarSensor`（SEARCH / STARE / WLR / ESM）· `RDF_RadarLockManager` · `GetLockedTarget` / `GetArmAim`
- [x] `RDF_LidarSensor` (FULL_SPHERE / CONE / RECT / SWEEP / ENTS) · DiagMenu · rectangular FOV
- [x] RWR（`RDF_RadarRwr`）· ESM 侦收 · 反辐射瞄点
- [x] 火箭制导示例（`RDF_RadarRocketGuidance` + Lock-Fire 回归）
- [x] Network 权威端挂 Sensor
- [x] Network：关键配置 RplProp + 扫描结果 Broadcast（plots / tracks / WLR / lock）+ 发射态

#### Engineering / performance

- [x] 散射体 XZ 网格 · Scanner 编排拆分（Geometry + PhysicalDetect）· DEM `$profile`→`DemData/`
- [x] LOS 缓存 · 多帧预算 · 优先级扫描 · 低速物理复用 · `m_FairScanCursor`
- [x] SurfaceTable / Signature `.conf`（工坊）+ JSON/CSV 回退

#### Tests

- [x] `AutoTestSuite` 5/5（Ballistics / DEM / Lock / Airborne / ShellFire）；双档 ideal / realistic
- [x] 独立回归：RWR / ESM-ARM / Rocket Lock-Fire；地图叠加 `RDF_RadarAutoTestMapOverlay`
- [x] 测试不加辐射源 / 抬 RCS / 强制写表
- [x] Offline Python golden: ballistics / CFAR / track + GitHub Actions CI

#### DEM / repo

- [x] V3 烘焙 · SURF JSON 发布包 · 运行时 GetSurfaceY + LRU · 杂波 + WLR 地面
- [x] `.gitignore` · 空目录清理 · P1 CHANGELOG 摘要

#### LiDAR (maintaining)

- [x] 点云 / HUD（`LidarPPI.layout`）/ CSV / 网络 / Bootstrap  
- UX items: see §4 above

---

### Explicitly out of scope (realtime main path)

- Full-field EM / FDTD · training-grade RD map every frame · full DRFM
- Remote replacing local Trace / entity queries

Product goal: **credible military sensor gameplay** (discover / lose / jam / deceive / lock) — not an EM simulator, and not pretending ideal-world accuracy is battlefield accuracy.
