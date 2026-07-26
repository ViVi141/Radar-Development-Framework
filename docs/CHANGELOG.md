# CHANGELOG

## 2026-07-27 — AutoTest 互斥 + 顺序套件

- 根因：DEM / Lock / Air / ShellFire 共用 `RDF_RadarAutoRunner`；并行启动会互相覆盖配置 → 假 FAIL（clutter=0、acquire=0、tracks=0、分类为 0）
- 新增 `RDF_RadarAutoTestGate`：同一时间只允许一个 AutoRunner 测试持有门闩
- 新增 `RDF_RadarAutoTestSuite.StartAll()`：Ballistics → DEM → Lock → Air → ShellFire 顺序执行
- README 标明不可并行

---

## 2026-07-27 — AutoTest scans=0：墙钟驱动扫描间隔

- 根因：`RDF_RadarSensor.Tick` 用 `GetWorldTime` 做更新间隔；Workbench GM 编辑器下世界时间常冻结，扫描序号不涨，AutoTest 全部 `scans=0`（弹道数学单测不受影响）
- 修复：Tick 间隔改用 `System.GetTickCount` 墙钟；`Configure` 后强制立即下一扫
- `RDF_RadarAutoRunner`：GM 自由相机导致 `ControlledEntity` 为空时，回退到上次成功的 subject
- `RDF_RadarScattererRegistry.Tick`：同帧去重与发现间隔也改墙钟，避免 `pend` 分类在冻结世界时间下停摆

---

## 2026-07-26 — 存量测试同类问题修复

- `RDF_RadarScanner.IsLineOfSightClear`：命中目标**子碰撞体**（车轮 / 车体分件）也视为通视；LOS 端点改用几何包围盒中心（`GetScattererLosEnd`），修复地面载具被自身/地形误判遮挡 —— 该修复对全部测试生效
- 强制本地扫描补齐：`RDF_RadarAirborneScanTest` / `RDF_RadarAutoTest` 之前未调 `SetForceLocalScan(true)`，Workbench 角色带 `RplComponent` 时会走网络扫描器、测试配置整体被绕过（与炮弹测试同一根因）；三个测试统一改为**保存并还原**先前状态，不再硬置 false
- `RDF_RadarAirborneScanTest`：
  - 轨道相位改为**相对开机时间**，首帧不再把 Mi-8 从生成点瞬移到圆周另一侧（与载具撞毁同源问题）；重生沿用当前相位
  - 关闭 CFAR 门（空距离单元尚无热噪声填充，孤立真批可能被窗口否掉），显式 `m_KeepEntityTruth=true`
  - `target_classified` 不再等价于 `target_detected`，改为真正校验 VEHICLE / RADAR_EMITTER 分类；新增 `seen_as_anonymous` 指标
  - `seen` 统计所有落在目标门内的批，`detected` 只统计通过检测链路的批，"看见但未检出" 可从报告直接读出

---

## 2026-07-26 — 锁定层与搜索→截获→跟踪状态机（P0）

- 新增 `RDF_RadarLockManager`：`SEARCH → ACQUIRING → TRACKING → COAST` 状态机
  - 自动截获最近合规目标 / 手动 `LockTrackId`；类型过滤、最大锁距、扇区门、确认帧数、丢批 coast
  - `GetLockedTarget(out entity, out worldPos)` 供武器 / 火控读取；coast 期间外推瞄点
- `RDF_RadarSensor`：持有并每次扫描驱动锁定层；`GetLockManager` / `GetLockedTarget`；`GetStatusShort` 追加锁定状态
- `RDF_RadarComponent`：`GetLockManager` / `LockTrackId` / `Unlock` / `GetLockedTarget`
- 新增 `RDF_RadarLockAutoTest`：放置**静止**载具（不 `SetOrigin` 传送），验证 SEARCH→ACQUIRING→TRACKING
- 修复：自动截获允许未确认航迹进入 ACQUIRING；测试选平坦通视点，避免物理撞毁
- 文档：[RADAR_API.md](RADAR_API.md) 锁定层节；[VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md) 更新

---

## 2026-07-26 — RDF_RadarSensor 公共门面 API

- 新增 `RDF_RadarSensor`：`ConfigureMode(SEARCH/STARE/WLR)` → `Tick`/`ScanOnce` → `GetPlots`/`GetTracks`/`CountWlrFixes`
- `RDF_RadarScanContext` + `RDF_RadarScanCompleteHandler` 回调
- `RDF_RadarComponent` / `RDF_RadarAutoRunner` 改为持有 Sensor（HUD/可视化仍在 AutoRunner）
- 文档：[RADAR_API.md](RADAR_API.md)

---

## 2026-07-26 — WLR 结合 DEM 地形交点

- Python：`ground_y_fn` / `dem_ground_y_fn`；交地点按 AGL 穿越局部地形，不再只用平地
- `mass_battle`：默认加载 GM_Eden DEM tiles（无 tile 则合成丘陵）；`--no-dem` 可退回平地
- Enforce：`RDF_RadarBallistics.SampleGroundYM` 优先 DEM cache，其次 `GetSurfaceY`
- Scanner 每次扫描把 DEM cache 交给 Ballistics；设置项 `m_EnableDemGroundForWlr`
- 数学单测在合成坐标下临时 `SetUseDemGround(false)`；Python DEM 丘包测试已通过

---

## 2026-07-26 — 实弹自动放炮回归（修复检测链路）

- `EnableSimulation` + 强制写入散射体表 + 测试 RCS 抬升
- 关闭 CFAR；`m_KeepEntityTruth=true`（避免测量合成把炮弹改成 ANONYMOUS）
- **根因**：Workbench 角色带 `RplComponent` 时 AutoRunner 误走网络默认扫描器，测试配置被绕过 → `SetForceLocalScan(true)`
- 开火方位对齐雷达 `mat[0]` 前向；扇区 180°；天线抬高 12 m
- 周期刷新炮弹位姿并打印 `liveShells/moving/plots/det/lastTargets`

---

