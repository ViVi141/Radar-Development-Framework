# TODO — Radar Development Framework

本文档记录已完成项目与后续改进建议。

---

> **⚠️ 雷达模块开发中** — 请勿用于生产环境。Demo 默认关闭，需手动启用。

---

## 一、电磁波雷达系统（主轨道）— **已全部完成** ✓

### 1. 电磁波基础模型
- [x] **波段 / 频率 / 波长** — `RDF_EMWaveParameters`：载波频率、波长（λ = c/f）、波段枚举（L/S/C/X/Ku/K/Ka/V/W）
- [x] **相位** — `RDF_RadarSample.m_PhaseRad`：回波相位 = 2π × 2d / λ
- [x] **衰减（传播损耗）** — `RDF_RadarPropagation`：FSPL、大气衰减、雨衰模型
- [x] **反射与 RCS** — `RDF_RCSModel`：解析模型（球/板/柱）、材质反射率查表、实体边界框估算、地物漫反射面积散射

### 2. 数据结构与 API
- [x] **雷达采样** — `RDF_RadarSample`：继承 `RDF_LidarSample`，增加 SNR、RCS、多普勒频移、相位、接收功率、径向速度等字段
- [x] **雷达设置** — `RDF_RadarSettings`：量程、距离门限 (`m_MinRange`)、系统损耗 (`m_SystemLossDB`)、检测门限、杂波过滤开关
- [x] **雷达扫描器** — `RDF_RadarScanner`：完整 10 步物理管线（FSPL→大气→雨衰→RCS→功率→距离门→噪声→多普勒→检测→杂波过滤）

### 3. 工作模式
- [x] **脉冲雷达（Pulse）** — 峰值功率、脉冲宽度、PRF
- [x] **连续波（CW）** — 固定频率、持续发射
- [x] **调频连续波（FMCW）** — 线性调频、距离-频率映射
- [x] **相控阵（Phased Array）** — 电子扫描、多波束

### 4. 可视化与 HUD
- [x] **3D 世界标记** — `RDF_RadarWorldMarkerDisplay`：彩色立柱标记命中点
- [x] **ASCII 控制台地图** — `RDF_RadarTextDisplay`：文字俯视雷达地图
- [x] **PPI 显示（脚本）** — `RDF_PPIDisplay`：调试用平面显示
- [x] **A-Scope** — `RDF_AScopeDisplay`：距离-幅度图
- [x] **PPI HUD（CanvasWidget）** — `RDF_RadarHUD`：
  - 圆形扫描图（背景圆盘、50%/100% 距离环、N/S/E/W 罗盘轴、玩家中心点）
  - 目标光点（颜色/大小按 RCS 区分实体/地物）
  - 数据面板（SNR/RCS/速度/命中数/量程）
  - 0.5 秒节流防闪烁

### 5. 演示系统
- [x] **五种预设** — `RDF_RadarDemoConfig`：X波段搜索/车辆防撞/气象/相控阵/L波段远程
- [x] **演示驱动器** — `RDF_RadarAutoRunner`：单例、Tick 驱动、回调派发、世界标记
- [x] **统计报告** — `RDF_RadarDemoStatsHandler`：控制台 SNR/RCS/速度/ASCII 地图输出
- [x] **预设轮换** — `RDF_RadarDemoCycler`：手动切换 + 定时自动轮换
- [x] **Bootstrap 自启动** — `RDF_RadarAutoBootstrap`：`modded SCR_BaseGameMode`，默认关闭，需 `SetRadarBootstrapEnabled(true)` 启用

### 6. 高级功能
- [x] **SAR 处理器** — `RDF_SARProcessor`：合成孔径积累与成像
- [x] **电子对抗（ECM）** — `RDF_JammingModel`：噪声干扰、欺骗干扰、自卫干扰
- [x] **目标分类** — `RDF_TargetClassifier`：按 RCS/速度自动分类（飞机/车辆/人员/建筑/未知）

### 7. 测试与文档
- [x] **单元测试** — `RDF_RadarTests`：传播损耗、多普勒、RCS、检测距离
- [x] **性能基准** — `RDF_RadarBenchmark`
- [x] **API 文档** — `docs/RADAR_API.md`
- [x] **使用教程** — `docs/RADAR_TUTORIAL.md`（14 章）

---

## 二、后续改进建议（可选）

### HUD 与可视化
- [ ] PPI 扫描动画（旋转扫描线，按角度逐步刷新而非全量刷新）
- [ ] 目标历史轨迹（保留上 N 帧光点位置，淡出效果）
- [ ] 悬停/点击光点显示目标详情（需要鼠标事件支持）
- [ ] A-Scope 接入 HUD（在 PPI 右侧叠加距离-幅度图）

### 物理模型
- [ ] 多路径效应（地面反射增强/减弱）
- [ ] 天线方向图加权（非均匀波束截面）
- [ ] 相位相干检测（合并多个脉冲的相位以提升 SNR）
- [ ] 杂波功率谱建模（气象杂波 / 海杂波 / 地杂波分类）

### 工程质量
- [ ] 导出到引擎调试 UI（`DiagMenu`）控制雷达参数
- [ ] 更多单元测试覆盖（雷达方程边界值、RCS 特殊角度）
- [ ] 网络同步雷达扫描结果（类似 LiDAR Network 模块）
- [ ] 配置持久化（保存/加载雷达参数到文件）

---

## 三、LiDAR 已完成（维护中）

- [x] 点云扫描核心（多种采样策略）
- [x] 可视化（渐变射线、仅点云模式）
- [x] CSV 导出
- [x] 多人网络同步（服务器权威、分片、RLE 压缩）
- [x] Bootstrap 自启动
- [x] 策略自动轮换

