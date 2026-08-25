# RDF 全项目 C 代码评审汇总（2026-08-25）

**范围**：133 个 .c 文件，约 47,188 行（Common 1 + Lidar 34 + Radar 82 + DEM 14 + Workbench 2）
**方式**：6 个并行 review agent

## 一、严重问题（必须修）

### A. 物理/数值错误（静默错误结果，P0）

| # | 位置 | 问题 |
|---|------|------|
| A1 | `RDF_RadarHwCalib.c:123-127` | MTI 杂波基底公式漏 `(2π)^(2N)` 因子。N=1 低估 ~40×，N=2 低估 ~1559×，`SuggestMtiClutterFloor` floor 严重偏小，真实杂波下漏警。修：`x = 2.0 * Math.PI * sigmaFd / prfHz` 后再 `Pow`。 |
| A2 | `RDF_RadarBallistics.c:737-741` | Nelder-Mead 单纯形退化。4D 拟合 `{vx,vy,vz,drag}` 第 5 顶点与 init 完全相同，drag 维度无独立扰动，反推 WLR 退化为 3D 速度搜索 + drag 恒为先验。修：第 5 顶点 `dart = dragInit * 1.3`。 |
| A3 | `RDF_JpdaAssociator.c:323-327` | JPDA 叶子节点要求所有 plot 被分配，clutter=0 假设下 `nPlots > nTracks` 时关联完全失效。需引入 clutter 概率或允许未分配 plot。 |
| A4 | `RDF_RadarScattererRegistry.c:1090` | `GridKey` 整数溢出：`(ix+32768)<<16 | (iz+32768)`，正向范围只到 32767，cell 缩小或地图扩大时 cell 碰撞。改 64 位打包或加 Clamp。 |
| A5 | `RDF_DemRuntimeCache.c:1122` | `m_FullyResident = (loaded > 0)`，90% tile 失败仍标 true，`BeginScan` 据此把 budget 设 INT_MAX，遮蔽 holes。改 `failed == 0 && loaded > 0`。 |
| A6 | `RDF_DemTileBake.c:249` | 边界 cell `cx > s_BoundsMax[0]` 时 `continue` 不写入，最东/最南 tile 缺整行/列。改写占位行或缩 `s_TileCells`。 |

### B. 每帧分配热点（GC 压力，P1）

| # | 位置 | 问题 |
|---|------|------|
| B1 | `RDF_RadarSensor.c:838,908-913` | `PlanDwells` 每帧 4 个 `new array`。提升为 scratch 字段。 |
| B2 | `RDF_RadarProjectileTracker.c:793,797` | `RefreshWeaponLocates` 每帧 2 个 `new array`，且 `m_WlrPending` 旧引用丢弃。 |
| B3 | `RDF_RadarProjectileTracker.c:1139-1181` | JPDA 路径完全未走 scratch 复用（GNN 路径已复用），每帧 7+ array + N 个 `RDF_JpdaPoint`。 |
| B4 | `RDF_RadarScanner.c:1238` | `AddDeceptionFalsePlots` 每帧 `new array<ref RDF_RadarFalsePlot>`。 |
| B5 | `RDF_RadarCfarProcessor.c:39-42` | `ApplyGate` 每帧 2 个 `new array`。 |
| B6 | `RDF_LidarNetworkComponent.c:192-237` + `RDF_LidarExport.c:209-226` | 服务端每帧 CSV `csv += part + ";"` O(n²) 拼接，4096 ray × ~80B ≈ 320KB/scan，每帧触发 RLE。改 `array<string>` + `Join` 或预分配。 |
| B7 | `RDF_LidarNetworkComponent.c:328-350` | 分块组装 `assembled += part` 再次 O(n²)。 |
| B8 | `RDF_LidarHUD.c:621-625` | 每帧每 blip `new array<float>` + `new PolygonDrawCommand`，512 blip × 100ms ≈ 5120 次/秒。 |
| B9 | `RDF_LidarVisualizer.c:671-677` | `DrawBatchedMeshes` 每次 Render `new array<ref array<vector>>` ×2。 |
| B10 | `RDF_LidarScanner.c:152-163` | `TraceDirectedRay` 每次调用 3 个新数组。 |
| B11 | `RDF_RadarHUD.c:414-482` | `UpdatePPI` 每帧 `m_AllCmds.Clear()` + 重新 Insert 全部静态命令，per-blip `new array<float>`。 |
| B12 | `RDF_DemMaterialTable.c:33-67` | bake 热路径每 cell `new array<string>`（3 个函数）。改静态 buffer。 |
| B13 | `RDF_DemColumnSpans.c:219-241` | `FormatSpanFields` 每 cell 24 次字符串拼接。改 `StringBuilder`。 |

