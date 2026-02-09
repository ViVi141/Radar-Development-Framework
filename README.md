# Radar Development Framework — LiDAR 模块 📡

轻量且模块化的 LiDAR（激光雷达）开发框架，用于在 Arma Reforger 中快速实现射线点云扫描、渲染与调试。该框架将扫描核心、可视化和演示隔离开，便于扩展与复用。

Repository: https://github.com/ViVi141/Radar-Development-Framework
Contact: 747384120@qq.com

---

## 主要内容 ✅
- `scripts/Game/RDF/Lidar/Core/` — 扫描核心（设置、采样策略、扫描器）
- `scripts/Game/RDF/Lidar/Visual/` — 可视化渲染（点云 + 渐变射线）
- `scripts/Game/RDF/Lidar/Util/` — 主体解析（玩家 / 载具）
- `scripts/Game/RDF/Lidar/Demo/` — 演示控制（可选自动运行）

许可证：Apache-2.0（见仓库根目录 `LICENSE`）。

---

## 快速上手 ⚡
- 默认：**不自动运行**（演示为可选，避免干扰其他模组）。
- 启用演示：
```c
// 开启演示
RDF_LidarAutoRunner.SetDemoEnabled(true);

// 关闭演示
RDF_LidarAutoRunner.SetDemoEnabled(false);
```
- 更改最小调度间隔：
```c
RDF_LidarAutoRunner.SetMinTickInterval(0.2);
```
- 替换采样策略示例：
```c
RDF_LidarScanner scanner = new RDF_LidarScanner();
// 默认策略
scanner.SetSampleStrategy(new RDF_UniformSampleStrategy());
// 示例：仅采样上半球（新增示例策略）
scanner.SetSampleStrategy(new RDF_HemisphereSampleStrategy());

// Convenience: start hemisphere demo (sets strategy and starts auto-run)
RDF_LidarAutoRunner.StartHemisphereDemo();
// Stop demo
RDF_LidarAutoRunner.SetDemoEnabled(false);
```
- 获取上次扫描数据以便导出：
```c
RDF_LidarVisualizer visual = new RDF_LidarVisualizer();
ref array<ref RDF_LidarSample> samples = visual.GetLastSamples();
// 导出由外部工具负责（CSV/JSON 等）
```

---

## 常见设置
- 扫描半径、射线数量与更新频率：在 `RDF_LidarSettings` 中配置（clamp 与校验已实现）。
- 可视化细节：在 `RDF_LidarVisualSettings` 中控制点大小、分段数、透明度等。

---

## 性能建议 ⚠️
- 降低 `m_RayCount` 或 `m_RaySegments` 可显著减少开销。
- 如果需要高密度点云，请增大 `m_UpdateInterval` 或仅在调试时启用渲染。

---

## 贡献与联系方式 🤝
欢迎提交 PR、Issue 或讨论扩展点。请在 PR 描述中添加变更目的与性能影响说明。若需直接联系：747384120@qq.com。

---

有关内部架构、扩展接口与开发约定，请参阅 `docs/DEVELOPMENT.md`。