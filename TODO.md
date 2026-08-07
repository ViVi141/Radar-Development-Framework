> **Languages / 语言**: [English](#english) · [中文](#中文)

# TODO — RDF

## 中文

按 **收益 ÷ 成本** 排期；标签只作分类，不决定顺序。  
相关：[RADAR_API.md](docs/RADAR_API.md) · [RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) · [VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) · [CHANGELOG.md](docs/CHANGELOG.md)

---

### 定位

框架已复现功能链：**搜索 → 点迹 → 跟踪 → 锁定/火控 → WLR**，并具备可选 **Pulse-Doppler / MTD bank + 旋翼微多普勒**。  
物理层是**目标级工程近似**（方程 + 小 Doppler 组 + DEM σ⁰），不是训练级「波形 → RD 立方体 → CFAR」实时孪生。  
回归“过准”是在低噪声数学世界验证闭环，不是战场精度。

| 当前强项 | 当前弱项 / 成本真相 |
|----------|---------------------|
| 功能链、Sensor 门面、双档回归、Network / 融合 | 多刃多径、DEM span（场景驱动，SURF 故意不带） |
| 散射体全局表 + SURF 共享预载；LOS/物理复用、分片预算 | **瓶颈在 Trace / 世界查询**，不在 MTD 小循环算术 |
| 测量噪声、GO/SO-CFAR、单刃绕射、MTD / coast / clutter map | 游戏内无 Debugger 批跑 CI；密码学 IFF 不做 |
| Python golden + GA 双 job（unittest + full_sim） | 远程实时算力对局内玩法性价比低（见停车场） |

入口：[README.md](README.md)

**成本模型（排期用）**

- 实体发现 / DEM 地表类：已有全局复用（`ScattererRegistry`、`DemRuntimeCache` 共享 SURF）。
- 通视与几何：每雷达各自 `TraceMove`——不可远程替代，也难全局共享。
- 「全面」信号处理按**分辨单元**缩放，不按活跃实体数；压缩 DEM 只解决分发/驻留，不降低 RD 阶复杂度。
- 离线 Python = 标定与防漂移；**不等于**对局远程协处理器。

---

### 实施顺序（按收益）

收益：玩法体感 / 可信度 / 可维护性。成本：改动面、回归风险、联调量。  
原则：**先让误差合常理，再加深局内近似；先可观测，再可调参；离线标定优先于远程实时；组网已完成的不再回退。**

#### 1 — 立刻做（高收益 · 低成本）← 已完成

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

#### 4 — 重评估后排期（2026-07-31）

原则：**局内目标级近似优先；离线 Python 标定；远程实时默认不做。**  
现状：逼真通道、Network、融合、MTD/旋翼/coast、Python CI 双 job 已落地。旧「远程卸算力」对玩法性价比低——Trace/实体不能卸，能卸的玩家难感知。

| 档 | 项 | 收益 | 成本 | 为何现在这样排 |
|----|----|------|------|----------------|
| **P1 已完成** | Python CFAR / track 单测 + golden | 防逼真模型漂移 | 低 | `test_rdf_radar_cfar.py` / `test_rdf_radar_track.py` |
| **P1 已完成** | 最小 CI → **双 job**（unittest discover + full_sim coverage） | 工程卫生 | 低 | `.github/workflows/python-dem-tests.yml`；含 MTD/CPA/PRF |
| **P1 已完成** | MTD bank + 旋翼微多普勒 + clutter map + track coast | 切向直升机可玩 | 中 | `RDF_MTI_MTD_BANK` / `PULSE_DOPPLER`；默认仍 TwoPulse |
| **产品** | 锁定层对接模组武器 | 玩法闭环 | 中 | 桥接已落地；**prefab / 实弹仍模组侧** |
| **框架已完成** | EW 噪声软化 + burn-through 可观测 | 压制可调 | 低中 | `SEARCH_AVG` 默认；`GetEwStatsShort` |
| **P2 已完成** | 刀刃绕射近似 | 山地半遮挡弱检 | 中 | 单刃 Fresnel + DEM 采样；非多刃/UTD |
| **P2 已完成** | 数据链 + 多雷达融合 | 战役组网 | 高 | Hub + Fusion；轻量 IFF |
| **P2 已完成** | LiDAR Sensor / Showcase / RECT FOV | LiDAR UX | 中 | DiagMenu 受引擎槽位限制已禁用 |
| **P2 可选** | 粗 RD 近似（少距离门 × 少 Doppler，**分帧摊销**） | 更像 PD 体感 | 中高 | **已完成**（默认关；Ideal 强制关） |
| **P2 可选** | 多雷达共用 discovery / focus 调度 | 减重复球查询 | 中 | **已完成**（RegisterFocus + 轮转） |
| **P2 场景驱动** | DEM span 遮挡 / 多径 | 城区/林冠 | 高 | **门控完成**（默认关；SURF 仍不带） |
| **P2 离线** | 拆分 `rdf_radar_mass_battle_sim.py`（~1.8k LOC） | 可维护 | 中 | **已完成**（`mass_battle/`） |
| **P2 离线优先** | 杂波 Doppler PSD / 多 PRF 模糊标定表 → 回灌硬件参数 | 可信度 | 中 | **已完成**（`rdf_radar_hw_calibrate.py`） |
| **P3 工程** | 游戏内 AutoTest 无 Debugger 批跑 | CI 完整 | 中高 | **flag 批跑已完成**；无头 Workbench CI 仍不做 |
| **停车场** | ~~远程计算全链路~~ | — | 很高 | 见 §5：对局内玩法鸡肋；非教研/专用服不排期 |

- [x] Network：权威结果（Reliable 航迹摘要 + 可选 Unreliable plots）+ 关键 Settings RplProp + 发射态
- [x] Network 协议：类型化 `array<int/float>` Rpc + `RplSave` JIP（`RDF_RadarNetCodec`；非 CSV）
- [x] **P1** Python CFAR / track 单测 + Enforce golden
- [x] **P1** CI：unittest discover + `full_sim` coverage gates（MTD / heli CPA / PRF / coast / sig-pack）
- [x] **P1** MTD Doppler bank + 旋翼微多普勒 + clutter-map EMA + track coast + `PULSE_DOPPLER` 模式
- [x] **产品** 锁定层对接模组武器（`RDF_RadarWeaponBridge` / `WeaponComponent` + 指南；prefab 仍模组侧）
- [x] **框架** EW 噪声软化：`SEARCH_AVG`/`BEAM`/`MAINLOBE_ONLY` + burn-through 状态行
- [x] **P2** 刀刃绕射近似（单刃 Fresnel + DEM 沿程采样；与 bounce 取 max）
- [x] **P2** 多雷达 plots 融合 / 交叉定位（`RDF_RadarFusionService` + DatalinkHub）
- [x] **P2** LiDAR：Showcase / Sensor / RECT FOV（DiagMenu 禁用）
- [x] **P2/P3** 轻量 IFF + 站间 Hub（非密码学 IFF）
- [x] **P2 可选** 粗 RD 分帧近似（默认关；Ideal 强制关；`RDF_RadarCoarseRdMap` + Settings）
- [x] **P2 可选** 多雷达 discovery / focus 调度合并（Registry RegisterFocus + 轮转 discovery + 多 focus 剪枝）
- [x] **P2 场景** DEM span 遮挡 / 多径（`m_EnableDemSpanOcclusion` 默认关；非 SURF 柱顶；SURF 仍不发布 span）
- [x] **P2 离线** 拆分 `rdf_radar_mass_battle_sim.py` → `mass_battle/` 包
- [x] **P2 离线** 杂波谱 / 多 PRF 标定 → `rdf_radar_hw_calibrate.py` + `calib/*.json`（不驱动局内检出）
- [x] **P3** 游戏内 AutoTest 无 Debugger 批跑（`RunAutoTestSuite.flag` + `RDF_RadarAutoTestBatch`）；完整无头 CI 仍不做 — 见 [AUTOTEST_CI_LIMITS.md](docs/AUTOTEST_CI_LIMITS.md)
- [ ] ~~**P3** 远程计算全链路~~ → **移入停车场**

#### 5 — 停车场（低收益或高风险，默认不做）

- [ ] `LICENSE` / `LICENSE.txt` 合并（内容已一致；工具链路径未确认前不动）
- [ ] **远程计算全链路**（设计文档 → Backend → Rest 异步 → 权威对齐）：对局内玩法鸡肋——不能替代 Trace/实体，延迟伤火控，运维重；仅教研孪生 / 专用服批仿真才重新评估；失败也必须 Local fallback
- 全场 FDTD / 训练级 RD 图每帧 / 完整 DRFM
- 远程替代本地 Trace / 实体查询
- 用压缩 DEM「换」训练级局内实时链（压缩只助分发，不改复杂度阶）

---

### 建议冲刺切片

**Sprint A+B（已完成）**：§1 + §2 — 观测、双档、测量噪声、热噪声填空、大气衰减。  
**Sprint C（已完成）**：GO/SO-CFAR、Settings 阈值、欺骗扩展、WLR HUD。  
**Network Sprint（已完成）**：关键 Settings RplProp、发射态、类型化 Rpc、Reliable 摘要 / Unreliable plots、上限降频/指纹/兴趣半径。  
**Sprint D（已完成）**：Python CFAR/track golden + CI（现为双 job）。  
**LiDAR UX（已完成）**：Showcase / PPI + `RDF_LidarSensor` + RECT FOV。  
**火控桥（已完成）**：`RDF_RadarWeaponBridge` / `WeaponComponent` + 指南 §2.3。  
**EW 软化（已完成）**：噪声干扰耦合模式 + burn-through 可观测。  
**刀刃绕射（已完成）**：单刃 Fresnel + DEM 沿程；与 NLOS bounce 取 max。  
**数据链/融合（已完成）**：Hub + FusionService + 轻量 IFF。  
**MTD Sprint（已完成）**：MTD bank、旋翼微多普勒、clutter map、coast、`PULSE_DOPPLER`、Python/CI 护栏。  
**P2 可选/离线（已完成）**：粗 RD（默认关）、多雷达 focus 调度、DEM span 门控、mass_battle 拆分、HW 标定 JSON、AutoTest flag 批跑。

#### 8 — 传播数学拟真（无波形，2026-08-05）

原则：加深目标级方程 / 测量层；**不做**波形 → RD / 全脉冲。

| 档 | 项 | 状态 |
|----|----|------|
| **P1** | LOS 双射线多径（Fresnel Γ + 粗糙度） | **已完成** |
| **P1** | 4/3 地球折射（地平线软衰减 + 俯仰偏置） | **已完成** |
| **P1** | PRF 距离/多普勒模糊折叠（WLR 跳过多普勒） | **已完成** |
| **P1** | 极化失配标量 | **已完成** |
| **P1** | 旁瓣地板 + 极化模式匹配表 + 可选 SLB | **已完成** |
| **P1** | Enforce 双主导刃 + ν LUT bake-back | **已完成** |
| **P1** | 分段方向图 LUT + 固定站路径因子表 | **已完成** |
| **护栏** | Python `test_rdf_radar_propagation` + full_sim 标签 | **已完成** |
| **API** | 取消理想/逼真双档 → 按需 Enable* | **已完成** |

- [x] Enforce：`TwoRay` / refraction / ambiguity helpers（默认关，按需 Enable*）
- [x] 删除 `ApplyIdeal/Realistic` + `StartAllRealistic`；测试用 `StabilizeForRegression()`
- [x] Python channel 对齐 + CI 收集 propagation 单测

**下一步（择一，按体感）**

1. **产品**：模组侧武器 prefab 接 `WeaponBridge`（框架外）。  
2. **场景**：有城区/林冠数据时打开 `m_EnableDemSpanOcclusion`（需非 SURF V3/CSV）。  
3. **调参**：`rdf_radar_hw_calibrate.py` 产出回灌 Hardware；粗 RD 仅 Perf 对照后显式打开。  
4. **工程**：本地 Play + `RunAutoTestSuite.flag` 批跑（非无头 CI）。

#### 6 — MTD 深化（补简化实现，2026-08-01）

原则：仍目标级近似；让已有字段真干活；离线标定可回灌局内；不做训练级 RD。

| 档 | 项 | 收益 | 成本 | 状态 |
|----|----|------|------|------|
| **P1** | 旋翼谱吃 `blade_count` + 仰角盘面姿态缩放 | 切向/俯视差异可信 | 低 | **已完成** |
| **P1** | `σ_vr` 推导 / `HwCalib.json` 回灌 `m_MtdClutterLeakage` | 标定不再悬空 | 低中 | **已完成** |
| **P1** | 状态行：win bin / PRF / 旋翼边带助检计数 | 可观测调参 | 低 | **已完成** |
| **P2** | 航迹多 PRF 消盲速关联 | PD 航迹更稳 | 中 | **已完成** |
| **P2** | 主要直升机 signature 显式填 tip/blades/hub | 少靠路径默认 | 内容 | **已完成** |

- [x] **P1** 旋翼微多普勒：`blade_count` 谐波间隔 + LOS 仰角盘面因子（Enforce + Python 对齐）
- [x] **P1** Hardware：`m_ClutterSigmaVrMs` / `m_DeriveMtdLeakageFromSigmaVr` + `$profile:RDF/RadarData/HwCalib.json` 回灌
- [x] **P1** `GetStatusShort` / Scanner：MTD win bin、旋翼边带助检
- [x] **P2** 航迹侧双 PRF 消盲速（门限加宽 + 盲速额外 miss 配额；Python `near_blind_speed` 对齐）
- [x] **P2** 直升机 signature 表显式旋翼列（`.conf` 28 机架 + `rdf_sig_patch_heli_rotors.py` + Mi-8/UH-1 家族表）

套件：`RDF_RadarAutoTestSuite.StartAll()`（各测试 `StabilizeForRegression()` / 按需 Enable*）

#### 7 — 经典 MTI + Track coast 完整（2026-08-01）

目标级近似边界内补全；不做 STAP / Kalman。

| 档 | 项 | 状态 |
|----|----|------|
| **MTI** | 三脉冲 + 参差 max 消盲 + 谱线 canceller + σ_vr 杂波地板 | **已完成** |
| **Coast** | 速度/Rdot 更新、PredictPolarAt 关联、门限增长、软 miss、coast 时限 | **已完成** |

- [x] `RDF_MTI_THREE_PULSE` + `MaxMtiCancellerSpectrumGain` + `m_MtiStaggerDeblind`
- [x] `SuggestMtiClutterFloor` / derive 与 HwCalib `three_pulse`
- [x] Track `CoastTo` 弹道速度 + LOS Rdot；门限 grow；软 miss；`m_TrackCoastMaxSec`
- [x] Python golden + heli CPA / full_sim 对齐

---

### 已落地（摘要）

#### 功能链

- [x] 扫描 + Trace LOS / NLOS · 分类跟踪 · 雷达方程 / 多普勒 / MTI·MTD / SNR
- [x] Pulse-Doppler 模式 · 旋翼微多普勒 · clutter-map EMA · track coast（含 Doppler-null）
- [x] PPI · DEM 杂波 · CA/GO/SO-CFAR · EW / 欺骗 · 测量合成
- [x] EW 噪声软化（`SEARCH_AVG`/`BEAM`/`MAINLOBE_ONLY` + burn-through 状态）
- [x] 刀刃绕射近似（单刃 Fresnel + DEM 沿程；与 bounce 取 max）
- [x] 弹道 + WLR（多点真空拟合）· 散射体表 / Signature / Swerling
- [x] `RDF_RadarSensor`（SEARCH / STARE / WLR / ESM / PULSE_DOPPLER）· `RDF_RadarLockManager` · `GetLockedTarget` / `GetArmAim`
- [x] `RDF_RadarWeaponBridge` / `WeaponComponent` · `FireSolution` · `GuideRocket`
- [x] `RDF_LidarSensor`（FULL_SPHERE / CONE / RECT / SWEEP / ENTS）· 矩形 FOV
- [x] RWR（`RDF_RadarRwr`）· ESM 侦收 · 反辐射瞄点
- [x] 火箭制导示例（`RDF_RadarRocketGuidance` + Lock-Fire 回归）
- [x] Network 权威端挂 Sensor + 规模对齐 Broadcast
- [x] 数据链 Hub + 多雷达融合 / 双站交会 + 轻量 IFF

#### 工程 / 性能

- [x] 全局散射体表（网格发现/刷新）· EmitterRegistry · Scanner 拆分（Geometry + PhysicalDetect）
- [x] DEM SURF 共享预载 + GetSurfaceY · LOS 缓存 · 多帧预算 · 优先级扫描 · 物理复用
- [x] SurfaceTable / Signature `.conf`（工坊）+ JSON/CSV 回退

#### 测试

- [x] `AutoTestSuite`（Ballistics / DEM / Lock / Airborne / ShellFire / Perf / Play）；双档 ideal / realistic
- [x] 独立回归：Stress / RWR / ESM-ARM / Rocket Lock-Fire / HeliDuel / SamEngage / Fusion；地图叠加
- [x] 离线 Python：ballistics / CFAR / track / EW / diffraction / fusion / MTD / heli CPA / sig-pack / coast / systems+full_sim + GA 双 job

#### DEM / 仓库

- [x] V3 烘焙 · SURF JSON 发布包（分发友好）· 运行时 GetSurfaceY + 共享 RAM / LRU · 杂波 + WLR 地面
- [x] `.gitignore` · 空目录清理 · CHANGELOG

#### LiDAR（维护）

- [x] 点云 / HUD / CSV / 网络 / Bootstrap · Sensor 门面

---

### 明确不做（实时主路径）

- 全场电磁波 / FDTD · 训练级 RD 图每帧 · 完整 DRFM
- 远程替代本地 Trace / 实体查询
- 默认对局远程协处理器（见停车场）
- 用「实体不多 + DEM 已压缩」论证局内训练级满链

产品目标：**可信的军武传感器玩法**（发现 / 丢失 / 压制 / 欺骗 / 锁定），不是电磁仿真器，也不用理想世界冒充战场精度。

---

## English

Prioritize by **benefit ÷ cost**; tags are categories only, not ranking.  
Related: [RADAR_API.md](docs/RADAR_API.md) · [RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) · [VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) · [CHANGELOG.md](docs/CHANGELOG.md)

---

### Positioning

The framework already ships: **search → plots → track → lock/fire-control → WLR**, plus optional **Pulse-Doppler / MTD bank + rotor micro-Doppler**.  
The physics layer is a **target-level engineering approx** (radar equation + small Doppler bank + DEM σ⁰), not a training-grade realtime “waveform → RD cube → CFAR” twin.  
Regressions that look “too accurate” are closed-loop checks in a low-noise math world, not battlefield precision.

| Current strengths | Current weaknesses / cost truth |
|----------|----------|
| Feature chain, Sensor facade, dual-tier regression, Network / fusion | Cascaded multipath, DEM span (scenario-only; SURF omits on purpose) |
| Global scatterer table + shared SURF preload; LOS/physics reuse, shard budgets | **Bottleneck is Trace / world queries**, not small MTD arithmetic |
| Measurement noise, GO/SO-CFAR, knife-edge, MTD / coast / clutter map | In-game AutoTest without Debugger; crypto IFF out of scope |
| Python golden + GA dual jobs (unittest + full_sim) | Remote realtime compute is poor value for in-game play (see parking lot) |

Entry: [README.md](README.md)

**Cost model (for scheduling)**

- Entity discovery / surface class: already shared (`ScattererRegistry`, `DemRuntimeCache` shared SURF).
- LOS / geometry: per-radar `TraceMove` — cannot be replaced remotely, hard to share globally.
- “Full” signal processing scales with **resolution cells**, not active entity count; compressing DEM helps distribution/residency, not RD complexity class.
- Offline Python = calibration and drift guards; **not** a realtime remote coprocessor for matches.

---

### Implementation order (by benefit)

Benefit: gameplay feel / credibility / maintainability. Cost: change surface, regression risk, integration effort.  
Principle: **sane errors first, then deepen in-game approx; observability before knobs; offline calibration before remote realtime; do not reopen finished networking.**

#### 1 — Do now (high benefit · low cost) ← done

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

#### 4 — Reprioritized (2026-07-31)

Principle: **in-game target-level approx first; offline Python calibration; remote realtime off by default.**  
Context: realistic channel, Network, fusion, MTD/rotor/coast, and dual-job Python CI are shipped. Old “remote offload” is poor gameplay value — Trace/entities cannot move off-box; what can move is hard for players to feel.

| Tier | Item | Benefit | Cost | Why this order |
|----|------|------|------|----------------|
| **P1 done** | Python CFAR / track unit tests + golden | Drift guard | Low | `test_rdf_radar_cfar.py` / `test_rdf_radar_track.py` |
| **P1 done** | Minimal CI → **dual jobs** (unittest discover + full_sim coverage) | Eng hygiene | Low | `.github/workflows/python-dem-tests.yml`; includes MTD/CPA/PRF |
| **P1 done** | MTD bank + rotor micro-Doppler + clutter map + track coast | Tangential heli playable | Med | `RDF_MTI_MTD_BANK` / `PULSE_DOPPLER`; default still TwoPulse |
| **Product** | Lock layer → mod weapons | Gameplay loop | Medium | Bridge shipped; **prefab / live rounds still mod-side** |
| **Framework done** | EW noise soft + burn-through observability | Tunable jam | Low–med | `SEARCH_AVG` default; `GetEwStatsShort` |
| **P2 done** | Knife-edge diffraction approx | Mountain partial LOS | Med | Single-edge Fresnel + DEM samples; not multi-edge/UTD |
| **P2 done** | Datalink + multi-radar fusion | Campaign net | High | Hub + Fusion; light IFF |
| **P2 done** | LiDAR Sensor / Showcase / RECT FOV | LiDAR UX | Medium | DiagMenu disabled (engine slot cap) |
| **P2 optional** | Coarse RD approx (few range × Doppler bins, **amortized**) | More PD-like feel | Med–high | **Done** (default off; Ideal forces off) |
| **P2 optional** | Shared multi-radar discovery / focus scheduling | Fewer duplicate sphere queries | Med | **Done** (RegisterFocus + round-robin) |
| **P2 scenario** | DEM span occlusion / multipath | Urban / canopy | High | **Gated** (default off; SURF still omits) |
| **P2 offline** | Split `rdf_radar_mass_battle_sim.py` (~1.8k LOC) | Maintainability | Medium | **Done** (`mass_battle/`) |
| **P2 offline-first** | Clutter Doppler PSD / multi-PRF ambiguity tables → hardware params | Credibility | Med | **Done** (`rdf_radar_hw_calibrate.py`) |
| **P3 eng** | In-game AutoTest without Debugger | Full CI | Med–high | **Flag batch done**; headless Workbench CI still out |
| **Parking** | ~~Remote compute full chain~~ | — | Very high | See §5: chicken-rib for match play; revisit only for training twin / dedicated batch servers |

- [x] Network: authoritative results (Reliable track summary + optional Unreliable plots) + key Settings RplProp + emit state
- [x] Network protocol: typed `array<int/float>` Rpc + `RplSave` JIP (`RDF_RadarNetCodec`; not CSV)
- [x] **P1** Python CFAR / track unit tests + Enforce golden
- [x] **P1** CI: unittest discover + `full_sim` coverage gates (MTD / heli CPA / PRF / coast / sig-pack)
- [x] **P1** MTD Doppler bank + rotor micro-Doppler + clutter-map EMA + track coast + `PULSE_DOPPLER` mode
- [x] **Product** Lock layer → mod weapons (`RDF_RadarWeaponBridge` / `WeaponComponent` + guide; prefab still mod-side)
- [x] **Framework** EW noise soft curve: `SEARCH_AVG`/`BEAM`/`MAINLOBE_ONLY` + burn-through status
- [x] **P2** Knife-edge diffraction approx (single-edge Fresnel + DEM samples; max with bounce)
- [x] **P2** Multi-radar plots fusion / cross-fix (`RDF_RadarFusionService` + DatalinkHub)
- [x] **P2** LiDAR: Showcase / Sensor / RECT FOV (DiagMenu disabled)
- [x] **P2/P3** Light IFF + station Hub (not crypto IFF)
- [x] **P2 optional** Coarse amortized RD (default off; Ideal forces off; `RDF_RadarCoarseRdMap`)
- [x] **P2 optional** Merge multi-radar discovery / focus scheduling (RegisterFocus + round-robin + multi-focus prune)
- [x] **P2 scenario** DEM span occlusion / multipath (`m_EnableDemSpanOcclusion` default off; non-SURF column top)
- [x] **P2 offline** Split `rdf_radar_mass_battle_sim.py` → `mass_battle/` package
- [x] **P2 offline** Clutter spectrum / multi-PRF calibration → `rdf_radar_hw_calibrate.py` (does not drive in-game detects)
- [x] **P3** In-game AutoTest without Debugger (`RunAutoTestSuite.flag` + `RDF_RadarAutoTestBatch`); headless CI still out — see [AUTOTEST_CI_LIMITS.md](docs/AUTOTEST_CI_LIMITS.md)
- [ ] ~~**P3** Remote compute full chain~~ → **moved to parking lot**

#### 5 — Parking lot (low benefit or high risk; default skip)

- [ ] `LICENSE` / `LICENSE.txt` merge (content identical; wait for toolchain path confirmation)
- [ ] **Remote compute full chain** (design doc → Backend → Rest async → authority align): chicken-rib for match play — cannot replace Trace/entities, hurts fire-control latency, heavy ops; revisit only for training twins / dedicated batch sims; Local fallback mandatory
- Full-field FDTD / training-grade RD map every frame / full DRFM
- Remote replacing local Trace / entity queries
- Treating compressed DEM as a ticket to training-grade in-game realtime chains (compression helps distribution, not complexity class)

---

### Suggested sprint slices

**Sprint A+B (done)**: §1 + §2 — observability, dual-tier, measurement noise, thermal fill, atmospheric attenuation.  
**Sprint C (done)**: GO/SO-CFAR, Settings thresholds, deception extensions, WLR HUD.  
**Network Sprint (done)**: key Settings RplProp, emit state, typed Rpc, Reliable summary / Unreliable plots, caps/throttle/fingerprint/interest.  
**Sprint D (done)**: Python CFAR/track golden + CI (now dual jobs).  
**LiDAR UX (done)**: Showcase / PPI + `RDF_LidarSensor` + RECT FOV.  
**Fire bridge (done)**: `RDF_RadarWeaponBridge` / `WeaponComponent` + guide §2.3.  
**EW soft curve (done)**: noise-jammer coupling modes + burn-through observability.  
**Knife-edge (done)**: single-edge Fresnel + DEM samples; max with NLOS bounce.  
**Datalink/fusion (done)**: Hub + FusionService + light IFF.  
**MTD Sprint (done)**: MTD bank, rotor micro-Doppler, clutter map, coast, `PULSE_DOPPLER`, Python/CI guards.  
**P2 optional/offline (done)**: coarse RD (default off), multi-radar focus sched, DEM span gate, mass_battle split, HW calib JSON, AutoTest flag batch.

#### 8 — Propagation math fidelity (no waveform, 2026-08-05)

Deepen target-level equations / measurement layer; **no** waveform → RD / full-pulse.

| Tier | Item | Status |
|------|------|--------|
| **P1** | LOS two-ray multipath (Fresnel Γ + roughness) | **done** |
| **P1** | 4/3-Earth refraction (horizon soft + elevation bias) | **done** |
| **P1** | PRF range/Doppler ambiguity fold (WLR skips Doppler) | **done** |
| **P1** | Polarization mismatch scalar | **done** |
| **P1** | Sidelobe floor + pol match table + optional SLB | **done** |
| **P1** | Enforce dual knife-edge + ν LUT bake-back | **done** |
| **P1** | Segmented pattern LUT + fixed-site path LUT | **done** |
| **Guard** | Python `test_rdf_radar_propagation` + full_sim tags | **done** |
| **API** | Drop ideal/realistic tiers → opt-in Enable* | **done** |

- [x] Enforce two-ray / refraction / ambiguity (default off; Enable*)
- [x] Remove `ApplyIdeal/Realistic` + `StartAllRealistic`; tests use `StabilizeForRegression()`
- [x] Python channel parity + CI collects propagation tests

**Next (pick one by feel)**

1. **Product**: mod-side weapon prefabs on `WeaponBridge` (outside framework).  
2. **Scenario**: enable `m_EnableDemSpanOcclusion` when non-SURF V3/CSV canopy data exists.  
3. **Tuning**: bake Hardware via `rdf_radar_hw_calibrate.py`; turn coarse RD on only after Perf checks.  
4. **Engineering**: local Play + `RunAutoTestSuite.flag` (not headless CI).

#### 6 — MTD deepen (fill simplified stubs, 2026-08-01)

Stay target-level: make rotor fields real, bake calib into leakage, observability; no training RD.

| Tier | Item | Status |
|------|------|--------|
| **P1** | Rotor spectrum: `blade_count` harmonics + elevation disk aspect | **done** |
| **P1** | `σ_vr` / `HwCalib.json` → `m_MtdClutterLeakage` | **done** |
| **P1** | Status: win bin / rotor-aided counts | **done** |
| **P2** | Track multi-PRF blind-speed association | **done** |
| **P2** | Explicit heli signature rotor columns | **done** |

- [x] **P1** Rotor micro-Doppler: blade harmonics + elevation aspect (Enforce + Python)
- [x] **P1** Hardware σ_vr derive + profile `HwCalib.json` bake-back
- [x] **P1** `GetStatusShort` / Scanner MTD stats
- [x] **P2** Track dual-PRF de-blind (wider gates + blind-speed extra misses)
- [x] **P2** Explicit heli signature rotor columns (conf + patch tool + family table)

Suite: `RDF_RadarAutoTestSuite.StartAll()` (tests use `StabilizeForRegression()` / opt-in Enable*)

#### 7 — Classic MTI + Track coast complete (2026-08-01)

Target-level approx only; no STAP / Kalman.

| Tier | Item | Status |
|------|------|--------|
| **MTI** | Three-pulse + stagger max de-blind + spectrum canceller + σ_vr floor | **done** |
| **Coast** | Velocity/Rdot update, PredictPolarAt assoc, gate grow, soft miss, coast max time | **done** |

- [x] `RDF_MTI_THREE_PULSE` + `MaxMtiCancellerSpectrumGain` + `m_MtiStaggerDeblind`
- [x] `SuggestMtiClutterFloor` / derive + HwCalib `three_pulse`
- [x] Track coast kinematics + soft miss + `m_TrackCoastMaxSec`
- [x] Python golden + heli CPA / full_sim aligned

---

### Shipped (summary)

#### Feature chain

- [x] Scan + Trace LOS / NLOS · classified track · radar equation / Doppler / MTI·MTD / SNR
- [x] Pulse-Doppler mode · rotor micro-Doppler · clutter-map EMA · track coast (incl. Doppler-null)
- [x] PPI · DEM clutter · CA/GO/SO-CFAR · EW / deception · measurement synthesis
- [x] EW noise soft curve (`SEARCH_AVG`/`BEAM`/`MAINLOBE_ONLY` + burn-through status)
- [x] Knife-edge diffraction approx (single-edge Fresnel + DEM samples; max with bounce)
- [x] Ballistics + WLR · scatterer table / Signature / Swerling
- [x] `RDF_RadarSensor` (SEARCH / STARE / WLR / ESM / PULSE_DOPPLER) · lock / ARM APIs
- [x] `RDF_RadarWeaponBridge` / `WeaponComponent` · `FireSolution` · `GuideRocket`
- [x] `RDF_LidarSensor` · rectangular FOV
- [x] RWR · ESM receive · anti-radiation aim
- [x] Rocket guidance sample + Lock-Fire regression
- [x] Network authority path + scale-aligned broadcast
- [x] Datalink Hub + multi-radar fusion / cross-fix + light IFF

#### Engineering / performance

- [x] Global scatterer table (grid discovery/refresh) · EmitterRegistry · Scanner split (Geometry + PhysicalDetect)
- [x] Shared SURF DEM preload + GetSurfaceY · LOS cache · multi-frame budgets · priority scan · physics reuse
- [x] SurfaceTable / Signature `.conf` (workshop) + JSON/CSV fallback

#### Tests

- [x] `AutoTestSuite` (Ballistics / DEM / Lock / Airborne / ShellFire / Perf / Play); dual-tier ideal / realistic
- [x] Standalone regressions: Stress / RWR / ESM-ARM / Rocket Lock-Fire / HeliDuel / SamEngage / Fusion; map overlay
- [x] Offline Python: ballistics / CFAR / track / EW / diffraction / fusion / MTD / heli CPA / sig-pack / coast / systems+full_sim + GA dual jobs

#### DEM / repo

- [x] V3 bake · SURF JSON publish packs (distribution-friendly) · GetSurfaceY + shared RAM / LRU · clutter + WLR ground
- [x] `.gitignore` · empty-dir cleanup · CHANGELOG

#### LiDAR (maintaining)

- [x] Point cloud / HUD / CSV / network / Bootstrap · Sensor facade

---

### Explicitly out of scope (realtime main path)

- Full-field EM / FDTD · training-grade RD map every frame · full DRFM
- Remote replacing local Trace / entity queries
- Default match-play remote coprocessor (see parking lot)
- Arguing “few entities + compressed DEM” into a training-grade in-game full chain

Product goal: **credible military sensor gameplay** (discover / lose / jam / deceive / lock) — not an EM simulator, and not pretending ideal-world accuracy is battlefield accuracy.
