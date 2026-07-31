# CHANGELOG

## 2026-07-31 — MTD 多普勒通道 + 旋翼微多普勒

- `ERDF_MtiMode`：`TwoPulse`（默认，兼容旧 GBRS/demo）/ `MtdBank`
- Hardware：`m_DopplerBinCount`、`m_MtdClutterLeakage`、`m_PrfSetHz` / `m_PrfStaggerRatio`
- PhysicalDetect：目标取杂波加权最强多普勒 bin；杂波进零速 bin，非零 bin 仅泄漏
- Signature / scatterer：旋翼字段（tip speed / blades / RCS fraction / hub）；`Helicopters/*` 缺省 UH-1/Mi-8 类默认
- Clutter map EMA（`m_EnableClutterMap`）+ 航迹 miss coast（`m_TrackCoastOnMiss` / `m_TrackCoastOnDopplerNull`）
- 产品模式：`RDF_RADAR_MODE_PULSE_DOPPLER` + `CreatePulseDopplerSettings` / `DemoConfig.CreatePulseDoppler`
- NetCodec plot 同步 `m_DopplerBin` / `m_PrfIndex` / `m_DopplerHz`；SamEngage 改用 MTD
- 离线验收：`test_rdf_radar_mtd.py` + `test_rdf_radar_heli_cpa.py`（UH-1 CPA Pd）
- CI：`python-dem-tests.yml` 双 job（unittest discover + `full_sim` coverage）；`full_sim` 增 MTD/CPA/PRF 场景；`run_tools.sh`；sig-pack / track-coast 单测

## 2026-07-30 — tools 目录整理

- 新增 [tools/README.md](../tools/README.md) 分类目录（Pack / 仿真库 / CLI / 测试）
- `tools/dem/RADAR_FRAMEWORK.md` Layers 按同类重组；`run_tools.ps1`（test / full-sim / demo）
- `tools/dem/out/.gitkeep` + gitignore 微调（忽略产物、保留空目录）

## 2026-07-30 — Docs sync（全文档对齐）

- 对齐 Network：类型化 Rpc、Reliable 摘要 / Unreliable plots、规模旋钮；去掉过时 CSV / `M|T|K|W|L` 描述
- 补齐 Datalink / Fusion / 刀刃绕射 / EW 烧穿 / Python `full_sim` 交叉链接
- 更新：`RADAR_GAME_FRAMEWORK`、`README`、`RADAR_CAPABILITIES`、`API`、`TODO`、`REQUIRED_APIS`、`OPTIMIZATION`、`LESSONS`、`VEHICLE_RADAR_LOCK_GUIDE`、`DEM`、`DEVELOPMENT`、`RADAR_API` 措辞

## 2026-07-30 — Python 全能力离线模拟

- `rdf_radar_systems.py`：锁定 FSM、ESM Friis、RWR/ARM、GO/SO-CFAR、测量噪声、降雨、拖距/角闪烁/间歇、Network 带宽策略
- `rdf_radar_full_sim.py`：无 DEM 16 场景编排 + `out/full_sim_report.json`；覆盖矩阵见 `tools/dem/RADAR_FRAMEWORK.md`
- 单测：`test_rdf_radar_systems.py`（接入现有 `test_rdf_*.py` CI）

## 2026-07-30 — Network 规模对齐（官方风格）

- Reliable：只同步确认航迹 + 锁 + 扫描几何（`RpcDo_ReceiveScanSummary`）
- Unreliable：可选有上限 plots（`RpcDo_ReceiveScanPlots`，HUD）
- 降频 / 指纹跳过不变摘要 / 兴趣半径；数据链同样限频与上限
- Attribute：`m_MaxSyncedTracks/Plots`、`m_Min*BroadcastIntervalS`、`m_InterestRadiusM`、`m_SyncPlotsUnreliable`

## 2026-07-30 — Network / Datalink 官方复制路径

- 扫描结果：CSV `string` Rpc → 类型化 `array<int>` / `array<float>`（`RDF_RadarNetCodec`）+ `RplSave`/`RplLoad`（JIP）
- Proxy→Server 配置：`C|…` 字符串 → 定型 Rpc 参数
- Datalink：`D|`/`F|` CSV → 同 codec 类型化 Broadcast + bit 流 JIP

## 2026-07-30 — 数据链 + 多雷达融合

- `RDF_RadarDatalinkHub`：站间确认航迹摘要库（TTL）；轻量 `ERDF_RadarIff` + `RDF_RadarIffResolver`
- `RDF_RadarNetworkComponent` 权威扫描后 `PublishTracksToDatalink`；可选 `RDF_RadarDatalinkComponent` Broadcast
- `RDF_RadarFusionService`：跨站关联 + SNR 加权融合 + 水平双站交会（夹角门限）
- 回归：`RDF_RadarFusionAutoTest.Start()`（独立）；Python `test_rdf_radar_fusion.py`
- 与武器中制导 `WeaponBridge.TryGetMidcourseAim`「datalink」语义分离

## 2026-07-30 — 刀刃绕射近似（NLOS 加深）

- Trace 不通视时：`max(地面反射 bounce, 单刃绕射)`；绕射胜出标记 `m_BeamName …/diff`
- 沿程 DEM/`GetSurfaceY` 采样（默认 ≤8）+ Trace `hitFraction` 刃候选；Fresnel ν + ITU-R P.526 近似损耗
- Settings：`m_EnableKnifeEdgeDiffraction`、`m_KnifeEdgeMaxSamples`、`m_KnifeEdgeClearanceSlackM`（仍受 `m_EnableNlosMultipath` 总闸）
- Python：`rdf_radar_diffraction.py` + `test_rdf_radar_diffraction.py`

## 2026-07-30 — EW 噪声压制软化曲线 + 可观测

