# 载具雷达扫描、锁定与武器打击 — 实现指南

本文档面向目标：**让载具和武器具备扫描/锁定能力 — 扫描载具、锁定载具、武器锁定并打击**。

RDF 现已提供：

- **雷达**：公共门面 `RDF_RadarSensor`（SEARCH / STARE / WLR），输出
  `RDF_RadarTarget` plot 与 `RDF_RadarTrack` 航迹；实体入口为
  `RDF_RadarComponent`。见 [RADAR_API.md](RADAR_API.md)。
- **LiDAR**：`RDF_LidarScanner` 射线点云，仍可用于近距感知或备用扫描。
- **锁定层**：`RDF_RadarLockManager`（内置于 Sensor）已提供
  `SEARCH → ACQUIRING → TRACKING → COAST` 状态机、自动/手动选锁、断锁与瞄点续航。

现在只剩**武器制导对接**需模组侧实现；本文给出最小路径与分工。

---

## 一、目标与实现分工

| 目标 | RDF 已有 | 需要你补充 |
|------|----------|------------|
| 载具能扫描载具 | ✅ `RDF_RadarSensor` / `RDF_RadarComponent`；或 `RDF_LidarScanner.Scan()` | 将扫描挂到载具，配置模式/量程/硬件 |
| 锁定载具 | ✅ `RDF_RadarLockManager`：自动/手动选锁、断锁、coast、`GetLockedTarget` | 按需调过滤/门限，或改手动选锁 |
| 武器锁定并打击 | ⚠️ RDF 有轨迹预测/WLR + 锁定目标，但不含武器制导 | **武器/导弹脚本**：每帧读 `GetLockedTarget` 驱动制导 |

---

## 二、推荐最小实现路径

### 2.1 扫描：只关心载具时

- **方案 A（推荐，雷达）**：挂 `RDF_RadarComponent`，通过
  `GetSensor().GetPlots()` 或 `GetTracks()` 读取结果，过滤载具类型后按
  距离、SNR 或航迹质量选锁。
- **方案 B（LiDAR 射线优先）**：使用 `RDF_LidarScanner`，配置为只打实体，扇区用锥形/扫掠策略；按 `m_Distance` 自动锁。
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

### 2.3 武器打击：与 Reforger 武器系统对接

RDF 的弹道模块用于轨迹预测和 WLR，不负责武器制导。武器“锁定并打击”
仍需要在你的**武器/发射物脚本**中：

1. **获取目标**：从上述锁定管理器取 `GetLockedTarget()` → `IEntity` 或世界坐标。
2. **制导**：若为制导弹，每帧用当前弹位与目标位置/速度计算朝向或加速度，驱动弹体转向目标；若为非制导武器，可用锁定目标做瞄准辅助（准星偏移或火控解算）。
3. **引信/命中**：若引擎支持“命中检测”，用目标实体或目标 AABB 判断是否命中；否则用距离/半径判定。

Reforger 的 BaseGame 中若有现成的制导导弹 API（如设置“目标实体”），只需把锁定管理器返回的 `IEntity` 传给它即可；若无，则需自己在弹体组件里每帧朝 `GetLockedTarget()` 的位置或预测位置推进。

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
