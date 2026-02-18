# TODO — Radar Development Framework

本文档记录已完成项目与后续改进建议。

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
- [x] **Bootstrap 自启动** — `RDF_RadarAutoBootstrap`：`modded SCR_BaseGameMode`，游戏启动后自动运行演示 + 显示 HUD

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

*最后更新：2026-02-18，电磁波雷达系统全部完成，HUD PPI 扫描图上线。*