- `RDF_RadarNoiseJammerEffect`：耦合模式 `SEARCH_AVG`（默认）/ `BEAM` / `MAINLOBE_ONLY`；`m_SidelobeLevelDb`（默认 -40）；`m_CouplingGain`；`m_SearchDutyOverride`
- 预设：`ConfigureGameplaySearchAvg` / `ConfigurePhysicsBeam` / `ConfigureMainlobeOnly`
- `RDF_RadarEwBurnThrough.RangeM(R0,N,J)`：`Reff = R0·(N/(N+J))¼`
- Scanner：`GetEwStatsShort` / `GetLastJnDb` / `GetLastBurnThroughRangeM`；Sensor `GetStatusShort` 附加 `ewJN` / `Reff`
- Python：`rdf_radar_ew.py` 对齐 + `test_rdf_radar_ew.py`；framework demo 显式 `configure_physics_beam` 以保留硬压制演示

## 2026-07-29 — 地面 SAM 扫描/识别/打击演示

- 新增 `RDF_RadarSamEngageAutoTest.Start()`（独立，不进 StartAll）
- BTR70 作 SAM 站（`SetScanSubjectOverride`）；6 架绕轨 Mi-8
- 流程：SHORAD 搜索 → 航迹识别（载具类确认航迹）→ 自动锁 + Hydra PN 逐次交战
- 近炸命中后解锁并**解除该机脚本轨道控制**（实体留在场景里交给物理坠落），再交战下一架

## 2026-07-29 — HeliDuel 拦截不再脚本删威胁弹

- 近炸只引爆拦截弹；威胁弹交给引擎破片/爆炸销毁
- `ReleaseThreatToPhysics` 停止脚本驱动；结束后打印威胁是否被炸掉
- 拦截近炸门限收紧到 12 m，提高命中破片概率

## 2026-07-29 — HeliDuel 拉远距离 + 放慢节奏

- 蓝/红约 1.2 km；轨道更慢；弹速约 90/55 m/s
- 阶段停顿：锁后 4 s、命中后 5 s、威胁起飞前 3 s、来袭观察 6 s、拦截后 3 s
- 总时长约 140 s；每阶段各 1 发

## 2026-07-29 — HeliDuel 改为蓝机开火（非玩家）

- 雷达扫描主体：`RDF_RadarAutoRunner.SetScanSubjectOverride(blue Mi-8)`
- 交战/拦截 Hydra 从蓝直升机机外发射（`Launch` owner = blue）
- 结束时清除 scan subject override

## 2026-07-29 — HeliDuel 拦截阶段修复

- 进入 `INTERCEPT` / `LOCK_THREAT` 时重置开火冷却（此前被 ENGAGE 的 3.5s 冷却挡住）
- 威胁弹改为机外脚本驱动飞行（避开机体内生成被物理立刻删）
- 缓存威胁瞄点；实体丢失后仍可短时拦截；收紧“已锁威胁”判定

## 2026-07-29 — 修复 DiagMenu「Too many Diags」崩溃

- 原因：引擎 Diag 表硬上限 512；`RDF_LidarDiagMenu` 使用 ID 5800100 越界
- `EnsureRegistered` 改为空操作，不再 `DiagMenu.Register*`；bootstrap 不再自动注册
- 调参改走 `RDF_LidarSensor.ConfigureMode` / `RDF_LidarDemoConfig` / AutoRunner API

## 2026-07-29 — 直升机对战 / 拦截导弹自动化示例

- 新增 `RDF_RadarHeliDuelAutoTest.Start()`（独立，不进 StartAll）
- 流程：蓝/红 Mi-8 → `WeaponBridge` 锁红机开火 → 红机发射威胁 Hydra → 切弹丸锁 → 拦截
- 使用 `RDF_RadarWeaponBridge` 发射门控 + `RDF_RadarRocketGuidance` ARH PN

## 2026-07-29 — 锁定层火控桥（武器对接）

- 新增 `RDF_RadarFireSolution` + `RDF_RadarWeaponBridge`：Sensor 锁定 → 发射门控 / 中段瞄点 / ARM
- 新增 `RDF_RadarWeaponComponent`：挂载于载具；`TryGetFireSolution` / `GuideRocket` / `FindOn`
- LiDAR 近距回退：`TryGetLidarAim(RDF_LidarSensor, ...)`
- 文档：`VEHICLE_RADAR_LOCK_GUIDE` §2.3 改为火控桥最小路径；产品缺口勾选完成（prefab 仍属模组侧）

## 2026-07-29 — LiDAR Sensor 门面 + DiagMenu + 矩形 FOV

- 新增 `RDF_LidarSensor`：对齐 `RDF_RadarSensor` 的玩法门面（`ConfigureMode` / `Tick` / `ScanOnce` / `GetSamples` / `GetClosestHit`）
- 模式：`FULL_SPHERE` / `FORWARD_CONE` / `FORWARD_RECT` / `SWEEP` / `ENTITIES_NEAR`
- 新增 `RDF_RectangularFOVSampleStrategy`（车载前向矩形视场；默认 120°×25.4° @ 120×32）
- 新增 `RDF_LidarDiagMenu`（Workbench「RDF LiDAR」：Demo 开关、模式、射线、量程、Showcase、HUD）；`OnGameStart` 注册，边缘触发避免踩掉 bootstrap 配置
- `RDF_LidarAutoRunner.SetDemoHudEnabled`

## 2026-07-29 — 修复 LiDAR 材质密度全紫

- `RDF_LidarScanner`：恢复 `hitSurface = param.SurfaceProps`（官方写法）；`GameMaterial.Cast(SurfaceProps)` 恒为 null，导致密度着色全部退回中间紫

## 2026-07-29 — LiDAR Showcase 可视化

