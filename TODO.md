# TODO — RDF

按 **收益 ÷ 成本** 排期；标签只作分类，不决定顺序。  
相关：[RADAR_API.md](docs/RADAR_API.md) · [RADAR_CAPABILITIES.md](docs/RADAR_CAPABILITIES.md) · [VEHICLE_RADAR_LOCK_GUIDE.md](docs/VEHICLE_RADAR_LOCK_GUIDE.md) · [CHANGELOG.md](docs/CHANGELOG.md)

---

## 定位

框架已复现功能链：**搜索 → 点迹 → 跟踪 → 锁定/火控 → WLR**。  
物理噪声 / 大气 / 信号处理高度理想化——回归“过准”是在无噪声数学世界验证闭环，不是战场精度。

| 当前强项 | 当前弱项 |
|----------|----------|
| 功能链、Sensor 门面、双档回归 | 精细多径、绕射 |
| 网格、LOS/物理复用、分片预算 + 统计 | GO/SO-CFAR、组网 |
| 测量噪声/偏差、热噪声填空、大气衰减 | 远程算力、IFF |

入口：[README.md](README.md)

---

## 实施顺序（按收益）

收益：玩法体感 / 可信度 / 可维护性。成本：改动面、回归风险、联调量。  
原则：**先让误差合常理，再加深模型；先可观测，再可调参；远程与组网最后。**

### 1 — 立刻做（高收益 · 低成本）

| 项 | 收益 | 成本 | 为何现在做 |
|----|------|------|------------|
| CHANGELOG 补扫描优化条目 | 文档与现状对齐 | 极低 | 欠账，几分钟 |
| `GetStatusShort` / stats：`reuseHits` `freshUpdates` `budgetSkips` | 能看见优化是否生效 | 低 | 无观测就无法调预算 |
| 回归双档：理想档仍可“准”；逼真档用误差带验收 | 防止 P2 把套件卡死 | 低 | 物理项的前置护栏 |

- [x] CHANGELOG：LOS 缓存 / 分片预算 / 物理复用 / 优先级扫描
- [x] 复用与预算命中统计写入状态行
- [x] AutoTest：`ideal` vs `realistic`（或 settings 开关）；逼真档断言改为误差带

### 2 — 下一波（高收益 · 中成本）← 已落地核心

目标：玩法上出现“发现不稳定 / 虚警 / 测距晃动”，而不是永远 0.3 m。

| 项 | 收益 | 成本 | 依赖 |
|----|------|------|------|
| 测量噪声与偏差（距离 / 方位 / 多普勒） | 跟踪与 WLR 立刻“合常理” | 中 | 双档回归 |
| 热噪声填空距离单元 | CFAR 有真实虚警率 | 中 | 无 |
| 大气 / 降雨简化衰减 | 远距探测变难，体感强 | 中 | 测量噪声后更自然 |

- [x] 测量噪声与系统性偏差（合成测量路径）
- [x] 热噪声填充空距离单元 → CA-CFAR 虚警可测
- [x] 大气衰减 / 降雨损耗（简化模型，可关）
- [x] 逼真档套件真正加压：CFAR+热填空保持开启；`MeasNoiseScale≈3.5`；WLR 误差带 800 m
- [x] 测量误差扩展接口：`RDF_RadarMeasurementModel`（CFAR 后 / Tracker 前，下游可 override）

### 3 — 再加深（中收益 · 中高成本）← 下一步

| 项 | 收益 | 成本 | 说明 |
|----|------|------|------|
| GO-CFAR / SO-CFAR | 杂波边缘更稳 | 中 | 热噪声落地后再做才有意义 |
| 扫描阈值进 `RDF_RadarSettings` | 不同雷达可调 | 低中 | 有 stats 后再调参 |
| 欺骗扩展：拖距 / 角闪烁 / 间歇假点 | EW 玩法厚度 | 中 | 不挡物理主线 |
| 反炮兵 HUD（发射/落点圈） | WLR 产品化 | 中 | 功能已有，缺呈现 |

- [ ] 可配置 GO-CFAR / SO-CFAR
- [ ] 扫描优化阈值下沉 `RDF_RadarSettings`
- [ ] 欺骗库扩展（拖距、角闪烁、间歇假点）
- [ ] 反炮兵专用 HUD（WLR 告警圈）