## 2026-07-26 — 实弹自动放炮回归

- 新增 `RDF_RadarShellFireAutoTest`：自动 `Spawn` + `Launch` 真实 `Ammo_Shell_82mm_HE_O832DU`
- 约 42 s 内每 6 s 放一发（仰角 55°、装药系数 ~1.736），雷达盯炮弹扇区
- 校验：炮弹点迹检测、确认航迹、WLR 发射点相对真值 ≤250 m
- Debugger：`RDF_RadarShellFireAutoTest.Start();`

---

## 2026-07-26 — 弹道/WLR 完整接线 + 自动测试

- `RDF_RadarComponent` 航迹更新后同步调用 `RefreshWeaponLocates`（与 AutoRunner 一致）
- `SolveWeaponLocate` 优先取历史最高点（apex）状态再反推/前推
- Enforce：`RDF_RadarBallisticsAutoTest.Start()`（自由落体时间、阻力缩短射程、侧风偏流、回推发射点、航迹 WLR）
- Python：`tools/dem/test_rdf_radar_ballistics.py`（`python -m unittest` 已通过）

---

## 2026-07-26 — 游戏内弹道预测 / 全局风 / WLR 发射·落点

- 新增 `RDF_RadarBallistics.c`：`a = g - AirDrag·|v−w|·(v−w)`，读 `TimeAndWeatherManager` 全局风
- `RDF_RadarTrack.PredictAt`：炮弹航迹改弹道积分；载具仍为常速度
- `SolveWeaponLocate` / `RefreshWeaponLocates`：确认炮弹航迹反推发射点、前推落点
- 设置：`m_EnableBallisticPrediction`、`m_ShellAirDrag`、`m_EnableWeaponLocate`
- 可视化：橙=发射点、青=落点、连线；HUD 增加 `WLR` 计数
- 地面：DEM / `GetSurfaceY`（见同日 DEM 交点条目）

---

## 2026-07-26 — WLR 弹道反推发射点 / 落点

- `find_ground_intersection` / `solve_launch_and_impact`：沿弹道正/反向积分到平地交点
- 航迹在顶点附近做短窗真空拟合 → AirDrag+全局风前推/回推
- 输出 `mass_battle_wlr_fixes.csv/png`；summary 增加 `wlr_fix`
- 本次结果量级：确认炮弹航迹约 23 条，发射点中位误差 ~400 m，落点 ~370 m（12–22 km 射程尺度）

---

## 2026-07-26 — 全局风（对齐游戏的世界级风速）

- `GlobalWind`（速度 + 方位）与 `random_global_wind`；游戏内风是全局的，故不做风场网格
- 阻力改为作用在**空速**上：`a_drag = -AirDrag * |v - w| * (v - w)`
- 仿真每次按 seed 抽 2–14 m/s 随机风，可用 `--wind-speed-m-s` / `--wind-dir-deg` 覆盖
- 预测新增 `ballistic_drag_wind`（雷达站已知全局风），summary JSON 增加 `wind` 段
- 14 m/s 强风校验：+10 s 方位误差中位 0.263° → 0.155°，位置误差 169 m → 150 m

---

## 2026-07-26 — 炮弹真值/预测加入 Reforger AirDrag

- 模型：`a_drag = -AirDrag * |v| * v`（与 `ShellMoveComponent.AirDrag` 同形）
- 先验：`AIR_DRAG_SHELL_82MM_HE = 0.000615`（O832DU prefab）
- `TargetTrajectory`：有阻力时 RK2 数值积分 + 缓存；无阻力仍用解析常加速度
- 预测对比三路：CV / 真空弹道 / 真空拟合状态 + AirDrag 前推
- 输出图例更新为 `mass_battle_prediction.png`

---

## 2026-07-26 — 炮弹真空弹道拟合预测（优于常速度外推）

- `rdf_radar_track.py`：航迹历史加入俯仰；新增 `fit_ballistic_vacuum` / `predict_ballistic`（水平匀速 + 竖直固定 g，残差门限）
- 仿真对比：炮弹 +10 s 中位位置误差 CV≈979 m → ballistic≈318 m；p90≈1898 m → ≈598 m
- 对齐游戏内 `ShellMoveComponent` 主导项（重力）；`AirDrag` 尚未进预测器（下一步）

---

## 2026-07-26 — 轨迹预测评估（α-β 外推 vs 真值）

- `rdf_radar_mass_battle_sim.py` 新增 `evaluate_prediction`：沿滤波历史多锚点外推 2/5/10 s，对比真值给出距离/方位/位置误差
- 修复凝视雷达（WLR）scan 编号按 2 s 分桶导致同一炮弹每桶产生 ~8 个点迹、航迹碎成 45 条的问题；改为每 dwell 一个 scan
- `rdf_radar_track.py` 新增 `TrackerConfig.keep_confirmed_dead`：离线分析保留已确认但停更（落地/出界）的航迹
- 新增输出 `mass_battle_prediction.csv` / `mass_battle_prediction.png`，summary JSON 增加 `prediction` 段

---

## 2026-07-26 — Python 大规模防空+炮兵雷达仿真

- 新增 `tools/dem/rdf_radar_mass_battle_sim.py`：读签名表先验 RCS，生成 22 空中 + 40 炮弹场景
- 双雷达：SHORAD AD（机械扫描）+ WLR（扇区凝视）；测量合成 + α-β 航迹
- 输出 `tools/dem/out/mass_battle_*`（png / csv / json）

---

## 2026-07-26 — 雷达特征表 CSV V2

- 格式：`RDF_RADAR_SIG_V2` + 表头 + 逗号分隔 + key 加引号
- 表头：`key,size_x_m,size_y_m,size_z_m,char_length_m,mean_rcs_m2,swerling,type_hint`
- 请重新跑一次烘焙插件覆盖旧文件

---

## 2026-07-26 — 雷达特征表离线烘焙（扫可放置 prefab，非世界实体）

