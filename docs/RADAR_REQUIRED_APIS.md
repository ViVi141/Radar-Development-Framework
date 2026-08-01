> **Languages / 语言**: [中文](#中文) · [English](#english)

# 雷达系统所需游戏 API — 列表与验证

---

## 中文

本文档列出当前游戏内雷达实现（见 [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)）所用的 Enfusion / Arma Reforger 脚本 API，并通过引擎 API 检索进行验证。验证日期：按文档编写时查询结果为准。

---

## 一、验证说明

- **来源**：`call_mcp_tool(server: "user-enfusion-mcp", toolName: "api_search", ...)` 查询结果。
- **状态**：✅ 表示 API 在引擎/Arma 文档中查得并可用；⚠️ 表示依赖现有 RDF 用法或需在运行时再确认；❓ 表示未在 api_search 中单独命中，但被相关类/方法间接覆盖。
- **引用**：Doc 链接来自 api_search 返回的 BIKI 外链（EnfusionScriptAPIPublic / ArmaReforgerScriptAPIPublic）。

---

## 二、世界与实体查询（炮弹/载具/雷达候选）

| API | 用途 | 验证状态 | 说明 |
|-----|------|----------|------|
| **BaseWorld / World** | 世界接口，实体查询与 Trace 的入口 | ✅ | `World` 继承 `BaseWorld`；`GetGame().GetWorld()`、`subject.GetWorld()` 返回 `World`。Doc: EnfusionScriptAPIPublic/interfaceWorld.html, interfaceBaseWorld.html |
| **BaseWorld.QueryEntitiesBySphere** | 扫描半径内实体（球体） | ✅ | `bool QueryEntitiesBySphere(vector center, float radius, QueryEntitiesCallback addEntity, QueryEntitiesCallback filterEntity=null, EQueryEntitiesFlags queryFlags=EQueryEntitiesFlags.ALL)` |
| **BaseWorld.QueryEntitiesByAABB** | 轴对齐盒内实体 | ✅ | `bool QueryEntitiesByAABB(vector mins, vector maxs, QueryEntitiesCallback addEntity, QueryEntitiesCallback filterEntity=null, EQueryEntitiesFlags queryFlags=EQueryEntitiesFlags.ALL)` |
| **BaseWorld.QueryEntitiesByOBB** | 定向盒内实体（扇区/锥体近似） | ✅ | `bool QueryEntitiesByOBB(vector mins, vector maxs, vector matrix[4], QueryEntitiesCallback addEntity, QueryEntitiesCallback filterEntity=null, EQueryEntitiesFlags queryFlags=EQueryEntitiesFlags.ALL)` |
| **BaseWorld.QueryEntitiesByLine** | 线段上的实体 | ✅ | 可用于射线方向上的实体检测 |
| **QueryEntitiesCallback** | 实体回调：`bool callback(IEntity e)` | ✅ | 在 Query 方法中用于 addEntity / filterEntity，返回 true 表示接受/加入。 |
| **EQueryEntitiesFlags** | 查询过滤标志 | ✅ | 默认 `EQueryEntitiesFlags.ALL`，可按需缩小范围。 |
| **BaseWorld.GetActiveEntities** | 当前活跃实体列表 | ✅ | `void GetActiveEntities(notnull out array<IEntity> entities)`，可作为备用或与 Query 结合。 |
| **BaseWorld.GetWorldTime** | 世界时间（毫秒） | ✅ | `float GetWorldTime()`，用于轨迹时间戳与扫描间隔。 |
| **BaseWorld.GetSurfaceY** | 地表高度 | ✅ | 用于高度过滤、地形相关逻辑。 |

---

## 三、射线与可见性（Trace）

| API | 用途 | 验证状态 | 说明 |
|-----|------|----------|------|
| **BaseWorld.TraceMove** | 射线检测（起点→终点） | ✅ | `float TraceMove(inout TraceParam param, TraceFilterCallback filterCallback=null)`，返回 0..1 为命中比例。 |
| **TraceParam** | 射线输入/输出结构 | ✅ | 属性含：Start, End, LayerMask, TargetLayers, **Flags**（TraceFlags）, Exclude, ExcludeArray, Include, IncludeArray, **TraceEnt**（命中实体）, **SurfaceProps**（命中表面）, TraceNorm 等。Doc: interfaceTraceParam.html |
| **TraceFlags** | 检测类型 | ✅ | 如 WORLD, ENTS, VISIBILITY；RDF 已用，见 LESSONS_FROM_ENGINE.md。 |
| **TraceFilterCallback** | 过滤回调：`bool filter(IEntity e)` | ✅ | 返回 false 时该实体被射线穿透。 |
| **World.TracePosition** | 单点碰撞检测 | ✅ | 可用于点是否在几何内等。 |
| **ChimeraCharacter.TraceMoveWithoutCharacters** | 排除角色的 Trace 优化 | ✅ | Arma 扩展，可选用于减少过滤开销。 |

---

## 四、实体与变换（IEntity）

| API | 用途 | 验证状态 | 说明 |
|-----|------|----------|------|
| **IEntity** | 实体基类 | ✅ | 继承 Managed；GetID, GetParent, GetRootParent, GetChildren, GetSibling, **GetWorld()**, GetPrefabData 等。Doc: interfaceIEntity.html |
| **IEntity.GetWorld** | 实体所在世界 | ✅ | `BaseWorld GetWorld()` |
| **IEntity.GetWorldTransform** | 世界变换矩阵 | ✅ | RDF 已用 `subject.GetWorldTransform(worldMat)`（见 RDF_LidarScanner、RDF_LidarVisualizer）。IEntity 在 Enfusion 中有 GetWorldTransform。 |
| **IEntity.GetParent / GetRootParent** | 层级与载具根 | ✅ | 用于主体解析、排除扫描自身。 |
| **IEntity.GetID** | 实体唯一 ID | ✅ | 用于跨帧追踪关联（同一发弹）。 |
| **EntityID** | 实体 ID 类型 | ✅ | 与 GetID 配套。 |
| **SCR_EntityHelper.FindComponent** | 在实体上查找组件 | ✅ | `Managed FindComponent(notnull IEntity entity, TypeName componentType, ...)`，用于判断是否为抛射物（ProjectileMoveComponent 等）。 |

---

## 五、抛射物（炮弹/子弹追踪）

| API | 用途 | 验证状态 | 说明 |
|-----|------|----------|------|
| **Projectile** | 抛射物实体类型（Arma） | ✅ | EntityTypes 组；用于过滤“是否为炮弹/子弹”。Doc: ArmaReforgerScriptAPIPublic/interfaceProjectile.html |
| **ProjectileClass** | 抛射物 Prefab 数据 | ✅ | 继承 BaseGameEntityClass, GameEntityClass。 |
| **ProjectileMoveComponent** | 抛射物运动组件 | ✅ | `GetVelocity()` 取速度；`Launch(...)` 等。抛射物实体上可 FindComponent 此类以识别并取速度。Doc: interfaceProjectileMoveComponent.html |
| **ProjectileMoveComponent.GetVelocity** | 弹体速度矢量 | ✅ | `vector GetVelocity()`，用于轨迹与预测。 |
| **BaseProjectileComponent** | 抛射物组件基类 | ✅ | GetInstigator, GetParentProjectile 等；导弹等继承此类。 |
| **MissileMoveComponent.GetVelocity** | 导弹速度 | ✅ | 与 ProjectileMoveComponent 类似。 |
| **EjectableProjectile** | 可抛射物（弹壳等） | ✅ | 子类为 Projectile，可按需过滤。 |

识别抛射物建议：先 `QueryEntitiesBySphere`，再在回调中对每个 `IEntity` 用 `FindComponent(entity, ProjectileMoveComponent)` 或检查 `GetPrefabData()` / 类名是否为 Projectile 系。

---

## 六、物理与速度（可选）

| API | 用途 | 验证状态 | 说明 |
|-----|------|----------|------|
| **Physics.GetVelocity** | 刚体线速度 | ✅ | 若抛射物无 ProjectileMoveComponent 但挂有 Physics，可用此取速度。 |
| **Physics.GetWorldTransform** | 刚体世界变换 | ✅ | 位置与朝向。 |
| **CharacterControllerComponent.GetVelocity** | 角色速度 | ✅ | 载具/步兵目标时可用。 |

---

## 七、游戏入口与世界获取

| API | 用途 | 验证状态 | 说明 |
|-----|------|----------|------|
| **GetGame()** | 游戏单例 | ❓ | 全局入口；RDF 与引擎脚本广泛使用 `GetGame().GetWorld()`，api_search 未单独命中“GetGame”，视为引擎约定。 |
| **GetGame().GetWorld()** | 当前 World | ✅ | 返回 `World`（BaseWorld）；RDF 已用。 |
| **WorldController.GetWorld** | 从 Controller 取 World | ✅ | `proto external World GetWorld()`。 |

---

## 八、网络同步（雷达状态、轨迹、可被探测列表）

| API | 用途 | 验证状态 | 说明 |
|-----|------|----------|------|
| **RplProp / RplComponent** | 复制属性与组件 | ❓ | Radar：关键 Settings / 发射态；LiDAR 网络组件亦用。属 Enfusion 复制注解。 |
| **RplRpc** | RPC 方法 | ❓ | Radar：Reliable 摘要 / Unreliable plots；参数优先 `array<int>` / `array<float>`（见 `RDF_RadarNetCodec`），**勿**用超长 CSV `string` 作 Rpc 载荷。 |
| **ScriptBitWriter / ScriptBitReader** | JIP 位流 | ❓ | 配 `RplSave` / `RplLoad`；**不是**直播 Rpc 参数类型。 |
| **Replication** | 服务器权威同步 | ⚠️ | 实现约定见 [RADAR_API.md](RADAR_API.md) § Network / Datalink。 |

Radar 规模旋钮（上限、降频、指纹跳过、兴趣半径）为组件 Attribute，非额外引擎 API。

---

## 九、雷达“可被探测”注册（自定义逻辑）

| 需求 | 实现方式 | 验证状态 |
|------|----------|----------|
| 雷达实体列表 | 自维护 `array<IEntity>` 或 WorldSystem 注册表，无引擎专属 API | N/A |
| 发射/静默状态 | 组件成员变量 + 可选 RplProp 同步 | N/A |
| 辐射强度/距离计算 | 标量运算 + 世界位置（GetWorldTransform / origin） | N/A |

“可被探测”不依赖额外引擎 API，仅需在扫描方用 `QueryEntitiesBySphere` 时合并自维护的“正在发射的雷达”列表，或通过 WorldSystem 查询。

---

## 十、汇总与使用建议

- **实体查询**：统一使用 **World（BaseWorld）** 的 `QueryEntitiesBySphere` / `QueryEntitiesByAABB` / `QueryEntitiesByOBB` + **QueryEntitiesCallback** 获取候选，再按类型过滤（载具、**Projectile**/ProjectileMoveComponent、自注册雷达）。
- **可见性**：对候选做 **TraceMove**（`RDF_RadarScanGeometry.TraceLineOfSight`：原点→目标代表点；`Flags=WORLD|ENTS|ANY_CONTACT`；`ExcludeArray`=subject+target；复用 TraceParam；每次清 TraceEnt/SurfaceProps）。`hitFraction ≥ 0.999` 或 `GetRootParent` 同层级 → 通视。
- **抛射物**：用 **Projectile**、**ProjectileMoveComponent.GetVelocity()** 及 **IEntity.GetID()** 做检测与多帧追踪；轨迹用 **GetWorldTime()** 打时间戳。
- **雷达可被探测**：自维护“主动雷达”注册表 + 在对方扫描时合并进目标列表，无需新引擎 API。

所有上表标为 ✅ 的 API 均已在检索中验证存在；❓/⚠️ 项依赖项目内已有用法或 BIKI 文档。实现约定以本列表、[RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)、[VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md) 为准。

---

## English

# Game APIs required by the radar system — checklist & verification

This document lists the Enfusion / Arma Reforger script APIs used by the in-game radar implementation (see [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)), verified via engine API search. Verification date: as of the queries run when this doc was written.

---

## 1. Verification notes

- **Source**: `call_mcp_tool(server: "user-enfusion-mcp", toolName: "api_search", ...)` results.
- **Status**: ✅ = found and usable in engine/Arma docs; ⚠️ = depends on existing RDF usage or needs runtime confirmation; ❓ = not hit alone in api_search, but covered indirectly by related classes/methods.
- **Citations**: Doc links from BIKI URLs returned by api_search (EnfusionScriptAPIPublic / ArmaReforgerScriptAPIPublic).

---

## 2. World & entity queries (shells / vehicles / radar candidates)

| API | Purpose | Status | Notes |
|-----|---------|--------|-------|
| **BaseWorld / World** | World interface; entry for entity queries and Trace | ✅ | `World` extends `BaseWorld`; `GetGame().GetWorld()`, `subject.GetWorld()` return `World`. Doc: EnfusionScriptAPIPublic/interfaceWorld.html, interfaceBaseWorld.html |
| **BaseWorld.QueryEntitiesBySphere** | Entities in scan radius (sphere) | ✅ | `bool QueryEntitiesBySphere(vector center, float radius, QueryEntitiesCallback addEntity, QueryEntitiesCallback filterEntity=null, EQueryEntitiesFlags queryFlags=EQueryEntitiesFlags.ALL)` |
| **BaseWorld.QueryEntitiesByAABB** | Entities in axis-aligned box | ✅ | `bool QueryEntitiesByAABB(vector mins, vector maxs, QueryEntitiesCallback addEntity, QueryEntitiesCallback filterEntity=null, EQueryEntitiesFlags queryFlags=EQueryEntitiesFlags.ALL)` |
| **BaseWorld.QueryEntitiesByOBB** | Entities in oriented box (sector/cone approx.) | ✅ | `bool QueryEntitiesByOBB(vector mins, vector maxs, vector matrix[4], QueryEntitiesCallback addEntity, QueryEntitiesCallback filterEntity=null, EQueryEntitiesFlags queryFlags=EQueryEntitiesFlags.ALL)` |
| **BaseWorld.QueryEntitiesByLine** | Entities along a line segment | ✅ | Useful for entity hits along a ray direction |
| **QueryEntitiesCallback** | Entity callback: `bool callback(IEntity e)` | ✅ | Used as addEntity / filterEntity in Query methods; return true to accept/include. |
| **EQueryEntitiesFlags** | Query filter flags | ✅ | Default `EQueryEntitiesFlags.ALL`; narrow as needed. |
| **BaseWorld.GetActiveEntities** | Currently active entity list | ✅ | `void GetActiveEntities(notnull out array<IEntity> entities)`; fallback or combine with Query. |
| **BaseWorld.GetWorldTime** | World time (ms) | ✅ | `float GetWorldTime()`; track timestamps and scan intervals. |
| **BaseWorld.GetSurfaceY** | Terrain height | ✅ | Height filtering and terrain-related logic. |

---

## 3. Rays & visibility (Trace)

| API | Purpose | Status | Notes |
|-----|---------|--------|-------|
| **BaseWorld.TraceMove** | Ray cast (start→end) | ✅ | `float TraceMove(inout TraceParam param, TraceFilterCallback filterCallback=null)`; returns 0..1 hit fraction. |
| **TraceParam** | Ray I/O struct | ✅ | Fields include: Start, End, LayerMask, TargetLayers, **Flags** (TraceFlags), Exclude, ExcludeArray, Include, IncludeArray, **TraceEnt** (hit entity), **SurfaceProps** (hit surface), TraceNorm, etc. Doc: interfaceTraceParam.html |
| **TraceFlags** | Detection kinds | ✅ | e.g. WORLD, ENTS, VISIBILITY; used by RDF — see LESSONS_FROM_ENGINE.md. |
| **TraceFilterCallback** | Filter callback: `bool filter(IEntity e)` | ✅ | Return false to let the ray pass through that entity. |
| **World.TracePosition** | Single-point collision test | ✅ | e.g. whether a point is inside geometry. |
| **ChimeraCharacter.TraceMoveWithoutCharacters** | Trace optimized to skip characters | ✅ | Arma extension; optional to reduce filter cost. |

---

## 4. Entities & transforms (IEntity)

| API | Purpose | Status | Notes |
|-----|---------|--------|-------|
| **IEntity** | Entity base class | ✅ | Extends Managed; GetID, GetParent, GetRootParent, GetChildren, GetSibling, **GetWorld()**, GetPrefabData, etc. Doc: interfaceIEntity.html |
| **IEntity.GetWorld** | World owning the entity | ✅ | `BaseWorld GetWorld()` |
| **IEntity.GetWorldTransform** | World transform matrix | ✅ | RDF already uses `subject.GetWorldTransform(worldMat)` (RDF_LidarScanner, RDF_LidarVisualizer). IEntity has GetWorldTransform in Enfusion. |
| **IEntity.GetParent / GetRootParent** | Hierarchy & vehicle root | ✅ | Subject resolve; exclude self from scan. |
| **IEntity.GetID** | Unique entity ID | ✅ | Cross-frame track association (same shell). |
| **EntityID** | Entity ID type | ✅ | Pairs with GetID. |
| **SCR_EntityHelper.FindComponent** | Find component on entity | ✅ | `Managed FindComponent(notnull IEntity entity, TypeName componentType, ...)`; identify projectiles (ProjectileMoveComponent, etc.). |

---

## 5. Projectiles (shell / missile tracking)

| API | Purpose | Status | Notes |
|-----|---------|--------|-------|
| **Projectile** | Projectile entity type (Arma) | ✅ | EntityTypes group; filter “is shell/missile”. Doc: ArmaReforgerScriptAPIPublic/interfaceProjectile.html |
| **ProjectileClass** | Projectile prefab data | ✅ | Extends BaseGameEntityClass, GameEntityClass. |
| **ProjectileMoveComponent** | Projectile motion component | ✅ | `GetVelocity()`; `Launch(...)`, etc. FindComponent on projectile entities. Doc: interfaceProjectileMoveComponent.html |
| **ProjectileMoveComponent.GetVelocity** | Body velocity vector | ✅ | `vector GetVelocity()`; trajectory and prediction. |
| **BaseProjectileComponent** | Projectile component base | ✅ | GetInstigator, GetParentProjectile, etc.; missiles inherit this. |
| **MissileMoveComponent.GetVelocity** | Missile velocity | ✅ | Similar to ProjectileMoveComponent. |
| **EjectableProjectile** | Ejectable (casings, etc.) | ✅ | Subclass of Projectile; filter as needed. |

Suggested identification: `QueryEntitiesBySphere`, then in the callback `FindComponent(entity, ProjectileMoveComponent)` or check `GetPrefabData()` / class name for Projectile family.

---

## 6. Physics & velocity (optional)

| API | Purpose | Status | Notes |
|-----|---------|--------|-------|
| **Physics.GetVelocity** | Rigid-body linear velocity | ✅ | If a projectile lacks ProjectileMoveComponent but has Physics. |
| **Physics.GetWorldTransform** | Rigid-body world transform | ✅ | Position and orientation. |
| **CharacterControllerComponent.GetVelocity** | Character velocity | ✅ | Vehicle/infantry targets. |

---

## 7. Game entry & world access

| API | Purpose | Status | Notes |
|-----|---------|--------|-------|
| **GetGame()** | Game singleton | ❓ | Global entry; RDF and engine scripts widely use `GetGame().GetWorld()`; api_search did not hit “GetGame” alone — treated as engine convention. |
| **GetGame().GetWorld()** | Current World | ✅ | Returns `World` (BaseWorld); used by RDF. |
| **WorldController.GetWorld** | World from controller | ✅ | `proto external World GetWorld()`. |

---

## 8. Network sync (radar state, tracks, detectable lists)

| API | Purpose | Status | Notes |
|-----|---------|--------|-------|
| **RplProp / RplComponent** | Replicated props & components | ❓ | Radar: key Settings / emit state; LiDAR network likewise. Enfusion replication annotations. |
| **RplRpc** | RPC methods | ❓ | Radar: Reliable summary / Unreliable plots; prefer `array<int>` / `array<float>` (`RDF_RadarNetCodec`) — **not** long CSV `string` Rpc payloads. |
| **ScriptBitWriter / ScriptBitReader** | JIP bit streams | ❓ | Pair with `RplSave` / `RplLoad`; **not** live Rpc argument types. |
| **Replication** | Server-authoritative sync | ⚠️ | Contract: [RADAR_API.md](RADAR_API.md) § Network / Datalink. |

Radar scale knobs (caps, throttle, fingerprint skip, interest radius) are component Attributes, not extra engine APIs.

---

## 9. Radar “detectable” registration (custom logic)

| Need | Approach | Status |
|------|----------|--------|
| Radar entity list | Self-maintained `array<IEntity>` or WorldSystem registry; no engine-specific API | N/A |
| Emit / silent state | Component fields + optional RplProp sync | N/A |
| Radiated power / range math | Scalar math + world pose (GetWorldTransform / origin) | N/A |

“Detectable” needs no extra engine API: when scanning, merge a self-maintained “emitting radars” list into `QueryEntitiesBySphere` results, or query via WorldSystem.

---

## 10. Summary & usage tips

- **Entity query**: use **World (BaseWorld)** `QueryEntitiesBySphere` / `QueryEntitiesByAABB` / `QueryEntitiesByOBB` + **QueryEntitiesCallback**, then filter by type (vehicles, **Projectile**/ProjectileMoveComponent, self-registered radars).
- **Visibility**: **TraceMove** via `RDF_RadarScanGeometry.TraceLineOfSight` (origin → representative aim point; `Flags=WORLD|ENTS|ANY_CONTACT`; `ExcludeArray`=subject+target; reused TraceParam; clear TraceEnt/SurfaceProps each cast). Clear when `hitFraction ≥ 0.999` or same `GetRootParent` hierarchy.
- **Projectiles**: **Projectile**, **ProjectileMoveComponent.GetVelocity()**, and **IEntity.GetID()** for detect + multi-frame track; stamp tracks with **GetWorldTime()**.
- **Radar detectable**: maintain an “active radar” registry and merge into the peer’s target list on scan — no new engine API.

All ✅ APIs above were verified in search; ❓/⚠️ items rely on in-repo usage or BIKI. Conventions follow this list, [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md), and [VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md).