### 4 — 延后（收益情景化 · 成本高）

| 项 | 收益 | 成本 | 何时启动 |
|----|------|------|----------|
| 刀刃绕射 / 精细多径 | 山地遮挡更真 | 高 | 基础噪声稳定后 |
| DEM 柱状 span 遮挡 / 多径 | 城区/林冠 | 高 | 有明确场景需求时 |
| 多雷达组网 / 交叉定位 | 战役级 | 很高 | 单雷达玩法打磨完 |
| 远程计算后端 | 卸服务器算力 | 很高 | 本地算力真成瓶颈 |
| `mass_battle_sim` 拆分 | 离线可维护 | 中 | 离线仿真要大改时 |
| Python CFAR/track golden | 防漂移 | 中 | 逼真模型定稿后 |
| IFF / 数据链 | 产品层 | 高 | 玩法需要识别友军时 |
| AutoTest 批跑 / 最小 CI | 工程卫生 | 中 | 多人协作或发版频繁时 |
| LiDAR PPI 动画 / DiagMenu / Sensor 对齐 | LiDAR 体验 | 中 | 雷达主线空窗 |

- [ ] 刀刃绕射 / 更精细多径
- [ ] DEM span 遮挡 / 多径
- [ ] 多雷达 plots 融合 / 交叉定位
- [ ] 远程计算：设计文档 → Backend 接口 → Rest 异步 → 服务端对齐（失败回退 Local）
- [ ] 拆分 `rdf_radar_mass_battle_sim.py`
- [ ] Python CFAR / track 单测 + 可选 Enforce golden
- [ ] IFF / 数据链抽象
- [ ] 游戏内 AutoTest 批跑或最小 CI
- [ ] LiDAR：PPI 动画、DiagMenu、Sensor 门面对齐

### 5 — 停车场（低收益或高风险，默认不做）

- [ ] `LICENSE` / `LICENSE.txt` 合并（工具链兼容未确认前不动）
- 全场 FDTD / 训练级 RD 图每帧 / 完整 DRFM
- 远程替代本地 Trace / 实体查询

---

## 建议冲刺切片

**Sprint A+B（已完成）**：§1 + §2 — 观测、双档、测量噪声、热噪声填空、大气衰减。  
**Sprint C（按需）**：GO/SO-CFAR、Settings 阈值、欺骗扩展、WLR HUD。  
**以后**：绕射 / DEM span / 组网 / 远程算力。

Ideal：`RDF_RadarAutoTestSuite.StartAll()`  
Realistic：`RDF_RadarAutoTestSuite.StartAllRealistic()`

---

## 已落地（摘要）

### 功能链

- [x] 扫描 + Trace LOS / NLOS · 分类跟踪 · 雷达方程 / 多普勒 / MTI / SNR
- [x] PPI · DEM 杂波 · CA-CFAR · EW / 欺骗 · 测量合成
- [x] 弹道 + WLR · 散射体表 / Signature / Swerling
- [x] `RDF_RadarSensor` · `RDF_RadarLockManager` · `GetLockedTarget`
- [x] Network 权威端挂 Sensor

### 工程 / 性能

- [x] 散射体 XZ 网格 · Scanner 拆分 · DEM `$profile`→`DemData/`
- [x] LOS 缓存 · 多帧预算 · 优先级扫描 · 低速物理复用

### 测试

- [x] `AutoTestSuite` 5/5（Ballistics / DEM / Lock / Airborne / ShellFire）
- [x] 测试不加辐射源 / 抬 RCS / 强制写表

### DEM / 仓库

- [x] V3 烘焙 · 运行时 LRU · 杂波 + WLR 地面
- [x] `.gitignore` · 空目录清理 · P1 CHANGELOG 摘要

### LiDAR（维护）

- [x] 点云 / HUD / CSV / 网络 / Bootstrap  
- 体验项见上方 §4

---

## 明确不做（实时主路径）

- 全场电磁波 / FDTD · 训练级 RD 图每帧 · 完整 DRFM
- 远程替代本地 Trace / 实体查询

产品目标：**可信的军武传感器玩法**（发现 / 丢失 / 压制 / 欺骗 / 锁定），不是电磁仿真器，也不用理想世界冒充战场精度。