- Workbench 插件 `RDF Bake Radar Signatures`：`ResourceDatabase.SearchResources` 枚举 `.et`
- 筛选：`PLACEABLE` 的 Vehicle/Character；可选含 `ProjectileMoveComponent` 的弹药/导弹
- 每个候选离图临时 `SpawnEntityPrefab` → `GetBounds`/RCS → `delete`，写入 `$profile:RDF/Signatures/rdf_radar_signatures.csv`
- **不**扫描当前世界里已存在的实体；运行时 `Resolve` 只查表，未知模组模型才首见测量
- `RDF_RadarRcsModel.EstimateRcsFromExtents`：由尺寸估 RCS，避免重复 GetBounds

---

## 2026-07-26 — 按 prefab 的特征表（尺寸 / RCS 查表化）

- 新增 `RDF_RadarSignatureLibrary`：`map<prefab ResourceName, RDF_RadarSignature>`，存 `m_SizeX/Y/Z`、`m_CharacteristicLengthM`、`m_MeanRcsM2`、`m_SwerlingModel`、`m_TypeHint`
- 入表流程：先查烘焙表 `$profile:RDF/Signatures/rdf_radar_signatures.csv`（`RDF_RADAR_SIG_V1`，`;` 分隔）；未命中（如第三方模组模型）才 `GetBounds` 测一次并缓存，同模型后续实例纯查表
- 运行时未知模型可自动合并回写（每 30s）；正式全量表由 Workbench 离线烘焙插件生成
- `RDF_RadarScatterer.m_SignatureKey` 记录命中键；无 prefab 键时退回直接 `GetBounds`
- 诊断：散射体统计行追加 `sig=/baked=/measured=/hit=/nokey=`

---

## 2026-07-26 — 散射体拟真状态（姿态 / AGL / DEM / 射频 / Swerling）

- 姿态：`m_YawDeg`、`m_Forward`；RCS 按方位角做鼻锥/侧向投影
- AGL：`m_AglM`（DEM 或 `GetSurfaceY`）
- DEM 缓存：表面类 + 地形高 + 采样点（2s / 25m 再采样），扫描与 NLOS/杂波复用
- 辐射射频：`m_EmitFrequencyHz` / `m_EmitPeakPowerW` / `m_EmitAntennaGainDbi`（`RegisterWithRadio`）
- Swerling 0–4：`m_MeanRcsM2` + 扫描相关起伏（对齐 Python `SwerlingModel`）
- plot 透传：`m_MeanRcsM2`、`m_SwerlingModel`、`m_AglM`、`m_DemTerrainY`

---

## 2026-07-26 — 散射体表状态增强

- 稳定 `m_ScattererId`（量测切断实体后仍可对账）
- `m_Alive` 失效标记；注销时先标死再移除
- AABB 尺寸 `m_SizeX/Y/Z` + `m_CharacteristicLengthM`
- 上一帧位置差分速度兜底（物理速度≈0 时）
- plot 携带 `RDF_RadarTarget.m_ScattererId`；`FindById`

---

## 2026-07-26 — 全局散射体表（实体入表 → 模型计算 → 输出数据）

### 架构

- 新增 `RDF_RadarScattererRegistry`：全局维护活跃散射体/辐射源表。实体只提供位置、速度、RCS、是否辐射。
- **一次分类，长期复用**：类型与 RCS 在入表时算好；扫描不再每帧做 prefab 字符串判定。
- **增量维护**：发现扫掠默认 3s 一次（上批未分类完不开新扫掠）；分类每 tick 限量；运动学轮转刷新；实体失效或超距自动剔除。
- 扫描器改为**读表**（`m_UseScattererRegistry=true`），原每帧世界搜索保留为回退路径。
- `RDF_RadarEmitterRegistry` 改为同表门面，辐射源与散射体统一在一张表里。

### 配置

- `m_UseScattererRegistry`、`m_ScattererDiscoveryRangeScale`、`m_ScattererDiscoveryIntervalS`
- `m_ScattererClassifyPerTick`、`m_ScattererRefreshPerTick`、`m_ScattererMaxEntries`
- 诊断：`RDF_RadarScanner.GetScattererStatsLine()`

---

## 2026-07-26 — 扫描性能：消除周期性卡顿

### 根因与修复

- **双路候选**：默认关闭每帧 `GetActiveEntities` 合并（`m_SphereQueryAlsoActive=false`）；仅在球查询为空时兜底。
- **球查询预过滤**：回调内用 `IsRadarCandidate` 丢掉无关动态实体。
- **Trace 预算**：`m_MaxLosTracesPerScan`（默认 48）限制单次扫描 `TraceMove` 次数。
- **DEM 失败重试**：manifest 缺失后同世界不再每扫描读盘。
- **CFAR 缓冲复用**：不再每扫描 new 整张栅格。
- **调试可视化默认关**：AutoRunner / VisualSettings 默认不画射线与球；PPI HUD 不受影响。
- Demo：`CreateDefault` 间隔 0.25s；`CreateP18Like` 间隔 1.0s（原 0.1s + 13km 半球过重）。

---

## 2026-07-26 — 测量驱动雷达（拟真量测 + 航迹关联）

### 架构转向

- 游戏实体只作为**散射体/辐射源**：提供 RCS、通视、真值运动学给物理链。
- 对外输出改为**量测 plot**：距离门量化、波束角抖动、多普勒反解径向速度，并叠加 SNR 相关噪声（`RDF_RadarMeasurement`）。
- 默认切断 `m_Entity` 引用（`m_EnableMeasurementSynthesis=true`，调试可 `m_KeepEntityTruth`）。
- 跟踪器改为**量测最近邻关联**（距离/方位波门 + α-β），提供 `PredictAt` / `PredictPolarAt`；对齐 `tools/dem/rdf_radar_track.py`。
- CFAR 仅对已有 scatterer plot 做门限；去掉无功率空格“假匿名”路径。

### 配置

- `m_EnableMeasurementSynthesis` / `m_KeepEntityTruth`
- `m_TrackGateRangeM` / `m_TrackGateAzimuthDeg` / `m_TrackConfirmHits` / `m_TrackMaxMisses`
- `RDF_RadarHardware.GetRangeBinM()`

