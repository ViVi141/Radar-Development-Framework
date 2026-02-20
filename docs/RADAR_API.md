# Radar Development Framework — API Reference

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Contact: 747384120@qq.com  
License: Apache-2.0

---

## 模块总览

```
scripts/Game/RDF/Radar/
  Core/
    RDF_EMWaveParameters.c      // 电磁波物理参数
    RDF_RadarSample.c           // 雷达采样数据结构（继承 RDF_LidarSample）
    RDF_RadarSettings.c         // 雷达扫描器配置（继承 RDF_LidarSettings）
    RDF_RadarScanner.c          // 雷达扫描器核心（继承 RDF_LidarScanner）
  Physics/
    RDF_RadarPropagation.c      // 电磁波传播模型（FSPL / 大气 / 雨衰）
    RDF_RCSModel.c              // 目标雷达截面积模型
    RDF_RadarEquation.c         // 雷达方程与 SNR 计算
    RDF_DopplerProcessor.c      // 多普勒效应处理
    RDF_CFar.c                  // CA-CFAR / OS-CFAR 恒虚警检测（PoC）
  Modes/
    RDF_RadarMode.c             // 工作模式基类 + 四种具体模式
  Visual/
    RDF_RadarColorStrategy.c    // 雷达专用颜色策略（SNR / RCS / 多普勒 / 复合）
    RDF_RadarDisplay.c          // PPI 平面显示 + A-Scope 距离幅度显示
  Advanced/
    RDF_SARProcessor.c          // 合成孔径雷达（SAR）处理器
  ECM/
    RDF_JammingModel.c          // 电子对抗干扰模型
  Classification/
    RDF_EntityPreClassifier.c   // 实体预分类（结构层次 / 名称启发式）
    RDF_TargetClassifier.c      // 目标自动分类器（SNR / 速度规则，8 类）
  Demo/
    RDF_RadarDemoConfig.c       // 预设演示配置工厂
  Tests/
    RDF_RadarTests.c            // 单元测试
    RDF_RadarBenchmark.c        // 性能基准测试
  Util/
    RDF_RadarExport.c           // CSV 数据导出
```

---

## Core — 核心数据与扫描器

### `RDF_EMWaveParameters`

电磁波物理参数容器，所有雷达模拟的基础。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `m_CarrierFrequency` | `float` | `10e9` | 载波频率 (Hz) |
| `m_Wavelength` | `float` | `0.03` | 波长 (m)，可自动计算 |
| `m_BandName` | `string` | `"X"` | 波段名称（L/S/C/X/Ku/K/Ka/V/W）|
| `m_TransmitPower` | `float` | `1000.0` | 发射功率 (W) |
| `m_PulseWidth` | `float` | `1e-6` | 脉冲宽度 (s) |
| `m_PRF` | `float` | `1000.0` | 脉冲重复频率 (Hz) |
| `m_AntennaGain` | `float` | `30.0` | 天线增益 (dBi) |
| `m_BeamwidthAzimuth` | `float` | `3.0` | 水平波束宽度 (度) |
| `m_BeamwidthElevation` | `float` | `3.0` | 垂直波束宽度 (度) |
| `m_ReceiverSensitivity` | `float` | `-90.0` | 接收机灵敏度 (dBm) |
| `m_NoiseFigure` | `float` | `5.0` | 噪声系数 (dB) |

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `CalculateWavelength()` | `void` | 由频率计算波长：λ = c / f |
| `DetermineBand()` | `void` | 根据频率自动设置 `m_BandName` |
| `Validate()` | `bool` | 校验并修正参数至合理范围，返回原始是否合法 |
| `GetDescription()` | `string` | 返回可读的调试描述字符串 |

---

### `RDF_RadarSample : RDF_LidarSample`

单次射线的雷达回波数据结构，继承 LiDAR 几何字段。

**继承自 `RDF_LidarSample` 的字段**：`m_Index`、`m_Hit`、`m_Start`、`m_End`、`m_Dir`、`m_HitPos`、`m_Distance`、`m_Entity`、`m_Surface`、`m_ColliderName`