- 世界空间 Showcase：点云余晖、½/全量程距离环、Sweep/Conical 扇面（`RDF_LidarVisualSettings.ApplyShowcaseDefaults`）
- `RDF_LidarVisualizer`：静态/动态/余晖 Shape 分桶；`scanSerial` 门控余晖；`SetSweepGeometry`
- PPI：磷光余晖衰减 + 前向扫线；`TickAfterglow`（AutoRunner ~100 ms）
- Demo：`CreateDefault` 默认开 Showcase；`CreateShowcase`（Sweep+HUD）；`CreateWithHUD` 关世界 Shape、保留 PPI 动画
- `RDF_SweepSampleStrategy` / `RDF_ConicalSampleStrategy` 暴露半角与方位 getter

## 2026-07-29 — Sprint D：CFAR/track golden + 最小 CI

- 新增 `tools/dem/test_rdf_radar_cfar.py`：CA-CFAR 检出/虚警/杂波边缘 + 与 `RDF_RadarCfarGate` CA 公式对齐的 Enforce golden
- 新增 `tools/dem/test_rdf_radar_track.py`：关联门限、α-β 滤波、方位 wrap、真空弹道拟合/预测
- 新增 `.github/workflows/python-dem-tests.yml`：对 `tools/dem/test_rdf_*.py` 跑 unittest
- 本地：`cd tools/dem` 后 `python -m unittest discover -s . -p 'test_rdf_*.py' -v`

## 2026-07-29 — 待办重评估（Sprint D）

- [TODO.md](../TODO.md) §4 由「一律延后」改为 P1/P2/P3：下一波 = Python CFAR/track golden + 最小 CI；产品缺口 = 锁定对接武器；绕射/DEM span/融合/LiDAR/IFF 场景驱动；远程算力与游戏内无 Debugger 批跑压到 P3
- [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md)「只选三条」与 Sprint D 对齐；弱项表去掉已落地的 GO/SO-CFAR / 单雷达 Network

## 2026-07-29 — DEM 异步分帧预载

- `RUNTIME_DEM_PRELOAD_ASYNC`：按约 6ms/帧解码 SURF，避免开局卡死 10s+
- 扫图路径不再同步整图解码；未完成时走原 LRU，完成后切共享 RAM
- Stress/Play 若需立刻 resident，可 `FlushAsyncWarmPreload`

## 2026-07-29 — DEM 开局预热

- 权威端 `OnGameStart` +2s 调用 `RDF_DemRuntimeCache.WarmPreloadCurrentWorld`
- SURF 写入进程级共享 RAM；之后任意 Scanner `EnsurePreloaded` 直接复用（无需等首次开雷达）

## 2026-07-29 — DEM 预载仅权威端

- `RDF_DemRuntimeCache.EnsurePreloaded`：`RplMode.Client` / `!Replication.IsServer()` 跳过
- 单机 / Listen / Dedicated 仍预载；纯客户端收网络 plots，不占 SURF RAM

## 2026-07-29 — DEM 预载不计扫时

- `EnsureDemPreloaded()` 在 `ScanOnce` 墙钟之前执行；Stress 启动时显式预载后再开 soak
- 压测采样丢弃 >1000ms 的单次异常，避免预载污染 tickAvg

## 2026-07-29 — DEM/SURF 全图预载入 RAM

- `m_DemPreloadAll`（默认开）：首次扫描把整图载入内存
- SURF：扁平地表类网格（避免上万 tile 对象）；状态 `SURF RAM`
- 其它包：整图填入 tile cache；扫描中不再因换 tile 读盘/解析 JSON

## 2026-07-29 — Stress AutoTest（性能压测）

- 新增 `RDF_RadarStressAutoTest`（独立，不进 `StartAll`）：UAZ×24 + Mi-8×3、杂波×1.0、180° 搜索、Origin 强漫步
- Registry 预注入负载；度量 scan/tick avg·P95·max + hitch≥16/33ms
- 报告 `$profile:RDF/RadarTests/radar_stress_autotest_*.txt`

## 2026-07-29 — Play AutoTest：地面可检

- `DemClutterScale=0.25`（全量 1.0 会把地面蒙皮压到 SNR≪0）
- 地面俯仰瓣 + 更近 UAZ 布置；warmup 后再 Origin 漫步
- 日志增加 `groundSeen` / discovery 计数

## 2026-07-29 — Play AutoTest（贴近游玩路径）

- 新增 `RDF_RadarPlayAutoTest`：AutoRunner Tick + HUD + showcase + DEM 杂波 + 正常发现（不 Registry 注入）
- 负载：UAZ×6 + 绕飞 Mi-8；`OriginOffset` 漫步逼 DEM 换 tile
- 度量：`scanMs` + `RadarTick` 墙钟（含 HUD/可视化）；报告 `$profile:RDF/RadarTests/radar_play_autotest_*.txt`
- `StartAll` / `StartAllRealistic` 末步改为 **7/7 Play**（Perf 仍为 6/7）

## 2026-07-29 — Perf：结束后强制空闲（防 DEM 假死）

- Perf / Suite 结束后不再恢复 Demo/HUD；避免第一次 SURF/DEM dwell 像卡住
- Suite 在同步 Perf 后立刻进入下一步 / `done`（不依赖 Debugger 下可能暂停的 CallLater）
- HEAVY：Registry 注入 UAZ、单独 DEM warm scan；报告增加 batchAvgMs

## 2026-07-29 — AutoTest：性能开销

- `RDF_RadarSensor.GetLastScanDurationMs()`：每 dwell 墙钟耗时
- 新增 `RDF_RadarPerfAutoTest`：弹道微基准 + 轻载/重载（UAZ×8）扫描 avg/P95
- `StartAll` / `StartAllRealistic` 接入 Perf；报告 `$profile:RDF/RadarTests/radar_perf_autotest_*.txt`