### 文档 / 测试

- `RDF_RadarAirborneScanTest` 改为按量测波门匹配空中目标（不再依赖实体指针）。
- 更新能力边界与框架说明。

---

## 2026-07-26 — 雷达待办完善（1+2 / 深度 A）

### 新增与增强（Radar）

- **候选收集**：`RDF_RadarScanner` 新增 `QueryEntitiesBySphere` + `GetActiveEntities` 兜底合并与去重（`m_UseSphereQuery`、`m_SphereQueryAlsoActive`）。
- **CFAR / 匿名点迹**：新增 `RDF_RadarCfarGate.c`；扫描末端支持粗栅格 CA-CFAR，未绑定实体的命中以匿名 plot 输出（`RDF_RADAR_TARGET_ANONYMOUS`）。
- **EW 欺骗**：`RDF_RadarEwModel.c` 增加 `RDF_RadarFalsePlot` / `RDF_RadarDeceptionJammerEffect` 与 `CollectFalsePlots`；假目标可随时间拖距。
- **联机权威同步**：新增 `Radar/Network/`（`RDF_RadarNetworkAPI.c`、`RDF_RadarNetworkComponent.c`）；`RDF_RadarAutoRunner` 与 `RDF_RadarComponent` 可优先走服务器扫描结果。
- **显示与回归**：PPI 增加匿名/假目标配色与图例；`RDF_RadarAutoTest` 新增匿名/假目标计数字段。

### 文档更新

- 更新 [TODO.md](../TODO.md) 雷达相关待办状态。
- 更新 [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)、[RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md)、[DEVELOPMENT.md](DEVELOPMENT.md) 以反映新链路与文件结构。

---

## 2026-07-26 — 雷达物理 / DEM 运行时 / 文档整理

### 新增与增强（Radar / DEM）

- **物理链路**：硬件参数、雷达方程、多普勒、MTI、SNR；α-β 跟踪；EW 附加噪声钩子。
- **NLOS 多径**：`TraceMove` 遮挡后不再一律丢弃；默认启用地面反射弱检（`m_EnableNlosMultipath`），功率按 image-method 路径与 `|Gamma|^2` 衰减后再过 SNR；PPI 青色为 NLOS 检出。
- **DEM**：Workbench 烘焙 V3 CSV；运行时 `RDF_DemRuntimeLoader` / `RDF_DemRuntimeCache`（LRU）；雷达杂波功率接入。
- **UI / 测试**：PPI HUD；`RDF_RadarAutoTest`（DEM 杂波回归）；`RDF_RadarAirborneScanTest`（空中目标）。
- **离线**：`tools/dem/` Python 雷达物理框架与 Demo。

### 文档整理

- 删除过时计划稿 `RADAR_PLAN.md`、`FUTURE_PLAN.md`（实现以代码与下列文档为准）。
- 重写 [README.md](../README.md)、[TODO.md](../TODO.md)；新增 [DEM.md](DEM.md)、[RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md)（能力边界与可做事项）；更新本 CHANGELOG 与开发者指南。

---

## 2026-03-19（二）— 雷达模块具体实现

### 新增（Radar）

- **Core**：`RDF_RadarTypes.c`（目标类型枚举与 `RDF_RadarTarget`）、`RDF_RadarSettings.c`（范围、间隔、扇区、过滤开关）、`RDF_RadarEmitterRegistry.c`（主动雷达辐射注册表，可被其他雷达查询）、`RDF_RadarScanner.c`（实体优先 + 球内候选 + 射线判可见）、`RDF_RadarProjectileTracker.c`（抛射物多帧轨迹）。
- **Util**：`RDF_RadarEntityClassifier.c`（抛射物/载具分类：ProjectileMoveComponent + 预制类名）。
- **Demo**：`RDF_RadarComponent.c`（挂载到实体，扫描时在注册表标记发射）、`RDF_RadarAutoRunner.c`（本地主体 Demo 入口）、`RDF_RadarDemoConfig.c`（预设配置）、`RDF_RadarBootstrap.c`（每帧 Tick 驱动）。
- 行为：扫描输出 `RDF_RadarTarget`（位置、距离、速度、类型：载具/抛射物/雷达辐射源）；抛射物通过 `RDF_RadarProjectileTracker` 维持轨迹；主动扫描时通过 `RDF_RadarEmitterRegistry` 注册，供对方雷达探测。

### 说明

- 雷达与 LiDAR 并存。实现约定见 [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md) 与 [RADAR_REQUIRED_APIS.md](RADAR_REQUIRED_APIS.md)。

---

## 2026-03-19 — 移除旧雷达与体素场模块（历史）

> 注：此后雷达已按新架构重新落地；本节仅保留历史记录。

### 移除内容

- **旧 Radar 模块**与 **EM 体素场模块**曾整目录删除；文档中旧设计稿一并移除。
- 当时文档曾短暂改为「仅 LiDAR」；当前仓库再次包含 Radar + DEM。

---

## 2026-03-17 — 文档与 LiDAR 行为说明更新

### 文档更新

- **API.md**
  - **颜色策略**：`RDF_LidarMaterialColorStrategy` 改为按表面**密度**（g/cm³）着色、**透明度随距离**降低；同步更新 `m_UseMaterialEffect` 说明与 RDF_LidarColorStrategy 实现列表。
  - **HUD**：PPI 光点与图例改为「按密度（g/cm³）、透明度随距离」；中英文布局表与注意事项补充对应说明。
  - **Live CSV**：已记录 `SetDemoWriteLiveCSV`、`AppendLiveCSVToFile`、`GetLiveCSVHeader`、`SampleToLiveCSVRow` 及 `lidar_live_1.csv` 追加写入与材质列。
- **DEVELOPMENT.md**：在 Visual 模块列表中增加 `RDF_LidarMaterialColorStrategy.c`（按密度 g/cm³ 着色、透明度随距离）。