**新增雷达字段**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `m_TransmitPower` | `float` | 发射功率 (W) |
| `m_ReceivedPower` | `float` | 接收功率 (W) |
| `m_SignalToNoiseRatio` | `float` | 信噪比 (dB)，初始 -999 表示无效 |
| `m_RadarCrossSection` | `float` | 目标 RCS (m²) |
| `m_PathLoss` | `float` | 自由空间路径损耗 (dB) |
| `m_AtmosphericLoss` | `float` | 大气衰减损耗 (dB) |
| `m_RainAttenuation` | `float` | 雨衰 (dB) |
| `m_PhaseShift` | `float` | 双程相位偏移 (rad)：φ = 4πR/λ |
| `m_DopplerFrequency` | `float` | 多普勒频移 (Hz) |
| `m_TargetVelocity` | `float` | 目标径向速度 (m/s)，正值 = 接近 |
| `m_IncidenceAngle` | `float` | 入射角 (度) |
| `m_ReflectionCoefficient` | `float` | 反射系数 (0–1) |
| `m_MaterialType` | `string` | 命中表面材质名 |
| `m_PolarizationType` | `string` | 极化类型（"H"/"V"/"C"）|
| `m_PolarizationLoss` | `float` | 极化损耗 (dB) |
| `m_TimeOfFlight` | `float` | 双程飞行时间 (s) |
| `m_DelayTime` | `float` | 传播延迟 (s)，单站雷达 = TimeOfFlight |

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `CalculateTimeOfFlight()` | `void` | 由距离计算飞行时间：t = 2R/c |
| `IsDetectable(float thresholdDB)` | `bool` | SNR ≥ 阈值时返回 true |
| `GetRCSdBsm()` | `float` | 将 RCS 转换为 dBsm |

---

### `RDF_RadarSettings : RDF_LidarSettings`

雷达扫描器的全量配置，传入 `RDF_RadarScanner` 构造函数。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `m_EMWaveParams` | `ref RDF_EMWaveParameters` | 自动创建 | 电磁波参数 |
| `m_RadarMode` | `ERadarMode` | `PULSE` | 工作模式枚举 |
| `m_DetectionThreshold` | `float` | `10.0` | 检测 SNR 门限 (dB) |
| `m_EnableDopplerProcessing` | `bool` | `true` | 启用多普勒计算 |
| `m_EnableMTI` | `bool` | `false` | 动目标指示（硬门限） |
| `m_EnableMTD` | `bool` | `false` | 动目标检测（渐进 SNR 惩罚） |
| `m_EnableAtmosphericModel` | `bool` | `false` | 启用大气衰减 |
| `m_Temperature` | `float` | `20.0` | 环境温度 (°C) |
| `m_Humidity` | `float` | `60.0` | 相对湿度 (%) |
| `m_RainRate` | `float` | `0.0` | 降雨率 (mm/h) |
| `m_UseRCSModel` | `bool` | `true` | 启用 RCS 估算 |
| `m_UseMaterialReflection` | `bool` | `true` | 材质反射修正 |
| `m_EnableClutterFilter` | `bool` | `true` | 杂波（地面）抑制 |
| `m_MinTargetVelocity` | `float` | `1.0` | MTI/MTD 最小速度 (m/s) |
| `m_EnableCFAR` | `bool` | `false` | 启用 CFAR 局部噪声自适应检测（PoC） |
| `m_CfarUseOrderStatistic` | `bool` | `false` | 使用 OS‑CFAR（Order‑Statistic）而非 CA‑CFAR |
| `m_CfarAutoScale` | `bool` | `false` | 自动根据 `m_CfarTargetPfa` 计算 CA‑CFAR 阈值倍数 |
| `m_CfarTargetPfa` | `float` | `1e-6` | 目标虚警率 Pfa（仅当 `m_CfarAutoScale`=true 生效） |
| `m_CfarWindowSize` | `int` | `16` | CFAR 参考单元数量（最近邻采样数） |
| `m_CfarOrderRank` | `int` | `8` | OS‑CFAR 排序阶位（1=最大, windowSize=最小，PoC） |
| `m_CfarGuardAngleDeg` | `float` | `2.0` | CFAR 守护带角半径（度） |
| `m_CfarMultiplier` | `float` | `6.0` | CFAR 门限倍数（线性功率倍率） |
| `m_CfarUseOfflineTable` | `bool` | `true` | 启用从仓库/磁盘加载离线 OS‑CFAR 查表（优先于运行时估算） |
| `m_CfarOfflineTablePath` | `string` | `"scripts/Game/RDF/Radar/Data/os_cfar_multipliers.csv"` | 离线查表文件路径 |
| `m_EnableBlindSpeedFilter` | `bool` | `false` | 启用盲速（Doppler alias）抑制 |
| `m_BlindSpeedToleranceHz` | `float` | `1.0` | 盲速容差（Hz） |
| `m_EnableSidelobes` | `bool` | `false` | 启用主波束周围旁瓣射线仿真（PoC）|
| `m_SidelobeSampleCount` | `int` | `6` | 旁瓣射线数量（PoC）|
| `m_SidelobeFraction` | `float` | `0.01` | 旁瓣总功率占发射功率比例（1%，均分到各旁瓣射线）|

