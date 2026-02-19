# LiDAR 借鉴雷达系统先进特性 — 实施计划

**文档版本**: 1.1  
**创建日期**: 2026-02-19  
**最后更新**: 2026-02-19  
**状态**: 部分已实施

---

## 一、背景与目标

雷达系统（RDF_RadarScanner）在继承 LiDAR 架构的基础上，增加了多项几何验证、后处理与可视化能力。本计划梳理雷达中可被 LiDAR 借鉴的先进方面，并按优先级制定实施路线。

---

## 二、雷达 vs LiDAR 能力对比（采纳状态）

| 能力 | 雷达 | LiDAR | 采纳状态 |
|------|------|-------|----------|
| 远距离门限（max-range 视为未命中） | ✅ | ✅ | **已实施** |
| 自命中过滤（平台/子实体排除） | ✅ | — | **引擎已实现**（TraceParam.Exclude），无需额外逻辑 |
| 实体预分类（Vehicle/Character/Building/Static） | ✅ | — | **不采纳**（现实无此功能） |
| 量程上限（Validate） | 100 km | 100 km | **已实施** |
| 颜色策略按实体类型 | ✅ | — | 不采纳（依赖实体预分类） |
| 扫描完成回调 + HUD 集成 | ✅ | ✅ | **已实施** |
| 工作模式（Pulse/CW/FMCW） | ✅ | N/A | 不适用 |
| 物理模型（RCS/SNR/多普勒） | ✅ | N/A | 不适用 |

---

## 三、借鉴项详细计划

### 3.1 高优先级：几何与命中验证

#### 3.1.1 远距离门限（Max-Range No-Hit）✅ 已实施

**现状**：LiDAR 在 `hit && hitFraction > 0` 时，若 `dist >= range * 0.9999` 仍视为命中，可能把天空/远平面当目标。

**实施**：已在 `RDF_LidarScanner.Scan()` 中增加 `dist >= range * 0.9999` 时 `hit = false` 逻辑。

---

#### 3.1.2 自命中过滤（Self-Hit Rejection）— 不采纳

**现状**：LiDAR 使用 `param.Exclude = subject`，引擎已处理子实体排除。

**结论**：引擎 `TraceParam.Exclude` 已实现，无需额外逻辑。

---

### 3.2 实体分类与颜色策略 — 不采纳

#### 3.2.1 实体预分类（EEntityKind）

**结论**：现实中 LiDAR 无此功能，不采纳。

---

### 3.3 配置与校验

#### 3.3.1 量程上限扩展 ✅ 已实施

**现状**：`RDF_LidarSettings.Validate()` 将 `m_Range` 限制在 0.1–1000 m。

**实施**：已将上限提高到 100 km（与雷达一致）。

---

#### 3.3.2 可选后处理开关

**现状**：LiDAR 无后处理；雷达有 CFAR、杂波滤波、自命中等。

**借鉴**：
- 自命中：必须做（见 3.1.2）。
- 杂波：LiDAR 可引入「纯地形点过滤」开关 `m_EnableTerrainFilter`，开启时 `entity == null` 的 hit 置为 false（与雷达 clutter 逻辑类似）。
- CFAR：LiDAR 通常不需要（无功率/SNR），可暂不移植。

**收益**：减少地面/植被等静态杂波，点云更聚焦目标。  
**工作量**：低–中。

---

### 3.4 低优先级：Demo 与可视化

#### 3.4.1 世界立柱标记（World Marker Display）

**现状**：雷达有 `RDF_RadarWorldMarkerDisplay`（立柱 + 罗盘），LiDAR 主要用 `RDF_LidarVisualizer` 点云。

**借鉴**：新增 `RDF_LidarWorldMarkerDisplay`，在命中点绘制立柱，颜色按 `m_EntityKind`，与雷达风格统一。可选集成到 `RDF_LidarAutoRunner`。

**收益**：LiDAR Demo 更直观，便于对比雷达。  
**工作量**：中。

---

#### 3.4.2 扫描完成回调与 HUD ✅ 已实施

**现状**：LiDAR 有 `RDF_LidarScanCompleteHandler`、`RDF_LidarDemoStatsHandler`；雷达有 HUD 集成。

**实施**：已新增 `RDF_LidarHUD`（2D 俯视点云 PPI 显示），蓝/青配色以区分雷达。通过 `RDF_LidarDemoConfig.m_ShowHUD` 或 `CreateWithHUD()` 启用。

---

## 四、实施路线图（已更新）

| 阶段 | 内容 | 状态 |
|------|------|------|
| **Phase 1** | 3.1.1 远距离门限 | ✅ 已实施 |
| | 3.1.2 自命中过滤 | 不采纳（引擎已实现） |
| **Phase 2** | 3.2.1 实体预分类 | 不采纳（现实无此功能） |
| **Phase 3** | 3.3.1 量程扩展 | ✅ 已实施 |
| **Phase 4** | 3.4.1 世界立柱标记 | 可选 |
| **Phase 5** | 3.4.2 LiDAR HUD | ✅ 已实施 |

---

## 五、共享代码与依赖

- **RDF_LidarHUD**：已新增，位于 `scripts/Game/RDF/Lidar/UI/RDF_LidarHUD.c`。
- **RDF_EntityPreClassifier**：已存在，LiDAR 未采纳实体预分类。

---

## 六、风险与注意事项

1. **性能**：实体分类、自命中遍历父链，每射线增加少量开销，需在大量射线场景下验证。
2. **兼容性**：`RDF_LidarSample` 增加 `m_EntityKind` 后，现有 `RDF_LidarColorStrategy` 子类需能处理（默认 UNKNOWN 即可）。
3. **雷达独立性**：借鉴时保持雷达逻辑不变，仅抽取公共部分，避免引入回归。

---

## 七、参考文件

- 雷达命中验证：`scripts/Game/RDF/Radar/Core/RDF_RadarScanner.c`（约 190–212 行）
- 自命中：`RDF_RadarScanner.IsHitOnRadarPlatform`（约 419–436 行）
- 实体分类：`scripts/Game/RDF/Radar/Classification/RDF_EntityPreClassifier.c`
- LiDAR 扫描：`scripts/Game/RDF/Lidar/Core/RDF_LidarScanner.c`

---

**© 2026 Radar Development Framework**