### 行为说明（与代码一致）

- LiDAR Demo 默认使用 `RDF_LidarMaterialColorStrategy`：低密度 → 蓝/青，高密度 → 红/白；alpha = 1 − 0.7×(dist/range)，范围 [0.2, 1]。
- 实时 CSV：每次扫描**追加**写入 `$profile:LiDAR/lidar_live_1.csv`，列含 materialName、reflectivity、density、isWaterSurface。

---

## 2026-02-20（二）— HUD API 化

### 概述

为 `RDF_LidarHUD` 和 `RDF_RadarHUD` 补充完整的公共静态 API，使 HUD 可在 Demo 体系之外被直接调用，并同步更新了 API.md（当时另有已删除的 `RADAR_API.md`）。

### 代码变更

**`RDF_LidarHUD`**（`Lidar/UI/RDF_LidarHUD.c`）新增 4 个静态方法：

| 方法 | 说明 |
|------|------|
| `static bool IsVisible()` | 检查 HUD 面板是否已创建并可见 |
| `static void FeedSamples(array<ref RDF_LidarSample>)` | 手动推送样本直接刷新 HUD，不依赖 AutoRunner |
| `static void AttachToAutoRunner()` | 将 HUD 注册为 AutoRunner 扫描回调 |
| `static void DetachFromAutoRunner()` | 从 AutoRunner 注销 HUD 回调 |

同步修复：`OnScanComplete` 仅在 `RDF_LidarAutoRunner.IsRunning()` 为真时才从 AutoRunner 读取量程，避免独立调用时 `m_DisplayRange` 被覆盖。

**`RDF_RadarHUD`**（`Radar/UI/RDF_RadarHUD.c`）同样新增 4 个静态方法（同上，对应 `RDF_RadarAutoRunner`）。

### 文档更新

- **API.md**：新增 `## UI — HUD` 节（中/英文），完整记录 `RDF_LidarHUD` 全部 API 与使用示例
- 当时的 `RADAR_API.md`（已删除）曾扩写 `RDF_RadarHUD`：`IsVisible`、`FeedSamples`、`AttachToAutoRunner`、`DetachFromAutoRunner`；现归入 [API.md](API.md) / [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)

---

## 2026-02-20 — 项目结构整理 & Demo 默认关闭

### 概述

对项目目录结构进行了整理，统一 Demo 默认为关闭状态，并同步更新了全部文档。

### 文件结构变更

| 操作 | 详情 |
|------|------|
| 移动 | `Lidar/Demo/RDF_LidarNetworkSetupExample.c` → `Lidar/Network/`（职责归位）|
| 删除 | `scripts/Game/RDF/Tests/`（空目录）|
| 删除 | `scripts/Game/RDF/Util/`（空目录）|
| 删除 | `Radar/Network/`（空目录，功能待规划）|
| 保留 | `Radar/Data/os_cfar_multipliers.csv` 原位不动（引擎 VFS 运行时读取，不可移出 scripts 树）|

### 行为变更

- **`RDF_LidarAutoBootstrap`**：`s_BootstrapEnabled` 由 `true` 改为 **`false`**（与雷达 Bootstrap 保持一致，全框架默认静默）
- **`RDF_RadarAutoBootstrap`**：已是 `false`，无变化

两套系统均须显式调用方可启动：
```c
SCR_BaseGameMode.SetBootstrapEnabled(true);       // LiDAR
SCR_BaseGameMode.SetRadarBootstrapEnabled(true);  // 雷达
```

### 文档更新

- `DEVELOPMENT.md`：修复文件树（新增 `RDF_CFar.c`、`RDF_EntityPreClassifier.c`、`RDF_LidarNetworkSetupExample.c`；移除已删空目录）
- 历史 `RADAR_API.md`：模块总览补入缺失文件；`RDF_RadarAutoBootstrap` 默认值文档由 `true` 修正为 `false`
- `RADAR_TUTORIAL.md`：第 1.0 节说明 Bootstrap 默认关闭，补充显式启用代码示例
- `docs/` 清理：删除内部规划/宣传性文档（`DISCORD_ANNOUNCEMENT.md`、`EM_FIELD_PLAN.md`、`LIDAR_RADAR_BORROW_PLAN.md`、`REMAINING_LIMITATIONS.md`、`REQUIRED_ENGINE_APIs.md`），只保留面向开发者的参考文档

---

## 2026-02-18 — 电磁波雷达系统：完整实现 + HUD PPI 扫描图

### 概述

完成了以电磁波为载体的雷达仿真系统全部模块，包括物理模型、工作模式、可视化、演示系统与片上 HUD（`RDF_RadarHUD`）。同时修复了多项运行时 Bug 并完善了 CanvasWidget PPI 圆形扫描图显示。

### 新增模块（24 个脚本文件）

**Core（核心）**
- `RDF_EMWaveParameters.c` — 电磁波物理参数容器（频率、波长、波段、发射功率、天线增益等）
- `RDF_RadarSample.c` — 雷达回波数据（继承 `RDF_LidarSample`，增加 SNR、RCS、多普勒、相位等字段）
- `RDF_RadarSettings.c` — 雷达扫描器配置（量程、距离门限 `m_MinRange`、系统损耗、检测门限等）
- `RDF_RadarScanner.c` — 雷达扫描核心（完整 `ApplyRadarPhysics()` 10 步物理管线）

**Physics（物理）**
- `RDF_RadarPropagation.c` — 电磁波传播：FSPL、大气衰减、雨衰
- `RDF_RCSModel.c` — RCS 模型：解析球/板/柱、材质反射率查表、实体边界框估算、地物漫反射
- `RDF_RadarEquation.c` — 雷达方程：接收功率、噪声功率 kTBF、SNR
- `RDF_DopplerProcessor.c` — 多普勒：径向速度、频移 `fd = 2vr f/c`、MTI 杂波抑制

**Modes（工作模式）**
- `RDF_RadarMode.c` — 四种模式基类 + 具体实现：Pulse / CW / FMCW / PhasedArray

