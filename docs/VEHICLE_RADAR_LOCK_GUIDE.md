> **Languages / 语言**: [中文](#中文) · [English](#english)

# 载具雷达扫描、锁定与武器打击 — 实现指南

---

## 中文

本文档面向目标：**让载具和武器具备扫描/锁定能力 — 扫描载具、锁定载具、武器锁定并打击**。

RDF 现已提供：

- **雷达**：公共门面 `RDF_RadarSensor`（SEARCH / STARE / WLR / ESM），输出
  `RDF_RadarTarget` plot 与 `RDF_RadarTrack` 航迹；实体入口为
  `RDF_RadarComponent`。见 [RADAR_API.md](RADAR_API.md)。
- **LiDAR**：`RDF_LidarSensor` / `RDF_LidarScanner` 射线点云，近距感知或备用扫描。
- **锁定层**：`RDF_RadarLockManager`（内置于 Sensor）已提供
  `SEARCH → ACQUIRING → TRACKING → COAST` 状态机、自动/手动选锁、断锁与瞄点续航。
- **火控桥**：`RDF_RadarWeaponBridge` / `RDF_RadarWeaponComponent` 把锁定变成
  `RDF_RadarFireSolution`（发射门控 + 中段上行）。
- **制导示例**：`RDF_RadarRocketGuidance` + `RDF_RadarRocketLockFireAutoTest`
  （Hydra70 BOOST/MIDCOURSE/TERMINAL）可作为武器对接参考；正式武器仍需模组侧接入 prefab。

---

## 一、目标与实现分工

| 目标 | RDF 已有 | 需要你补充 |
|------|----------|------------|
| 载具能扫描载具 | ✅ `RDF_RadarSensor` / `RDF_RadarComponent`；或 `RDF_LidarScanner.Scan()` | 将扫描挂到载具，配置模式/量程/硬件 |
| 锁定载具 | ✅ `RDF_RadarLockManager`：自动/手动选锁、断锁、coast、`GetLockedTarget` | 按需调过滤/门限，或改手动选锁 |
| 武器锁定并打击 | ✅ `RDF_RadarWeaponBridge` / `WeaponComponent` + 火箭制导示例；不含通用武器 prefab | **武器/导弹脚本**：读 `FireSolution` 或 `GuideRocket`（可参考 Lock-Fire 回归） |

---

## 二、推荐最小实现路径

### 2.1 扫描：只关心载具时

- **方案 A（推荐，雷达）**：挂 `RDF_RadarComponent`，通过
  `GetSensor().GetPlots()` 或 `GetTracks()` 读取结果，过滤载具类型后按
  距离、SNR 或航迹质量选锁。
- **方案 B（LiDAR 射线优先）**：使用 `RDF_LidarSensor`（`FORWARD_RECT` / `ENTITIES_NEAR`）或底层 `RDF_LidarScanner`；按 `GetClosestHit()` / `m_Distance` 自动锁。
- **方案 C（底层扩展）**：直接扩展 `RDF_RadarScanner` 或第五节 Query +
  Trace；仅在公共 Sensor 无法满足需求时采用。

### 2.2 锁定层（已内置 `RDF_RadarLockManager`）

锁定层已随 Sensor 提供，无需自研。它维护单一“当前锁定目标”，并实现
`SEARCH → ACQUIRING → TRACKING → COAST` 状态机（丢批时按速度外推瞄点）。

配置与读取：

```c
RDF_RadarLockManager lockMgr = radarComponent.GetLockManager();
lockMgr.SetAutoAcquire(true);              // 自动锁最近合规目标
lockMgr.SetTypeFilter(true, false, false); // 仅载具
lockMgr.SetMaxLockRange(4000.0);           // 0 = 用扫描量程
lockMgr.SetLockSector(0.0);                // 0 = 不做扇区门
lockMgr.SetAcquireHits(2);                 // 进入 TRACKING 所需帧数
lockMgr.SetCoastMaxSec(2.0);               // 丢批后保持时间

// 手动选锁（HUD blip）：lockMgr.LockTrackId(id); lockMgr.Unlock();

// 武器每帧读取：
IEntity target;
vector aimPos;
if (radarComponent.GetLockedTarget(target, aimPos))
{
    // 用 aimPos 驱动制导；COAST 期间为外推瞄点
}
```

断锁规则（自动）：目标航迹消失超过 `CoastMaxSec`、超出最大锁距、或超出扇区门。

选锁默认取**最近**的已确认合规航迹；需要不同策略时可读
`GetEligibleTrackIds(...)` 自行挑选后 `LockTrackId`。

回归示例：`RDF_RadarLockAutoTest.Start()`。

### 2.2.1 ESM / 反辐射瞄点

被动侦收模式与反辐射瞄点（无武器 prefab，仅 API）：

```c
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM, 64);

IEntity ent;
vector aim;
bool radiating;
if (sensor.GetArmAim(ent, aim, radiating) && radiating)
{
    // 制导读 aim；辐射源 SetEmitting(false) 后自动丢锁
}

sensor.LockArmTrackId(trackId); // 仅辐射源航迹
// RDF_RadarEmitterRegistry.IsEmitting(ent);
```

单独回归：`RDF_RadarEsmArmAutoTest.Start()`。

### 2.2.2 RWR（被照射 / 被锁定）

主动雷达扫描后会把威胁写入 `RDF_RadarRwr`。载具侧每帧查询：

```c
if (RDF_RadarRwr.HasLockWarning(owner))
{
    // 锁定告警音 / HUD
}
else if (RDF_RadarRwr.HasSearchWarning(owner))
{
    // 搜索照射提示
}
```

`RDF_RadarComponent.HasRwrLockWarning()` 等价于对本实体查询。  
回归：`RDF_RadarRwrAutoTest.Start()`。

### 2.3 武器打击：与 Reforger 武器系统对接

RDF 提供**火控桥**与**示例制导**：

- **`RDF_RadarWeaponBridge` / `RDF_RadarWeaponComponent`**：从 Sensor 锁定读出
  `RDF_RadarFireSolution`（瞄点、速度、航迹 ID、是否允许发射、ARM 标志）。
- **`RDF_RadarRocketGuidance`**：Hydra70 风格 BOOST → MIDCOURSE → TERMINAL PN
  （`RDF_RadarRocketLockFireAutoTest` 回归）。

不含通用武器 prefab。正式武器在你的**武器/发射物脚本**中：

```c
// 载具上同时挂 RDF_RadarComponent + RDF_RadarWeaponComponent
RDF_RadarWeaponComponent fireCtrl = RDF_RadarWeaponComponent.FindOn(vehicle);
RDF_RadarFireSolution sol = new RDF_RadarFireSolution();
if (fireCtrl && fireCtrl.TryGetFireSolution(sol) && sol.m_CanAuthorizeFire)
{
    // 发射；飞行中：
    // fireCtrl.GuideRocket(rocket, guideState, dt, nowS);
}

// 或不挂组件：
RDF_RadarWeaponBridge.TryGetFireSolutionFromOwner(vehicle, sol, true);
```

1. **获取目标**：`TryGetFireSolution` → `m_AimPos` / `m_Target`；ESM/ARM 时
   `m_IsArm` + `m_Radiating`（也可直接 `GetArmAim`）。
2. **制导**：`GuideRocket` 或自写 PN；非制导武器用瞄点做瞄准辅助。
3. **引信/命中**：用目标实体 / AABB / 距离半径判定。

独立回归：`RDF_RadarRocketLockFireAutoTest.Start()`；完整机战脚本：
`RDF_RadarHeliDuelAutoTest.Start()`（蓝/红 Mi-8 → 开火 → 拦截来袭 Hydra；不进 `StartAll`）。

---

## 三、需要用的 RDF 类（小结）

- **扫描**：`RDF_RadarSensor`；用 `ConfigureMode` 或自定义
  `RDF_RadarSettings`，实体挂载则使用 `RDF_RadarComponent`。
- **过滤/跟踪**：从 `GetPlots()` 按 `ERDF_RadarTargetType` 过滤，或从
  `GetTracks()` 读取平滑航迹。
- **显示（可选）**：`RDF_RadarAutoRunner` / `RDF_RadarHUD` 仅用于演示；
  产品代码可只持有 Sensor。

---

## 四、可选简化

- 若只做“锥体内最近一辆车”的自动锁，可减少射线数（如 64～128），扇区用 30° 锥即可，扫描间隔 0.1～0.2 秒通常足够。
- 若不需要多普勒/SNR，用 LiDAR 扫描器 + 实体过滤即可，逻辑更简单。
- 锁定管理器可先做“单目标锁”，后续再扩展多目标跟踪或“锁 A/B 切换”。

按上述路径，可实现“载具扫描 → 锁定载具 → 武器锁定并打击”的闭环；RDF LiDAR 或自实现的“实体优先”模块负责“谁可见、距离与实体”，你负责“选谁当锁、武器怎么跟过去”。

---

## 五、简化方案：先查实体、再射线判可见（“实体优先”雷达）

你描述的是一种与当前 LiDAR/雷达**相反**的检测顺序，更适合“只关心载具”的场景，且往往更省。

### 5.1 两种方式对比

| 方式 | 顺序 | 成本量级 | 适用 |
|------|------|----------|------|
| **现有 LiDAR** | **先射线、后结果**：按方向打多条射线，打中谁就检测到谁，“先检测才知道有什么”。 | O(射线数) 次 TraceMove | 需要点云/方位分辨率、地形回波、多目标无先验 |
| **简化雷达（你的思路）** | **先实体、后可见**：用引擎在扫描半径内筛出“可能被扫到的实体”，再对每个候选做**一次射线**判断是否可见。 | O(范围内实体数) 次 TraceMove | 只关心离散目标（载具等），范围内容体数远小于射线数时更省 |

也就是说：**激光雷达是“先打射线才知道有什么”；简化雷达是“先知道范围内有哪些实体，再射线判断谁可见”。**

### 5.2 实现思路（Reforger/Enfusion）

1. **范围内实体**：用 `BaseWorld`（通过 `GetGame().GetWorld()` 取得）的实体查询，例如：
   - `QueryEntitiesBySphere(origin, range)`：扫描半径内所有实体；
   - `QueryEntitiesByAABB(boxMin, boxMax)`：轴对齐包围盒内实体；
   - `QueryEntitiesByOBB(...)`：定向包围盒，可用于扇区/锥体（若需按朝向限制扫描角）；
   - 若引擎只提供球或 AABB，可在球/盒内再按方位角、俯仰角过滤，只保留扇区内的候选。
2. **过滤**：排除自己、排除不需要的类型（如仅保留载具/飞机），得到候选列表。
3. **可见性**：对每个候选做射线判是否被遮挡（见下）；若判为可见，加入检测结果。
4. **输出**：得到“可见实体 + 距离 + 命中点”，格式可与 RDF 的 sample 对齐（每条一个 `m_Entity`、`m_Distance`、`m_HitPos`），后续锁定、HUD、武器对接与第二节一致。

这样，**射线数 = 范围内候选实体数 × 每实体射线数**（见下）；在载具密度不高时仍通常少于“全扇区多射线”方案。

### 5.2.1 部分露出时的可见性（“只露出一部分也能被扫到”）

若只用**一条射线**从雷达打到目标**中心**，当目标只露出一部分（例如车体大半被墙/地形挡住，只有一小块朝向雷达）时，这条中心射线会先打到墙或地形，从而被判为“不可见”，产生**漏检**。现实中雷达只要能看到目标任意一块有效反射面，就可以检测到。

做法：**对每个候选实体做多条射线**，而不是一条：

- **采样点**：在目标的 AABB 上取若干点，例如 8 个角点、或 6 个面心、或“中心 + 朝向雷达的若干面心”。从雷达原点向这些点各打一条射线。
- **判定**：若**任意一条**射线先命中**该实体**（即 `TraceMove` 返回的命中实体等于当前候选），且命中点在该实体的几何上（或命中距离在合理范围内），则判为“可见”；取其中**最近一次命中**的 `hitPos`、`distance` 作为该目标的检测结果（或取中心射线若可见，否则取最近可见点）。
- **成本**：每实体 3～8 条射线是常见折中（例如 4 条：中心 + 3 个面心，或 8 条：AABB 角点）。总 Trace 数 = 候选实体数 × 每实体射线数；例如 20 个候选 × 5 条 ≈ 100 次，仍远少于 512 条全扇区扫描，同时能覆盖“只露出一部分”的情况。

若希望进一步省射线，可先打**一条射线到实体中心**；若已可见则不再多打；若被挡，再对同一实体补打若干条到 AABB 角点/面心（“先单射线、再按需多射线”）。

### 5.3 与 RDF 的关系

- 此方案**不依赖** RDF 的“多射线扫描”，可单独实现为**轻量扫描模块**（例如“实体优先”工具类或组件）。
- 若希望复用 RDF 的 HUD 与锁定逻辑，只需让该模块输出与 `RDF_LidarSample` 兼容的列表（每条：`m_Entity`、`m_Hit`=true、`m_Distance`、`m_HitPos`、`m_Dir` 等），即可接入 `RDF_LidarHUD.FeedSamples()` 与同一套锁定管线。

总结：**“扫描半径内引擎筛实体 + 按实体做射线判可见”** 即简化版检测：先知道“可能有什么”，再判断“谁真的被看到”，适合载具/武器锁定场景。

---

## English

# Vehicle radar scan, lock & weapon engagement — implementation guide

This guide targets: **give vehicles and weapons scan/lock capability — scan vehicles, lock vehicles, weapon lock and engage**.

RDF already provides:

- **Radar**: public facade `RDF_RadarSensor` (SEARCH / STARE / WLR / ESM), outputting
  `RDF_RadarTarget` plots and `RDF_RadarTrack` tracks; entity entry point is
  `RDF_RadarComponent`. See [RADAR_API.md](RADAR_API.md).
- **LiDAR**: `RDF_LidarSensor` / `RDF_LidarScanner` ray point clouds for short-range sensing or backup scan.
- **Lock layer**: `RDF_RadarLockManager` (built into Sensor) already provides the
  `SEARCH → ACQUIRING → TRACKING → COAST` state machine, auto/manual lock selection, break-lock, and aim-point coast.
- **Fire-control bridge**: `RDF_RadarWeaponBridge` / `RDF_RadarWeaponComponent` turn lock into
  `RDF_RadarFireSolution` (launch gate + midcourse uplink).
- **Guidance example**: `RDF_RadarRocketGuidance` + `RDF_RadarRocketLockFireAutoTest`
  (Hydra70 BOOST/MIDCOURSE/TERMINAL) as a weapon-integration reference; production weapons still need mod-side prefab wiring.

---

## 1. Goals vs ownership

| Goal | RDF already has | You still supply |
|------|-----------------|------------------|
| Vehicle can scan vehicles | ✅ `RDF_RadarSensor` / `RDF_RadarComponent`; or `RDF_LidarScanner.Scan()` | Mount scan on the vehicle; configure mode/range/hardware |
| Lock vehicles | ✅ `RDF_RadarLockManager`: auto/manual lock, unlock, coast, `GetLockedTarget` | Tune filters/thresholds as needed, or switch to manual lock |
| Weapon lock & engage | ✅ `RDF_RadarWeaponBridge` / `WeaponComponent` + rocket guidance sample; no generic weapon prefab | **Weapon/missile scripts**: read `FireSolution` or `GuideRocket` (see Lock-Fire regression) |

---

## 2. Recommended minimal path

### 2.1 Scan: vehicles only

- **Option A (recommended, radar)**: attach `RDF_RadarComponent`, read
  `GetSensor().GetPlots()` or `GetTracks()`, filter vehicle types, then pick lock by
  range, SNR, or track quality.
- **Option B (LiDAR ray-first)**: use `RDF_LidarSensor` (`FORWARD_RECT` / `ENTITIES_NEAR`) or low-level `RDF_LidarScanner`; auto-lock via `GetClosestHit()` / `m_Distance`.
- **Option C (low-level)**: extend `RDF_RadarScanner` or section 5 Query +
  Trace; only when the public Sensor cannot meet the need.

### 2.2 Lock layer (built-in `RDF_RadarLockManager`)

The lock layer ships with Sensor; no need to reinvent it. It keeps a single “current lock” and runs
`SEARCH → ACQUIRING → TRACKING → COAST` (on miss, extrapolates aim by velocity).

Configure and read:

```c
RDF_RadarLockManager lockMgr = radarComponent.GetLockManager();
lockMgr.SetAutoAcquire(true);              // 自动锁最近合规目标
lockMgr.SetTypeFilter(true, false, false); // 仅载具
lockMgr.SetMaxLockRange(4000.0);           // 0 = 用扫描量程
lockMgr.SetLockSector(0.0);                // 0 = 不做扇区门
lockMgr.SetAcquireHits(2);                 // 进入 TRACKING 所需帧数
lockMgr.SetCoastMaxSec(2.0);               // 丢批后保持时间

// 手动选锁（HUD blip）：lockMgr.LockTrackId(id); lockMgr.Unlock();

// 武器每帧读取：
IEntity target;
vector aimPos;
if (radarComponent.GetLockedTarget(target, aimPos))
{
    // 用 aimPos 驱动制导；COAST 期间为外推瞄点
}
```

Auto break-lock: track gone longer than `CoastMaxSec`, beyond max lock range, or outside the sector gate.

Default selection is the **nearest** confirmed eligible track; for other policies, read
`GetEligibleTrackIds(...)` and call `LockTrackId`.

Regression: `RDF_RadarLockAutoTest.Start()`.

### 2.2.1 ESM / anti-radiation aim

Passive receive mode and ARM aim (API only; no weapon prefab):

```c
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM, 64);

IEntity ent;
vector aim;
bool radiating;
if (sensor.GetArmAim(ent, aim, radiating) && radiating)
{
    // 制导读 aim；辐射源 SetEmitting(false) 后自动丢锁
}

sensor.LockArmTrackId(trackId); // 仅辐射源航迹
// RDF_RadarEmitterRegistry.IsEmitting(ent);
```

Standalone regression: `RDF_RadarEsmArmAutoTest.Start()`.

### 2.2.2 RWR (illuminated / locked)

Active radar scans write threats into `RDF_RadarRwr`. Query each frame on the vehicle:

```c
if (RDF_RadarRwr.HasLockWarning(owner))
{
    // 锁定告警音 / HUD
}
else if (RDF_RadarRwr.HasSearchWarning(owner))
{
    // 搜索照射提示
}
```

`RDF_RadarComponent.HasRwrLockWarning()` is equivalent for the owner entity.  
Regression: `RDF_RadarRwrAutoTest.Start()`.

### 2.3 Weapon engagement: Reforger weapon system

RDF provides a **fire-control bridge** and **sample guidance**:

- **`RDF_RadarWeaponBridge` / `RDF_RadarWeaponComponent`**: read `RDF_RadarFireSolution`
  from Sensor lock (aim, velocity, track id, launch authorization, ARM flags).
- **`RDF_RadarRocketGuidance`**: Hydra70-style BOOST → MIDCOURSE → TERMINAL PN
  (`RDF_RadarRocketLockFireAutoTest`).

No generic weapon prefab. Production weapons live in your **weapon/projectile scripts**:

```c
// Mount RDF_RadarComponent + RDF_RadarWeaponComponent on the vehicle
RDF_RadarWeaponComponent fireCtrl = RDF_RadarWeaponComponent.FindOn(vehicle);
RDF_RadarFireSolution sol = new RDF_RadarFireSolution();
if (fireCtrl && fireCtrl.TryGetFireSolution(sol) && sol.m_CanAuthorizeFire)
{
    // Fire; in flight:
    // fireCtrl.GuideRocket(rocket, guideState, dt, nowS);
}

// Or without the component:
RDF_RadarWeaponBridge.TryGetFireSolutionFromOwner(vehicle, sol, true);
```

1. **Acquire target**: `TryGetFireSolution` → `m_AimPos` / `m_Target`; ESM/ARM sets
   `m_IsArm` + `m_Radiating` (or call `GetArmAim` directly).
2. **Guide**: `GuideRocket` or custom PN; unguided weapons can use the lock as aim assist.
3. **Fuze / hit**: decide by target entity / AABB / range radius.

Standalone regression: `RDF_RadarRocketLockFireAutoTest.Start()`; full heli duel:
`RDF_RadarHeliDuelAutoTest.Start()` (blue/red Mi-8 → fire → intercept inbound Hydra; not in `StartAll`).

---

## 3. RDF classes you need (summary)

- **Scan**: `RDF_RadarSensor`; use `ConfigureMode` or custom
  `RDF_RadarSettings`; on entities use `RDF_RadarComponent`.
- **Filter / track**: filter `GetPlots()` by `ERDF_RadarTargetType`, or read
  smoothed tracks from `GetTracks()`.
- **Display (optional)**: `RDF_RadarAutoRunner` / `RDF_RadarHUD` are demo-only;
  product code can hold Sensor alone.

---

## 4. Optional simplifications

- For “nearest vehicle in a cone” auto-lock, cut rays (e.g. 64–128), use a 30° cone, and 0.1–0.2 s scan interval is usually enough.
- Without Doppler/SNR needs, LiDAR scanner + entity filter is simpler.
- Start the lock manager as single-target lock; add multi-track or A/B switch later.

Along this path you close the loop “vehicle scan → lock vehicle → weapon lock & engage”; RDF LiDAR or a custom entity-first module answers “who is visible, range, entity”, and you answer “whom to lock, how the weapon follows”.

---

## 5. Simplified approach: query entities first, then ray for visibility (“entity-first” radar)

This is the **inverse** of the current LiDAR/radar detection order — better when you only care about vehicles, and often cheaper.

### 5.1 Comparison

| Approach | Order | Cost scale | Fit |
|----------|-------|------------|-----|
| **Existing LiDAR** | **Rays first, then results**: fire many directional rays; whoever is hit is detected — “detect to discover”. | O(ray count) TraceMove | Point clouds / angular resolution, terrain returns, multi-target with no prior |
| **Simplified radar (your idea)** | **Entities first, then visibility**: engine query for entities in scan radius, then **one ray per candidate** for visibility. | O(entities in range) TraceMove | Discrete targets (vehicles, etc.); cheaper when entity count ≪ ray count |

In short: **LiDAR discovers by shooting rays; simplified radar already knows candidates in range, then rays decide who is visible.**

### 5.2 Implementation sketch (Reforger/Enfusion)

1. **Entities in range**: query via `BaseWorld` (`GetGame().GetWorld()`), e.g.:
   - `QueryEntitiesBySphere(origin, range)`: all entities in scan radius;
   - `QueryEntitiesByAABB(boxMin, boxMax)`: axis-aligned box;
   - `QueryEntitiesByOBB(...)`: oriented box for sector/cone (heading-limited scan);
   - if only sphere/AABB exist, further filter by azimuth/elevation to keep sector candidates.
2. **Filter**: drop self and unwanted types (keep vehicles/aircraft only) → candidate list.
3. **Visibility**: ray-test occlusion per candidate (below); if visible, add to detections.
4. **Output**: “visible entity + range + hit point”, shape-compatible with RDF samples (`m_Entity`, `m_Distance`, `m_HitPos`); lock/HUD/weapon wiring matches section 2.

Then **ray count = candidates in range × rays per entity** (below); at modest vehicle density this is usually still below a full multi-ray sector scan.

### 5.2.1 Partial exposure (“still detectable if only part shows”)

A **single ray** to the target **center** fails when only part of the target faces the radar (most of the hull behind wall/terrain) — the center ray hits wall/terrain first → **false miss**. Real radar detects any useful reflecting facet.

Do **multiple rays per candidate**, not one:

- **Sample points**: several AABB points — 8 corners, or 6 face centers, or “center + faces toward the radar”. One ray from radar origin to each.
- **Decision**: if **any** ray hits **that entity** first (`TraceMove` hit entity == candidate) and the hit is on its geometry (or range is sensible), mark visible; use the **nearest** hit’s `hitPos`/`distance` (or center ray if visible, else nearest visible point).
- **Cost**: 3–8 rays/entity is a common tradeoff (e.g. 4: center + 3 faces, or 8 AABB corners). Total Trace = candidates × rays/entity; e.g. 20 × 5 ≈ 100, still far below a 512-ray sector scan, while covering partial exposure.

To save more rays: try **one center ray** first; if visible, stop; if blocked, fire extra AABB corner/face rays (“single ray first, multi-ray on demand”).

### 5.3 Relation to RDF

- This path **does not require** RDF multi-ray scanning; implement as a **lightweight scan module** (entity-first util/component).
- To reuse RDF HUD and lock logic, emit lists compatible with `RDF_LidarSample` (`m_Entity`, `m_Hit`=true, `m_Distance`, `m_HitPos`, `m_Dir`, …) and feed `RDF_LidarHUD.FeedSamples()` plus the same lock pipeline.

Summary: **engine entity query in scan radius + per-entity visibility rays** is simplified detection — know “what might be there”, then “who is actually seen” — a good fit for vehicle/weapon lock.
