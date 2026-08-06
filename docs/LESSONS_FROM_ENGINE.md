> **Languages / 语言**: [中文](#中文) · [English](#english)

# 从引擎用法中提炼的经验

---

## 中文

本文档记录从 Arma Reforger / Enfusion 引擎及官方/社区代码中总结的经验，供 RDF 及类似项目参考。

---

## 一、射线检测 (TraceMove) 基础

### 1.1 核心 API

```c
float hitFraction = world.TraceMove(param, TraceFilter);
```

- `TraceParam`：输入 Start/End、Flags、LayerMask、Exclude 等；输出 TraceEnt、SurfaceProps、TraceNorm。
- `TraceFilter(entity)`：可选回调，返回 `false` 时该实体被排除（射线穿透）。

### 1.2 常用 Flags 与字段

| 字段 | 说明 |
|------|------|
| `TraceFlags.WORLD` | 检测世界/地形 |
| `TraceFlags.ENTS` | 检测实体 |
| `TraceFlags.VISIBILITY` | 检测可见性遮挡物（粒子、烟雾等），用于视线/激光被遮蔽 |
| `TraceFlags.ANY_CONTACT` | 遇任意接触即停；**最适可见性 / LOS 测试**（官方注释） |
| `TraceParam.Exclude` | 排除单个实体 |
| `TraceParam.ExcludeArray` | 排除实体数组（与 `Exclude` **二选一**，勿同时设） |
| `TraceParam.TargetLayers` | 指定物理层（如 `EPhysicsLayerDefs.FireGeometry`） |
| `TraceParam.SurfaceProps` | 命中表面材质 (GameMaterial) |
| `TraceParam.TraceNorm` | 命中表面法线 |
| `TraceParam.TraceEnt` | 命中的实体 |

### 1.3 hitFraction 与命中位置插值

- `TraceMove` 返回 `hitFraction`（0～1），表示命中点在射线上的比例。
- **自上而下射线**（Start 在上、End 在下）：

  ```c
  surfaceY = trace.Start[1] - (trace.Start[1] - trace.End[1]) * traceCoef;
  ```

- **SnapToGeometry 写法**（Start 在上、End 在下）：

  ```c
  pos[1] = trace.Start[1] + (trace.End[1] - trace.Start[1]) * traceDistPercentage;
  ```

---

## 二、旋翼碰撞 (RPC_OnContactBroadcast)

### 2.1 TraceFilter 排除自碰撞

```c
protected bool TraceFilter(notnull IEntity e)
{
    return e != GetOwner() && e != m_RootDamageManager.GetOwner();
}
// ...
GetGame().GetWorld().TraceMove(trace, TraceFilter);
```

- 排除旋翼自身与机体，避免自碰撞导致错误材质。

### 2.2 短射线沿法线复测材质

```c
trace.Start = contactPos + contactNormal;
trace.End   = contactPos - contactNormal;
```

- 物理接触可能无材质；用 TraceMove 再打短线，从 `SurfaceProps` 取 `GameMaterial`。
- 粒子、音效根据材质播放。

### 2.3 物理在服务器、特效在客户端

- 物理碰撞在服务器；粒子/音效通过 `[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]` 广播到客户端执行。

---

## 三、撞击粒子 (RPC_OnImpactParticlesBroadcast)

### 3.1 粒子朝向矩阵

```c
vector transform[4];
Math3D.MatrixFromUpVec(contactNormal, transform);
transform[3] = contactPos;
EmitParticles(transform, resourceName);
```

- `MatrixFromUpVec` 以法线为「上」方向构建矩阵；`transform[3]` 为位置。
- 粒子沿接触面法线方向发射。

### 3.2 材质到粒子资源

```c
GameMaterial contactMat = trace.SurfaceProps;
HitEffectInfo effectInfo = contactMat.GetHitEffectInfo();
ResourceName resourceName = effectInfo.GetBayonetHitParticleEffect();

if (resourceName.IsEmpty())
    resourceName = GetDefaultParticles()[magnitude];
```

- 材质 → `GetHitEffectInfo()` → 特定效果（如刺刀命中）。
- 材质未配置时，用 `magnitude` 选默认粒子。

---

## 四、视线/遮挡检测 (IsObstructed)

### 4.1 TraceFilter 穿透特定类型

```c
protected static bool IsCharacter(notnull IEntity entity)
{
    return ChimeraCharacter.Cast(entity) == null;  // 角色返回 false，射线穿透
}
```

- 返回 `false` 的实体会被射线穿透。
- 用于视线检测时忽略角色，只检测地形/障碍物。

### 4.2 hitFraction 与 threshold

```c
if (world.TraceMove(m_RaycastParam, IsCharacter) < GetRaycastThreshold())
```

- `hitFraction < threshold`：在到达目标前就命中，视为遮挡。

### 4.3 同一物体不算遮挡

```c
IEntity parentEntityRay = m_RaycastParam.TraceEnt.GetRootParent();
IEntity parentEntityAct = entAction.GetRootParent();
if (parentEntityRay != parentEntityAct)
    return true;  // 命中的是别的物体 → 遮挡
```

- 命中的若是交互对象自身或其子部件（同一根父实体），不算遮挡。

---

## 五、地形工具 (SCR_TerrainHelper)

### 5.1 GetTerrainY / GetHeightAboveTerrain

- **GetTerrainY**：从 `pos` 向下 trace 或 `GetSurfaceY`，得到地表高度。
- **GetHeightAboveTerrain**：`pos[1] - GetTerrainY(...)`。

### 5.2 GetTerrainNormal

- 射线：`pos + vector.Up` → `pos - vector.Up`（短射线垂直向下）。
- 命中后取 `trace.TraceNorm` 作为地表法线。

### 5.3 GetTerrainBasis

- 根据法线构建 4×4 地表坐标系；用于 Snap/Orient 到地形。

### 5.4 SnapToGeometry

- `trace.ExcludeArray`：排除指定实体。
- `trace.TargetLayers = EPhysicsLayerDefs.FireGeometry`：仅检测火线几何。
- 输出：贴合后的位置 + `trace.TraceNorm`（表面法线）。

### 5.5 自定义 TraceParam 传入

- 各方法支持可选 `TraceParam trace` 参数；传入时可自定义 Flags、Exclude、ExcludeArray 等。
- `trace.Flags == 0` 时会自动设为 `TraceFlags.WORLD | TraceFlags.ENTS`。

---

## 六、通用经验汇总

### 6.1 TraceFilter 的典型用法

| 目的 | 回调逻辑 |
|------|----------|
| 排除自身 | `return e != GetOwner()` |
| 穿透角色 | `return ChimeraCharacter.Cast(e) == null` |
| 排除多实体 | 在回调中判断 e 是否在排除列表 |

### 6.2 RPC 选型

- **Unreliable**：粒子、音效、非关键可视化；允许丢包。Radar：**有上限 plots**（HUD）走 Unreliable。
- **Reliable**：关键状态同步。Radar：**确认航迹摘要 + 锁 + 扫描元数据**走 Reliable；载荷用类型化数组（`RDF_RadarNetCodec`），非 CSV 字符串。
- 详见 [RADAR_API.md](RADAR_API.md) § Network。

### 6.3 命中结果校验

- 不依赖单一字段；结合 `TraceEnt`、`SurfaceProps`、`hitFraction` 等。
- RDF 已用 `hitFraction >= 0.9999` 过滤远平面/天空假命中。

### 6.4 TraceParam 扩展字段

| 字段 | 典型用途 |
|------|----------|
| Exclude | 排除单个实体（与 ExcludeArray 二选一） |
| ExcludeArray | 批量排除（nametag：自身 + 目标） |
| TargetLayers | 限定物理层 |
| TraceNorm | 表面法线（地物 RCS、朝向计算） |

### 6.5 官方 LOS 热路径惯例（雷达已对齐）

| 惯例 | 官方示例 | 说明 |
|------|----------|------|
| 复用 `TraceParam` | `SCR_PlacedCommandInfoDisplay` / `SCR_NearbyContextDisplay` | 成员持有，勿每帧 `new` |
| 每次清输出 | `SCR_PhysicsHelper` | `TraceEnt = null`；RDF 另清 `SurfaceProps` |
| `ANY_CONTACT` | nametag / command HUD | 可见性测试首选 Flags |
| `ExcludeArray` | nametag | 排除观察者 + 目标；通视 ≈ `percent == 1` |
| `GetRootParent()` | `SCR_NearbyContextDisplay` | 同层级命中不算遮挡 |
| 勿写 owned 字符串 | TraceParam 生成头 | 不要写 `ColliderName` / `TraceMaterial` |
| 起点出壳 | LiDAR 教训 + TerrainHelper | 壳内 `Start` 可致 SEH；雷达 LOS 外推 `LOS_START_CLEARANCE_M` |
| DEM 先挡再 Trace | RDF（HEIGHT RAM） | `m_EnableDemLosPrecheck`：地形刺穿跳过 TraceMove；状态 `demBlk=` |

---

## 七、与 RDF 的对应关系

| 引擎/示例做法 | RDF 中的对应 |
|---------------|--------------|
| nametag `ExcludeArray` + `percent==1` | `RDF_RadarScanGeometry.FillLosExclude` + `IsLineOfSightClear` |
| `ANY_CONTACT` 可见性 | `ConfigureLosParam`：`WORLD \| ENTS \| ANY_CONTACT` |
| 复用 TraceParam | `RDF_RadarScanner.m_TraceParam` / `RDF_LidarScanner.m_TraceParam` / DEM `s_TraceParam` |
| `GetRootParent` 同层级 | `IsLineOfSightClear` 回退分支 |
| 起点出壳 | LiDAR `ComputeStartClearanceM`；雷达 `TraceLineOfSight` 外推 |
| hitFraction 校验 | LiDAR 0.9999 远平面；雷达 LOS `≥ 0.999` |
| Unreliable / Reliable | 见 [RADAR_API.md](RADAR_API.md) § Network |
| GetTerrainY / TraceNorm | DEM / 杂波路径 |

实现入口：`RDF_RadarScanGeometry.TraceLineOfSight`（Scanner 三条 LOS 路径统一调用）。

---

## 八、可落地的改动建议

1. 新 Trace 热路径：复用 param、清输出、`Exclude`/`ExcludeArray` 二选一；LOS 优先 `ANY_CONTACT`。
2. 地物散射/RCS 可结合 `GetTerrainY`、`TraceNorm` 判断命中类型与表面朝向。
3. LiDAR/雷达网络：非关键帧用 Unreliable；Radar 关键检测用 Reliable **摘要**，plots 用 Unreliable + 上限（见 RADAR_API）。

---

## 九、RDF 已实现：Trace 目标模式开关

`RDF_LidarSettings` 新增 `m_TraceTargetMode`（`ERDF_TraceTargetMode` 枚举）：

| 模式 / 选项 | TraceFlags | 说明 |
|-------------|------------|------|
| 0 / `TERRAIN_ONLY` | `TraceFlags.WORLD` | 仅地形/水面/静态世界 |
| 1 / `ALL` | `TraceFlags.WORLD` + `TraceFlags.ENTS` | 地形 + 实体 |
| 2 / `ENTITIES_ONLY` | `TraceFlags.ENTS` | 仅实体（载具、角色等），不检测地形 |

用法（方式一：创建配置后设置属性）：

```c
// 直接设置（RDF_LidarSettings 仍用 ERDF_TraceTargetMode 枚举）
scanner.GetSettings().m_TraceTargetMode = ERDF_TraceTargetMode.TERRAIN_ONLY;
scanner.GetSettings().Validate();

// Demo 预设：m_TraceTargetMode 为 int（0=仅地形, 1=全部, 2=仅实体）
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(256);
cfg.m_TraceTargetMode = 2;
RDF_LidarAutoRunner.StartWithConfig(cfg);

// 运行时切换
RDF_LidarAutoRunner.SetDemoTraceTargetMode(0);
```

雷达通视不走 `m_TraceTargetMode`；LOS 固定由 `RDF_RadarScanGeometry.ConfigureLosParam`
（`WORLD | ENTS | ANY_CONTACT`）配置。

---

## 十、烟雾遮挡激光（TraceFlags.VISIBILITY）

为让烟雾、粒子效果等遮挡激光探测，需在 Trace 中加入 `TraceFlags.VISIBILITY`。现实语义：烟雾阻挡 = 无有效回波，烟雾及后方区域视为未命中。

RDF 通过 `RDF_LidarSettings.m_TraceSmokeOcclusion` 控制，采用双 Trace 区分烟雾与实体：若 visibility 先于几何命中，则 `hit=false`，不返回数据。原版烟雾弹已验证有效。

---

*文档维护：随项目与引擎使用持续补充。*

---

## English

# Lessons distilled from engine usage

Notes from Arma Reforger / Enfusion engine and official/community code, for RDF and similar projects.

---

## 1. TraceMove basics

### 1.1 Core API

```c
float hitFraction = world.TraceMove(param, TraceFilter);
```

- `TraceParam`: inputs Start/End, Flags, LayerMask, Exclude, etc.; outputs TraceEnt, SurfaceProps, TraceNorm.
- `TraceFilter(entity)`: optional callback; return `false` to exclude that entity (ray passes through).

### 1.2 Common flags & fields

| Field | Meaning |
|-------|---------|
| `TraceFlags.WORLD` | World / terrain |
| `TraceFlags.ENTS` | Entities |
| `TraceFlags.VISIBILITY` | Visibility occluders (particles, smoke) — LOS / laser blocked |
| `TraceFlags.ANY_CONTACT` | Stop on any contact; **best for visibility / LOS** (engine docs) |
| `TraceParam.Exclude` | Exclude a single entity |
| `TraceParam.ExcludeArray` | Exclude an entity array (**never with** `Exclude`) |
| `TraceParam.TargetLayers` | Physics layers (e.g. `EPhysicsLayerDefs.FireGeometry`) |
| `TraceParam.SurfaceProps` | Hit surface material (GameMaterial) |
| `TraceParam.TraceNorm` | Hit surface normal |
| `TraceParam.TraceEnt` | Hit entity |

### 1.3 hitFraction & hit position interpolation

- `TraceMove` returns `hitFraction` (0–1), the fraction along the ray to the hit.
- **Top-down ray** (Start above, End below):

  ```c
  surfaceY = trace.Start[1] - (trace.Start[1] - trace.End[1]) * traceCoef;
  ```

- **SnapToGeometry style** (Start above, End below):

  ```c
  pos[1] = trace.Start[1] + (trace.End[1] - trace.Start[1]) * traceDistPercentage;
  ```

---

## 2. Rotor contact (RPC_OnContactBroadcast)

### 2.1 TraceFilter excludes self-collision

```c
protected bool TraceFilter(notnull IEntity e)
{
    return e != GetOwner() && e != m_RootDamageManager.GetOwner();
}
// ...
GetGame().GetWorld().TraceMove(trace, TraceFilter);
```

- Exclude rotor and airframe so self-hits do not yield wrong materials.

### 2.2 Short ray along normal to re-sample material

```c
trace.Start = contactPos + contactNormal;
trace.End   = contactPos - contactNormal;
```

- Physics contact may lack material; short TraceMove reads `GameMaterial` from `SurfaceProps`.
- Particles/SFX keyed off material.

### 2.3 Physics on server, FX on client

- Collision physics on server; particles/SFX via `[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]` to clients.

---

## 3. Impact particles (RPC_OnImpactParticlesBroadcast)

### 3.1 Particle orientation matrix

```c
vector transform[4];
Math3D.MatrixFromUpVec(contactNormal, transform);
transform[3] = contactPos;
EmitParticles(transform, resourceName);
```

- `MatrixFromUpVec` builds a matrix with the normal as “up”; `transform[3]` is position.
- Particles emit along the contact normal.

### 3.2 Material → particle resource

```c
GameMaterial contactMat = trace.SurfaceProps;
HitEffectInfo effectInfo = contactMat.GetHitEffectInfo();
ResourceName resourceName = effectInfo.GetBayonetHitParticleEffect();

if (resourceName.IsEmpty())
    resourceName = GetDefaultParticles()[magnitude];
```

- Material → `GetHitEffectInfo()` → specific effect (e.g. bayonet hit).
- If unset, pick default particles by `magnitude`.

---

## 4. LOS / occlusion (IsObstructed)

### 4.1 TraceFilter passes through certain types

```c
protected static bool IsCharacter(notnull IEntity entity)
{
    return ChimeraCharacter.Cast(entity) == null;  // 角色返回 false，射线穿透
}
```

- Entities returning `false` are penetrated by the ray.
- For LOS, ignore characters; only terrain/obstacles count.

### 4.2 hitFraction vs threshold

```c
if (world.TraceMove(m_RaycastParam, IsCharacter) < GetRaycastThreshold())
```

- `hitFraction < threshold`: hit before the target → obstructed.

### 4.3 Same object is not obstruction

```c
IEntity parentEntityRay = m_RaycastParam.TraceEnt.GetRootParent();
IEntity parentEntityAct = entAction.GetRootParent();
if (parentEntityRay != parentEntityAct)
    return true;  // 命中的是别的物体 → 遮挡
```

- Hits on the interactable itself or its children (same root parent) are not obstruction.

---

## 5. Terrain helpers (SCR_TerrainHelper)

### 5.1 GetTerrainY / GetHeightAboveTerrain

- **GetTerrainY**: downward trace from `pos` or `GetSurfaceY` → surface height.
- **GetHeightAboveTerrain**: `pos[1] - GetTerrainY(...)`.

### 5.2 GetTerrainNormal

- Ray: `pos + vector.Up` → `pos - vector.Up` (short vertical down).
- On hit, use `trace.TraceNorm` as terrain normal.

### 5.3 GetTerrainBasis

- Build a 4×4 surface frame from the normal; Snap/Orient to terrain.

### 5.4 SnapToGeometry

- `trace.ExcludeArray`: exclude listed entities.
- `trace.TargetLayers = EPhysicsLayerDefs.FireGeometry`: fire geometry only.
- Output: snapped position + `trace.TraceNorm`.

### 5.5 Optional custom TraceParam

- Methods accept optional `TraceParam trace` to customize Flags, Exclude, ExcludeArray, etc.
- When `trace.Flags == 0`, auto-set `TraceFlags.WORLD | TraceFlags.ENTS`.

---

## 6. General lessons

### 6.1 Typical TraceFilter uses

| Goal | Callback |
|------|----------|
| Exclude self | `return e != GetOwner()` |
| Pass through characters | `return ChimeraCharacter.Cast(e) == null` |
| Exclude many entities | test membership in an exclude list |

### 6.2 RPC channel choice

- **Unreliable**: particles, SFX, non-critical viz; loss OK. Radar: **capped plots** (HUD) use Unreliable.
- **Reliable**: critical state sync. Radar: **confirmed-track summary + lock + scan meta** use Reliable; typed arrays (`RDF_RadarNetCodec`), not CSV strings.
- See [RADAR_API.md](RADAR_API.md) § Network.

### 6.3 Validate hit results

- Do not trust a single field; combine `TraceEnt`, `SurfaceProps`, `hitFraction`, etc.
- RDF already filters far-plane/sky false hits with `hitFraction >= 0.9999`.

### 6.4 Extra TraceParam fields

| Field | Typical use |
|-------|-------------|
| Exclude | Single exclude (mutually exclusive with ExcludeArray) |
| ExcludeArray | Batch exclude (nametag: self + target) |
| TargetLayers | Limit physics layers |
| TraceNorm | Surface normal (clutter RCS, facing) |

### 6.5 Stock LOS hot-path habits (radar aligned)

| Habit | Stock sample | Note |
|-------|--------------|------|
| Reuse `TraceParam` | `SCR_PlacedCommandInfoDisplay` / `SCR_NearbyContextDisplay` | Member field; avoid per-frame `new` |
| Clear outputs each cast | `SCR_PhysicsHelper` | `TraceEnt = null`; RDF also clears `SurfaceProps` |
| `ANY_CONTACT` | nametag / command HUD | Preferred Flags for visibility |
| `ExcludeArray` | nametag | Observer + target; clear LOS ≈ `percent == 1` |
| `GetRootParent()` | `SCR_NearbyContextDisplay` | Same hierarchy ≠ obstruction |
| Do not write owned strings | TraceParam generated header | Never assign `ColliderName` / `TraceMaterial` |
| Start outside hull | LiDAR lesson + TerrainHelper | In-solid `Start` can SEH; radar LOS uses `LOS_START_CLEARANCE_M` |
| DEM before Trace | RDF (HEIGHT RAM) | `m_EnableDemLosPrecheck`: terrain pierce skips TraceMove; status `demBlk=` |

---

## 7. Mapping to RDF

| Engine / sample practice | RDF counterpart |
|--------------------------|-----------------|
| nametag `ExcludeArray` + `percent==1` | `FillLosExclude` + `IsLineOfSightClear` |
| `ANY_CONTACT` visibility | `ConfigureLosParam`: `WORLD \| ENTS \| ANY_CONTACT` |
| Reuse TraceParam | `RDF_RadarScanner.m_TraceParam` / LiDAR / DEM static param |
| `GetRootParent` same hierarchy | `IsLineOfSightClear` fallback |
| Start outside hull | LiDAR clearance; radar `TraceLineOfSight` push |
| hitFraction check | LiDAR 0.9999 far-plane; radar LOS `≥ 0.999` |
| Unreliable / Reliable | See [RADAR_API.md](RADAR_API.md) § Network |
| GetTerrainY / TraceNorm | DEM / clutter path |

Entry point: `RDF_RadarScanGeometry.TraceLineOfSight` (all three Scanner LOS paths).

---

## 8. Actionable suggestions

1. New Trace hot paths: reuse param, clear outs, Exclude XOR ExcludeArray; prefer `ANY_CONTACT` for LOS.
2. Clutter/RCS can use `GetTerrainY` and `TraceNorm` for hit kind and surface facing.
3. LiDAR/radar network: Unreliable for non-critical frames; Radar critical detects use Reliable **summary**, plots use Unreliable + caps (see RADAR_API).

---

## 9. Already in RDF: Trace target mode switch

`RDF_LidarSettings` adds `m_TraceTargetMode` (`ERDF_TraceTargetMode`):

| Mode / option | TraceFlags | Meaning |
|---------------|------------|---------|
| 0 / `TERRAIN_ONLY` | `TraceFlags.WORLD` | Terrain / water / static world only |
| 1 / `ALL` | `TraceFlags.WORLD` + `TraceFlags.ENTS` | Terrain + entities |
| 2 / `ENTITIES_ONLY` | `TraceFlags.ENTS` | Entities only (vehicles, characters); no terrain |

Usage (create config, then set properties):

```c
// 直接设置（RDF_LidarSettings 仍用 ERDF_TraceTargetMode 枚举）
scanner.GetSettings().m_TraceTargetMode = ERDF_TraceTargetMode.TERRAIN_ONLY;
scanner.GetSettings().Validate();

// Demo 预设：m_TraceTargetMode 为 int（0=仅地形, 1=全部, 2=仅实体）
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(256);
cfg.m_TraceTargetMode = 2;
RDF_LidarAutoRunner.StartWithConfig(cfg);

// 运行时切换
RDF_LidarAutoRunner.SetDemoTraceTargetMode(0);
```

Radar LOS does not use `m_TraceTargetMode`; it is fixed by
`RDF_RadarScanGeometry.ConfigureLosParam` (`WORLD | ENTS | ANY_CONTACT`).

---

## 10. Smoke blocking laser (TraceFlags.VISIBILITY)

To let smoke/particles block laser sensing, include `TraceFlags.VISIBILITY` on Trace. Real-world meaning: smoke block = no valid return; smoke and everything behind counts as miss.

RDF gates this with `RDF_LidarSettings.m_TraceSmokeOcclusion` via dual Trace (visibility vs geometry): if visibility hits first, `hit=false` and no data. Verified with vanilla smoke grenades.

---

*Maintained as project and engine usage evolve.*
