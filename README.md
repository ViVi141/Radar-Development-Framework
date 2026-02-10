# Radar Development Framework — LiDAR 模块 📡

轻量且模块化的 LiDAR（激光雷达）开发框架，用于在 Arma Reforger 中快速实现射线点云扫描、渲染与调试。该框架将扫描核心、可视化和演示隔离开，便于扩展与复用。

Repository: https://github.com/ViVi141/Radar-Development-Framework
Contact: 747384120@qq.com

---

## 主要内容 ✅
- `scripts/Game/RDF/Lidar/Core/` — 扫描核心（设置、采样策略、扫描器）
- `scripts/Game/RDF/Lidar/Visual/` — 可视化渲染（点云 + 渐变射线）
- `scripts/Game/RDF/Lidar/Util/` — 主体解析、**导出 CSV**（`RDF_LidarExport`）、**统计/过滤**（`RDF_LidarSampleUtils`）、**扫描完成回调**（`RDF_LidarScanCompleteHandler`）
- `scripts/Game/RDF/Lidar/Demo/` — 演示控制（可选自动运行）

许可证：Apache-2.0（见仓库根目录 `LICENSE`）。

---

## 快速上手 ⚡
- 默认：**不自动运行**（演示为可选，避免干扰其他模组）。

### 统一开关（唯一入口）
```c
// 开启演示（需先通过 SetDemoConfig 或 StartWithConfig 设定策略等）
RDF_LidarAutoRunner.SetDemoEnabled(true);

// 关闭演示
RDF_LidarAutoRunner.SetDemoEnabled(false);

// 查询是否已开启
RDF_LidarAutoRunner.IsDemoEnabled();
```

### 通过 API 预设启动（推荐）
所有演示均通过 `RDF_LidarDemoConfig` 预设 + `RDF_LidarAutoRunner` 完成，不再使用独立 Demo 类：
```c
// 使用预设并启动（一条调用）
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefault(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateDefaultDebug(512)); // 带原点轴 + 控制台统计
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateHemisphere(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateConical(25.0, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateStratified(256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateScanline(32, 256));
RDF_LidarAutoRunner.StartWithConfig(RDF_LidarDemoConfig.CreateSweep(30.0, 20.0, 45.0, 512)); // 雷达扫描动画

// 或分步：先设置配置再开开关
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateConical(25.0, 256);
RDF_LidarAutoRunner.SetDemoConfig(cfg);
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

### 自定义配置
```c
RDF_LidarDemoConfig cfg = new RDF_LidarDemoConfig();
cfg.m_Enable = true;
cfg.m_SampleStrategy = new RDF_ConicalSampleStrategy(25.0);
cfg.m_RayCount = 256;
cfg.m_MinTickInterval = 0.25;
cfg.m_ColorStrategy = new RDF_IndexColorStrategy();
RDF_LidarAutoRunner.SetDemoConfig(cfg);
RDF_LidarAutoRunner.SetDemoEnabled(true);
```

### 策略轮换（Cycler）
```c
RDF_LidarDemoCycler.Cycle(256);                    // 切换到下一策略并开演示
RDF_LidarDemoCycler.StartIndex(2, 256);            // 按索引启动（如 2=锥形）
RDF_LidarDemoCycler.StartAutoCycle(10.0);          // 每 10 秒自动轮换
RDF_LidarDemoCycler.StopAutoCycle();
RDF_LidarDemoCycler.SetAutoCycleInterval(5.0);
```

### 统一 Bootstrap（游戏启动时可选开启）
仅一个开关，默认关闭：
```c
SCR_BaseGameMode.SetBootstrapEnabled(true);        // 开局自动开演示（默认策略）
SCR_BaseGameMode.SetBootstrapAutoCycle(true);      // 开局自动轮换策略
SCR_BaseGameMode.SetBootstrapAutoCycleInterval(10.0);
```

### 其它 API
```c
RDF_LidarAutoRunner.SetMinTickInterval(0.2);
RDF_LidarAutoRunner.SetDemoRayCount(128);
RDF_LidarAutoRunner.SetDemoSampleStrategy(new RDF_HemisphereSampleStrategy());
RDF_LidarAutoRunner.SetDemoColorStrategy(new RDF_IndexColorStrategy());
```

### 自检（控制台/脚本）
```c
RDF_RunAllSampleChecks();
```

### 导出与逻辑
- **导出 CSV**：`RDF_LidarExport.ExportLastScanToConsole(visualizer)` 或 `RDF_LidarExport.PrintCSVToConsole(samples)`，从控制台复制到外部文件。
- **扫描完成回调**：继承 `RDF_LidarScanCompleteHandler` 并重写 `OnScanComplete(samples)`，再调用 `RDF_LidarAutoRunner.SetScanCompleteHandler(handler)`。
- **统计/过滤**：`RDF_LidarSampleUtils.GetClosestHit(samples)`、`GetHitCount(samples)`、`GetHitsInRange(...)`、`GetAverageDistance(...)` 等。详见 `docs/API.md`。
- **仅逻辑不渲染**：直接使用 `RDF_LidarScanner.Scan(subject, outSamples)`，不创建 Visualizer；见 `docs/API.md` 中「仅逻辑不渲染」一节。
- 获取上次扫描数据：`visual.GetLastSamples()` 后可用 `RDF_LidarExport.PrintCSVToConsole(samples)` 导出，或自行处理（CSV/JSON 等）。

---

## 常见设置
- 扫描半径、射线数量与更新频率：在 `RDF_LidarSettings` 中配置（clamp 与校验已实现）。
- 可视化细节：在 `RDF_LidarVisualSettings` 中控制点大小、分段数、透明度等。调试时可开启 `m_DrawOriginAxis = true` 绘制扫描原点与 X/Y/Z 三轴。

---

## 性能建议 ⚠️
- 降低 `m_RayCount` 或 `m_RaySegments` 可显著减少开销。
- 如果需要高密度点云，请增大 `m_UpdateInterval` 或仅在调试时启用渲染。

---

## 贡献与联系方式 🤝
欢迎提交 PR、Issue 或讨论扩展点。请在 PR 描述中添加变更目的与性能影响说明。若需直接联系：747384120@qq.com。

---

有关内部架构、扩展接口与开发约定，请参阅 `docs/DEVELOPMENT.md`。