**`ERadarMode` 枚举**：

| 值 | 名称 | 说明 |
|----|------|------|
| `0` | `PULSE` | 脉冲雷达：距离量化、最大无模糊距离限制 |
| `1` | `CW` | 连续波：仅测速，距离归零 |
| `2` | `FMCW` | 调频连续波：高分辨率距离量化 |
| `3` | `PHASED_ARRAY` | 相控阵：多波束 SNR 损耗修正 |

---

### `RDF_RadarScanner : RDF_LidarScanner`

雷达扫描器主类，继承 LiDAR 扫描器并添加完整电磁波物理流水线。

```c
// 构造，传入 RDF_RadarSettings（或 null 使用默认值）
void RDF_RadarScanner(RDF_LidarSettings settings = null)

// 执行扫描，outSamples 中填充 RDF_RadarSample 对象
override void Scan(IEntity subject, array<ref RDF_LidarSample> outSamples)

// 获取雷达专用配置
RDF_RadarSettings GetRadarSettings()

// 运行时切换工作模式（同时重建模式处理器）
void SetRadarMode(ERadarMode mode)

// 获取当前模式处理器对象
RDF_RadarMode GetRadarModeProcessor()
```

**每条射线的物理流水线顺序**（`ApplyRadarPhysics` 内部）：

```
1. 传播损耗（FSPL + 可选大气/雨衰）
2. 目标 RCS（基于包围盒体积 + 材质反射率 + 入射角修正）
3. 接收功率（雷达方程线性形式）
4. SNR（热噪声模型，假定 1 MHz 等效带宽）
5. 飞行时间 / 相位偏移 / 延迟时间
6. 多普勒处理（实体物理速度投影）
7. 检测判决（SNR 门限）
8. MTI 滤波（硬门限）
9. MTD 滤波（渐进惩罚）
10. 杂波滤波（地形静止回波抑制）
11. 工作模式处理（距离量化 / 无模糊范围 / 相控阵损耗）
```

---

## Physics — 物理模型

### `RDF_RadarPropagation`（全静态）

| 方法 | 说明 |
|------|------|
| `CalculateFSPL(distance, frequency)` | 自由空间路径损耗 (dB)：FSPL = 92.45 + 20·log(d_km) + 20·log(f_GHz) |
| `CalculateAtmosphericAttenuation(d, f, T, RH)` | ITU-R P.676 简化大气衰减 (dB)，10 GHz 以下返回 0 |
| `CalculateRainAttenuation(d, f, rainRate)` | ITU-R P.838 简化雨衰 (dB) |
| `CalculateTotalLoss(d, f, T, RH, rain, enableAtm)` | 合并三项损耗的便捷方法 |

---

### `RDF_RCSModel`（全静态）

| 方法 | 说明 |
|------|------|
| `CalculateSphereRCS(radius, wavelength)` | 球体解析 RCS（光学/瑞利/过渡三区） |
| `CalculatePlateRCS(area, wavelength)` | 平板正向入射 RCS：σ = 4π·A²/λ² |
| `CalculateCylinderRCS(length, radius, wavelength)` | 圆柱侧向 RCS（光学区） |
| `GetMaterialReflectivity(matName)` | 按材质名返回反射率 (0–1)，区分金属/混凝土/木材/水体等 |
| `GetAngleDependentReflection(angleDeg, isMetal)` | 入射角反射修正因子 |
| `EstimateEntityRCS(entity, radarPos, rayDir, wavelength, materialName)` | 根据实体包围盒综合估算 RCS |