> **Languages / 语言**: Historical entries are primarily **中文**. New entries should include **English summary bullets**. Full policy: [I18N.md](I18N.md).  
> 历史条目以中文为主；新条目应附英文摘要要点。完整约定见 [I18N.md](I18N.md)。

## 2026-07-28 — Docs bilingual (EN + 中文)

- EN: Document language policy (`docs/I18N.md`); major docs now ship English + 中文 sections
- 中文：文档语言约定见 `docs/I18N.md`；入口与核心文档改为中英双语对照

## 2026-07-28 — 文档与代码对齐

- EN: README / TODO / DEVELOPMENT / API / RADAR_* / OPTIMIZATION / VEHICLE_LOCK / tools DEM aligned to code — ESM·RWR·Rocket·MapOverlay, SURF JSON publish path, CA/GO/SO-CFAR, dual ShellFire error bands, PPI panel size, LiDAR layout HUD, `m_MaxRayCount` soft cap, Sensor RWR wrapper APIs, Python pack/signature script inventory
- 中文：README / TODO / DEVELOPMENT / API / RADAR_* / OPTIMIZATION / VEHICLE_LOCK / tools DEM：
  按当前代码补齐 ESM·RWR·Rocket·MapOverlay、SURF JSON 发布路径、CA/GO/SO-CFAR、
  双档 ShellFire 误差带、PPI 面板尺寸、LiDAR layout HUD、`m_MaxRayCount` 软封顶、
  Sensor RWR 包装 API、Python 打包/特征脚本清单

## 2026-07-29 — RCS 方位加入俯仰

- `RDF_RadarRcsModel`：`ElevationFactor` / `AspectFactor3D` / `AspectRcsFromExtents3D`（鼻锥·侧向·俯视投影）
- 散射体记录 `m_PitchDeg`；扫描传入 LOS 俯仰角
- Python `rdf_radar_channel.aspect_rcs_from_extents` 对齐；Ballistics AutoTest 增加回归

## 2026-07-29 — 特征表改为 Enfusion .conf

- `Signatures/rdf_radar_signatures.conf` + `.meta`（`{C8A3F15E902B47D1}`，759 条）
- 加载：模组 `.conf` → profile CSV → 遗留 `.sig.data`
- 已删除 `rdf_radar_signatures.sig.data`；打包：`python tools/dem/rdf_sig_pack_conf.py`

## 2026-07-29 — 删除模组内遗留 `.dem.data`

- 已删 `GM_Arland` / `GM_Eden` / `GM_Cain` 的 `.dem.data`（约 127 MB）
- 模组 `DemData/` 仅保留 SURF JSON（`surf_manifest.json` + `surf_chunks/`）

## 2026-07-29 — SurfaceTable 改为 Enfusion .conf

- `RadarData/SurfaceTable.conf` + `.meta`（`{B4E81F2A7C903D65}`）
- `RDF_RadarSurfaceTableConf` / `RDF_RadarSurfaceEntryConf`
- 加载：profile JSON → 模组 `.conf` → 模组 JSON → 内置

## 2026-07-29 — SurfaceTable：地表类 → 电磁参数

- `RadarData/SurfaceTable.json`（`RDF_SURF_TABLE_V1`）：σ⁰、gamma_k、clutter_scale、dielectric、roughness、attenuation
- `RDF_RadarSurfaceTable` 加载；`RDF_RadarClutterModel` 改查表
- 杂波路径应用 `attenuation_db_per_km`（双程）
- Python `Sigma0Table.from_json` 兼容该格式

## 2026-07-29 — 杂波：实时 GetSurfaceY + 地表类 SURF JSON

- 高度优先 `BaseWorld.GetSurfaceY`（官方地形）；地表类用 `RDF_SURF_JSON_V1`
- 查找顺序：profile/packaged `surf_manifest.json` → 全量 CSV/JSON/bin DEM → LIVE 回退
- `tools/dem/rdf_dem_pack_surface_json.py`；已生成 Arland(~2.3MB) / Eden·Cain(~20MB)
- HUD：`SURF` / `LIVE` / `DEM OK`

## 2026-07-29 — 模组内去掉 DEM CSV

- `DemData/` 仅保留 `GM_Eden` / `GM_Arland` / `GM_Cain` 的 `.dem.data`
- 特征表仅保留 `Signatures/rdf_radar_signatures.sig.data`

## 2026-07-28 — DEM / 特征表二进制可打包格式

- `RDF_DEM_BIN_V1`：每世界一个 `DemData/<world>/<world>.dem.data`（Seek 按 tile；无 span；高程 0.1 m）
- 运行时查找：profile CSV → profile/bin → 模组 bin → 模组 CSV
- `tools/dem/rdf_dem_pack_bin.py`；已生成 Arland/Eden/Cain 包
- `RDF_SIG_BIN_V1`：`Signatures/rdf_radar_signatures.sig.data`；Library 优先二进制，CSV 回退
- `tools/dem/rdf_sig_pack_bin.py`；工坊需对 `.data` Register `.meta`

## 2026-07-28 — 打包官方图 DEM 到模组

- 自 `$profile:RDF/DemData` 拷入模组根 `DemData/`：`GM_Eden`、`GM_Arland`、`GM_Cain`
- 未包含实验目录 `GM_Eden_noentity_*`

## 2026-07-28 — 开局静默：移除 LiDAR 网络预绑定

- `OnGameStart` 不再调用 `BindAutoRunnerToLocalSubject`
- 仅在 `RDF_LidarAutoRunner.StartAutoRun`（含 Bootstrap / `StartWithConfig`）时惰性绑定

## 2026-07-28 — 主动雷达弹制导（助推 / 中段 / 末段 PN）

