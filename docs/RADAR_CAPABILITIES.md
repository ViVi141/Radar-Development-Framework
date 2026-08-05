> **Languages / 语言**: [中文](#中文) · [English](#english)

## 中文

# RDF 雷达 — 能力边界与可做事项

本文说明游戏内雷达**现在是什么**、相对现实电磁波雷达**缺什么**、在 Arma / Enforce 约束下**还能做什么**。  
实现细节见 [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)；对外契约见 [RADAR_API.md](RADAR_API.md)；排期以 [TODO.md](../TODO.md) 为准；离线原型见 [tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)。

---

### 1. 架构定位（不是什么）

| 误解 | 实际情况 |
|------|----------|
| 离线数字孪生算完再回灌游戏 | **否**。游戏内 Enforce 实时跑检测；`tools/dem/` 是同约定的离线原型/调参，不驱动游戏内检出结果。 |
| 真正的电磁波传播（场求解 / FDTD） | **否**。是功率与检测链路的工程近似，不是波在空间里传。 |
| 训练级雷达信号处理整链 | **否**。没有完整「波形 → RD 图 → CFAR → 航迹」实时链；但已有测量合成、量测关联、热噪声填空与 Swerling/方位 RCS。 |
| 直接把实体坐标当雷达读数 | **否（默认）**。实体只作散射体；输出为量化+噪声的量测 plot。 |

一句话：适合做**可玩、带物理门限与测量不确定性的传感器玩法**；不适合当电磁仿真器或训练级雷达孪生。

---

### 2. 当前已能做到的效果

#### 检测与物理门限

- 扇区 / 可选机械扫描内，发现**载具、炮弹、主动辐射源**（仅作散射体输入）
- **ESM 模式**（`RDF_RADAR_MODE_ESM`）：仅辐射源；单向 Friis 侦收功率；平台静默不登记发射
- **反辐射瞄点 API**：`GetArmAim` / `LockArmTrackId`（关辐射丢锁）；不含导弹 prefab
- **RWR**：被搜索 / 跟踪 / 锁定告警（`RDF_RadarRwr`）；无专用 UI，模组读 API
- 通视用 `TraceMove`（`RDF_RadarScanGeometry`：`ANY_CONTACT` + `ExcludeArray` + 复用 TraceParam + 起点出壳）；遮挡时可选 **NLOS 地面反射弱检** + **单刃绕射**（DEM/`GetSurfaceY` 沿程，`/diff`）；可选 **DEM 柱 span 顶**（`m_EnableDemSpanOcclusion`，默认关，非 SURF）
- 可选粗 RD 图（`m_EnableCoarseRd`，默认关）与多雷达共用 discovery focus 调度
- 硬件参数 → 雷达方程、多普勒、MTI（Two/ThreePulse + 可选参差消盲 / MTD bank）、处理增益、SNR 门限（辐射源在 `m_EnableEsmReceive` 下用 ESM 方程）
- DEM σ⁰ **地面杂波**进入噪声分母（ESM 路径跳过）；可选 clutter-map EMA
- **航迹 coast**：弹道速度更新 + 门限随 miss 增长 + Doppler-null/盲速软 miss
- **测量合成**：距离门中心 + 波束角抖动 + 多普勒反解径向速度（SNR 越低越抖）
- **测量噪声/偏差可调**：`SetMeasurementNoise` / `MeasNoiseScale`；下游可 override `RDF_RadarMeasurementModel`
- **大气 / 降雨 / 天气驱动损耗**：简化模型，按需 `EnableAtmosphericPathLoss`
- **LOS 双射线多径** + **4/3 地球折射** + **PRF 距离/多普勒模糊折叠**（无波形仿真；按需 Enable*；WLR 跳过多普勒折叠）
- **极化失配**标量（`Hardware.m_PolarizationFactor`）
- **无理想/逼真双档**：需要什么开什么；AutoTest 用 `StabilizeForRegression()`
- **散射体表拟真输入**：姿态方位+俯仰 RCS（AABB 投影）、Swerling 起伏、AGL、DEM 缓存、辐射源射频摘要；烘焙表优先读 `Signatures/*.conf`（工坊），profile CSV 回退
- DEM / 地表：`GetSurfaceY` + SURF JSON；电磁参数优先 `RadarData/SurfaceTable.conf`
- **EW 效果栈**：噪声压制 + 欺骗假目标
- **简化 CFAR**：粗栅格 CA / GO / SO（`m_CfarMode`）；空单元可填热噪声
- **联机权威路径**：`RDF_RadarNetworkComponent`（RplProp 关键配置 + **Reliable** 航迹/WLR/锁摘要 + 可选 **Unreliable** 有上限 plots + 发射态；类型化 `array<int/float>` Rpc，非 CSV）
- **站间数据链 / 多雷达融合**：`RDF_RadarDatalinkHub` + `RDF_RadarFusionService`（确认航迹关联 + 双站交会）；轻量 IFF 字段
- **欺骗扩展**：静态假点 + 拖距 / 角闪烁 / 间歇假点 Effect
- **反炮兵呈现**：PPI 发射/落点告警圈；世界空间地面环

#### 显示与测试

- PPI HUD：默认为匿名量测点；假目标白色；NLOS 青色
- 量测驱动 α-β 跟踪 + `PredictAt` 外推（最近邻波门；匿名/假目标可进跟踪）
- **锁定层** `RDF_RadarLockManager`：SEARCH → 截获 → 跟踪 → coast + `GetLockedTarget`
- AutoTest：`RDF_RadarAutoTestSuite.StartAll()`（各测试自管通道）；套件 7 项 + 独立 Stress / RWR / ESM-ARM / Rocket / HeliDuel / SamEngage / Fusion
- 单项：DEM / Ballistics / ShellFire / Airborne / Lock / Perf / Play
- 压测（独立）：`RDF_RadarStressAutoTest`（重负载 soak）
- DEM/SURF 默认可全图预载入 RAM（`m_DemPreloadAll`，HUD 显示 `SURF RAM`）
- **公共门面** `RDF_RadarSensor`（SEARCH / STARE / WLR / ESM → Plots / Tracks / Lock / ARM / RWR），见 [RADAR_API.md](RADAR_API.md)

#### 观感上「像雷达」的部分

- 有扫到、漏检、弱检、杂波压目标、干扰抬噪声、测距晃动的感觉
- 远距 / 低 SNR 时位置抖动、航迹漂、丢批
- **不是**地图全亮；远、挡、低 SNR、被压制会丢

---

### 3. 相对现实电磁波雷达还缺什么

#### 传播与回波

- 真多径级联仍缺；**LOS 双射线 + NLOS bounce/单刃**已接（非多刃/UTD）
- **大气折射（4/3 地球）**已接简化版（地平线软衰减 + 俯仰偏置）；衰减与雨雾损耗见 §2
- 距离门上的杂波谱（不只是 σ⁰ 功率进噪声；离线 PSD 标定可回灌 leakage）
- 目标起伏（Swerling）已接简化版（0–4）；完整极化闪烁谱仍缺（有 `m_PolarizationFactor` 失配标量）

#### 信号与检测

- 训练级 RD 图 / 全脉冲 CFAR（当前为粗栅格 CA/GO/SO + 热填空）— **刻意不做波形仿真**
- 完整方向图仍简化（高斯主瓣）；副瓣以 EW 耦合 / `m_SidelobeLevelDb` 近似
- **距离/速度模糊折叠**已接（测量层 PRF fold；WLR 跳过多普勒折叠）
- STAP 等更认真的杂波抑制（现有经典 Two/ThreePulse + 参差消盲 + MTD bank；非自适应阵列）

#### 跟踪与系统

- 多假设关联 / JPDA（现为单假设最近邻；跨站融合为门限关联非 JPDA）
- ~~多雷达组网、IFF、数据链融合~~ **轻量站间 Hub + 融合已接**；密码学 IFF / 完整数据链协议仍缺
- 搜索 → 截获 → 跟踪的**资源管理**；武器制导有示例（`RDF_RadarRocketGuidance`），通用武器对接仍靠模组

#### 电子战

| 类型 | 状态 |
|------|------|
| 噪声压制 | **已有**（抬接收噪声 → 降 SNR） |
| 欺骗 / DRFM / 拖距拖速 | **已接**：静态假点 + 拖距 / 角闪烁 / 间歇；非完整 DRFM |
| 完整 ECCM | **无** |

#### 引擎与联机现实

- 候选为散射体表 + Sphere/Active；仍非体素体积搜索
- 已有服务器权威同步路径；轻量多雷达融合经 DatalinkHub 已落地；带宽自适应（Reliable 摘要 / Unreliable plots / 上限降频 / 兴趣半径）已接第一版

---

### 4. 在现有约束下「能做到」的增强

与 [TODO.md](../TODO.md) 延后项对齐：

#### 游戏内（推荐）

1. **锁定层对接武器**（见 [VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md)）
2. ~~更细多径/绕射近似~~ **单刃绕射已落地**；DEM span / 多刃仍场景驱动
3. ~~联机：多雷达融合~~ **已落地**；~~带宽自适应~~ **第一版已接**（摘要/plots 分流 + 上限降频）

#### 离线（Python + DEM）

- **优先**：CFAR / track 单测 + golden，防逼真通道漂移  
- **全能力编排**：`tools/dem/rdf_radar_full_sim.py`（无 DEM；锁/ESM/RWR/CFAR/EW/融合/Network 策略等）；矩阵见 `tools/dem/RADAR_FRAMEWORK.md`
- 更认真的传播、杂波谱、标定表再喂回游戏参数  
- `mass_battle_sim` 拆分（离线大改时）

#### 不宜作为游戏内实时主路径

- FDTD / 全场电磁波  
- 训练级信号处理整链每帧跑满  

---

### 5. 建议优先级（只选三条时）

重评估（2026-07-29，与 [TODO.md](../TODO.md) 对齐）：

1. ~~**Python CFAR/track golden + 最小 CI**~~ **已完成**（`test_rdf_radar_cfar.py` / `test_rdf_radar_track.py` + GitHub Actions）  
2. **锁定层对接武器**（示例制导已有；产品武器仍需模组接入）  
3. **场景驱动**：DEM span / 多刃 — 勿与远程算力抢位  

目标产品形态：**可信的军武传感器玩法**（发现 / 丢失 / 压制 / 欺骗 / 锁定），而不是真实雷达仿真器。

---

### 6. 相关入口

- 游戏内流水线：[RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)  
- 公共 API：[RADAR_API.md](RADAR_API.md)  
- DEM：[DEM.md](DEM.md)  
- 待办清单：[TODO.md](../TODO.md)  
- 噪声干扰用法示例见 `RADAR_GAME_FRAMEWORK.md` 中 Optional EW noise 一节  

---

## English

# RDF Radar — Capability Boundaries & What Is Feasible

This document explains **what the in-game radar is now**, **what it lacks** versus real EM radar, and **what is still feasible** under Arma / Enforce constraints.  
Implementation details: [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md); public contract: [RADAR_API.md](RADAR_API.md); schedule: [TODO.md](../TODO.md); offline prototype: [tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md).

---

### 1. Architectural positioning (what it is not)

| Misconception | Reality |
|---------------|---------|
| Offline digital twin computes then feeds results back into the game | **No**. In-game Enforce runs detection in real time; `tools/dem/` is an offline prototype / tuning tool under the same conventions and does not drive in-game detections. |
| True EM wave propagation (field solvers / FDTD) | **No**. It is an engineering approximation of power and detection chains, not waves propagating in space. |
| Full training-grade radar signal-processing pipeline | **No**. There is no complete real-time “waveform → RD map → CFAR → track” chain; there is measurement synthesis, measurement association, thermal-noise fill, and Swerling / aspect RCS. |
| Entity world coordinates used directly as radar readings | **No (default)**. Entities are scatterers only; output is quantized, noisy measurement plots. |

In short: suited for **playable sensor gameplay with physical thresholds and measurement uncertainty**; not suited as an EM simulator or training-grade radar twin.

---

### 2. What it can already do

#### Detection & physical thresholds

- Within a sector / optional mechanical scan, discover **vehicles, shells, and active emitters** (scatterer inputs only)
- **ESM mode** (`RDF_RADAR_MODE_ESM`): emitters only; one-way Friis receive power; platform stays silent (no transmit registration)
- **Anti-radiation aim API**: `GetArmAim` / `LockArmTrackId` (lock drops when emission stops); no missile prefab included
- **RWR**: search / track / lock warnings (`RDF_RadarRwr`); no dedicated UI — mods read the API
- LOS via `TraceMove` (`RDF_RadarScanGeometry`: `ANY_CONTACT` + `ExcludeArray` + reused TraceParam + start clearance); optional **NLOS ground-bounce** + **single knife-edge diffraction** when occluded (DEM/`GetSurfaceY` samples, `/diff`); optional **DEM column-span tops** (`m_EnableDemSpanOcclusion`, default off, non-SURF)
- Optional coarse RD map (`m_EnableCoarseRd`, default off) and shared multi-radar discovery focus scheduling
- Hardware params → radar equation, Doppler, MTI / optional **MTD filter bank**, processing gain, SNR threshold (emitters use the ESM equation when `m_EnableEsmReceive` is on)
- Default MTI remains two-pulse (`RDF_MTI_TWOPULSE`); `RDF_MTI_THREE_PULSE` uses sin⁴; classic paths score rotor spectrum + optional PRF-set max de-blind; `RDF_MTI_MTD_BANK` puts clutter in the near-zero bin and picks the best Doppler channel
- DEM σ⁰ **ground clutter** enters the noise denominator (skipped on the ESM path); optional runtime clutter-map EMA
- **Measurement synthesis**: range-gate center + beam-angle jitter + Doppler-derived radial velocity (more jitter at lower SNR)
- **Tunable measurement noise / bias**: `SetMeasurementNoise` / `MeasNoiseScale`; downstream can override `RDF_RadarMeasurementModel`
- **Atmosphere / rain / weather-driven loss**: simplified; opt-in via `EnableAtmosphericPathLoss`
- **LOS two-ray multipath** + **4/3-Earth refraction** + **PRF range/Doppler ambiguity fold** (no waveform sim; opt-in Enable*; WLR skips Doppler fold)
- **Polarization mismatch** scalar on hardware (`m_PolarizationFactor`)
- **No ideal/realistic tiers**: enable what you need; AutoTest uses `StabilizeForRegression()`
- **Scatterer-table fidelity inputs**: aspect azimuth + elevation RCS (AABB projection), Swerling fluctuation, AGL, DEM cache, emitter RF summary; baked tables prefer `Signatures/*.conf` (workshop), profile CSV fallback
- DEM / surface: `GetSurfaceY` + SURF JSON; EM params prefer `RadarData/SurfaceTable.conf`
- **EW effect stack**: noise jamming + deceptive false targets
- **Simplified CFAR**: coarse-grid CA / GO / SO (`m_CfarMode`); empty cells can be filled with thermal noise
- **MP authority path**: `RDF_RadarNetworkComponent` (RplProp antenna config + **Reliable** track/WLR/lock summary + optional **Unreliable** capped plots + transmit state; typed `array<int/float>` Rpc, not CSV)
- **Station datalink / multi-radar fusion**: `RDF_RadarDatalinkHub` + `RDF_RadarFusionService` (confirmed-track association + dual-station cross-fix); light IFF field
- **Deception extensions**: static false plots + range-gate pull-off / angular scintillation / intermittent false-plot Effects
- **Counter-battery presentation**: PPI launch / impact alert circles; world-space ground rings

#### Display & testing

- PPI HUD: anonymous measurement points by default; false targets white; NLOS cyan
- Measurement-driven α-β tracking + `PredictAt` extrapolation (nearest-neighbor gate; anonymous / false targets can enter tracking)
- **Lock layer** `RDF_RadarLockManager`: SEARCH → acquire → track → coast + `GetLockedTarget`
- AutoTest: `RDF_RadarAutoTestSuite.StartAll()` (each test owns channel flags); suite of 7 + standalone Stress / RWR / ESM-ARM / Rocket / HeliDuel / SamEngage / Fusion
- Singles: DEM / Ballistics / ShellFire / Airborne / Lock / Perf / Play
- Stress (standalone): `RDF_RadarStressAutoTest` (heavy soak)
- DEM/SURF may preload full map into RAM by default (`m_DemPreloadAll`, HUD shows `SURF RAM`)
- **Public façade** `RDF_RadarSensor` (SEARCH / STARE / WLR / ESM → Plots / Tracks / Lock / ARM / RWR); see [RADAR_API.md](RADAR_API.md)

#### Parts that “feel like radar”

- Sense of detection, miss, weak detect, clutter burying targets, jamming raising noise, ranging wobble
- At long range / low SNR: position jitter, track drift, dropouts
- **Not** a fully lit map; distance, occlusion, low SNR, and jamming cause losses

---

### 3. What is still missing vs real EM radar

#### Propagation & returns

- Cascaded multipath still missing; **LOS two-ray + NLOS bounce / single knife-edge shipped** (not multi-edge/UTD)
- **Atmospheric refraction (4/3 Earth)** simplified (horizon soft factor + elevation bias); attenuation / rain-fog — see §2
- Clutter spectrum on range gates (not only σ⁰ into noise; offline PSD calib can feed leakage)
- Target fluctuation (Swerling) simplified (0–4); full polarization scintillation spectra still missing (`m_PolarizationFactor` scalar exists)

#### Signal & detection

- Training-grade RD maps / full-pulse CFAR (coarse-grid CA/GO/SO + thermal fill) — **waveform sim intentionally out of scope**
- Full antenna pattern still simplified (Gaussian mainlobe); sidelobes via EW coupling / `m_SidelobeLevelDb`
- **Range/velocity ambiguity fold** shipped (measurement-layer PRF fold; WLR skips Doppler fold)
- More serious clutter rejection such as STAP (classic Two/ThreePulse + stagger de-blind + MTD bank are present; not adaptive array)

#### Tracking & systems

- Multi-hypothesis association / JPDA (currently single-hypothesis nearest neighbor; cross-site fusion is gated association, not JPDA)
- ~~Multi-radar networking, IFF, datalink fusion~~ **light station Hub + fusion shipped**; crypto IFF / full datalink protocol still missing
- **Resource management** for search → acquire → track; weapon guidance has an example (`RDF_RadarRocketGuidance`); general weapon integration still depends on the mod

#### Electronic warfare

| Type | Status |
|------|--------|
| Noise jamming | **Present** (raises receive noise → lowers SNR) |
| Deception / DRFM / RGPO–VGPO | **Hooked**: static false plots + range pull-off / angular scintillation / intermittent; not a full DRFM |
| Full ECCM | **None** |

#### Engine & multiplayer reality

- Candidates are scatterer tables + Sphere/Active; still not voxel volume search
- Server-authoritative sync path exists; light multi-radar fusion via DatalinkHub shipped; bandwidth adaptation v1 shipped (Reliable summary / Unreliable plots / caps / interest)

---

### 4. Enhancements that are feasible under current constraints

Aligned with deferred items in [TODO.md](../TODO.md):

#### In-game (recommended)

1. **Lock layer → weapon integration** (see [VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md))
2. ~~Finer multipath / diffraction~~ **single knife-edge shipped**; DEM span / multi-edge still scenario-driven
3. ~~MP: multi-radar fusion~~ **shipped**; ~~bandwidth adaptation~~ **v1 shipped** (summary/plots split + caps/throttle)

#### Offline (Python + DEM)

- **First**: CFAR / track unit tests + golden to pin the realistic channel  
- **Full-capability orchestration**: `tools/dem/rdf_radar_full_sim.py` (no DEM; lock/ESM/RWR/CFAR/EW/fusion/Network policy); matrix in `tools/dem/RADAR_FRAMEWORK.md`
- More serious propagation, clutter-spectrum, calibration tables fed back into game parameters  
- Split `mass_battle_sim` when offline needs a big rewrite  

#### Not suitable as the in-game real-time main path

- FDTD / full-field EM waves  
- Running a full training-grade signal-processing chain every frame  

---

### 5. Suggested priority (if picking only three)

Reprioritized 2026-07-29 (aligned with [TODO.md](../TODO.md)):

1. ~~**Python CFAR/track golden + minimal CI**~~ **done** (`test_rdf_radar_cfar.py` / `test_rdf_radar_track.py` + GitHub Actions)  
2. **Lock layer → weapon integration** (example guidance exists; product weapons still need mod wiring)  
3. **Scenario-driven**: DEM span / multi-edge — do not let remote compute jump the queue

Target product shape: **credible military-sensor gameplay** (detect / lose / jam / deceive / lock), not a real radar simulator.

---

### 6. Related entry points

- In-game pipeline: [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)  
- Public API: [RADAR_API.md](RADAR_API.md)  
- DEM: [DEM.md](DEM.md)  
- Todo list: [TODO.md](../TODO.md)  
- Noise-jamming usage examples: Optional EW noise section in `RADAR_GAME_FRAMEWORK.md`  
