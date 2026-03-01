# Radar Development Framework

> **⚠️ 开发中 (IN DEVELOPMENT)** — 雷达模块与 Demo 尚在开发，请勿用于生产环境。  
> LiDAR 模块相对稳定，可酌情使用。

轻量、模块化的**双系统传感器框架**，用于在 **Arma Reforger** 中实现：

- **LiDAR**：激光雷达射线点云扫描、可视化与演示
- **电磁波雷达**：完整物理仿真（传播衰减、RCS、多普勒、SNR），含 PPI 扫描圆图 HUD

两套系统共享采样核心与扩展接口，相互隔离，互不干扰。

- **Repository**: [Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **Contact**: 747384120@qq.com
- **License**: Apache-2.0

---

## 特性总览

### LiDAR 模块
- 可配置射线数、范围、间隔；多种采样策略（均匀、半球、锥形、分层、扫描线、雷达扫掠）
- 点云 + 渐变射线可视化；可插拔颜色策略；仅点云模式（纯色背景）
- CSV 导出、统计/过滤、扫描完成回调；支持多人网络同步（服务器权威）

### 电磁波雷达模块
- **物理模型**：FSPL、大气衰减、雨衰、雷达方程、SNR、系统噪声、多普勒频移
- **RCS 模型**：球/板/柱解析 RCS、材质反射率查表、实体边界框估算、地物漫反射
- **工作模式**：脉冲（Pulse）、连续波（CW）、调频连续波（FMCW）、相控阵（Phased Array）
- **可视化**：3D 世界立柱标记、ASCII 控制台地图、PPI 圆形扫描图 HUD
- **HUD**：底层直接用 `WorkspaceWidget.CreateWidgetInWorkspace` 动态构建，无需 `.layout` 文件
- **高级**：SAR 处理器、电子对抗（ECM/干扰）、目标自动分类
- **演示系统**：五种预设、自动轮换、Bootstrap 自启动

---

## 项目结构

```
scripts/Game/RDF/
  Lidar/
    Core/       扫描核心：设置、类型、扫描器、采样策略
    Visual/     可视化：点云/射线渲染、颜色策略
    Util/       工具：主体解析、CSV 导出、统计/过滤、回调
    Demo/       演示：AutoRunner、DemoConfig、Cycler、Bootstrap
    Network/    网络：服务器权威同步

  Radar/
    Core/       核心：EMWaveParameters、RadarSample、RadarSettings、RadarScanner
    Physics/    物理：传播损耗、RCS 模型、雷达方程、多普勒处理器
    Modes/      工作模式：Pulse / CW / FMCW / PhasedArray
    Visual/     可视化：颜色策略、PPI/A-Scope 显示、世界标记、文字地图
    Advanced/   高级：SAR 处理器
    ECM/        电子对抗：干扰模型
    Classification/ 目标分类器
    Demo/       演示：RadarDemoConfig、AutoRunner、DemoStatsHandler、Cycler、Bootstrap
    UI/         HUD：RDF_RadarHUD（纯脚本 CanvasWidget PPI 扫描图）
    Tests/      单元测试 + 性能基准
    Util/       工具：RadarExport

docs/
  README.md               本文件
  DEVELOPMENT.md          开发者架构与扩展指南
  API.md                  LiDAR API 参考
  RADAR_API.md            雷达 API 参考
  RADAR_TUTORIAL.md       雷达使用教程
  CHANGELOG.md            版本历史
  TODO.md                 已完成/待办事项
```

---

## 快速上手 — 雷达 Demo

雷达 Demo 默认 **关闭**。需手动启用：

```c
// 启用自启动
SCR_BaseGameMode.SetRadarBootstrapEnabled(true);

// 或以指定配置启动
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateHelicopterRadar());
```

### 手动控制

```c
// 以指定配置启动
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateXBandSearch());

// 自动轮换五种预设（每 15 秒切换）
RDF_RadarDemoCycler.StartAutoCycle(15.0);

// 显示/隐藏 HUD
RDF_RadarHUD.Show();
RDF_RadarHUD.Hide();

// 更新 PPI 显示量程（与扫描器配置保持一致）
RDF_RadarHUD.SetDisplayRange(2500.0);
```

### 五种预设配置

| 工厂方法 | 波段 | 典型场景 | 最大量程 |
|---------|------|---------|---------|
| `CreateXBandSearch()` | X (10 GHz) | 地面搜索/监视 | 2 500 m |
| `CreateAutomotiveRadar()` | Ka (77 GHz) | 车辆防撞 FMCW | 200 m |
| `CreateWeatherRadar()` | S (3 GHz) | 气象探测 | 50 000 m |
| `CreatePhasedArrayRadar()` | X (10 GHz) | 多目标相控阵跟踪 | 10 000 m |
| `CreateLBandSurveillance()` | L (1.3 GHz) | 远程预警 | 100 000 m |

---

## HUD 界面说明

游戏启动后，屏幕**左下角**自动出现雷达 HUD，包含：

```
+==========================================+
| [>] RADAR              X-Band Pulse     |  ← 标题 + 当前模式
+------------------------------------------+
|               N (北)                     |
|         CanvasWidget PPI 圆形扫描图        |
|  W (西)    (+)玩家位置     E (东)          |
|               S (南)                     |
|  内圈=半量程  外圈=全量程                  |
+------------------------------------------+
| SNR  65.2 dB   Hits 23/512 rays         |
| RCS  +18.0 dBsm   Vel  3.2 m/s         |
| Range  30 m  to  1447 m                 |
+==========================================+
```

**目标光点颜色含义**：
- 亮绿色（大实体，RCS > 10 dBsm）
- 青色（小实体）
- 黄色（强地物回波）
- 暗橙色（弱地物回波）

HUD 更新频率最高每 0.5 秒一次，防止数据闪烁。

---

## 快速上手 — LiDAR

```c
// 以预设配置一步启动
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateConical(25.0, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateSweep(30.0, 20.0, 45.0, 512));

// 可选：创建配置后设置 Trace 目标（0=仅地形, 1=全部, 2=仅实体）及烟雾遮挡
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(256);
cfg.m_TraceTargetMode = 0;
cfg.m_TraceSmokeOcclusion = true;  // 烟雾/粒子阻挡激光射线
RDF_LidarAutoRunner.StartWithConfig(cfg);

// 轮换策略
RDF_LidarDemoCycler.StartAutoCycle(10.0);

// 开关
RDF_LidarAutoRunner.SetDemoEnabled(true);
RDF_LidarAutoRunner.SetDemoEnabled(false);
```

完整 LiDAR API 见 [docs/API.md](docs/API.md)。

---

## 雷达物理管线简介

每条射线命中后，`RDF_RadarScanner.ApplyRadarPhysics()` 执行以下步骤：

1. **自由空间路径损耗**（FSPL）：`20·log10(d) + 20·log10(f) + 92.45 dB`
2. **大气衰减**（频率 × 距离相关）
3. **雨衰**（可选，降雨率参数）
4. **RCS 估算**：实体 → 边界框体积 × 材质反射率；地物 → 漫反射面积散射模型
5. **接收功率**：完整雷达方程 `Pr = Pt·Gt·Gr·σ·λ²／(4π)³／R⁴／L`
6. **距离门限**（Range Gate）：丢弃近场盲区
7. **噪声功率**：`kTBF`，计算 SNR
8. **多普勒频移**：`fd = 2·vr·f／c`（相对速度）
9. **检测门限**：`SNR ≥ m_DetectionThreshold`
10. **杂波过滤**（可选，MTI 模式）

---

## 扩展点

- **采样策略**：实现 `RDF_LidarSampleStrategy::BuildDirection()` 并注入
- **颜色策略**：实现 `RDF_RadarColorStrategy::BuildPointColorFromRadarSample()`
- **RCS 模型**：修改 `RDF_RCSModel.EstimateEntityRCS()` 或 `EstimateTerrainRCS()`
- **工作模式**：继承 `RDF_RadarMode` 并重写 `ApplyMode()`
- **扫描完成回调**：继承 `RDF_LidarScanCompleteHandler`，重写 `OnScanComplete()`

---

## 文档索引

| 文件 | 内容 |
|------|------|
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 架构、模块布局、数据流、扩展指南 |
| [docs/API.md](docs/API.md) | LiDAR 完整 API 参考 |
| [docs/RADAR_API.md](docs/RADAR_API.md) | 雷达完整 API 参考 |
| [docs/RADAR_TUTORIAL.md](docs/RADAR_TUTORIAL.md) | 雷达使用教程（14 章）|
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 版本更新历史 |

---

## 贡献

欢迎提交 PR、Issue 或讨论扩展点。  
PR 请说明变更目的、兼容性影响与性能影响（如有）。  
联系：747384120@qq.com