- `RDF_RadarRocketGuidance` 改为 ARH 三段：BOOST 推力+弱 loft → MIDCOURSE 数据链拦截点 PN → TERMINAL 导引头视场内真 PN
- 状态机 `RDF_RadarRocketGuidanceState`；过载限幅、速度软封顶；偏锥丢失则 COAST
- `RDF_RadarRocketLockFireAutoTest` 接入相位日志与目标速度
- 近炸引信：进入 `PROXIMITY_FUZE_M`（15m）或触地时 `BaseTriggerComponent.SetLive` + `OnUserTrigger` 引爆 Hydra 战斗部
- 调参：末段锥角 55° + 0.9s 宽限、可重捕；近炸 28m + 掠飞起爆；中段增益/过载加强

## 2026-07-28 — 火箭锁射演示（Hydra70 + 脚本制导）

- 新增 `RDF_RadarRocketLockFireAutoTest.Start()`：扫描 → 锁定 Mi-8 → 发射 Hydra70 模型
- `RDF_RadarRocketGuidance`：官方制导不可用，脚本侧闭环制导（后改为 ARH PN）
- 独立回归（不进 `StartAll`）

---

## 2026-07-28 — Radar HUD 回退为单 PPI 圆盘

- 撤掉 A-Scope / R-D / Waterfall / SNR 热力侧栏
- 恢复 `RadarPPI.layout` + `RDF_RadarHUD` 为 **144×184 / 128×128** 单圈 PPI
- 删除 `RDF_RadarHudCharts.c`

---

## 2026-07-28 — AutoTest 地图叠加（M 键）

- 新增 `RDF_RadarAutoTestMapOverlay`：地图打开时用本地 `PLACED_CUSTOM` 标记
- **Lock / Airborne**：飞机位置、高度、航向；Lock 测试增加 RWR SEARCH/LOCK 断言
- **ShellFire**：真值发射/落点 + WLR 解算发射/落点标记
- 机型名由 `RDF_RadarSignatureLibrary.FormatDisplayName` 从 prefab key 解析，不再硬编码 Mi-8
- 飞机轨迹：历史采样点（`trail` / `.`）+ 恒速外推未来路径（`+` / `pred +25s`）
- 图标改用更易辨认的官方符号：飞机当前位置用军事符号 `AIR` + `ROTARY_WING`/`FIXED_WING`；轨迹 `circle`、预测 `waypoint`、落点 `cross`；标签用 `AC` / `past` / `pred` / `launch` / `impact`

---

## 2026-07-28 — RWR 被照射 / 被锁定告警

- 新增 `RDF_RadarRwr`：SEARCH / TRACK / LOCK 威胁表（TTL ~1.5s）
- 主动雷达每 dwell 写入；ESM 模式不写；`m_EnableRwrReporting`
- API：`HasSearchWarning` / `HasTrackWarning` / `HasLockWarning` / `CollectForVictim` / `GetStatusShort`
- Component：`HasRwrLockWarning()` 等对 owner 查询
- 回归：`RDF_RadarRwrAutoTest.Start()`（不进 `StartAll`）

---

## 2026-07-28 — ESM 模式 + 反辐射瞄点 API

- **ESM 功率**：`ReceivedPowerEsmW`（单向 Friis \(R^2\)）；`m_EnableEsmReceive`；`RADAR_EMITTER` 走侦收、跳过 DEM 杂波/MTI
- **模式**：`RDF_RADAR_MODE_ESM` / `CreateEsmSettings` / `RDF_RadarDemoConfig.CreateEsm`；ESM 平台不登记自身发射
- **ARM API**：`LockArmTrackId` / `GetArmAim` / `IsEmitting`；关辐射立即解锁（`SetArmRequireLiveEmitter`）
- **回归**：`RDF_RadarEsmArmAutoTest.Start()`（不进 `StartAll`）
- 文档：`RADAR_API` / `RADAR_CAPABILITIES` / `VEHICLE_RADAR_LOCK_GUIDE`

---

## 2026-07-28 — 可视化靠近官方（ShapeManager + Layout HUD）

- 新增 `RDF_DebugShapeManager`（对齐 `SCR_DebugShapeManager` 子集：`AddLine` / `AddPolyLine` / `AddSphere` / `AddCircleXZ` / `AddCircleArcXZ` / `Add` / `Clear`）
- `RDF_RadarVisualizer`：四层 Shape 改由 ShapeManager 托管；距离环 / 告警环 / 扇区弧走官方 `CreateCircle*`
- `RDF_LidarVisualizer`：Shape 生命周期改交 ShapeManager（batched mesh 逻辑不变）
- PPI HUD：`UI/layouts/RDF/RadarPPI.layout` / `LidarPPI.layout` + `Workspace.CreateWidgets`；公开 `Show`/`Hide`/`FeedScan`/`FeedSamples` 签名不变
- 文档：`DEVELOPMENT` / `RADAR_API` 登记 Common 与 layout 路径

---

## 2026-07-28 — Network Sprint：官方模式同步补强

- **RplProp**：关键 Settings 原子字段（扇区 / maxTargets / WLR / CFAR / SNR / Include*）+ `onRpl` 写回权威 Sensor；空回调已实现
- **Ask 配置**：`RpcAsk_SetDemoConfig` 同步上述子集并 `ApplyNetworkScalarsToSensor`（与权威 `SetConfig` 对齐）
- **载荷加厚**：`T|` 增加 velocity / az / el / radialSpeed / scattererId；新增 `K` 航迹、`W` WLR、`L` 锁摘要
- **Sensor 网络路径**：注入同步航迹/锁，跳过该帧本地 Tracker/Lock 重算
- **发射态**：`m_IsEmitting` RplProp + `EmitterRegistry`；`RDF_RadarComponent` 经 Network API 同步
- 文档：`RADAR_API` Network 节更新载荷 tag 与方法选型

---

## 2026-07-28 — 扫描公平性增量优化（Fair Cursor）