---

### `RDF_RadarEquation`（全静态）

| 方法 | 说明 |
|------|------|
| `CalculateReceivedPower(Pt, Gt, λ, σ, R, L)` | 单基地雷达方程（线性形式），返回接收功率 (W) |
| `CalculateReceivedPowerDB(Pt_dBm, Gt_dB, λ, σ_dBsm, R, L_dB)` | 雷达方程（dB 形式），返回接收功率 (dBm) |
| `CalculateMaxDetectionRange(Pt, Gt, λ, σ_min, Pr_min, L)` | 最大探测距离 (m) |
| `CalculateSNR(Pr, Pn, integrationGain)` | 信噪比 (dB) |
| `CalculateNoisePower(bandwidth, NF_dB, T)` | 热噪声功率 (W)：Pn = k·T·B·F |
| `DBmToWatts(dBm)` | 单位转换：dBm → W |
| `WattsToDBm(watts)` | 单位转换：W → dBm |
| `DBiToLinear(gainDB)` | dBi → 线性增益 |
| `LinearToDBi(linearGain)` | 线性增益 → dBi |

---

### `RDF_DopplerProcessor`（全静态）

| 方法 | 说明 |
|------|------|
| `CalculateDopplerShift(vr, f0)` | 双程多普勒频移：fd = 2·vr·f0/c |
| `CalculateRadialVelocity(radarEntity, targetEntity)` | 从实体物理组件获取径向速度 (m/s) |
| `CalculateRadialVelocityFromVectors(rPos, tPos, rVel, tVel)` | 从速度向量计算径向速度 |
| `VelocityFromDoppler(fd, f0)` | 逆运算：由频移计算速度 |
| `IsMovingTarget(fd, minHz)` | MTI 判决：|fd| > minHz 则为运动目标 |
| `MaxUnambiguousVelocity(prf, f0)` | 最大无模糊速度：v_max = PRF·c/(4·f0) |

---

### `RDF_CFar`（检测门限辅助，PoC）

提供 CA‑CFAR 与 OS‑CFAR 的 PoC 实现，并支持基于目标 Pfa 的阈值查表/估计。

| 方法 | 说明 |
|------|------|
| `ApplyCA_CFAR(samples, window, guardDeg, mult)` | CA‑CFAR（平均法）应用到扫描样本 |
| `ApplyOS_CFAR(samples, window, guardDeg, mult, rank)` | OS‑CFAR（Order‑Statistic）应用 |
| `CalculateCAThresholdMultiplier(N, Pfa)` | 解析式计算 CA‑CFAR 的阈值倍数 alpha |
| `EstimateOSMultiplier(N, rank, Pfa, sims)` | Monte‑Carlo 估算 OS‑CFAR multiplier（会缓存结果） |
| `PrecomputeOSMultiplierTable(windows, ranks, pfas, sims)` | 预计算并缓存常用 OS‑CFAR 查表值 |
| `GetCachedOSMultiplier(N, rank, Pfa)` | 从缓存读取 OS‑CFAR multiplier，找不到返回 -1 |

注：当前实现为 PoC，已包含缓存/查表以避免重复 Monte‑Carlo 计算；生产级使用建议用闭式近似或预计算查表文件。
---

## Modes — 工作模式

所有模式类继承 `RDF_RadarMode`，由 `RDF_RadarScanner` 在构造时自动实例化。

| 类名 | `ERadarMode` | 核心行为 |
|------|-------------|---------|
| `RDF_RadarMode` | 基类 | 无操作（pass-through） |
| `RDF_PulseRadarMode` | `PULSE` | 按 c·τ/2 量化距离；超出 c/(2·PRF) 的回波置为未命中 |
| `RDF_CWRadarMode` | `CW` | 静止目标置为未命中；`m_Distance` 归零（CW 不测距） |
| `RDF_FMCWRadarMode` | `FMCW` | 按 c/(2·B) 量化距离；构造器参数 `sweepBandwidth`（默认 500 MHz） |
| `RDF_PhasedArrayMode` | `PHASED_ARRAY` | 多波束 SNR 惩罚：ΔL = 10·log(beamCount)；构造器参数 `beamCount` |

