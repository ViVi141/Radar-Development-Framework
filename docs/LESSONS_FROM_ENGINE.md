# 从引擎用法中提炼的经验

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
| `TraceParam.Exclude` | 排除单个实体 |
| `TraceParam.ExcludeArray` | 排除实体数组 |
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

- **Unreliable**：粒子、音效、非关键可视化；允许丢包。
- **Reliable**：检测结果、关键状态同步。

### 6.3 命中结果校验

- 不依赖单一字段；结合 `TraceEnt`、`SurfaceProps`、`hitFraction` 等。
- RDF 已用 `hitFraction >= 0.9999` 过滤远平面/天空假命中。

### 6.4 TraceParam 扩展字段

| 字段 | 典型用途 |
|------|----------|
| Exclude | 排除扫描主体 |
| ExcludeArray | 批量排除实体 |
| TargetLayers | 限定物理层 |
| TraceNorm | 表面法线（地物 RCS、朝向计算） |

---

## 七、与 RDF 的对应关系

| 引擎/示例做法 | RDF 中的对应或可借鉴点 |
|---------------|------------------------|
| TraceFilter 排除自身 | 可用 `Exclude = subject` 或自定义 TraceFilter |
| 短射线复测材质 | 已用长射线取 SurfaceProps，可扩展用途 |
| RPC 广播特效 | 参考 LiDAR 网络同步的服务器/客户端分工 |
| Unreliable 通道 | 用于非关键可视化同步 |
| hitFraction 校验 | 已有 0.9999 远平面过滤 |
| ExcludeArray | 批量排除时可用 |
| GetTerrainY / TraceNorm | 地物 RCS、地形散射计算 |
| TargetLayers | 限定检测火线/碰撞几何 |

---

## 八、可落地的改动建议

1. 保持 `param.Exclude = subject`；必要时实现 `TraceFilter` 排除主体及其子实体。
2. 地物散射/RCS 可结合 `GetTerrainY`、`TraceNorm` 判断命中类型与表面朝向。
3. LiDAR/雷达网络可视化：非关键帧用 Unreliable，关键检测用 Reliable。

---

## 九、RDF 已实现：Trace 目标模式开关

`RDF_LidarSettings` 新增 `m_TraceTargetMode`（`ETraceTargetMode` 枚举）：

| 模式 / 选项 | TraceFlags | 说明 |
|-------------|------------|------|
| 0 / `TERRAIN_ONLY` | `TraceFlags.WORLD` | 仅地形/水面/静态世界 |
| 1 / `ALL` | `TraceFlags.WORLD` + `TraceFlags.ENTS` | 地形 + 实体 |
| 2 / `ENTITIES_ONLY` | `TraceFlags.ENTS` | 仅实体（载具、角色等），不检测地形 |

用法（方式一：创建配置后设置属性）：

```c
// 直接设置（RDF_LidarSettings 仍用 ETraceTargetMode 枚举）
scanner.GetSettings().m_TraceTargetMode = ETraceTargetMode.TERRAIN_ONLY;
scanner.GetSettings().Validate();

// Demo 预设：m_TraceTargetMode 为 int（0=仅地形, 1=全部, 2=仅实体）
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(256);
cfg.m_TraceTargetMode = 2;
RDF_LidarAutoRunner.StartWithConfig(cfg);

// 运行时切换
RDF_LidarAutoRunner.SetDemoTraceTargetMode(0);
```

雷达（`RDF_RadarSettings` 继承 `RDF_LidarSettings`）同样支持。

---

*文档维护：随项目与引擎使用持续补充。*