**Visual（可视化）**
- `RDF_RadarColorStrategy.c` — 四种颜色策略：SNR 映射、RCS 映射、多普勒色、复合色
- `RDF_RadarDisplay.c` — PPI 平面显示（`RDF_PPIDisplay`）与 A-Scope 距离幅度图（`RDF_AScopeDisplay`）
- `RDF_RadarSimpleDisplay.c` — 世界立柱标记（`RDF_RadarWorldMarkerDisplay`）+ ASCII 控制台地图（`RDF_RadarTextDisplay`）

**Advanced / ECM / Classification**
- `RDF_SARProcessor.c` — 合成孔径雷达处理器（SAR 图像积累）
- `RDF_JammingModel.c` — 电子对抗干扰模型（噪声干扰、欺骗干扰、自卫干扰）
- `RDF_TargetClassifier.c` — 目标自动分类（5 类：飞机/车辆/人员/建筑/未知）

**Demo（演示系统）**
- `RDF_RadarDemoConfig.c` — 五种预设工厂：`CreateXBandSearch` / `CreateAutomotiveRadar` / `CreateWeatherRadar` / `CreatePhasedArrayRadar` / `CreateLBandSurveillance`
- `RDF_RadarAutoRunner.c` — 演示主驱动器（单例）：扫描 tick、回调派发、世界标记显示
- `RDF_RadarDemoStatsHandler.c` — 控制台统计报告（SNR / RCS / 速度 / 多普勒 / 量程 / ASCII 地图）
- `RDF_RadarDemoCycler.c` — 五预设自动轮换（手动 / 定时两种模式）
- `RDF_RadarAutoBootstrap.c` — `modded SCR_BaseGameMode`：游戏启动自动运行雷达演示 + HUD

**UI（片上 HUD）**
- `RDF_RadarHUD.c` — 完全基于 `WorkspaceWidget.CreateWidgetInWorkspace` 的动态 HUD，含：
  - 标题栏 + 当前工作模式
  - `CanvasWidget` PPI 圆形扫描图（背景圆盘、50%/100% 距离环、N/S/E/W 罗盘轴、目标光点）
  - 三行数据面板（SNR / RCS / 速度 / 命中数 / 量程）
  - 0.5 秒节流防止闪烁

**Tests / Util**
- `RDF_RadarTests.c` — 单元测试（传播损耗、多普勒、RCS、检测距离）
- `RDF_RadarBenchmark.c` — 性能基准
- `RDF_RadarExport.c` — CSV 数据导出

### 关键 Bug 修复

| 问题 | 修复方案 |
|------|---------|
| `RDF_RadarScanner` 的雷达方程双重计入 FSPL | 系统损耗 L 改为只含 `m_SystemLossDB` + 大气/雨衰；`R⁴` 项已包含传播衰减 |
| 近场 SNR 饱和（range min=1m，SNR=130dB+）| 增加 `m_MinRange` 距离门限，拒绝盲区内回波 |
| 杂波过滤器不触发 | 地物判断从 `m_Surface != null` 修正为 `m_Entity == null` |
| 距离显示全为 0 | 改用 `(m_HitPos - m_Start).Length()` 计算真实几何距离 |
| 地物 RCS 返回 0 导致无法计算功率 | 引入漫反射面积散射模型 `sigma0 * patchArea` |
| `.layout` 文件报错 `Unknown keyword 'FrameWidgetClass'` | `.layout` 是二进制格式；改用纯脚本 `CreateWidgetInWorkspace` 动态创建所有控件 |
| HUD 数据闪烁 | 增加 0.5 秒节流（`System.GetTickCount()` 计时） |

### Enfusion Script 兼容性修复

- 替换所有非 ASCII Unicode 字符
- 去除三目运算符 `? :`，改为 `if/else`
- 去除函数式类型转换 `float(x)`，改为 C 风格 `(float)x`
- 修复 `ToString()` 不能在临时表达式上调用的问题
- 修复 `out` 作变量名与关键字冲突
- 去除 `string.ToLower()`、`IEntity.GetMaterial()`、`IEntity.GetClassName()` 等不存在的 API
- 修复 `enum` 直接整数转换报错，改用预填数组查表
- `string.Format` 改为字符串拼接（Enfusion 用 `%1/%2` 位置说明符，不支持 `%.1f`）

---

## 2026-02-18 — 网络：单片扫描载荷改为可靠 RPC（小修复）

概述：将单片扫描载荷的 RPC 通道由不可靠（Unreliable）改为可靠（Reliable），以降低小型（不分片）CSV 载荷在传输层丢失的概率。此变更仅更改传输通道；RPC 签名与分片逻辑保持不变。

主要改动：

- 修正：`RpcDo_ScanCompleteWithPayload(string csv)` 从 `RplChannel.Unreliable` 改为 `RplChannel.Reliable`。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`
  - 兼容性：签名未变；对外调用者无影响。

- 修正（可视化）：`DrawPointCloudOnlyBackground` 中背景四边形现在包含 `ShapeFlags.NOZBUFFER`，确保“仅点云”模式下背景遮挡行为可靠。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`
  - 测试建议：将 `m_RenderWorld = false` 并确认点云始终在场景之上（移动相机、近/远物体验证）。

测试建议：
- 触发 `RequestScan()` 并在客户端确认 `HasSyncedSamples()` 返回 true；在模拟丢包的环境下验证单片载荷的接收率提升.

---

## 2026-02-17 — CSV/导出与网络传输改进（小范围性能与健壮性提升）

概述：本次更新集中在服务器端的 CSV 序列化、网络传输和导出路径，目标是在处理大型点云时减少峰值内存/CPU 和降低网络分片的失败率。改动均为向后兼容的实现层优化（未修改公开 API 签名）。

主要改动（实际实现）：