---

## Visual — 可视化

### `RDF_RadarColorStrategy`（基类，继承 `RDF_LidarColorStrategy`）

提供 `BuildPointColorFromRadarSample(sample, settings)` 方法，子类覆盖此方法实现雷达专用配色。

| 子类 | 映射逻辑 |
|------|---------|
| `RDF_SNRColorStrategy` | SNR < 10 dB 红→黄；10–20 dB 黄→绿；20–30 dB 绿→青；> 30 dB 纯青 |
| `RDF_RCSColorStrategy` | −20 ~ +20 dBsm 映射蓝→青→黄→红 |
| `RDF_DopplerColorStrategy` | 接近=红，静止=白，远离=蓝；构造器参数 `maxVelocity`（默认 50 m/s） |
| `RDF_RadarCompositeColorStrategy` | 多普勒决定色调，SNR 决定亮度 |

### `RDF_PPIDisplay`

平面位置显示（俯视图）。

```c
void DrawPPI(array<ref RDF_LidarSample> samples, IEntity radarEntity)
```

可配置字段：`m_DisplayRadius`（默认 200 m）、`m_GridColor`、`m_HeightOffset`（默认 2 m）。

### `RDF_AScopeDisplay`

距离-幅度显示。

```c
void DrawAScope(array<ref RDF_LidarSample> samples, float maxRange, float thresholdDB)
```

### `RDF_RadarWorldMarkerDisplay`

在 3D 世界坐标系中于命中点上方绘制彩色竖直立柱标记。

```c
void DrawWorldMarkers(array<ref RDF_LidarSample> samples, float heightM = 5.0)
void ClearMarkers()
```

颜色映射逻辑：SNR 越高颜色越亮；地物（无实体）使用灰色系。内部持有 `array<ref Shape>` 防止 GC 提前释放。

### `RDF_RadarTextDisplay`

在引擎控制台打印 ASCII 俯视雷达地图。

```c
void PrintRadarMap(array<ref RDF_LidarSample> samples, IEntity radarEntity,
                   int gridSize = 41, float cellSizeM = 50.0)
```

坐标映射：`delta[0]`（东，+X）→ 列增大；`delta[2]`（北，+Z）→ 行减小（北在图上方）。字符含义：`+` = 玩家，`E` = 实体，`.` = 地物回波，空格 = 无回波。

可配置字段：`m_Origin`、`m_HorizontalScale`、`m_VerticalScale`、`m_MaxBarHeight`、`m_DetectedColor`、`m_SubthresholdColor`。

---

## Advanced — 高级功能

### `RDF_SARProcessor`

简化合成孔径雷达处理器。

```c
void AddScan(array<ref RDF_LidarSample> samples, vector platformPos)
void GenerateSARImage(float areaSize, int resolution, vector areaCenter,
                      out array<vector> outPositions, out array<float> outIntensities)
void VisualiseImage(array<vector> positions, array<float> intensities,
                    float maxIntensity, float sphereRadius)
void Reset()
int  GetSnapshotCount()
```

`m_MaxSnapshots`（默认 32）控制最大历史快照数。每帧调用 `VisualiseImage` 以刷新可视化（内部自动释放旧形状）。

---

## ECM — 电子对抗

### `RDF_JammingModel`（全静态）

| 方法 | 说明 |
|------|------|
| `CalculateNoiseJammingJS(Pj, Gj, Gr, λ, Rj, Br)` | 计算噪声干扰 J/S 比 (dB) |
| `ApplyNoiseJamming(samples, jsDB, thresholdDB)` | 对扫描结果批量施加噪声干扰，降低 SNR |
| `InjectFalseTargets(samples, count, x, y, z, minR, maxR, snr)` | 注入欺骗性假目标样本 |
| `CalculateChaffRCS(numDipoles, wavelength)` | 计算箔条云等效 RCS (m²) |

---

## Classification — 目标分类

### `RDF_TargetClassifier`（全静态）

