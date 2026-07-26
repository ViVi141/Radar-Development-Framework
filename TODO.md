# TODO — RDF

已完成项与后续改进建议。

---

## 一、LiDAR（维护中）

- [x] 点云扫描核心（多种采样策略）
- [x] 可视化 / HUD / CSV 导出
- [x] 多人网络同步（服务器权威）
- [x] Bootstrap / 策略轮换
- [ ] PPI 扫描动画、命中点历史淡出（可选）
- [ ] DiagMenu 参数面板（可选）

## 二、雷达（已落地，继续增强）

- [x] 实体优先扫描 + Trace 可见性
- [x] 炮弹/载具/辐射源分类与 α-β 跟踪
- [x] 硬件参数、雷达方程、多普勒、MTI、SNR
- [x] PPI HUD、机械扫描、EW 噪声钩子
- [x] DEM 运行时杂波接入
- [x] 自动化 DEM 回归 / 空中目标测试
- [x] Trace 遮挡时 NLOS 地面反射弱检（`m_EnableNlosMultipath`）
- [x] 匿名 plot / CFAR 栅格检测（粗栅格 CA-CFAR + 匿名 blip）
- [x] 联机权威同步检测结果（`RDF_RadarNetworkComponent`）
- [x] 欺骗干扰 / 假目标（`RDF_RadarDeceptionJammerEffect`）
- [x] 测量合成（距离门/波束量化 + SNR 噪声，切断实体真值）
- [x] 量测驱动航迹关联与外推（`PredictAt`）
- [x] 炮弹弹道外推（重力 + AirDrag + 全局风）与 WLR 发射/落点反推（`RDF_RadarBallistics`）
- [x] 全局散射体表（`RDF_RadarScattererRegistry`，增量发现 + 缓存分类/RCS）
- [x] 散射体拟真状态（姿态/AGL/DEM 缓存/射频摘要/Swerling）
- [x] 按 prefab 的尺寸/RCS 特征表（`RDF_RadarSignatureLibrary`，Workbench 扫可放置 prefab 离线烘焙 + 运行时查表）
- [ ] 散射体表按空间网格索引（当前为线性遍历）
- [ ] 完整搜索-截获-跟踪状态机
- [ ] 刀刃绕射 / 更精细多径（当前为 image-method 简化）
- [ ] 热噪声填充空距离单元 → 真实 CFAR 虚警率

## 三、DEM（已落地）

- [x] Workbench 烘焙 V3 CSV（tile + manifest）
- [x] 深水捷径、列 span、材质分类
- [x] 运行时 manifest/tile 解析与 LRU 缓存
- [x] 雷达杂波功率接入
- [ ] 模组内只读发布数据包（当前为 `$profile` 开发路径）
- [ ] 柱状 span 遮挡 / 多径（可选）

## 四、载具集成（可选）

- [ ] 载具扫描、锁定、武器对接：见 [docs/VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md)

---

能力边界与可做事项见 [docs/RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md)。  
文档入口见 [README.md](README.md)。
