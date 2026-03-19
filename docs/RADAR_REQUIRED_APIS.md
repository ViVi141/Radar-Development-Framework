# 雷达系统所需游戏 API — 列表与验证

本文档列出实现完整雷达系统（见 [RADAR_PLAN.md](RADAR_PLAN.md)）所需的 Enfusion / Arma Reforger 脚本 API，并通过 **api_search**（user-enfusion-mcp）进行验证。验证日期：按文档编写时 MCP 查询结果为准。

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
| **RplProp / RplComponent** | 复制属性与组件 | ❓ | RDF 已在 LiDAR 网络组件中使用 `[RplProp]` 等；api_search 未直接命中，属 Enfusion 复制注解与基类。 |
| **RplRpc** | RPC 方法 | ❓ | 同上，RDF 已用；见 RDF_LidarNetworkComponent。 |
| **Replication** | 服务器权威同步 | ⚠️ | 实现方式以现有 RDF 网络模块与 BIKI “Replication” 文档为准。 |

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
- **可见性**：对候选做 **TraceMove**（雷达原点到目标代表点），用 **TraceParam**（Flags、Exclude、TraceEnt、SurfaceProps）判断是否被遮挡。
- **抛射物**：用 **Projectile**、**ProjectileMoveComponent.GetVelocity()** 及 **IEntity.GetID()** 做检测与多帧追踪；轨迹用 **GetWorldTime()** 打时间戳。
- **雷达可被探测**：自维护“主动雷达”注册表 + 在对方扫描时合并进目标列表，无需新引擎 API。

所有上表标为 ✅ 的 API 均已在 **api_search** 中验证存在；❓/⚠️ 项依赖项目内已有用法或 BIKI 文档。实现雷达系统时以本列表与 [RADAR_PLAN.md](RADAR_PLAN.md)、[VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md) 为准。