**`ERadarTargetClass` 枚举**：`UNKNOWN(0)`, `INFANTRY(1)`, `VEHICLE_LIGHT(2)`, `VEHICLE_HEAVY(3)`, `AIRCRAFT(4)`, `STATIC_OBJECT(5)`, `SMALL_UAV(6)`, `NAVAL(7)`

| 方法 | 说明 |
|------|------|
| `ClassifyTarget(sample)` | 按 RCS + 速度规则分类单个样本 |
| `ClassifyAll(samples, out outClasses)` | 批量分类并输出平行数组 |
| `GetClassName(cls)` | 枚举值转可读字符串 |
| `PrintStats(samples)` | 打印本次扫描分类统计到日志 |

**分类规则**（优先级从高到低）：

| 类别 | 速度条件 | RCS 条件 |
|------|---------|---------|
| STATIC_OBJECT | < 0.5 m/s | 任意 |
| AIRCRAFT | > 50 m/s | 任意 |
| SMALL_UAV | 5–50 m/s | < 0.05 m² |
| INFANTRY | < 5 m/s | < 1 m² |
| NAVAL | < 15 m/s | > 1000 m² |
| VEHICLE_HEAVY | < 40 m/s | ≥ 10 m² |
| VEHICLE_LIGHT | < 40 m/s | ≥ 1 m² |
| UNKNOWN | — | — |

---

## Demo — 演示系统

### `RDF_RadarDemoConfig : RDF_LidarDemoConfig`

| 工厂方法 | 频段 | 功率 | 增益 | 模式 | 采样策略 | 量程 |
|---------|------|------|------|------|---------|------|
| `CreateHelicopterRadar(rayCount=512)` | X (10 GHz) | 500 W | 28 dBi | PULSE | SweepStrategy 60°×30° | 8 km |
| `CreateXBandSearch(rayCount=512)` | X (10 GHz) | 5 W¹ | 30 dBi | PULSE | SweepStrategy 60°×30° | 3 km |
| `CreateAutomotiveRadar(rayCount=256)` | Ka (77 GHz) | 100 mW | 25 dBi | FMCW | ConicalStrategy 15° | 150 m |
| `CreateWeatherRadar(rayCount=1024)` | S (3 GHz) | 50 kW | 40 dBi | PULSE | HemisphereStrategy | 5 km |
| `CreatePhasedArrayRadar(rayCount=2048)` | C (5.6 GHz) | 10 kW | 35 dBi | PHASED_ARRAY | SweepStrategy 60°×30° | 2 km |
| `CreateLBandSurveillance(rayCount=512)` | L (1.3 GHz) | 500 kW | 33 dBi | PULSE | UniformStrategy | 5 km |

¹ 演示缩放值，场景建筑物约 97 dB SNR。

```c
// 构建纯配置对象（需要手动创建 Scanner）
RDF_RadarSettings BuildRadarSettings()

// 构建完整 Scanner（含采样策略，推荐使用）
RDF_RadarScanner BuildRadarScanner()
```

---

### `RDF_RadarAutoRunner`（单例）

雷达演示系统的主驱动器，管理扫描循环、回调派发与世界标记显示。

| 静态方法 | 说明 |
|---------|------|
| `GetInstance()` | 获取单例（懒初始化） |
| `StartWithConfig(cfg)` | 以指定预设启动演示 |
| `Stop()` | 停止演示并清理 |
| `SetScanCompleteHandler(handler)` | 注册 `RDF_LidarScanCompleteHandler` 回调 |
| `GetLastSamples()` | 获取上次扫描结果副本（返回 `array<ref RDF_LidarSample>`，防御性复制）|
| `IsRunning()` | 是否正在运行 |

**Bootstrap 控制变量**（在 `RDF_RadarAutoBootstrap.c` 中设置）：

```c
static bool s_RadarBootstrapEnabled    = false;  // 默认关闭，需调用 SetRadarBootstrapEnabled(true) 启用
static bool s_RadarBootstrapShowHUD    = true;   // 显示 HUD
static int  s_RadarBootstrapConfigIdx  = 0;      // 默认预设索引（0=XBandSearch）
```