### C. 生命周期/资源泄漏（P2）

| # | 位置 | 问题 |
|---|------|------|
| C1 | `RDF_RadarAutoTestMapOverlay.c:216-223` | MapOverlay hook 永不注销，`RegisterHooks` 后 `StopInternal` 不 `Remove`，首次 Start 后终身存活。 |
| C2 | `RDF_RadarRwrAutoTest.c:137-141` + `RDF_RadarEsmArmAutoTest.c:142-148` | 删目标前未 `RDF_RadarScattererRegistry.Unregister`，残留 stale 条目（其它测试均成对）。 |
| C3 | `RDF_RadarAutoTest.c:212-227` | DEM AutoTest `StopInternal` 不 `ResetSession`，单独跑两次时第二次继承上次状态。 |
| C4 | `RDF_LidarNetworkScanner.c:113-153` | 静态 `s_Pollers` 永不释放，poller 持 `m_Subject`(IEntity) + `m_Scanner`(ref) 强引用，实体销毁后仍引用，模块卸载泄漏。 |
| C5 | `RDF_LidarAutoRunner.c:36` | 构造里 `CallLater(StaticTick, ..., true)` 永不 Remove，`StopAutoRun` 仅置 `m_Running=false`，tick 空跑。 |
| C6 | `RDF_RadarCfarProcessor.c:69-70` | `m_RowHits` 只增不减，azBinCount×rangeBinCount 变小时旧条目残留。 |
| C7 | `RDF_DemRuntimeLoader.c:463-477` | `s_WarnCount` 达 20 后永不复位，长 DS 运行后续 warn 全静默。 |
| C8 | `RDF_DemRuntimeCache.c:1400` | `m_TouchCounter` 单调递增无上限，长时 DS 溢出后 LRU 错乱。 |

### D. 网络/多人正确性（P0/P1）

| # | 位置 | 问题 |
|---|------|------|
| D1 | `RDF_RadarNetworkComponent.c:396-422` | `HasInterestedAudience` 用未初始化的 `m_LastScanOrigin`（首次为 `"0 0 0"`），玩家远离原点时首帧被跳过，JIP 拿不到首帧。改用 `GetOwner().GetOrigin()`。 |
| D2 | `RDF_RadarNetworkComponent.c:276-296` | `RequestScan` 无速率限制，恶意/失控客户端可触发扫描风暴。 |
| D3 | `RDF_RadarDatalinkComponent.c:162-167` | `RplSave` 用 `maxTracks=0, maxFused=0`（无限），JIP 序列化可达数十 KB。 |
| D4 | `RDF_RadarNetCodec.c:962-977` | `ReadDatalinkBits/ReadScanBits` 始终 `return true`，不检查 `ScriptBitReader` 返回值，损坏 JIP 数据被静默接受。 |
| D5 | `RDF_RadarEwModel.c:437` | `RangeWalkOffEffect.m_StartTimeS = 0.0` 默认（应 -1.0 懒锚定），任务 t=300s 创建时 plot 立刻飞到 24km 被门过滤掉，效果静默失效。 |
| D6 | `RDF_LidarNetworkComponent.c:328-350` | 缓冲清理用 `GetWorldTime()`（暂停冻结），与 poller 注释"必须用 wall-clock"矛盾，暂停时未完成 payload 永不清。 |
| D7 | `RDF_LidarNetworkComponent.c:278` | `RpcDo_ScanCompleteChunk` 用 Unreliable，`isLast` 丢失则 `m_ExpectedParts` 永不赋值，缓冲泄漏至超时。 |
| D8 | `RDF_LidarExport.c:284` | 网络反序列化丢 `m_End`/`m_Entity`/`m_Surface`/`m_ColliderName`，客户端 sample 与本地不对等，`MaterialColorStrategy` 在客户端因 `m_Surface=null` 全走 fallback 灰色。 |