---

## 四、EM 体素场系统（EMVoxelField）— **Phase 1-5 已全部完成** ✓

### Phase 1-4：基础体素场
- [x] **稀疏哈希格** — `EMVoxelCell`（power / freqMask / mainDir / TTL / 脉冲历史）
- [x] **注入 API** — `InjectPowerAt`、`InjectAlongRay`、`InjectReflection`、`InjectJammer`
- [x] **读取 API** — `GetPowerAt`、`GetTopCells`、`GetSignalDescriptorAt`
- [x] **时间衰减** — `TickDecay`（线性衰减 + TTL 过期清除）
- [x] **被动传感器** — `EMPassiveSensor`：采样场功率、提取信号描述子、检测判断

### Phase 5：扇区活跃化 / 服务端客户端架构 / 网络同步 / 调试可视化
- [x] **扇区活跃化** — `RegisterActiveSector` / `EMActiveSector`（锥形区域，TTL 过期自动清除）
- [x] **扇区外加速衰减** — `m_OutOfSectorDecayMul` × 基础衰减率，超出预算时 `PruneToBudget`
- [x] **网络接口** — `EMFieldNetworkAPI`（接口基类）+ `EMDetectionResult`（可序列化，逗号分隔）
- [x] **网络组件** — `EMFieldNetworkComponent`（服务器权威：Tick 衰减 + 广播；客户端：存储结果）
  - `[RplProp]` 同步雷达原点；客户端 → 服务器 RPC 注册扇区
  - 非可靠广播 RPC（`RplChannel.Unreliable`）广播检测结果包
- [x] **调试可视化** — `EMVoxelDebugVisualizer`（静态类，`Draw()` 委托 `DebugDrawColorMapped`）
  - `PowerToDebugColor`：蓝（噪底 <-90dBm）→ 绿（-90~-60）→ 黄（-60~-30）→ 红（≥-30）
  - 方向卫星球 + 扇区内绿色高亮圈

### 高级空间优化重构（两级分块网格 + 惰性光线 + 三线性插值 + 时序 LoD + Morton 码）
- [x] **两级分块网格（SOA）** — `EMChunk`：200m 宏格 × 10m 微体素（20³=8000 个），平坦并行数组
  - `m_Powers[]` / `m_FreqMasks[]` / `m_DirsX/Y/Z[]` / `m_TTLs[]`（纯 float/int 数组，缓存友好）
  - 稀疏 `m_Descriptors` Map（仅存储有脉冲历史的格）
  - `EMMorton` Z-order 编解码（10bit/轴，确保空间局部性）
- [x] **惰性光线评估** — `EMRayDescriptor` + `PushRay()`：注入时零写入，`GetPowerAt` 时按需计算 1/R²×高斯波束
- [x] **宏格占用跳过** — `ChunkHasEnergy()` + `m_OccupancyBits` 位掩码：空宏格直接跳过，光线游走无需进入
- [x] **三线性插值** — `GetPowerAt`：对邻近 8 个微体素双线性混合（空间连续，消除格界不连续）
- [x] **时序 LoD** — `EMUpdatePriority`（HIGH/MEDIUM/LOW）：`InjectAlongRay` 高优先级取 0.5 步长，低优先级取 4× 步长
- [x] **块预算管理** — `m_MaxActiveChunks=256`，`EvictOldestChunk()` LRU 驱逐；空块自动释放

### 优化后性能参数
| 指标 | 旧架构（平坦哈希） | 新架构（两级分块） |
|------|-------------------|-------------------|
| 读取精度 | 10m（离散） | 10m（三线性连续） |
| 空宏格跳过 | 无 | 位掩码 O(1) |
| 内存峰值 | 无上限 | ≈9.6 MB (256 块) |
| 缓存局部性 | 随机哈希 | Morton Z-order |
| 光线注入开销 | 全量写入 | 惰性（零写开销） |

---

## 五、完全波/场基雷达（抛弃射线检测）— 设计已定，待实现

- 目标：雷达检测**完全不再使用** `TraceMove` 等扫描射线，改为体素场传播 + 占位网格 + 场采样。
- 设计文档：**`docs/WAVE_BASED_RADAR_DESIGN.md`**（体素占位、传播、接收与检测、与现有 API 对接、分阶段实现）。
- 待办（可选，按设计文档阶段推进）：
  - [ ] **体素占位网格**：与 EM 网格对齐，用 GetSurfaceY + 实体 AABB 填充，零射线
  - [ ] **TickPropagate**：6 邻扩散 + 固体反射，无射线
  - [ ] **发射时间戳** + 接收端按时延→距离 bin
  - [ ] **场采样 → (距离, 方位, 功率) 聚类** → 输出 RDF_RadarSample
  - [ ] **检测点→实体**：占位表或每点一次查询（可选）

---

## 六、后续改进建议（EM 体素场）

- [ ] **EMPassiveSensor 对接新 API**：将 `SampleSignalDescriptorAt` 改为对应新 `EMChunk` descriptor map
- [ ] **EMFieldNetworkComponent 频率描述子**：`BroadcastDetections` 可补充频率/波形字段（需 `GetSignalDescriptorAt` 调用）
- [ ] **体素场持久化**：场景保存/加载时序列化活跃块（用于任务脚本重放）
- [ ] **多字段实例**：支持不同频段各有独立 `EMVoxelField`（如 ESM / EW 系统分离）
- [ ] **GPU/并行解算**：将 `TickDecay` 分片到多帧（每帧处理 N 个块的衰减，摊销开销）

---

*最后更新：2026-03-17，新增「完全波/场基雷达」设计（docs/WAVE_BASED_RADAR_DESIGN.md），雷达检测可完全抛弃射线。*