### `RDF_RadarDemoStatsHandler : RDF_LidarScanCompleteHandler`

扫描完成后打印统计摘要到引擎日志，并附 ASCII 俯视雷达地图。

```c
override void OnScanComplete(array<ref RDF_LidarSample> samples)
```

输出字段：总射线数 / 命中数 / 平均 SNR / 最大 SNR / 平均 RCS / 平均速度 / 量程区间 / ASCII 地图。

### `RDF_RadarDemoCycler`（全静态）

五种预设的轮换控制器。

```c
static void Cycle()                                    // 切换到下一预设
static void StartIndex(int idx, int rayCount = -1)     // 按索引启动（0–5）
static void Stop()                                     // 停止演示
static void StartAutoCycle(float intervalSec, int rayCount = -1)  // 定时自动轮换
static void StopAutoCycle()
static void SetAutoCycleInterval(float intervalSec)
static bool IsAutoCycling()
static string GetPresetName(int idx)                   // 获取预设名称
static int  GetPresetCount()                           // 预设总数（当前 6）
static int  GetCurrentIndex()                          // 当前预设索引（-1=未启动）
static void RebuildList(int rayCount)                  // 以不同射线数重建预设列表
```

### `RDF_RadarAutoBootstrap`

通过 `modded class SCR_BaseGameMode` 实现游戏启动时自动运行雷达演示。默认**已关闭**，需显式启用。

```c
// 控制开关（在运行时调用或修改源文件静态变量）
static bool s_RadarBootstrapEnabled = false;  // 默认关闭，需手动启用
static bool s_RadarBootstrapShowHUD = true;

// 运行时启用：
SCR_BaseGameMode.SetRadarBootstrapEnabled(true);
```

启动序列：
1. `SCR_BaseGameMode.OnGameStart()` → `super.OnGameStart()`
2. 若 `s_RadarBootstrapEnabled`：创建 `RDF_RadarDemoStatsHandler` 并注册回调
3. 以 `CreateXBandSearch()` 启动演示
4. 若 `s_RadarBootstrapShowHUD`：调用 `RDF_RadarHUD.Show()` 并将 HUD 注册为扫描回调

---

## UI — 片上 HUD

### `RDF_RadarHUD : RDF_LidarScanCompleteHandler`

完全基于脚本的雷达 HUD，使用 `WorkspaceWidget.CreateWidgetInWorkspace` 动态创建所有控件，无需 `.layout` 文件。

**单例访问**：

```c
static RDF_RadarHUD GetInstance()
static void Show()    // 创建并显示 HUD
static void Hide()    // 销毁 HUD 所有控件
```

**配置方法**：

```c
static void SetMode(string modeName)        // 更新标题栏模式文本（如 "Heli Radar"）
static void SetDisplayRange(float rangeM)   // 设置 PPI 显示量程（米），控制 PPI 比例尺（默认 8000 m）
```

**可见性查询**：

```c
static bool IsVisible()                     // HUD 当前是否可见（控件已创建）
```

**手动推送数据（脱离 AutoRunner 使用）**：

```c
// 自行扫描后直接驱动 HUD，无需开启 Demo。
// 调用前须先 Show()，并通过 SetDisplayRange() 设置与 scanner 相符的量程。
static void FeedSamples(array<ref RDF_LidarSample> samples)
```

示例（纯逻辑扫描 + HUD 显示）：

```c
RDF_RadarScanner scanner = RDF_RadarDemoConfig.CreateHelicopterRadar().BuildRadarScanner();
array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
IEntity subject = RDF_LidarSubjectResolver.ResolveLocalSubject(true);
scanner.Scan(subject, samples);

RDF_RadarHUD.Show();
RDF_RadarHUD.SetDisplayRange(8000.0);
RDF_RadarHUD.SetMode("Heli Radar");
RDF_RadarHUD.FeedSamples(samples);    // 立即刷新 PPI 与数据行
```

**与 AutoRunner 对接**：

```c
// 将 HUD 注册为 AutoRunner 扫描回调，每次 AutoRunner 扫描完成后自动刷新 HUD。
static void AttachToAutoRunner()            // 等价于 RDF_RadarAutoRunner.SetScanCompleteHandler(GetInstance())
static void DetachFromAutoRunner()          // 清除 AutoRunner 的 handler 引用
```