### E. 采样策略边界（P4）

| # | 位置 | 问题 |
|---|------|------|
| E1 | `RDF_HemisphereSampleStrategy.c:11-15` | `z = t = (index+0.5)/count`，z→1 时 r→0，所有高索引 ray 挤极点，分布严重不均。应 `z = 1 - t` 或 `z = sqrt(1-t)`。 |
| E2 | `RDF_ScanlineSampleStrategy.c:15-22` | `count ≤ sectors` 时 lines=1、t=0、z=1.0，所有 ray 指向 +Z。应 `m_Sectors = Math.MinInt(m_Sectors, count)`。 |
| E3 | `RDF_ConicalSampleStrategy.c:25` | count=1 时 phi=0，所有 ray 同向。应退化为 `Vector(0,0,1)`。 |

## 二、重要问题（应修）

- `RDF_RadarScanner.c:660,665,820-828` maxTargets/LOS 预算截断时 cursor 推进偏 1，造成扫描空洞或公平性偏移。
- `RDF_RadarPatternLut.c:63-83` 硬件 HPBW/SLL 变更时 profile 存在则 LUT 不重建，运行时切 preset 不刷新。
- `RDF_RadarBallistics.c:743` Nelder-Mead 90 次迭代 × 12000 步 RK2 ≈ 5.4M 积分/弹/扫描，显著热点。
- `RDF_RadarMeasurement.c:120-123` `rangeBin` 未 clamp 到 `binCount-1`。
- `RDF_RadarClutterModel.c:49` `sin(thetaRef)` 零除未防。
- `RDF_RadarFusionService.c:97-98` `m_NextFusedId` 单调递增无回绕；`:262-271` `TryCrossFixHorizontal` det 阈值 1e-4 过严（≈0.006°）。
- `RDF_RadarAutoTestBatch.c:72,87-145` Batch `timed_out` 字段 `WriteStartFailedResult` 用字面量 `"false"`、`WriteResult` 用 `bool.ToString()`，不一致。
- `RDF_RadarShellFireAutoTest.c:658-689` 关联 gate 120m 过松，非弹种但靠近弹的 plot 被误计。建议收紧到 40m 或强约束弹种类型。
- `RDF_RadarAutoTestSuite.c:288-296` step 0 同步链无 gate 兜底，gate 被占时失败原因记成 "dem" 而非 "gate_busy"。
- `RDF_RadarPerfAutoTest.c:188-209` + `RDF_RadarDemLosBenchAutoTest.c:84-118` 不恢复 prior config，破坏 Suite 契约。
- `RDF_LidarNetworkComponent.c:148-149` `if (updateInterval > 0 && updateInterval < 0.01)` 死分支。
- `RDF_LidarHUD.c:483` + `RDF_LidarVisualizer.c:685` sample 首元素/迭代元素未判空，潜在 NPE。
- `RDF_DemTileBake.c:506-534` 死代码 `HasGroundObstacle` / `QueryObstacleCallback` / `NormalToSlopeDeg` 从未调用。
- `RDF_DemTileBake.c:252-298` 每 cell 4 次 `GetSurfaceY` + 1 次 `GetWaterSurfaceY` + 1 次 `TraceMove`，32×32 tile = 6144 次重调用/tile。
- `RDF_RadarLockManager.c:393-394` `m_ArmRequireLiveEmitter` 把配置标志同步到运行态 `m_ArmLockActive`，调用顺序敏感。
- `RDF_EccmDecisionLayer.c:51-54` `m_PrfActive = deceptionCount > 0` 无滞回，deceptionCount 在 0/1 间抖动时 PRF 捷变翻转。

## 三、模块级总结

