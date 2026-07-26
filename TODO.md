# TODO — RDF

已完成项与后续路线。优先级：**P0** 玩法闭环 / **P1** 工程与性能 / **P2** 物理逼真 / **P3** 可选增强。

相关文档：[RADAR_API.md](docs/RADAR_API.md) · [RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) · [VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md)

---

## 一、LiDAR（维护中）

- [x] 点云扫描核心（多种采样策略）
- [x] 可视化 / HUD / CSV 导出
- [x] 多人网络同步（服务器权威）
- [x] Bootstrap / 策略轮换
- [ ] （P3）PPI 扫描动画、命中点历史淡出
- [ ] （P3）DiagMenu 参数面板
- [ ] （P3）可选 Sensor 门面，与雷达 API 形态对齐

---

## 二、雷达 — 已落地

- [x] 实体优先扫描 + Trace 可见性
- [x] 炮弹 / 载具 / 辐射源分类与 α-β 跟踪
- [x] 硬件参数、雷达方程、多普勒、MTI、SNR
- [x] PPI HUD、机械扫描、EW 噪声钩子
- [x] DEM 运行时杂波接入
- [x] NLOS 地面反射弱检（`m_EnableNlosMultipath`）
- [x] 粗栅格 CA-CFAR + 匿名 blip
- [x] 联机权威同步（`RDF_RadarNetworkComponent`）
- [x] 欺骗干扰 / 假目标
- [x] 测量合成（距离门 / 波束量化 + SNR 噪声）
- [x] `PredictAt` 航迹外推
- [x] 弹道（重力 + AirDrag + 全局风）与 WLR；DEM 地面交点
- [x] 散射体表 + Signature 库 + Swerling / 方位 RCS
- [x] 公共门面 `RDF_RadarSensor`（SEARCH / STARE / WLR）
- [x] 自动化：`AutoTest` / `BallisticsAutoTest` / `ShellFireAutoTest` / `AirborneScanTest`
- [x] 离线 Python 原型 + `mass_battle` + ballistics 单测

---

## 三、雷达 — 待做（按优先级）

### P0 — 玩法闭环

- [x] 锁定层 `RDF_RadarLockManager`：选目标、断锁、coast、当前锁 API
- [x] 搜索 → 截获 → 跟踪状态机（`ERDF_RadarLockState`，含 coast 丢批）
- [x] 载具挂载示例：`RDF_RadarComponent` 锁定 API + `RDF_RadarLockAutoTest`  
  见 [VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md)
- [x] 武器接口：`GetLockedTarget(out entity, out worldPos)`（游戏原生暂无制导弹，接口即交付；制导留给未来模组）

### P1 — 工程、性能、架构

- [ ] 散射体表空间网格索引（当前线性遍历）
- [ ] 拆分过重的 `RDF_RadarScanner`（CFAR / 候选 / 物理链）
- [ ] Network 对齐 Sensor（权威端统一门面，避免旁路双 Scanner）
- [ ] 收窄门面穿透：`GetDemStatusShort` 等到 Sensor，减少 `GetScanner()` 依赖
- [ ] DEM 模组内只读发布包（当前偏 `$profile` 开发路径）
- [ ] 拆分 `tools/dem/rdf_radar_mass_battle_sim.py`（scenario / engine / eval / render）
- [ ] 加厚 Python 单测（CFAR / track）；可选与 Enforce 同输入 golden

### P1 — 远程计算后端（可选增强）

> **原则**：游戏服务器仍做实体查询 + `TraceMove`（世界状态在本地）。  
> 远程只外包 **RF 通道 / 跟踪 / WLR / 批量评估**；失败必须回退 Local。  
> 不把整次实时扫描无条件搬到外网。

- [ ] 设计文档：协议、超时、降级、安全边界
- [ ] `RDF_RadarComputeBackend` 接口（Local 默认 / Remote 可选）
- [ ] 请求摘要：origin / forward / settings + 已 Trace 候选（位置、速度、类型、LOS、DEM 采样）
- [ ] 响应：plots / tracks / WLR fixes（JSON 或 CSV）；权威校验后再同步客户端
- [ ] 游戏侧：`RestApi` / `RestContext` 异步请求；超时丢弃、不卡帧
- [ ] 计算服务：复用 `tools/dem` 物理链（HTTP/WebSocket），与 Enforce 常量对齐
- [ ] 首期范围建议：异步 WLR / 批量航迹 / 离线孪生；实时 SEARCH/STARE 仍默认 Local
- [ ] 配置项：启用开关、endpoint、超时 ms、最大并发、密钥（勿入库）

### P2 — 物理与检测逼真

- [ ] 热噪声填充空距离单元 → 真实 CFAR 虚警率
- [ ] 可配置 GO-CFAR / SO-CFAR 等多门限
- [ ] 刀刃绕射 / 更精细多径（当前 image-method 简化）
- [ ] 欺骗库扩展：拖距、角闪烁、间歇假点
- [ ] DEM 柱状 span 遮挡 / 多径（可选）

### P3 — 远期产品

- [ ] 多雷达组网与 plots 融合 / 交叉定位
- [ ] 反炮兵专用 HUD（WLR 发射 / 落点告警圈）
- [ ] IFF / 数据链抽象
- [ ] 游戏内 AutoTest 批跑入口或最小 CI（Python ballistics + 文档链接）

---

## 四、DEM

- [x] Workbench 烘焙 V3 CSV（tile + manifest）
- [x] 深水捷径、列 span、材质分类
- [x] 运行时 manifest / tile 解析与 LRU 缓存
- [x] 雷达杂波功率 + WLR 地面采样接入
- [ ] （P1）模组内只读发布数据包
- [ ] （P2）柱状 span 遮挡 / 多径

---

## 五、仓库卫生（可选）

- [x] Sensor API 文档与 Demo 接线
- [x] `.gitignore`：`out/`、`TrainData/`、`*.rdb`、根目录数字 JPG
- [ ] 删除空目录 `Radar/Detection`、`Radar/Tracking`（若仍存在）
- [ ] CHANGELOG 后半英文重复段压缩或标注归档
- [ ] `LICENSE` / `LICENSE.txt` 二选一（确认工具兼容后再合并）

---

## 六、明确不做（游戏内实时主路径）

- 全场电磁波 / FDTD
- 训练级 RD 图每帧满跑
- 完整 DRFM 硬件仿真
- 远程服务器替代本地 Trace / 实体查询（状态无法干净搬迁）

目标产品形态：**可信的军武传感器玩法**（发现 / 丢失 / 压制 / 欺骗 / 锁定），可选远程算力增强重计算，而不是电磁仿真器。

入口：[README.md](README.md)