- `RDF_RadarScanner.ScanFromRegistry`：新增持久游标 `m_RegistryScanCursor`，Trace 预算触顶时下次从断点续扫，避免候选头部长期吃满预算
- 新增“脏候选优先 fresh”规则：炮弹 / 发射源 / 近距 / 高速目标优先走 full update，其余走 round-robin + 复用兜底
- 新增 `RDF_RadarSettings.m_FairScanCursor`（默认 `true`），可切回旧的从 0 号候选开始扫描行为
- 预算耗尽不再直接丢弃：先尝试 `TryGetReusedTarget` 回填，再结束本轮 fresh Trace

---

## 2026-07-28 — Showcase 可视化性能优化

- **Shape 批量化**：距离环 / WLR 告警环 / 扇区外弧改为单条 `CreateLines` 折线；扇区填充一次 `CreateTris`
- **静态几何缓存**：origin / range / yaw / half-angle 未变时不重建扇区与环
- **分层 Shape**：static / dynamic / afterglow / WLR；无新扫描时仅刷新余辉与 WLR
- **告警环段数解耦**：`m_WeaponLocateAlertSegments`（默认 12），不再跟 `m_RangeRingSegments`
- **Showcase 默认**：余辉上限 80、缎带默认关、环段 24、扇区段 12；余辉环形覆写避免 `Remove(0)`

---

## 2026-07-28 — Sprint C：阈值 / GO-SO-CFAR / 欺骗扩展 / WLR HUD

- **扫描阈值**下沉 `RDF_RadarSettings`：LOS 缓存、目标/物理复用、优先级距离与速度、频带间隔、fresh budget
- **CFAR**：`ERDF_CfarMode`（CA / GO / SO）；`m_CfarMode` 默认 CA
- **欺骗库**：`RDF_RadarRangeWalkOffEffect`、`RDF_RadarAngleScintillationEffect`、`RDF_RadarIntermittentFalsePlotEffect`
- **反炮兵 HUD**：PPI 发射/落点告警圈 + 连线；世界空间地面告警环（`m_WeaponLocateAlertRadiusM` / `m_WlrHudAlertRadiusM`）

---

## 2026-07-28 — RDF_RadarScanner 拆分

- 新增 `RDF_RadarScanGeometry`：LOS / 实体中心 / 速度辅助
- 新增 `RDF_RadarPhysicalDetect`：雷达方程、NLOS、DEM 杂波、大气衰、SNR
- `RDF_RadarScanner`：删除已迁出的 CFAR 遗留实现与未用候选缓冲；编排层约 1070 行

---

## 2026-07-28 — 规范性 / 整洁性修复

- 文档同步：`RADAR_CAPABILITIES` / `RADAR_GAME_FRAMEWORK` / `API` 雷达摘要 / `DEVELOPMENT` 树 / `DEM` 联机说明 / README 双档测试，对齐 TODO 与 RADAR_API
- `RDF_DemMaterialTable`：不再写 `TraceParam.ColliderName`（引擎 owned 缓冲反模式）
- Network API：新增 `SetEnabled` / `SetConfig`；`SetDemo*` 保留为 AutoRunner 兼容别名
- LiDAR Network 子树 Tab→4 spaces；`RDF_LidarScanPayloadBuffer` 命名；`ERDF_TraceTargetMode`；`RDF_LidarDemoCycler.c` 文件名对齐类名
- 双语注释清理；`tools/dem/requirements.txt` + 离线脚本列表补全

---

## 2026-07-27 — LiDAR Scan 堆损坏修复（Workbench）

- **根因**：`TraceMove` 起点落在 subject 壳体/AABB 内时，高频 Scan 可触发 SEH `0xc0000374`
- `RDF_LidarScanner`：按局部半轴推算出壳 `Start`（`m_StartClearanceM`）；复用成员 `TraceParam`；不再写 `owned ColliderName`
- `RDF_LidarSettings`：新增 `m_StartClearanceM`、`m_CaptureSurface`、`m_MaxRayCount`（默认软上限 4096）
- `ScanDirectedRays` / `TraceDirectedRay`：统一走同一安全 Trace 路径

---

## 2026-07-27 — Showcase 可视化去闪烁

- 世界 Shape 去掉 `ONCE`：在两次 AutoRunner 重绘之间保持可见（CallLater≈5 Hz 时不再闪）
- 余晖仅在新 `scanSerial` 时采样，避免同批点迹重复灌入

---

## 2026-07-27 — Radar Showcase 可视化 + PPI 右下压缩

- 世界空间 Showcase：扇区扫掠扇面、距离环、点迹余晖、锁定锥、弹道缎带、加大 WLR 标记
- `RDF_RadarVisualSettings.ApplyShowcaseDefaults` / `ApplyMinimalDefaults`；AutoRunner 默认 Showcase
- `RDF_RadarAutoRunner.SetShowcaseVisuals(bool)` 一键切换
- PPI HUD：缩小至 128×128，半透明，贴右下角（少挡视野）

---

## 2026-07-27 — 天气驱动雨衰 / 雾衰

- `RDF_RadarBallistics.SampleWorldWeather()`：一次采样风速、风向、`GetRainIntensity`、`GetFogAmount`
- `m_EnableWeatherDrivenRainLoss`：每扫描用雨强/雾量叠加到大气雨衰（晴空拟合仍独立）
- 逼真档默认开启（满雨 0.5 dB/km、满雾 0.05 dB/km）；理想档关闭以保证回归稳定
- 风速仍驱动弹道 / WLR（既有 `SampleGlobalWind`）

---

## 2026-07-27 — 测量误差模型扩展接口

