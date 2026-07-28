# RDF 雷达 — 能力边界与可做事项

本文说明游戏内雷达**现在是什么**、相对现实电磁波雷达**缺什么**、在 Arma / Enforce 约束下**还能做什么**。  
实现细节见 [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)；对外契约见 [RADAR_API.md](RADAR_API.md)；排期以 [TODO.md](../TODO.md) 为准；离线原型见 [tools/dem/RADAR_FRAMEWORK.md](../tools/dem/RADAR_FRAMEWORK.md)。

---

## 1. 架构定位（不是什么）

| 误解 | 实际情况 |
|------|----------|
| 离线数字孪生算完再回灌游戏 | **否**。游戏内 Enforce 实时跑检测；`tools/dem/` 是同约定的离线原型/调参，不驱动游戏内检出结果。 |
| 真正的电磁波传播（场求解 / FDTD） | **否**。是功率与检测链路的工程近似，不是波在空间里传。 |
| 训练级雷达信号处理整链 | **否**。没有完整「波形 → RD 图 → CFAR → 航迹」实时链；但已有测量合成、量测关联、热噪声填空与 Swerling/方位 RCS。 |
| 直接把实体坐标当雷达读数 | **否（默认）**。实体只作散射体；输出为量化+噪声的量测 plot。 |

一句话：适合做**可玩、带物理门限与测量不确定性的传感器玩法**；不适合当电磁仿真器或训练级雷达孪生。

---

## 2. 当前已能做到的效果

### 检测与物理门限

- 扇区 / 可选机械扫描内，发现**载具、炮弹、主动辐射源**（仅作散射体输入）
- 通视用 `TraceMove`；遮挡时可选 **NLOS 地面反射弱检**
- 硬件参数 → 雷达方程、多普勒、MTI、处理增益、SNR 门限
- DEM σ⁰ **地面杂波**进入噪声分母
- **测量合成**：距离门中心 + 波束角抖动 + 多普勒反解径向速度（SNR 越低越抖）
- **测量噪声/偏差可调**：理想档 vs 逼真档（`MeasNoiseScale` 等）；下游可 override `RDF_RadarMeasurementModel`
- **大气 / 降雨 / 天气驱动损耗**：简化模型，可关；逼真档可开天气雨雾衰
- **散射体表拟真输入**：姿态方位 RCS、Swerling 起伏、AGL、DEM 缓存、辐射源射频摘要
- **EW 效果栈**：噪声压制 + 欺骗假目标
- **CA-CFAR**：粗栅格；空距离单元可填热噪声 → 虚警率可测（`m_EnableCfarThermalFill`）
- **联机权威路径**：`RDF_RadarNetworkComponent`（权威端挂 Sensor）

### 显示与测试

- PPI HUD：默认为匿名量测点；假目标白色；NLOS 青色
- 量测驱动 α-β 跟踪 + `PredictAt` 外推（最近邻波门；匿名/假目标可进跟踪）
- **锁定层** `RDF_RadarLockManager`：SEARCH → 截获 → 跟踪 → coast + `GetLockedTarget`
- AutoTest 双档：`RDF_RadarAutoTestSuite.StartAll()`（ideal）/
  `StartAllRealistic()`（逼真误差带）
- 单项：DEM / Ballistics / ShellFire / Airborne / Lock
- **公共门面** `RDF_RadarSensor`（SEARCH / STARE / WLR → Plots / Tracks / Lock），见 [RADAR_API.md](RADAR_API.md)

### 观感上「像雷达」的部分

- 有扫到、漏检、弱检、杂波压目标、干扰抬噪声、测距晃动的感觉
- 远距 / 低 SNR 时位置抖动、航迹漂、丢批
- **不是**地图全亮；远、挡、低 SNR、被压制会丢

---

## 3. 相对现实电磁波雷达还缺什么

### 传播与回波

- 真多径 / 刀刃绕射 / **大气折射**（衰减与雨雾损耗已有简化模型，见 §2）
- 距离门上的杂波谱（不只是 σ⁰ 功率进噪声）
- 目标起伏（Swerling）已接简化版（0–4）；完整极化 / 闪烁谱仍缺

### 信号与检测

- 训练级 RD 图 / 全脉冲 CFAR（当前为粗栅格 CA-CFAR + 热填空；尚无可配置 GO-CFAR / SO-CFAR）
- 完整方向图、副瓣、距离/速度模糊
- STAP 等更认真的杂波抑制（现仅简化 MTI）

### 跟踪与系统

- 多假设关联 / JPDA（现为单假设最近邻）
- 多雷达组网、IFF、数据链融合
- 搜索 → 截获 → 跟踪的**资源管理**与**武器制导对接**（锁定状态机已有）

### 电子战

| 类型 | 状态 |
|------|------|
| 噪声压制 | **已有**（抬接收噪声 → 降 SNR） |
| 欺骗 / DRFM / 拖距拖速 | **已接简化假目标**；拖距 / 角闪烁 / 间歇假点等扩展仍缺；非完整 DRFM |
| 完整 ECCM | **无** |

### 引擎与联机现实

- 候选为散射体表 + Sphere/Active；仍非体素体积搜索
- 已有服务器权威同步路径；未覆盖复杂多雷达融合与带宽自适应

---

## 4. 在现有约束下「能做到」的增强

与 [TODO.md](../TODO.md) Sprint C / 延后项对齐：

### 游戏内（推荐）

1. **可配置 GO-CFAR / SO-CFAR**（杂波边缘更稳）
2. **扩展欺骗效果库**（拖距、角闪烁、间歇假点）
3. **锁定层对接武器**（见 [VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md)）
4. 扫描优化阈值下沉 `RDF_RadarSettings`；反炮兵 WLR HUD
5. 联机：带宽自适应与多雷达融合（单雷达权威路径已有）

### 离线（Python + DEM）

- 更认真的传播、杂波谱、CFAR 原型  
- 标定表（功率因子、σ⁰、干扰耦合）再喂回游戏参数  

### 不宜作为游戏内实时主路径

- FDTD / 全场电磁波  
- 训练级信号处理整链每帧跑满  

---

## 5. 建议优先级（只选三条时）

1. GO/SO-CFAR + 欺骗扩展（玩法虚警 / EW 厚度）  
2. 锁定层对接武器 / 反炮兵 HUD  
3. 更细多径/绕射近似（有明确山地场景需求时）

目标产品形态：**可信的军武传感器玩法**（发现 / 丢失 / 压制 / 欺骗 / 锁定），而不是真实雷达仿真器。

---

## 6. 相关入口

- 游戏内流水线：[RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)  
- 公共 API：[RADAR_API.md](RADAR_API.md)  
- DEM：[DEM.md](DEM.md)  
- 待办清单：[TODO.md](../TODO.md)  
- 噪声干扰用法示例见 `RADAR_GAME_FRAMEWORK.md` 中 Optional EW noise 一节  
