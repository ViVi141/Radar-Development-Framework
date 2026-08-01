> **Languages / 语言**: [中文](#中文) · [English](#english)

# RDF — 开发者指南

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Contact: 747384120@qq.com  
License: Apache-2.0

入口文档：[README.md](../README.md) · 雷达公共 API：[RADAR_API.md](RADAR_API.md) · 雷达内部框架：[RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md) · DEM：[DEM.md](DEM.md)

---

## 中文

## 设计原则

- 框架默认**静默**，只有在明确启用时才运行，不干扰其他模组。
- `OnGameStart` 不预绑 LiDAR 网络；仅在启动 LiDAR AutoRunner 时惰性绑定。
- 扫描核心、可视化与演示驱动相互隔离，可独立替换。
- LiDAR / Radar / DEM 并列；雷达检测可走物理链路，DEM 杂波缺失时安全退化。
- 对外暴露稳定的扩展接口（采样策略、颜色策略、扫描完成回调）。

---

## 模块布局 — 总览

```
scripts/Game/RDF/
├── Common/    RDF_DebugShapeManager（世界空间 Shape 托管）
├── Lidar/     点云扫描、可视化、网络、HUD
├── Radar/     实体扫描、物理检测、EW、CFAR、网络同步、PPI、自动化测试
└── DEM/       Workbench 烘焙 + Runtime 加载
UI/layouts/RDF/
├── RadarPPI.layout   雷达 PPI（右下角，绿磷光）
└── LidarPPI.layout   LiDAR PPI（左下角，蓝主题）
scripts/WorkbenchGame/RDF/
├── RDF_DemBakePlugin.c
└── RDF_RadarSignatureBakePlugin.c   离线扫可放置 prefab → 特征 CSV / pack 为 .conf
tools/dem/     离线打包、雷达物理原型、full_sim / golden 测试（目录索引：[tools/README.md](../tools/README.md)；契约：tools/dem/RADAR_FRAMEWORK.md）
```

首次加载 layout 前请在 Workbench Resource Browser 中打开/注册 `UI/layouts/RDF/*.layout`（或确认 `.meta` GUID 已入资源库），否则 `CreateWidgets` 会失败并打 ERROR 日志。玩法模组若要挂进 `SCR_HUDManager`，可复用同一 layout 路径。

---

## 模块布局 — LiDAR

```
scripts/Game/RDF/
└── Lidar/
    ├── Core/
    │   ├── RDF_LidarSettings.c        参数与校验
    │   ├── RDF_LidarTypes.c           数据结构（RDF_LidarSample）
    │   ├── RDF_LidarScanner.c         扫描器与射线构建
    │   ├── RDF_LidarSensor.c          玩法门面（ConfigureMode / Tick）
    │   ├── RDF_LidarSampleStrategy.c  采样策略接口（均匀策略）
    │   ├── RDF_HemisphereSampleStrategy.c
    │   ├── RDF_ConicalSampleStrategy.c
    │   ├── RDF_RectangularFOVSampleStrategy.c  矩形视场（车载）
    │   ├── RDF_StratifiedSampleStrategy.c
    │   ├── RDF_ScanlineSampleStrategy.c
    │   └── RDF_SweepSampleStrategy.c  扇区扫掠策略
    ├── Visual/
    │   ├── RDF_LidarVisualSettings.c  可视化参数（含仅点云开关）
    │   ├── RDF_LidarVisualizer.c      ShapeManager 托管 + 数据获取
    │   ├── RDF_LidarColorStrategy.c   颜色策略接口与默认实现
    │   ├── RDF_LidarMaterialColorStrategy.c  按密度 g/cm³ 着色、透明度随距离
    │   ├── RDF_IndexColorStrategy.c
    │   └── RDF_ThreeColorStrategy.c   近/中/远三段渐变
    ├── UI/
    │   └── RDF_LidarHUD.c             PPI ← UI/layouts/RDF/LidarPPI.layout
    ├── Network/
    │   ├── RDF_LidarNetworkAPI.c      网络同步基类
    │   ├── RDF_LidarNetworkComponent.c Rpl 实现（服务器权威）
    │   ├── RDF_LidarNetworkUtils.c    网络辅助工具
    │   ├── RDF_LidarNetworkScanner.c  网络扫描适配器
    │   └── RDF_LidarNetworkSetupExample.c 网络接入示例（ScriptComponent）
    ├── Util/
    │   ├── RDF_LidarSubjectResolver.c 解析扫描主体（玩家/载具）
    │   ├── RDF_LidarExport.c          CSV 导出
    │   ├── RDF_LidarSampleUtils.c     统计与过滤
    │   ├── RDF_LidarMaterialReflectivity.c  表面反射率（材质着色用）
    │   └── RDF_LidarScanCompleteHandler.c 扫描完成回调基类
    └── Demo/
        ├── RDF_LidarAutoBootstrap.c   统一 bootstrap（默认关闭）
        ├── RDF_LidarAutoRunner.c      演示唯一入口
        ├── RDF_LidarDemoConfig.c      配置与预设工厂
        ├── RDF_LidarDemoCycler.c       策略轮换
        ├── RDF_LidarDiagMenu.c         Workbench DiagMenu
        └── RDF_LidarDemoStatsHandler.c 内置统计回调
```

---

## 模块布局 — Radar

```
scripts/Game/RDF/Radar/
├── Core/
│   ├── RDF_RadarSettings.c / RDF_RadarTypes.c / RDF_RadarHardware.c
│   ├── RDF_RadarScanner.c              编排：Registry/Legacy 扫描 + Trace + 复用预算
│   ├── RDF_RadarScanGeometry.c         LOS TraceMove（ANY_CONTACT / ExcludeArray / 出壳）+ 实体中心 / 速度
│   ├── RDF_RadarPhysicalDetect.c       雷达方程 / NLOS bounce+刀刃绕射 / DEM 杂波 / SNR / ESM
│   ├── RDF_RadarScattererRegistry.c    全局散射体/辐射源表（增量维护）
│   ├── RDF_RadarEmitterRegistry.c      辐射标记门面（转发到散射体表）
│   ├── RDF_RadarCandidateCollect.c     候选收集辅助
│   ├── RDF_RadarLosCache.c / RDF_RadarScanReuseCache.c / RDF_RadarScanPassContext.c
│   ├── RDF_RadarCfarProcessor.c        扫描侧 CFAR 编排（热填空等）
│   ├── RDF_RadarProjectileTracker.c    量测关联 / α-β / 弹道 PredictAt / WLR fix
│   ├── RDF_RadarLockManager.c          锁定层 SEARCH/ACQUIRING/TRACKING/COAST + ARM
│   ├── RDF_RadarWeaponBridge.c         火控桥 FireSolution / 发射门控 / 中段上行
│   ├── RDF_RadarWeaponComponent.c      载具挂载入口 GuideRocket
│   ├── RDF_RadarRwr.c                  被搜索/跟踪/锁定告警
│   └── RDF_RadarSensor.c               公共门面 SEARCH/STARE/WLR/ESM → Plots/Tracks/Lock/ARM/RWR
├── Physics/
│   ├── RDF_RadarRcsModel.c / RDF_RadarBallistics.c / RDF_RadarSignatureLibrary.c
│   ├── RDF_RadarSignatureTableConf.c / RDF_RadarSurfaceTable.c / RDF_RadarSurfaceTableConf.c
│   ├── RDF_RadarClutterModel.c         DEM σ⁰ → 杂波功率
│   ├── RDF_RadarMeasurement.c          距离门/波束量化 + SNR 噪声
│   ├── RDF_RadarMeasurementModel.c     CFAR 后 / Tracker 前可扩展测量误差
│   └── RDF_RadarCfarGate.c             粗栅格 CA/GO/SO-CFAR 判检
├── EW/
│   └── RDF_RadarEwModel.c              噪声（SEARCH_AVG/BEAM/MAINLOBE）+ 欺骗（假点/拖距/角闪烁/间歇）
├── Network/
│   ├── RDF_RadarNetworkAPI.c           基类（含 intentional no-op；SetEnabled 别名 SetDemoEnabled）
│   ├── RDF_RadarNetworkComponent.c     服务器权威同步（挂 Sensor；发布到 DatalinkHub）
│   ├── RDF_RadarDatalinkTypes.c        IFF 枚举 / 航迹摘要 / IffResolver
│   ├── RDF_RadarDatalinkAPI.c          数据链 API 基类
│   ├── RDF_RadarDatalinkHub.c          站间航迹库 + 融合结果
│   ├── RDF_RadarDatalinkComponent.c    可选类型化 Broadcast + RplSave
│   └── RDF_RadarNetCodec.c             array/bit 打包（扫描 + 数据链）
├── Fusion/
│   └── RDF_RadarFusionService.c        多雷达关联 + 双站交会
├── Visual/ / UI/
│   ├── RDF_RadarVisualizer.c / RDF_RadarVisualSettings.c  （ShapeManager 托管）
│   └── RDF_RadarHUD.c                  PPI ← UI/layouts/RDF/RadarPPI.layout（面板 144×184 / 画布 128×128）
├── Util/
│   └── RDF_RadarEntityClassifier.c
└── Demo/
    ├── RDF_RadarAutoRunner.c / Bootstrap / Component / DemoConfig / ManualDemo
    ├── RDF_RadarAutoTestSuite.c / RDF_RadarAutoTestGate.c / RDF_RadarAutoTestMapOverlay.c
    ├── RDF_RadarAutoTest.c             DEM 杂波回归（入 StartAll）
    ├── RDF_RadarBallisticsAutoTest.c   弹道/WLR 数学回归（入 StartAll）
    ├── RDF_RadarShellFireAutoTest.c    实弹 Spawn+Launch + WLR（入 StartAll）
    ├── RDF_RadarAirborneScanTest.c     空中目标扫描（入 StartAll）
    ├── RDF_RadarLockAutoTest.c         载具锁定状态机（入 StartAll）
    ├── RDF_RadarPerfAutoTest.c         扫描/弹道墙钟开销（入 StartAll）
    ├── RDF_RadarPlayAutoTest.c         游玩路径：HUD + 发现 + DEM + 漫步负载（入 StartAll）
    ├── RDF_RadarStressAutoTest.c       性能压测：重负载 soak（独立）
    ├── RDF_RadarRwrAutoTest.c          RWR 告警（独立）
    ├── RDF_RadarEsmArmAutoTest.c       ESM + GetArmAim（独立）
    ├── RDF_RadarFusionAutoTest.c       数据链融合 / 交会（独立）
    ├── RDF_RadarRocketLockFireAutoTest.c / RDF_RadarRocketGuidance.c  锁定→制导（独立）
    ├── RDF_RadarHeliDuelAutoTest.c     机打机 + 拦截导弹（独立）
    └── RDF_RadarSamEngageAutoTest.c    地面 SAM（BTR）扫描/识别/打击 6×Mi-8（独立）
```

数据流摘要见 [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)；文件树若与仓库不一致，以 `scripts/Game/RDF/Radar/` 为准。

---

## 模块布局 — DEM

```
scripts/Game/RDF/DEM/
├── RDF_DemTileBake.c / RDF_DemBakeConstants.c / RDF_DemMaterialTable.c
├── RDF_DemColumnSpans.c / RDF_DemWorldKey.c
└── Runtime/
    ├── RDF_DemRuntimeTypes.c
    ├── RDF_DemRuntimeLoader.c          查找顺序编排
    ├── RDF_DemRuntimeCache.c           世界坐标采样 + LRU
    ├── RDF_DemSurfaceJsonPack.c        SURF JSON（工坊推荐）
    ├── RDF_DemJsonPack.c               全量 DEM JSON
    └── RDF_DemBinPack.c                .dem.data 二进制包
```

烘焙与 `$profile` / 工坊路径见 [DEM.md](DEM.md)。

---

## LiDAR 数据流

```
RDF_LidarSettings
       ↓
RDF_LidarScanner.Scan(entity)
  ├── SampleStrategy.BuildDirection() × N
  ├── Trace(ray) → RDF_LidarSample
  └── 返回 array<ref RDF_LidarSample>
       ↓
RDF_LidarVisualizer.RenderWithSamples()   ← 可选，3D 点云/射线
       ↓
RDF_LidarScanCompleteHandler.OnScanComplete()  ← 可选回调（如 RDF_LidarHUD）
```

---

## 烟雾遮挡

启用 `m_TraceSmokeOcclusion` 后，采用双 Trace 区分烟雾与实体：若 visibility occluder（烟雾）先于实体被命中，则该射线视为未命中（无有效回波），烟雾及后方区域均不返回数据。

```c
// 直接启用
scanner.GetSettings().m_TraceSmokeOcclusion = true;
scanner.GetSettings().Validate();

// Demo 预设
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(256);
cfg.m_TraceSmokeOcclusion = true;
RDF_LidarAutoRunner.StartWithConfig(cfg);

// 运行时切换
RDF_LidarAutoRunner.SetDemoTraceSmokeOcclusion(true);
```

实现方式：双 Trace（带 VISIBILITY 与纯几何）对比 hitFraction，若烟雾先被命中则视为未命中。优化：第一次 Trace 无命中时跳过第二次 Trace（开阔/天空等场景节省约半额 Trace 调用）。原版烟雾弹已验证有效。

---

## 扩展点

### 采样策略
```c
class MyStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        // 返回归一化方向向量
    }
}
// 注入：
scanner.SetSampleStrategy(new MyStrategy());
```

### 颜色策略
```c
class MyColorStrategy : RDF_LidarColorStrategy
{
    override int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        // 返回 ARGB int
    }
    override int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)
    {
        // 返回 ARGB int
    }
}
visualizer.SetColorStrategy(new MyColorStrategy());
```

### 扫描完成回调
```c
class MyHandler : RDF_LidarScanCompleteHandler
{
    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        // 处理扫描结果
    }
}
RDF_LidarAutoRunner.SetScanCompleteHandler(new MyHandler());
```

---

## HUD 实现说明（`.layout` + Canvas）

Radar / LiDAR PPI 面板结构由文本 `.layout` 定义（官方式），脚本通过 `Workspace.CreateWidgets` 实例化后按 `Name` 绑定：

| Layout | 脚本 | 锚点 |
|--------|------|------|
| `UI/layouts/RDF/RadarPPI.layout` | `RDF_RadarHUD` | 右下 |
| `UI/layouts/RDF/LidarPPI.layout` | `RDF_LidarHUD` | 左下 |

```c
m_wRoot = GetGame().GetWorkspace().CreateWidgets(LAYOUT_PPI, null);
m_Canvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("PpiCanvas"));
m_wMode  = TextWidget.Cast(m_wRoot.FindAnyWidget("ModeText"));
// Canvas 绘制仍用 TessellateCircle + SetDrawCommands（逻辑不变）
```

世界空间调试图形由 `RDF_DebugShapeManager` 持有 `Shape` 引用（对齐 `SCR_DebugShapeManager`），环/弧优先 `CreateCircle` / `CreateCircleArc`。

**关键约束**：
- `SetDrawCommands` 只保存指针，调用者必须持有 `drawCmds` 数组的引用（成员变量）
- 目标光点每次扫描重建，静态背景命令保持不变，避免重复分配
- 坐标系：`delta[0]` = 东（右），`delta[2]` = 北（上，负 screenY）

---

## Enfusion Script 关键约束

| 约束 | 正确写法 |
|------|---------|
| 无三目运算符 | `if/else` 替代 `? :` |
| 无函数式类型转换 | `(float)x` 替代 `float(x)` |
| 无 `string.ToLower()` | 直接检查大写或小写字面量 |
| 无 `enum` 整数转换 | 预填 `EMyEnum[]` 数组查表 |
| `string.Format` 用位置说明符 | `%1 %2` 替代 `%d %.1f` |
| `.ToString()` 不能在临时表达式调用 | 先赋给局部变量再 `.ToString()` |
| `out` 是关键字 | 局部变量改名，如 `outSamples` |
| 非 ASCII 字符报语法错误 | 全文件保持纯 ASCII |

---

## 已知控制台警告（与 RDF 无关）

- `SCR_CustomArrayEditableEntityUIComponent tried to set the UI color but the EditableEntity is not using SCR_ColorUIInfo`：来自基 game 的编辑器/UI 脚本，与 RDF 雷达/LiDAR 无关，可忽略。

---

## 性能建议

- 降低 `m_RayCount`（默认 512）或增大 `m_MinTickInterval`
- 减少 `m_RaySegments` 可降低绘制开销
- HUD：节流间隔 `UPDATE_INTERVAL`（默认 0.5s）防止过度重绘
- CanvasWidget `SetDrawCommands`：每次调用重建动态命令，静态命令缓存复用

详见 [OPTIMIZATION_AND_MEMORY.md](OPTIMIZATION_AND_MEMORY.md)。

---

## 开发与贡献指南

- 代码风格：Google C++ Style（与项目现有约定一致）
- 文件放在对应模块目录
- PR 说明：变更动机、兼容性影响、性能影响

---

## English

# RDF — Developer Guide

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Contact: 747384120@qq.com  
License: Apache-2.0

Entry points: [README.md](../README.md) · Radar public API: [RADAR_API.md](RADAR_API.md) · Radar internal framework: [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md) · DEM: [DEM.md](DEM.md)

---

## Design principles

- The framework is **silent by default** and only runs when explicitly enabled, so it does not disturb other mods.
- `OnGameStart` does not pre-bind the LiDAR network; binding is lazy when the LiDAR AutoRunner starts.
- Scan core, visualization, and demo drivers are isolated and independently replaceable.
- LiDAR / Radar / DEM sit side by side; radar detection can use the physics path and safely degrade when DEM clutter is missing.
- Stable extension points are exposed (sample strategy, color strategy, scan-complete callback).

---

## Module layout — overview

```
scripts/Game/RDF/
├── Common/    RDF_DebugShapeManager（世界空间 Shape 托管）
├── Lidar/     点云扫描、可视化、网络、HUD
├── Radar/     实体扫描、物理检测、EW、CFAR、网络同步、PPI、自动化测试
└── DEM/       Workbench 烘焙 + Runtime 加载
UI/layouts/RDF/
├── RadarPPI.layout   雷达 PPI（右下角，绿磷光）
└── LidarPPI.layout   LiDAR PPI（左下角，蓝主题）
scripts/WorkbenchGame/RDF/
├── RDF_DemBakePlugin.c
└── RDF_RadarSignatureBakePlugin.c   离线扫可放置 prefab → 特征 CSV / pack 为 .conf
tools/dem/     离线打包、雷达物理原型、full_sim / golden 测试（目录索引：[tools/README.md](../tools/README.md)；契约：tools/dem/RADAR_FRAMEWORK.md）
```

Before first load, open/register `UI/layouts/RDF/*.layout` in the Workbench Resource Browser (or confirm `.meta` GUIDs are in the resource DB); otherwise `CreateWidgets` fails and logs ERROR. Gameplay mods that attach into `SCR_HUDManager` can reuse the same layout paths.

---

## Module layout — LiDAR

```
scripts/Game/RDF/
└── Lidar/
    ├── Core/
    │   ├── RDF_LidarSettings.c        参数与校验
    │   ├── RDF_LidarTypes.c           数据结构（RDF_LidarSample）
    │   ├── RDF_LidarScanner.c         扫描器与射线构建
    │   ├── RDF_LidarSensor.c          玩法门面（ConfigureMode / Tick）
    │   ├── RDF_LidarSampleStrategy.c  采样策略接口（均匀策略）
    │   ├── RDF_HemisphereSampleStrategy.c
    │   ├── RDF_ConicalSampleStrategy.c
    │   ├── RDF_RectangularFOVSampleStrategy.c  矩形视场（车载）
    │   ├── RDF_StratifiedSampleStrategy.c
    │   ├── RDF_ScanlineSampleStrategy.c
    │   └── RDF_SweepSampleStrategy.c  扇区扫掠策略
    ├── Visual/
    │   ├── RDF_LidarVisualSettings.c  可视化参数（含仅点云开关）
    │   ├── RDF_LidarVisualizer.c      ShapeManager 托管 + 数据获取
    │   ├── RDF_LidarColorStrategy.c   颜色策略接口与默认实现
    │   ├── RDF_LidarMaterialColorStrategy.c  按密度 g/cm³ 着色、透明度随距离
    │   ├── RDF_IndexColorStrategy.c
    │   └── RDF_ThreeColorStrategy.c   近/中/远三段渐变
    ├── UI/
    │   └── RDF_LidarHUD.c             PPI ← UI/layouts/RDF/LidarPPI.layout
    ├── Network/
    │   ├── RDF_LidarNetworkAPI.c      网络同步基类
    │   ├── RDF_LidarNetworkComponent.c Rpl 实现（服务器权威）
    │   ├── RDF_LidarNetworkUtils.c    网络辅助工具
    │   ├── RDF_LidarNetworkScanner.c  网络扫描适配器
    │   └── RDF_LidarNetworkSetupExample.c 网络接入示例（ScriptComponent）
    ├── Util/
    │   ├── RDF_LidarSubjectResolver.c 解析扫描主体（玩家/载具）
    │   ├── RDF_LidarExport.c          CSV 导出
    │   ├── RDF_LidarSampleUtils.c     统计与过滤
    │   ├── RDF_LidarMaterialReflectivity.c  表面反射率（材质着色用）
    │   └── RDF_LidarScanCompleteHandler.c 扫描完成回调基类
    └── Demo/
        ├── RDF_LidarAutoBootstrap.c   统一 bootstrap（默认关闭）
        ├── RDF_LidarAutoRunner.c      演示唯一入口
        ├── RDF_LidarDemoConfig.c      配置与预设工厂
        ├── RDF_LidarDemoCycler.c       策略轮换
        ├── RDF_LidarDiagMenu.c         Workbench DiagMenu
        └── RDF_LidarDemoStatsHandler.c 内置统计回调
```

---

## Module layout — Radar

```
scripts/Game/RDF/Radar/
├── Core/
│   ├── RDF_RadarSettings.c / RDF_RadarTypes.c / RDF_RadarHardware.c
│   ├── RDF_RadarScanner.c              编排：Registry/Legacy 扫描 + Trace + 复用预算
│   ├── RDF_RadarScanGeometry.c         LOS TraceMove（ANY_CONTACT / ExcludeArray / 出壳）+ 实体中心 / 速度
│   ├── RDF_RadarPhysicalDetect.c       雷达方程 / NLOS bounce+刀刃绕射 / DEM 杂波 / SNR / ESM
│   ├── RDF_RadarScattererRegistry.c    全局散射体/辐射源表（增量维护）
│   ├── RDF_RadarEmitterRegistry.c      辐射标记门面（转发到散射体表）
│   ├── RDF_RadarCandidateCollect.c     候选收集辅助
│   ├── RDF_RadarLosCache.c / RDF_RadarScanReuseCache.c / RDF_RadarScanPassContext.c
│   ├── RDF_RadarCfarProcessor.c        扫描侧 CFAR 编排（热填空等）
│   ├── RDF_RadarProjectileTracker.c    量测关联 / α-β / 弹道 PredictAt / WLR fix
│   ├── RDF_RadarLockManager.c          锁定层 SEARCH/ACQUIRING/TRACKING/COAST + ARM
│   ├── RDF_RadarWeaponBridge.c         火控桥 FireSolution / 发射门控 / 中段上行
│   ├── RDF_RadarWeaponComponent.c      载具挂载入口 GuideRocket
│   ├── RDF_RadarRwr.c                  被搜索/跟踪/锁定告警
│   └── RDF_RadarSensor.c               公共门面 SEARCH/STARE/WLR/ESM → Plots/Tracks/Lock/ARM/RWR
├── Physics/
│   ├── RDF_RadarRcsModel.c / RDF_RadarBallistics.c / RDF_RadarSignatureLibrary.c
│   ├── RDF_RadarSignatureTableConf.c / RDF_RadarSurfaceTable.c / RDF_RadarSurfaceTableConf.c
│   ├── RDF_RadarClutterModel.c         DEM σ⁰ → 杂波功率
│   ├── RDF_RadarMeasurement.c          距离门/波束量化 + SNR 噪声
│   ├── RDF_RadarMeasurementModel.c     CFAR 后 / Tracker 前可扩展测量误差
│   └── RDF_RadarCfarGate.c             粗栅格 CA/GO/SO-CFAR 判检
├── EW/
│   └── RDF_RadarEwModel.c              噪声（SEARCH_AVG/BEAM/MAINLOBE）+ 欺骗（假点/拖距/角闪烁/间歇）
├── Network/
│   ├── RDF_RadarNetworkAPI.c           基类（含 intentional no-op；SetEnabled 别名 SetDemoEnabled）
│   ├── RDF_RadarNetworkComponent.c     服务器权威同步（挂 Sensor；发布到 DatalinkHub）
│   ├── RDF_RadarDatalinkTypes.c        IFF 枚举 / 航迹摘要 / IffResolver
│   ├── RDF_RadarDatalinkAPI.c          数据链 API 基类
│   ├── RDF_RadarDatalinkHub.c          站间航迹库 + 融合结果
│   ├── RDF_RadarDatalinkComponent.c    可选类型化 Broadcast + RplSave
│   └── RDF_RadarNetCodec.c             array/bit 打包（扫描 + 数据链）
├── Fusion/
│   └── RDF_RadarFusionService.c        多雷达关联 + 双站交会
├── Visual/ / UI/
│   ├── RDF_RadarVisualizer.c / RDF_RadarVisualSettings.c  （ShapeManager 托管）
│   └── RDF_RadarHUD.c                  PPI ← UI/layouts/RDF/RadarPPI.layout（面板 144×184 / 画布 128×128）
├── Util/
│   └── RDF_RadarEntityClassifier.c
└── Demo/
    ├── RDF_RadarAutoRunner.c / Bootstrap / Component / DemoConfig / ManualDemo
    ├── RDF_RadarAutoTestSuite.c / RDF_RadarAutoTestGate.c / RDF_RadarAutoTestMapOverlay.c
    ├── RDF_RadarAutoTest.c             DEM 杂波回归（入 StartAll）
    ├── RDF_RadarBallisticsAutoTest.c   弹道/WLR 数学回归（入 StartAll）
    ├── RDF_RadarShellFireAutoTest.c    实弹 Spawn+Launch + WLR（入 StartAll）
    ├── RDF_RadarAirborneScanTest.c     空中目标扫描（入 StartAll）
    ├── RDF_RadarLockAutoTest.c         载具锁定状态机（入 StartAll）
    ├── RDF_RadarRwrAutoTest.c          RWR 告警（独立）
    ├── RDF_RadarEsmArmAutoTest.c       ESM + GetArmAim（独立）
    ├── RDF_RadarFusionAutoTest.c       数据链融合 / 交会（独立）
    ├── RDF_RadarRocketLockFireAutoTest.c / RDF_RadarRocketGuidance.c  锁定→制导（独立）
    ├── RDF_RadarHeliDuelAutoTest.c     机打机 + 拦截导弹（独立）
    └── RDF_RadarSamEngageAutoTest.c    地面 SAM（BTR）扫描/识别/打击 6×Mi-8（独立）
```

Data-flow summary: [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md). If the tree drifts from the repo, treat `scripts/Game/RDF/Radar/` as authoritative.

---

## Module layout — DEM

```
scripts/Game/RDF/DEM/
├── RDF_DemTileBake.c / RDF_DemBakeConstants.c / RDF_DemMaterialTable.c
├── RDF_DemColumnSpans.c / RDF_DemWorldKey.c
└── Runtime/
    ├── RDF_DemRuntimeTypes.c
    ├── RDF_DemRuntimeLoader.c          查找顺序编排
    ├── RDF_DemRuntimeCache.c           世界坐标采样 + LRU
    ├── RDF_DemSurfaceJsonPack.c        SURF JSON（工坊推荐）
    ├── RDF_DemJsonPack.c               全量 DEM JSON
    └── RDF_DemBinPack.c                .dem.data 二进制包
```

Bake and `$profile` / Workshop paths: [DEM.md](DEM.md).

---

## LiDAR data flow

```
RDF_LidarSettings
       ↓
RDF_LidarScanner.Scan(entity)
  ├── SampleStrategy.BuildDirection() × N
  ├── Trace(ray) → RDF_LidarSample
  └── 返回 array<ref RDF_LidarSample>
       ↓
RDF_LidarVisualizer.RenderWithSamples()   ← 可选，3D 点云/射线
       ↓
RDF_LidarScanCompleteHandler.OnScanComplete()  ← 可选回调（如 RDF_LidarHUD）
```

---

## Smoke occlusion

With `m_TraceSmokeOcclusion` enabled, a dual Trace separates smoke from entities: if a visibility occluder (smoke) is hit before the entity, the ray is treated as a miss (no valid return); neither the smoke nor anything behind it yields data.

```c
// 直接启用
scanner.GetSettings().m_TraceSmokeOcclusion = true;
scanner.GetSettings().Validate();

// Demo 预设
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(256);
cfg.m_TraceSmokeOcclusion = true;
RDF_LidarAutoRunner.StartWithConfig(cfg);

// 运行时切换
RDF_LidarAutoRunner.SetDemoTraceSmokeOcclusion(true);
```

Implementation: dual Trace (VISIBILITY vs pure geometry) compares hitFraction; smoke-first counts as a miss. Optimization: skip the second Trace when the first misses (open sky / clear scenes save about half the Trace calls). Verified against vanilla smoke grenades.

---

## Extension points

### Sample strategy
```c
class MyStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        // 返回归一化方向向量
    }
}
// 注入：
scanner.SetSampleStrategy(new MyStrategy());
```

### Color strategy
```c
class MyColorStrategy : RDF_LidarColorStrategy
{
    override int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        // 返回 ARGB int
    }
    override int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)
    {
        // 返回 ARGB int
    }
}
visualizer.SetColorStrategy(new MyColorStrategy());
```

### Scan-complete callback
```c
class MyHandler : RDF_LidarScanCompleteHandler
{
    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        // 处理扫描结果
    }
}
RDF_LidarAutoRunner.SetScanCompleteHandler(new MyHandler());
```

---

## HUD notes (`.layout` + Canvas)

Radar / LiDAR PPI panel structure is defined by text `.layout` files (official style). Scripts instantiate via `Workspace.CreateWidgets` and bind by `Name`:

| Layout | Script | Anchor |
|--------|--------|--------|
| `UI/layouts/RDF/RadarPPI.layout` | `RDF_RadarHUD` | bottom-right |
| `UI/layouts/RDF/LidarPPI.layout` | `RDF_LidarHUD` | bottom-left |

```c
m_wRoot = GetGame().GetWorkspace().CreateWidgets(LAYOUT_PPI, null);
m_Canvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("PpiCanvas"));
m_wMode  = TextWidget.Cast(m_wRoot.FindAnyWidget("ModeText"));
// Canvas 绘制仍用 TessellateCircle + SetDrawCommands（逻辑不变）
```

World-space debug shapes are held by `RDF_DebugShapeManager` (`Shape` refs, aligned with `SCR_DebugShapeManager`); prefer `CreateCircle` / `CreateCircleArc` for rings and arcs.

**Critical constraints**:
- `SetDrawCommands` only stores a pointer; the caller must keep a live reference to the `drawCmds` array (member field)
- Target blips are rebuilt each scan; static background commands stay unchanged to avoid reallocations
- Coordinates: `delta[0]` = east (right), `delta[2]` = north (up, negative screenY)

---

## Enfusion Script hard constraints

| Constraint | Correct form |
|------------|--------------|
| No ternary operator | use `if/else` instead of `? :` |
| No functional casts | `(float)x` instead of `float(x)` |
| No `string.ToLower()` | compare upper/lower literals directly |
| No `enum`↔int cast | prefill an `EMyEnum[]` lookup table |
| `string.Format` uses positional specs | `%1 %2` instead of `%d %.1f` |
| `.ToString()` cannot be called on temporaries | assign to a local, then `.ToString()` |
| `out` is a keyword | rename locals, e.g. `outSamples` |
| Non-ASCII causes syntax errors | keep source files pure ASCII |

---

## Known console warnings (unrelated to RDF)

- `SCR_CustomArrayEditableEntityUIComponent tried to set the UI color but the EditableEntity is not using SCR_ColorUIInfo`: from base-game editor/UI scripts; unrelated to RDF radar/LiDAR; safe to ignore.

---

## Performance tips

- Lower `m_RayCount` (default 512) or raise `m_MinTickInterval`
- Fewer `m_RaySegments` reduces draw cost
- HUD: throttle with `UPDATE_INTERVAL` (default 0.5s) to avoid over-redraw
- CanvasWidget `SetDrawCommands`: rebuild dynamic commands each call; cache and reuse static commands

See [OPTIMIZATION_AND_MEMORY.md](OPTIMIZATION_AND_MEMORY.md).

---

## Development & contribution

- Code style: Google C++ Style (matches existing project convention)
- Place files in the matching module directory
- PR notes: motivation, compatibility impact, performance impact
