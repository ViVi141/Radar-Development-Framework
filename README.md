# Radar Development Framework (RDF) — LiDAR 模块

轻量、模块化的 **LiDAR 传感器框架**，用于在 **Arma Reforger** 中实现激光雷达射线点云扫描、可视化与演示。

- **Repository**: [Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **Contact**: 747384120@qq.com
- **License**: Apache-2.0

---

## 特性

- 可配置射线数、范围、间隔；多种采样策略（均匀、半球、锥形、分层、扫描线、扇区扫掠）
- 点云 + 渐变射线可视化；可插拔颜色策略；仅点云模式（纯色背景）
- CSV 导出、统计/过滤、扫描完成回调；支持多人网络同步（服务器权威）
- HUD：底层直接用 `WorkspaceWidget.CreateWidgetInWorkspace` 动态构建，无需 `.layout` 文件

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
    Core/       雷达核心：类型、设置、辐射注册表、扫描器、抛射物追踪
    Visual/    可视化：射线与点云绘制（RDF_RadarVisualSettings、RDF_RadarVisualizer）
    Util/       实体分类（抛射物/载具）
    Demo/       组件、AutoRunner、DemoConfig、Bootstrap

docs/
  API.md                  LiDAR API 参考
  DEVELOPMENT.md          开发者架构与扩展指南
  OPTIMIZATION_AND_MEMORY.md  优化与内存防护
  CHANGELOG.md            版本历史
  TODO.md                 已完成/待办事项
  VEHICLE_RADAR_LOCK_GUIDE.md  载具扫描/锁定与武器打击实现指南（通用方案）
```

---

## 快速上手 — LiDAR

```c
// 以预设配置一步启动
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateConical(25.0, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateSweep(30.0, 20.0, 45.0, 512));

// 可选：设置 Trace 目标（0=仅地形, 1=全部, 2=仅实体）及烟雾遮挡
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

启用 Bootstrap 自启动（需在 modded SCR_BaseGameMode 或配置中开启）：

```c
SCR_BaseGameMode.SetBootstrapEnabled(true);
```

完整 API 见 [docs/API.md](docs/API.md)。  
载具扫描、锁定与武器打击的通用实现思路（含“先查实体、再射线判可见”的简化方案）见 [docs/VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md)。

---

## 快速上手 — 雷达（与 LiDAR 一致，Callqueue 自驱动，默认绘制射线+点云）

**要让雷达 Demo 在进图后自动显示**，任选其一即可：

1. **GameMode 里启用雷达 Bootstrap**（与 LiDAR 同一处）：在游戏模式或初始化里调用  
   `SCR_BaseGameMode.SetRadarBootstrapEnabled(true);`  
   则 `OnGameStart()` 时会自动执行 `StartWithConfig` + `SetDemoEnabled(true)`。
2. **在场景里挂组件**：给任意实体挂上 **RDF_RadarBootstrap** 组件，该实体会在 EOnInit 时启动雷达 Demo。

```c
// 或在脚本里手动一步启动（无需挂组件时）
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateDefault(64));
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateLongRange(5000.0, 32));
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateProjectileOnly(128));

// 开关
RDF_RadarAutoRunner.SetDemoEnabled(true);
RDF_RadarAutoRunner.SetDemoEnabled(false);

// 取结果与轨迹
array<ref RDF_RadarTarget> targets = RDF_RadarAutoRunner.GetLastTargets();
RDF_RadarProjectileTracker tracker = RDF_RadarAutoRunner.GetTracker();

// 可选：关闭/开启射线或点云
RDF_RadarAutoRunner.GetVisualSettings().m_DrawRays = false;
RDF_RadarAutoRunner.GetVisualSettings().m_DrawPoints = true;
```

雷达 Demo 默认通过 Callqueue 自驱动，**默认开启射线与点云绘制**（载具=绿、抛射物=红、辐射雷达=黄）；可通过 `GetVisualSettings()` 调节。

---

## 扩展点

- **采样策略**：实现 `RDF_LidarSampleStrategy::BuildDirection()` 并注入
- **颜色策略**：实现 `RDF_LidarColorStrategy` 并设置到 Visualizer / DemoConfig
- **扫描完成回调**：实现 `RDF_LidarScanCompleteHandler`，重写 `OnScanComplete()`

---

## 文档索引

| 文件 | 内容 |
|------|------|
| [docs/API.md](docs/API.md) | LiDAR 完整 API 参考 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 架构、模块布局、数据流、扩展指南 |
| [docs/OPTIMIZATION_AND_MEMORY.md](docs/OPTIMIZATION_AND_MEMORY.md) | 优化与内存防护 |
| [docs/VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) | 载具扫描/锁定/武器打击实现指南（通用，不依赖已移除的雷达模块） |
| [docs/RADAR_PLAN.md](docs/RADAR_PLAN.md) | 雷达模块计划需求（炮弹追踪、雷达可被探测、主动/被动） |
| [docs/RADAR_REQUIRED_APIS.md](docs/RADAR_REQUIRED_APIS.md) | 雷达系统所需游戏 API 列表与验证（api_search） |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 版本更新历史 |

---

## 贡献

欢迎提交 PR、Issue 或讨论扩展点。  
联系：747384120@qq.com