示例（开启 Demo 并同步显示 HUD）：

```c
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateHelicopterRadar());
RDF_RadarHUD.Show();
RDF_RadarHUD.AttachToAutoRunner();          // HUD 自此随每次扫描自动更新
RDF_RadarHUD.SetDisplayRange(8000.0);
RDF_RadarHUD.SetMode("Heli Radar");
```

> **提示**：`Bootstrap` 在 `s_RadarBootstrapShowHUD = true` 时已自动调用 `Show()` + `AttachToAutoRunner()`，手动控制时可跳过 Bootstrap 直接使用以上 API。

**布局（屏幕左下角）**：

```
位置：X=20, Y=820（相对 1920×1080 参考分辨率）
尺寸：250 × 310（含头部 32 px + PPI 210 px + 数据 68 px）
```

| 子控件 | 类型 | 说明 |
|--------|------|------|
| 背景面板 | `FrameWidget` | 深绿半透明背景 |
| 标题栏 | `TextWidget` | `[>] RADAR` + 当前模式 |
| PPI 圆盘 | `CanvasWidget` (210×210) | 圆形扫描图 |
| 距离环 | `PolygonDrawCommand` | 50% / 100% 量程环 |
| 罗盘轴 | `LineDrawCommand` | N/S/E/W 十字轴 |
| 目标光点 | `PolygonDrawCommand` | 每次扫描动态重建 |
| 数据行 | `TextWidget` × 3 | SNR / RCS / 速度 / 命中数 / 量程 |

**PPI 坐标映射**：

```c
// 世界坐标 → 画布坐标
float bx = PPI_CX + (delta[0] / displayRange) * PPI_R;   // 东 → 右
float by = PPI_CY - (delta[2] / displayRange) * PPI_R;   // 北 → 上（负 Y）
```

**目标光点颜色规则**：

| 条件 | 颜色（ARGB）| 半径 |
|------|------------|------|
| 实体且 RCS > 10 dBsm | 亮绿 #00FF44 | 4 px |
| 实体且 RCS ≤ 10 dBsm | 青色 #00CCCC | 3 px |
| 地物且 SNR > 20 dB | 黄色 #FFCC00 | 2 px |
| 地物（弱回波） | 暗橙 #AA6600 | 2 px |

**节流**：`UPDATE_INTERVAL = 0.5 秒`（`System.GetTickCount()` 计时），防止高频扫描导致数据闪烁。

---

## Util — 数据导出

### `RDF_RadarExport`（全静态）

```c
static string GetCSVHeader()                               // 返回 CSV 列头字符串
static string SampleToCSVRow(RDF_RadarSample sample)       // 单样本序列化为 CSV 行
static string ScanToCSV(array<ref RDF_LidarSample> samples)// 全量导出（含未命中）
static string HitsToCSV(array<ref RDF_LidarSample> samples)// 仅导出命中样本
static void   PrintScanSummary(array<ref RDF_LidarSample> samples)// 打印统计摘要到日志
```

**CSV 列顺序**：
`Index, Hit, Distance_m, HitPos_X/Y/Z, Dir_X/Y/Z, TransmitPower_W, ReceivedPower_W, SNR_dB, RCS_m2, RCS_dBsm, PathLoss_dB, AtmLoss_dB, RainAtten_dB, DopplerFreq_Hz, TargetVel_ms, IncidenceAngle_deg, ReflectionCoeff, TimeOfFlight_s, MaterialType, EntityClass`

---

## Tests & Benchmarks

```c
RDF_RadarTests.RunAllTests()          // 运行全部物理单元测试
RDF_RadarBenchmark.RunAllBenchmarks() // 运行 128/512/1024/2048 射线基准 + LiDAR 对比
```

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.1 | 2026-02-18 | 新增 Demo 演示系统、UI HUD PPI 扫描图、RDF_RadarSimpleDisplay；修复雷达方程双重 FSPL、距离门限、杂波过滤等 Bug |
| 1.0 | 2026-02-18 | 初始电磁波雷达系统实现（核心物理、RCS、多普勒、工作模式、颜色策略、高级功能） |