| 模块 | 文件/行数 | 评价 | 主要短板 |
|------|-----------|------|----------|
| Radar/Core | 21 / ~392KB | 分层清晰，GNN 路径 scratch 复用范本，BudgetGovernor EMA+滞回成熟 | JPDA 路径未贯彻 scratch；`GridKey` 溢出；JPDA 叶子 clutter=0 失效 |
| Radar/Physics | 17 / ~183KB | 公式扎实，LUT 一致 clamp 外推 | MTI floor 漏 2π（量级错误）；Nelder-Mead 单纯形退化（WLR 失效）；PatternLut 不重建 |
| Radar/Demo | 30 / ~605KB | `discovered_unaided` 守护到位，gate/ResetSession/snapshot-then-clear 规范，PN 制导真实 | 3 处生命周期遗漏；2 处不恢复 prior config；P95/BoolLabel 9+ 份复制 |
| Radar/Net/EW/Fusion/UI | 14 / ~175KB | Reliable+Unreliable+RplSave 三层分流正确 | `HasInterestedAudience` 用未初始化 origin；`RequestScan` 无节流；`RplSave` 无封顶；Codec 不检查 reader；`RangeWalkOff` 默认 0.0 静默失效 |
| Lidar | 34 / ~158KB | 内存防护意识强（4096 上限、TraceParam 复用、shape 缓存） | 网络层全链路重灾区；HUD/Visualizer 未池化；Hemisphere/Scanline 分布退化 |
| DEM + Common + Workbench | 16 / ~316KB | manifest+chunk+LRU+async 预热成熟 | 边界 cell 丢失；`FullyResident` 判定过宽；bake 每 cell 重调用；warn 静默；touch 溢出 |

## 四、正面观察

1. LUT 外推保护：KnifeEdge/Pattern/SitePath 一致 clamp 到端点。
2. Enforce 类型陷阱意识：`RDF_RadarClutterModel.c:461` 显式 `(float)binCount` 防 int/int 截断；`RDF_RadarNetCodec.c:451-460` 改 `(int)Math.Round` 防哈希精度丢失。
3. GNN 路径 scratch 范本：`RDF_RadarProjectileTracker` 的 `m_ScratchPlots/PlotUsed/PairCosts` 并行 array 复用。
4. `discovered_unaided` 守护：跨 Demo 测试普遍通过 `ScattererRegistry.Find` 验证 sweep 自发现。
5. SEH 0xc0000374 防护：`RDF_LidarScanner` ray 起点推出 AABB 外；`RDF_DemMaterialTable` 注释解释 WST_NONE 下 TraceMove 崩溃根因。
6. BudgetGovernor EMA+滞回（0.25..1.0）避免单帧抖动 thrash。
7. 网络分层分流：Reliable（航迹+锁定）+ Unreliable（HUD plots）+ RplSave（JIP）符合官方模式。
8. 全项目未使用三元运算符，符合用户规则；终端命令也未使用 `&&`。

## 五、建议修复优先级

**P0（静默错误结果，立即修）**：
- A1 MTI floor 漏 2π
- A2 Nelder-Mead 单纯形退化
- A3 JPDA 叶子 clutter=0 失效
- A5 DEM `FullyResident` 判定过宽
- D1 `HasInterestedAudience` 未初始化 origin
- D5 `RangeWalkOffEffect.m_StartTimeS=0.0`
- D8 Lidar 网络反序列化丢字段

**P1（每帧分配热点，下个迭代修）**：
- B3 JPDA 路径 scratch 复用
- B6/B7 Lidar 网络 CSV O(n²) 拼接
- B8/B11 HUD per-blip 分配
- B12/B13 DEM bake 热路径 per-cell 分配

**P2（生命周期/资源泄漏）**：
- C1 MapOverlay hook 注销
- C2 Rwr/EsmArm Unregister
- C4 Lidar 静态 poller 泄漏
- C5 Lidar AutoRunner 永久调度

**P3（测试隔离一致性）**：
- C3 DEM AutoTest ResetSession
- I4/I5 Perf/DemLosBench 恢复 prior config
- 抽 `RDF_RadarTestUtil` 公共类消除 P95/BoolLabel 9+ 份复制

**P4（采样策略边界 + 数值 clamp）**：
- E1/E2/E3 Hemisphere/Scanline/Conical 退化
- A4 GridKey 溢出
- A6 DEM 边界 cell 丢失
- 各处 `Math.Max(eps, x)` 下界保护

---

整体评价：RDF 在 Reforger mod 中属上乘工程，模块边界清晰、性能优化意识强、测试守护到位。主要修复集中在 **2 条物理量级错误（P0）** 和 **Lidar 网络层重写（P1）**，其余多为生命周期一致性与 scratch 复用扩展。修完 P0+P1 后可作为可信生产基线。