- 新增 `RDF_RadarMeasurementModel` / `RDF_RadarDefaultMeasurementModel`
- 扫描管线：CFAR → **MeasurementModel** → Tracker/WLR（自定义噪声会影响跟踪与定位）
- `RDF_RadarSettings.m_MeasurementModel`；`Sensor.SetMeasurementModel` / `GetMeasurementModel`
- 默认仍走内置 `RDF_RadarMeasurement.Synthesize`（CRLB + 量化）

---

## 2026-07-27 — 逼真档真正加压

- `ApplyRealisticChannel`：默认开 CFAR；`m_MeasNoiseScale=3.5`；距离偏置 5 m；方位/俯仰偏置加大；轻雨衰 0.05 dB/km
- Lock / Airborne / ShellFire 逼真路径不再事后关掉 CFAR；DEM 杂波回归仍关 CFAR（测功率不测门限）
- ShellFire 逼真 WLR 误差带仍为 800 m

---

## 2026-07-27 — Sprint A/B：通道逼真 + 扫描可观测性

按 TODO 收益序落地：

- **扫描统计**：`reuse` / `fresh` / `skip` / `losHit` 写入 `Sensor.GetStatusShort` 与 PPI 模式行
- **双档通道**：`RDF_RadarSettings.ApplyIdealChannel` / `ApplyRealisticChannel`；套件 `StartAll()`（理想）与 `StartAllRealistic()`（逼真，ShellFire WLR 误差带 800 m）
- **测量噪声**：合成路径支持 `m_MeasNoiseScale` 与距离/方位/俯仰/多普勒偏差
- **CFAR 热噪声填空**：`m_EnableCfarThermalFill` 给空距离单元注入 ~noiseFloor 样本
- **大气衰减**：`m_EnableAtmosphericLoss` + 晴空 dB/km 拟合 + 可选雨衰；作用于接收功率

理想档保持逻辑闭环回归；逼真档让误差“合常理”。

---

## 2026-07-27 — 扫描优化：LOS 缓存 / 分片预算 / 物理复用 / 优先级

- `RDF_RadarLosCache`：短时复用 TraceMove 结果（原点/目标位移阈值）
- 多帧 `freshBudget`：近距/弹丸/高速每帧更新，远距低威胁降频并复用上帧点迹
- 低速目标短周期复用 `ProcessPhysicalDetection` 结果（SNR/杂波）
- `RDF_RadarScanPassContext`：规避 Enforce 16 参数上限

---

## 2026-07-27 — P1 工程核心 + 轻量项（A+B）

- AutoRunner 的 DEM 状态读取改走 `RDF_RadarSensor.GetDemStatusShort()`，去掉 Demo 层 `GetScanner()` 穿透。
- `RDF_RadarScattererRegistry` 增加 XZ 空间网格与 `CollectInSphere`，`ScanFromRegistry` 改走球域网格查询。
- `RDF_RadarScanner` 拆分协作类：`RDF_RadarCandidateCollect`（候选收集）与 `RDF_RadarCfarProcessor`（CFAR 门限）。
- `RDF_RadarNetworkComponent` 权威扫描改挂 `RDF_RadarSensor`，避免网络层和 Sensor 层双 Scanner 配置漂移。
- DEM 运行时加载新增双路径：优先 `$profile:RDF/DemData/`，缺失时回退 `DemData/`（模组内只读发布目录）。
- 仓库卫生：更新 `TODO.md` 勾选、补充本摘要，`LICENSE` 与 `LICENSE.txt` 仅检查未合并。

---

## 2026-07-27 — PPI 扫线 / 雷达视轴用错了坐标轴

Enfusion 实体朝向是 `GetWorldTransform()[2]`（与原版 heading / 迫击炮方位 / LOS 一致），
`[0]` 是右侧。RDF 此前把 `[0]` 当 boresight，导致：

- PPI 绿色扫线相对角色朝向偏约 90°
- 测试把目标放在 `mat[0]` 上仍能自洽通过，但和玩家视角对不齐

已改为 `mat[2]`：`GetSubjectForward`、Visualizer、ScattererRegistry、Lock/DEM/ShellFire 测试布置。

PPI 仍是**北向上**图：绿线指向世界中的雷达视轴，不是自由视角相机方向；角色扭头时绿线跟身体朝向，不跟准星。

---

## 2026-07-27 — 测试用例改走 `RDF_RadarSensor` 公共 API

按 [docs/RADAR_API.md](RADAR_API.md) 分层：测试 / 玩法代码应优先用 Sensor facade；
AutoRunner 只保留 Tick 宿主与 PPI HUD。

改动：

- Sensor 新增门面：`ClearTracks` / `ClearDemCache` / `ResetSession` / `GetDemStatusShort`
- DEM / Lock / Airborne / ShellFire / ManualDemo：
  - 配置：`sensor.Configure(...)`，预设从 `CreateSearchSettings` / `CreateStareSettings` / `CreateWlrSettings` 起步
  - 读数：`GetPlots` / `GetTracks` / `GetScanSerial` / `CountDetectedPlots` / `GetLockedTarget`
  - 清场：`ResetSession()`（替代直接碰 Tracker / Scanner）
- 不再经 `SetDemoConfig` / `GetLastTargets` / `GetTracker` 等演示壳层间接调用

---

## 2026-07-27 — LockTest `plots=0`：90° 波束 + DEM 杂波把目标埋了

根因由 DEM 测试自己的输出坐实：同一套 Shorad、同样 90° 方位波束

- `dem_on_scale1` `avgSNR=-28.6 dB`（杂波开）
- `dem_on_budget0` `avgSNR=+41.8 dB`（杂波关）

70 dB 的差距来自波束宽度：300 m 处 90° 波束的杂波单元横向 471 m，杂波截面比 Mi-8 本体大一个量级。
Lock 用的正是"90° 波束 + `EnableDemClutter=true` + `DetectionSnrDb=8`"，每个目标算出来 ≈ -28 dB，
全被门限刷掉，`KeepUndetected=false` 再丢弃 → `plots=0`。这不是 bug，是宽波束的物理后果。