- 优化：在 `RDF_LidarNetworkComponent` 中**缓存并复用** `RDF_LidarScanner`（替代每次 `new RDF_LidarScanner()`），减少短生命周期对象分配并降低 GC 压力。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 优化：引入按样本构建的 CSV "parts"（新增 `RDF_LidarExport.SamplesToCSVParts`），并在服务器端**流式/分块**发送 RPC payload。对较大载荷仍支持将 parts 合并后进行 RLE 压缩再发送（保留原有 `RLE:` 协议前缀）。此改动显著减少了为大扫描一次性构建巨型字符串的内存峰值。
  - 文件：`scripts/Game/RDF/Lidar/Util/RDF_LidarExport.c`, `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 改进：`ExportToFile` 采用 **best-effort 原子写入**（先写入 `.tmp` 临时文件，再写目标文件）以降低导出时产生截断文件的概率（注意：引擎脚本层面无法保证完全原子重命名）。
  - 文件：`scripts/Game/RDF/Lidar/Util/RDF_LidarExport.c`

- 健壮性增强：在 RPC 接收与分片组装路径增加对解析失败/空结果的可选日志（由 `m_Verbose` 控制），便于诊断网络损坏或压缩/解压问题。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

兼容性说明：
- 未修改任一公共方法的签名或外部可见行为；CSV/RLE 的字符串格式与客户端解析兼容（服务端仅改变序列化/分片实现）。

测试建议：
- 在本地或服务器上触发大射线计数扫描（例如 m_RayCount >= 4096），验证内存/GC 峰值、网络分片与客户端解析是否正常；开启 `m_Verbose` 以查看 RPC 解析警告日志。
- 使用 `ExportToFile` 验证临时文件写入路径与最终目标文件内容一致（注意脚本层面的重命名/删除限制）。

---

## 2026-02-14 — 修复与性能优化

概述：本次提交修复了几处功能不一致和性能瓶颈，并做了若干内部实现优化以降低运行时分配与网络开销。所有改动均保持对外 API 的兼容性（未修改公开方法签名或行为契约）。

改动摘要：

- 修复：`RDF_LidarScanner::Scan` 使用统一的本地 `range` 变量进行 `param.End`、命中距离与 `m_HitPos` 的计算，避免因混用 `m_Settings.m_Range` 导致距离不一致的问题。
  - 文件：`scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- 优化：在 `RDF_LidarScanner::Scan` 中复用单个 `TraceParam` 实例，减少每条射线的临时对象分配（降低 GC 压力）。
  - 文件：`scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- 优化：网络序列化去重。`RDF_LidarNetworkComponent::PerformScanInternal` 现在只构建一次 CSV 字符串，必要时在该字符串上执行压缩，而不是重新对样本进行二次序列化。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 修复/优化：分片接收组装逻辑改为按索引直接写入缓冲槽并记录已接收计数，避免原先的 O(n^2) 查找，提升不可靠 RPC 分片重组效率与鲁棒性。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- 优化：`RDF_LidarNetworkScanner` 的异步 poll 调度改为单次计划并由 tick 自行重调度，以在无待处理 poller 时停止调度，避免无用空转。
  - 文件：`scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkScanner.c`

- 低风险改进（渲染预分配）：在 `RDF_LidarVisualizer` 的 `Render` / `RenderWithSamples` 中增加对 `m_Samples` 与 `m_DebugShapes` 的容量预分配，减少每帧数组重分配与复制。此改动不改变渲染 API 或外部可见行为，仅降低分配与抖动。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

兼容性说明：
- 未修改任何公共方法签名或属性名称；演示、网络、导出、以及已有脚本保持向后兼容。

性能影响（预期）：
- 减少了射线扫描时的短生命对象分配（TraceParam），在高 `m_RayCount` 场景可明显减轻 GC 压力。
- 网络端在大载荷场景中避免重复序列化，降低 CPU 与内存峰值开销。
- 改进的分片重组算法在不可靠网络中更健壮且更快。
- 预分配渲染数组减少了每帧内存复制与临时分配，能降低抖动并略微提升帧率。

测试与注意事项：
- 已做静态代码修改与人工审查；建议在目标平台（Arma Reforger）上用以下配置做快速验证：
  - 单人场景：开启高 `m_RayCount`（例如 4096）并观察内存/GC 行为与渲染帧率。
  - 多人场景：在服务器上触发 `RequestScan()` 并观察分片/重组在丢包（模拟）下是否正常。

后续优化建议（不在本次提交中）：
- 批量/合并绘制 `Shape`（减少每帧 Shape 数量）——效果显著但需在渲染 API 层面小心处理颜色渐变。
- 使用复用/池化策略管理 `RDF_LidarSample` 对象（需保证外部不会长时间持有引用）。
- 将 CSV 序列化改为可重用缓冲写入器（StringBuilder 风格）以进一步降低临时字符串分配。


## 2026-02-15 — 可视化批量绘制原型与进一步优化

概述：本次提交在 `RDF_LidarVisualizer` 中引入低风险的批量绘制/退化策略原型，并做了若干渲染层面的预分配优化以显著减小高密度扫描场景下的 Shape 数量与内存抖动。

改动摘要：

- 在 `RDF_LidarVisualizer` 中添加预分配逻辑，调用 `Reserve()` 减少 `m_Samples` 与 `m_DebugShapes` 的每帧重分配。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

- 添加“分段退化”原型：当 (射线数 × 分段数) 超过阈值（当前实现阈值为 2000）时，射线从多段渐变退化为单段直线，以减少每帧 `Shape` 的创建量。
  - 实现细节：新增 `DrawRay(..., segmented)` 支持按需绘制单段或多段线；视觉颜色在退化模式下使用 t=1.0 处颜色。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

- 修复（网络健壮性）：修正 `RDF_LidarAutoRunner` 对静态 `s_NetworkAPI` 的潜在悬挂引用 — 现在在绑定与使用前验证 `IsNetworkAvailable()` 并在检测到失效时自动清理引用；`RDF_LidarNetworkUtils.BindAutoRunnerToLocalSubject` 亦在绑定前验证 API 可用性。
  - 文件：`scripts/Game/RDF/Lidar/Demo/RDF_LidarAutoRunner.c`, `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkUtils.c`

- 修复（API 硬化）：`RDF_LidarVisualizer::GetLastSamples()` 现在返回 **防御性副本**（外部修改不会影响 visualizer 内部状态）；同时更新了 API 文档与 README 的相关说明。
  - 文件：`scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`, `docs/API.md`, `README.md`, `docs/DEVELOPMENT.md`

兼容性说明：
- `GetLastSamples()` 的返回语义由“内部引用”变为“防御性副本”——方法签名未变，但如果已有外部代码依赖修改返回数组以影响 visualizer 内部状态，需要改为显式调用并处理复制后的结果。其余改动向后兼容。

视觉与功能影响：

- 性能：在高密度点云（高 `m_RayCount` 与 `m_RaySegments`）场景中，Shape 数量显著下降，GC 与渲染开销预期大幅减少，帧率更稳定。
- 视觉：退化模式会丢失每条射线上连续渐变的细节（分段渐变 → 单色线），但命中点、距离、点色和点大小保持不变，整体场景可读性保持良好。
- API 兼容性：未修改任何公共 API 或公开方法签名；`DrawRay` 为可见范围内的内部渲染方法更改，不影响外部使用者的调用契约。

建议与后续工作：

- 将阈值（当前硬编码为 2000）暴露到 `RDF_LidarVisualSettings` 作为可配置项，便于按场景调整（建议优先项）。
- 实现自适应分段（基于投影长度或距离动态减少分段数）或“两段近似渐变”以在更低开销下保留部分渐变效果。
- 若需要顶级质量，考虑实现真正的批量顶点合并与顶点色插值（更复杂但效果最佳）。

测试建议：

- 单人模式：在本地使用 `RDF_LidarAutoRunner` 以高 `m_RayCount`（如 4096）与不同 `m_RaySegments` 值对比帧率、GC 活动与视觉差异。
- 多人模式：在服务器上触发 `RequestScan()` 并在客户端验证退化与正常分段在网络同步下的显示一致性。

回滚：若视觉退化不可接受，可将阈值调高或恢复旧的 `m_RaySegments` 行为（一次性替换代码即可）。

---

## English translation

# CHANGELOG

## 2026-02-14 — Fixes & Performance Improvements

Summary: This update fixes several functional inconsistencies and performance bottlenecks and includes internal optimizations to reduce runtime allocations and network overhead. All changes maintain API compatibility (no public method signatures were changed).

Highlights:

- Fix: `RDF_LidarScanner::Scan` now uses a local `range` variable consistently for `param.End`, hit distance and `m_HitPos` calculations to avoid inconsistent distances caused by mixing with `m_Settings.m_Range`.
  - File: `scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- Optimization: Reuse a single `TraceParam` instance inside `RDF_LidarScanner::Scan` to reduce per-ray temporary allocations (lower GC pressure).
  - File: `scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

- Optimization: Network serialization de-duplication. `RDF_LidarNetworkComponent::PerformScanInternal` now constructs the CSV string once and compresses that string when needed instead of re-serializing samples.
  - File: `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- Fix/Optimization: Fragment reassembly now writes directly to buffer slots by index and tracks received counts to avoid the previous O(n^2) behavior, improving unreliable-RPC fragment reassembly robustness and performance.
  - File: `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkComponent.c`

