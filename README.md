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
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | 版本更新历史 |

---

## 贡献

欢迎提交 PR、Issue 或讨论扩展点。  
联系：747384120@qq.com
