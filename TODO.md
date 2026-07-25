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
- [ ] 匿名 plot / CFAR 栅格检测（当前仍为实体候选）
- [ ] 联机权威同步检测结果（雷达目前本地）
- [ ] 欺骗干扰 / 假目标（离线框架有，游戏内待接）

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

文档入口见 [README.md](README.md)。