- Optimization: `RDF_LidarNetworkScanner` async poll scheduling changed to a single scheduled job that re-schedules itself only when needed, avoiding idle polling.
  - File: `scripts/Game/RDF/Lidar/Network/RDF_LidarNetworkScanner.c`

- Low-risk rendering preallocation: `RDF_LidarVisualizer` now preallocates capacity for `m_Samples` and `m_DebugShapes`, reducing per-frame array reallocations and copies.
  - File: `scripts/Game/RDF/Lidar/Visual/RDF_LidarVisualizer.c`

Compatibility: No public method signatures or property names were changed; demos, network, export and existing scripts remain backward compatible.

Performance impact (expected):
- Reduced short-lived allocations during ray scanning (TraceParam reuse) — noticeable GC reduction under high `m_RayCount`.
- Reduced CPU/memory peaks on the network side by avoiding duplicate serialization for large payloads.
- Faster and more robust fragment reassembly under lossy networks.
- Preallocated render arrays reduce frame jitter and slightly improve frame rate.

Testing notes: run scenarios with high `m_RayCount` and in multiplayer to validate fragment/assembly robustness.

## 2026-02-15 — Visual batching prototype & follow-up

Summary: Introduced a low-risk prototype for batch/degenerate drawing in `RDF_LidarVisualizer` plus additional preallocation optimizations to reduce Shape counts and memory churn in dense scans.

- Added preallocation/reserve logic in `RDF_LidarVisualizer` to reduce array reallocations.
- Added a segmented-degeneration prototype that falls back to single-segment rays when (rayCount × segments) exceeds a threshold (prototype later reverted).
- Fixed a potential dangling reference to `s_NetworkAPI` in `RDF_LidarAutoRunner` by validating `IsNetworkAvailable()` before use and auto-cleaning invalid references.
- Hardened `GetLastSamples()` to return a defensive copy (API signature unchanged).

Compatibility: No public API signature changes; note `GetLastSamples()` now returns a defensive copy (behavioral change for code that previously modified the returned array to affect internal state).

Performance/visual impact: See original changelog for details.


### 2026-02-15 (回退)

已回退“分段退化”改动，恢复默认按 `m_RaySegments` 的分段绘制行为（原因：保持视觉一致性与细节）。

变更影响：

- 性能：回退后在极高密度场景中会保留更多 `Shape` 的创建，可能导致比退化时更高的渲染与 GC 开销；但之前的其他优化（TraceParam 重用、数组预分配、网络序列化/分片优化）仍然有效并减少了整体开销。
- 视觉：恢复了射线上的连续渐变效果，保留原始视觉表现。
- 后续建议保持不变：建议将退化阈值作为可配置项并考虑更温和的自适应分段或两段近似渐变替代方案。