修复 —— 给锁定测试真实的火控几何：

- 目标改飞**视轴中心跑道**：圆心在 boresight 上 800 m，半径 120 m → 方位摆动仅 ±8.5°
- 硬件改窄波束火控：方位 20°、带宽 20 MHz（距离分辨率 37.5 m → 7.5 m）、俯仰波束对准 11°
- 关 DEM 杂波并注明理由：杂波模型用目标的方向图增益照射地面块，没有俯仰分辨力，会把空中目标埋掉；
  杂波本身有独立回归（`RDF_RadarAutoTest`），此处测的是锁定状态机
- `KeepUndetected=true` 且 SNR 统计不再只取已检测点迹 —— 漏检时能看到差门限多少，而不是永远 -300

顺带修掉两个会造成**假通过**的问题：

- 套件里上一步遗留的航迹被 LockTest 继承：日志开头 `tracks=15` / `LOCK TRACKING` / `locked=14` 全是 DEM 阶段的残留，
  掩盖了真实的零检测。新增 `RDF_RadarProjectileTracker.ClearTracks()`，Lock 与 ShellFire 启动时清空
- DEM 的 `dem_off` 阶段 `targets=0`（发现扫掠还没找到目标），"低杂波"检查空转通过。
  改为先预热等待全部目标进表再进入 phase 0，并新增 `discovered_unaided` 检查

---

## 2026-07-27 — 移除测试对被探测物体的一切"探测助攻"

原则：测试不得给目标加辐射源、抬 RCS，或绕过发现流程直接写进散射体表。
目标只能靠自身回波被发现和检测。

清除的后门：

- `RDF_RadarAirborneScanTest`：Mi-8 之前 `Register(..., emitting=true)` 被当成雷达辐射源；改为纯蒙皮回波，`IncludeRadarEmitters=false`，分类断言收紧为"必须判为 VEHICLE 且 emitter 计数为 0"
- `RDF_RadarShellFireAutoTest`：删除 `TEST_SHELL_RCS_M2=0.5` 的 RCS 抬升（真实 82mm 迫击炮弹约 0.01 m²）与逐帧强制写表；反炮兵功率预算本身足够，无需给弹丸加成
- `RDF_RadarLockAutoTest`：删除 `Register()` 预置，Mi-8 必须被发现扫掠自行找到
- `RDF_RadarAutoTest`（DEM）：不再劫持 `MusicManager` / `MapEntity` 之类的世界实体当合成辐射源并瞬移；改为沿 boresight 阶梯距离生成 3 架真实 Mi-8，用它们自身回波测杂波

四个测试新增 `discovered_unaided` 检查：目标出现在散射体表即证明是发现扫掠自己找到的。

根因修复 —— 散射体表发现积压：

- 每轮扫掠把全世界实体重新入队（Eden 上 `pend=11428`），128/tick 分类要约 15 s 才排空，随后又整批重入队；新生成的目标实际上永远排在队尾。这正是各测试当初改用强制注册的原因
- `RDF_RadarScattererRegistry` 增加 `s_Seen` 备忘表：扫掠只入队没见过的实体。被距离裁剪或反注册时从备忘表移除，以便重新拾取
- 实体删除后键指针置空且无法按键删除，所以备忘表按 20 s 节奏由存活键重建
- 统计行新增 `seen=`

配套调整（属于雷达配置，不是目标加成）：

- 三个生成目标的测试延长时长（Lock / Air 35 s，DEM 每阶段 8 s），给冷启动的散射体表分类时间
- DEM 与 Airborne 关 MTI：DEM 目标按设计静止，且 MTI 会把杂波压到底噪，正是被测量；Airborne 轨道目标径向速度过零，MTI 盲速会掩盖真实漏检
- `RDF_RadarScattererRegistry.Register` 补充文档：仅供实体自报（如雷达把自己标为辐射源），禁止用于注入目标

---

## 2026-07-27 — LockAutoTest 改为物理检测 + 轨道 Mi-8

- 不再用静止地面车 + `EnablePhysicalDetection=false`（几何糊弄锁定状态机）
- 改为轨道 Mi-8、`EnablePhysicalDetection=true`；报告新增 `physical_snr` / `max_snr_db`
- 宽波束、关 CFAR；直升机可 `SetOrigin` 运动（地面车禁止）

---

## 2026-07-27 — 手动摆车 PPI 无点：MTI + 球查询回退

- 静止载具多普勒≈0 时 `MtiTwoPulseGain→0`，物理检测 SNR 被压穿门限 → `Det 0/0`
- `QueryEntitiesBySphere(DYNAMIC)` 常漏掉 GM 编辑器放置的车；球查询空结果时回退 `GetActiveEntities`

---

## 2026-07-27 — DEM AutoTest budget0 清缓存

- 杂波已正确：`scale1→scale5` 约 5×（7.9e-18 → 3.9e-17）
- `budget0` 假 FAIL：前几阶段散射体表仍保留 `demValid`，即使 `DemTileLoadsPerScan=0` 也继续算杂波
- `InvalidateDemSamples` + phase 切入时 `ClearDemCache`；刷新 DEM 时 cache miss 不再保留陈旧 `demValid`

---

## 2026-07-27 — DEM AutoTest 宽波束修复

- 根因：默认 Shorad 方位波束 2.5°，合成辐射源落在主瓣外 → `patternGain≈0` → `avgSNR=-300`、`avgClutter=0`（假 FAIL）
- `BuildBaseConfig`：方位 90°、加宽仰角波束；关 CFAR / 测量合成；天线抬高 12 m
- 合成辐射源改沿 boresight 阶梯距离放置（±小侧摆），运动用墙钟

---

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
