# Radar Development Framework — 使用教程

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Contact: 747384120@qq.com  
License: Apache-2.0

---

## 目录

1. [快速开始 — Bootstrap 演示、HUD 控制、程序化扫描](#1-快速开始)
2. [理解雷达样本数据](#2-理解雷达样本数据)
3. [配置雷达参数](#3-配置雷达参数)
4. [选择工作模式](#4-选择工作模式)
5. [使用可视化显示（含 HUD PPI 扫描图）](#5-使用可视化显示)
6. [采样策略](#6-采样策略)
7. [物理模型详解](#7-物理模型详解)
8. [目标分类与统计](#8-目标分类与统计)
9. [电子对抗（ECM）](#9-电子对抗)
10. [数据导出](#10-数据导出)
11. [高级：SAR 处理](#11-高级sar-处理)
12. [单元测试与基准](#12-单元测试与基准)
13. [扩展框架](#13-扩展框架)
14. [常见问题](#14-常见问题)

---

## 1. 快速开始

### 1.0 Bootstrap 自动演示（最推荐）

> **⚠️ Bootstrap 默认关闭** — LiDAR 与雷达的 Demo 均默认不自动启动，需显式启用。

启用后，游戏启动时会自动：

1. 以 X 波段搜索雷达配置（512 射线）开始周期性扫描
2. 在屏幕**左下角**显示 PPI 圆形扫描图 HUD
3. 每次扫描完成后在引擎日志中打印统计摘要

**启用方式（在你自己的 modded SCR_BaseGameMode 中调用）**：

```c
// 启用雷达 Demo
SCR_BaseGameMode.SetRadarBootstrapEnabled(true);

// 启用 LiDAR Demo（可选）
SCR_BaseGameMode.SetBootstrapEnabled(true);
```

或直接修改源文件中的静态变量：

```c
// RDF_RadarAutoBootstrap.c
protected static bool s_RadarBootstrapEnabled = true;  // 改为 true

// RDF_LidarAutoBootstrap.c
protected static bool s_BootstrapEnabled = true;       // 改为 true
```

这由 `RDF_RadarAutoBootstrap.c` 与 `RDF_LidarAutoBootstrap.c` 通过 `modded SCR_BaseGameMode` 实现。

```
+==========================================+
| [>] RADAR              X-Band Pulse     |
+------------------------------------------+
|               N                          |
|   W       (+)玩家       E               |
|               S                          |
|  内圈=1250m  外圈=2500m                  |
+------------------------------------------+
| SNR  65.2 dB   Hits  23 / 512 rays      |
| RCS  +18.0 dBsm   Vel  3.2 m/s         |
| Range  30 m  ..  1447 m                 |
+==========================================+
```

### 1.1 手动控制演示

```c
// 切换预设（0=X波段 1=车载FMCW 2=气象 3=相控阵 4=L波段远程）
RDF_RadarDemoCycler.StartIndex(2);   // 气象雷达

// 定时自动轮换（每 15 秒切换）
RDF_RadarDemoCycler.StartAutoCycle(15.0);
RDF_RadarDemoCycler.StopAutoCycle();

// 切换到下一个预设
RDF_RadarDemoCycler.CycleNext();

// 直接以某配置启动
RDF_RadarAutoRunner.StartWithConfig(RDF_RadarDemoConfig.CreateWeatherRadar());

// 停止
RDF_RadarAutoRunner.Stop();
```

### 1.2 HUD 控制

```c
// 显示/隐藏 HUD
RDF_RadarHUD.Show();
RDF_RadarHUD.Hide();

// 更新模式标签
RDF_RadarHUD.SetMode("FMCW Automotive");

// 设置 PPI 显示量程（应与扫描器 m_MaxRange 一致）
RDF_RadarHUD.SetDisplayRange(200.0);
```

### 1.3 程序化单次扫描（纯逻辑）

```c
void QuickDemo(IEntity radarEntity)
{
    RDF_RadarDemoConfig cfg = RDF_RadarDemoConfig.CreateXBandSearch(512);
    RDF_RadarScanner scanner = cfg.BuildRadarScanner();
    array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
    scanner.Scan(radarEntity, samples);
    RDF_RadarExport.PrintScanSummary(samples);
}
```

日志输出示例：
```
[RadarExport] Scan summary: 512 rays, 47 hits
[RadarExport]   Avg SNR       : 18.3 dB
[RadarExport]   Avg RCS       : 12.5 m^2
[RadarExport]   Max range     : 483.2 m
[RadarExport]   Min range     : 15.7 m
[RadarExport]   Avg Doppler   : 23.4 Hz
```

### 1.4 预设配置一览

| 工厂方法 | 典型用途 | 波段 | 最大量程 |
|---------|---------|------|---------|
| `CreateHelicopterRadar()` | 直升机前向扇博·地面目标 | X (10 GHz) | 8 000 m |
| `CreateXBandSearch()` | 地面搜索/监视 | X (10 GHz) | 3 000 m |
| `CreateAutomotiveRadar()` | 车辆防撞（FMCW，77 GHz） | Ka | 150 m |
| `CreateWeatherRadar()` | 气象探测（大功率 S 波段） | S | 5 000 m |
| `CreatePhasedArrayRadar()` | 多目标跟踪（相控阵） | C | 2 000 m |
| `CreateLBandSurveillance()` | 远程预警（L 波段） | L | 5 000 m |

---

## 2. 理解雷达样本数据

每次 `Scan()` 返回一个 `array<ref RDF_LidarSample>`。
强制转换为 `RDF_RadarSample` 即可访问雷达物理字段：

```c
foreach (RDF_LidarSample base : samples)
{
    if (!base.m_Hit)
        continue;  // 未命中射线，跳过

    RDF_RadarSample s = RDF_RadarSample.Cast(base);
    if (!s)
        continue;

    Print(string.Format("[%1] dist=%2m  SNR=%3dB  RCS=%4m^2  vel=%5m/s",
        s.m_Index,
        s.m_Distance,
        s.m_SignalToNoiseRatio,
        s.m_RadarCrossSection,
        s.m_TargetVelocity));

    // 判断是否被探测到（高于 SNR 门限）
    if (s.IsDetectable(10.0))
        Print("  --> TARGET DETECTED");
}
```

**关键字段速查**：

| 字段 | 含义 | 典型单位 |
|------|------|---------|
| `m_Distance` | 目标斜距 | m |
| `m_SignalToNoiseRatio` | 信噪比 | dB（初始 -999 = 无效） |
| `m_RadarCrossSection` | RCS | m² |
| `m_TargetVelocity` | 径向速度 | m/s（正 = 接近） |
| `m_DopplerFrequency` | 多普勒频移 | Hz |
| `m_TimeOfFlight` | 双程飞行时间 | s |
| `m_PathLoss` | 自由空间损耗 | dB |
| `m_MaterialType` | 命中材质名 | 字符串 |

---

## 3. 配置雷达参数

当预设不满足需求时，可以手动构建配置：

```c
// 创建电磁波参数
RDF_EMWaveParameters em = new RDF_EMWaveParameters();
em.m_CarrierFrequency     = 9.4e9;    // 9.4 GHz，X 波段
em.m_TransmitPower        = 2500.0;   // 2.5 kW
em.m_AntennaGain          = 32.0;     // 32 dBi
em.m_PulseWidth           = 0.5e-6;   // 0.5 us
em.m_PRF                  = 1500.0;   // 1500 Hz
em.m_NoiseFigure          = 4.0;      // 4 dB
em.CalculateWavelength();             // 自动填充 m_Wavelength
em.DetermineBand();                   // 自动填充 m_BandName

// 创建雷达配置
RDF_RadarSettings settings = new RDF_RadarSettings();
settings.m_EMWaveParams            = em;
settings.m_RadarMode               = ERadarMode.PULSE;
settings.m_DetectionThreshold      = 8.0;   // 8 dB SNR 门限
settings.m_EnableDopplerProcessing = true;
settings.m_EnableMTI               = true;   // 过滤静止目标
settings.m_EnableAtmosphericModel  = true;
settings.m_Temperature             = 15.0;
settings.m_Humidity                = 70.0;
settings.m_RainRate                = 5.0;    // 5 mm/h（毛毛雨）

// LiDAR 基础配置（扫描范围与射线数）
settings.m_Rays         = 1024;
settings.m_MaxRange     = 3000.0;   // 3 km
settings.m_HFov         = 60.0;
settings.m_VFov         = 20.0;

// 创建扫描器
RDF_RadarScanner scanner = new RDF_RadarScanner(settings);
```

### 3.1 参数验证

调用 `em.Validate()` 会自动修正超界值并返回原始是否合法：

```c
bool wasValid = em.Validate();
if (!wasValid)
    Print("[Radar] EMWave parameters were out of range, auto-corrected.");
```

---

## 4. 选择工作模式

### 4.1 脉冲雷达（PULSE）— 默认

适合距离测量场景。内部自动按 `c * pulseWidth / 2` 量化距离分辨率，
超出最大无模糊距离（`c / (2 * PRF)`）的回波被标记为未命中。

```c
settings.m_RadarMode = ERadarMode.PULSE;
```

### 4.2 连续波（CW）

只能测速，无法测距（`m_Distance` 被置零）。静止目标被过滤。

```c
settings.m_RadarMode = ERadarMode.CW;
```

### 4.3 调频连续波（FMCW）

高距离分辨率，常用于车载防撞雷达。
扫频带宽默认 500 MHz（距离分辨率 ≈ 0.3 m）。

```c
settings.m_RadarMode = ERadarMode.FMCW;
// 如需自定义带宽，可在构造扫描器后替换模式处理器：
RDF_RadarScanner scanner = new RDF_RadarScanner(settings);
scanner.SetRadarMode(ERadarMode.FMCW);  // 重新实例化，使用默认带宽
```

### 4.4 相控阵（PHASED_ARRAY）

多波束扫描，SNR 会按 `10 * log10(beamCount)` 有所惩罚。
获取处理器对象后可直接修改参数：

```c
settings.m_RadarMode = ERadarMode.PHASED_ARRAY;
RDF_RadarScanner scanner = new RDF_RadarScanner(settings);

RDF_PhasedArrayMode paMode = RDF_PhasedArrayMode.Cast(scanner.GetRadarModeProcessor());
if (paMode)
    paMode.m_BeamCount = 16;  // 设置 16 个波束
```

---

## 5. 使用可视化显示

### 5.1 PPI 平面位置显示

```c
// 每帧调用（通常在 EOnFrame 回调中）
RDF_PPIDisplay ppi = new RDF_PPIDisplay();
ppi.m_DisplayRadius = 500.0;       // 显示半径 500 m
ppi.m_HeightOffset  = 3.0;         // 高于地面 3 m 绘制
ppi.DrawPPI(samples, radarEntity); // 绘制 PPI
```

### 5.2 A-Scope 距离-幅度显示

```c
RDF_AScopeDisplay ascope = new RDF_AScopeDisplay();
ascope.m_Origin          = radarEntity.GetOrigin() + "0 5 0";  // 高于雷达 5 m
ascope.m_MaxBarHeight    = 20.0;
ascope.m_HorizontalScale = 0.2;
ascope.DrawAScope(samples, 3000.0, 10.0);  // 最大距离 3 km，10 dB 门限
```

### 5.3 片上 HUD — CanvasWidget PPI 扫描图

`RDF_RadarHUD` 是框架提供的一体化 HUD 控制器，无需任何 `.layout` 文件，全部通过脚本动态创建。

**自动启动**：游戏启动时通过 Bootstrap 自动显示，也可手动调用：

```c
// 显示 HUD（若尚未创建则自动创建控件）
RDF_RadarHUD.Show();

// 将 HUD 注册为扫描完成回调（每次扫描后自动更新）
RDF_RadarAutoRunner.SetScanCompleteHandler(RDF_RadarHUD.GetInstance());
```

**光点颜色含义**：

| 类型 | 条件 | 颜色 |
|------|------|------|
| 大实体 | RCS > 10 dBsm | 亮绿 |
| 小实体 | RCS ≤ 10 dBsm | 青色 |
| 强地物 | SNR > 20 dB | 黄色 |
| 弱地物 | SNR ≤ 20 dB | 暗橙 |
| 玩家位置 | 中心 | 白色圆点 |

**坐标系**：北（+Z）在圆图上方，东（+X）在右侧，与地图方向一致。

### 5.4 颜色策略

将颜色策略设置到扫描器的基类 LiDAR 可视化器中，或在自定义渲染循环里调用：

```c
// SNR 着色（低 SNR = 红，高 SNR = 青）
RDF_SNRColorStrategy snrColor = new RDF_SNRColorStrategy();

// 多普勒着色（接近 = 红，远离 = 蓝）
RDF_DopplerColorStrategy dopColor = new RDF_DopplerColorStrategy(100.0); // 最大速度 100 m/s

// 复合策略（多普勒色调 + SNR 亮度）
RDF_RadarCompositeColorStrategy compColor = new RDF_RadarCompositeColorStrategy();

// 在样本循环中获取颜色
foreach (RDF_LidarSample base : samples)
{
    RDF_RadarSample s = RDF_RadarSample.Cast(base);
    if (s && s.m_Hit)
    {
        int argb = compColor.BuildPointColorFromRadarSample(s, null);
        // 使用 argb 渲染点...
    }
}
```

---

## 6. 采样策略

雷达扫描器完全兼容所有 LiDAR 采样策略，通过 `SetSampleStrategy()` 设置：

| 策略类 | 典型雷达用途 |
|--------|------------|
| `RDF_UniformSampleStrategy` | 均匀分布，通用 |
| `RDF_SweepSampleStrategy` | 扇形扫描（最常用于 PPI 雷达） |
| `RDF_ConicalSampleStrategy` | 锥形波束（车载毫米波） |
| `RDF_HemisphereSampleStrategy` | 半球形（气象雷达） |
| `RDF_StratifiedSampleStrategy` | 分层采样（降低聚集误差） |

```c
RDF_RadarScanner scanner = new RDF_RadarScanner(settings);

// 换用扇形扫描，水平 ±30°，俯仰 ±10°，3 轮扫描
RDF_SweepSampleStrategy sweep = new RDF_SweepSampleStrategy();
sweep.m_HorizontalFov = 60.0;
sweep.m_VerticalFov   = 20.0;
sweep.m_Passes        = 3;
scanner.SetSampleStrategy(sweep);
```

---

## 7. 物理模型详解

### 7.1 手动计算路径损耗

```c
float freq    = 10e9;    // 10 GHz
float dist    = 5000.0;  // 5 km
float T       = 20.0;    // 20 °C
float RH      = 60.0;    // 60 % 湿度
float rain    = 10.0;    // 10 mm/h 降雨

float loss = RDF_RadarPropagation.CalculateTotalLoss(dist, freq, T, RH, rain, true);
Print(string.Format("Total loss: %1 dB", loss));
```

### 7.2 手动估算 RCS

```c
float wavelength = 0.03;  // 10 GHz

// 球形目标
float sphereRCS = RDF_RCSModel.CalculateSphereRCS(0.5, wavelength);  // 半径 0.5 m

// 金属平板
float plateRCS  = RDF_RCSModel.CalculatePlateRCS(2.0, wavelength);   // 面积 2 m²

Print(string.Format("Sphere: %1 m^2  |  Plate: %2 m^2", sphereRCS, plateRCS));
```

### 7.3 最大探测距离计算

```c
float Pt  = 1000.0;    // 1 kW
float Gt  = 316.2;     // 25 dBi (线性)
float lam = 0.03;      // 波长 0.03 m
float rcs = 1.0;       // 1 m^2 最小目标 RCS
float Lsys = 6.0;      // 6 dB 系统损耗

float NF_lin = RDF_RadarEquation.DBiToLinear(5.0);          // 5 dB 噪声系数
float Pn     = RDF_RadarEquation.CalculateNoisePower(1e6, 5.0, 293.15); // 1 MHz 带宽
float Pr_min = Pn * 10.0;                                   // SNR = 10 dB 门限
float maxR   = RDF_RadarEquation.CalculateMaxDetectionRange(Pt, Gt, lam, rcs, Pr_min, Lsys);

Print(string.Format("Max detection range: %1 km", maxR / 1000.0));
```

### 7.4 多普勒分析

```c
float freq   = 10e9;
float vel    = -30.0;   // 30 m/s 接近（负 = 接近）
float fd     = RDF_DopplerProcessor.CalculateDopplerShift(-vel, freq);
Print(string.Format("Doppler shift: %1 Hz", fd));

float vMax = RDF_DopplerProcessor.MaxUnambiguousVelocity(1000.0, freq); // PRF 1000 Hz
Print(string.Format("Max unambiguous velocity: %1 m/s", vMax));
```

---

## 8. 目标分类与统计

```c
// 分类所有样本
array<ERadarTargetClass> classes = new array<ERadarTargetClass>();
RDF_TargetClassifier.ClassifyAll(samples, classes);

// 遍历结果
for (int i = 0; i < samples.Count(); i++)
{
    RDF_LidarSample base = samples[i];
    if (!base.m_Hit) continue;

    ERadarTargetClass cls = classes[i];
    Print(string.Format("Sample %1 -> %2", i, RDF_TargetClassifier.GetClassName(cls)));
}

// 打印统计摘要到日志
RDF_TargetClassifier.PrintStats(samples);
```

日志示例：
```
[Classifier] Target breakdown:
  INFANTRY        : 5
  VEHICLE_LIGHT   : 2
  VEHICLE_HEAVY   : 1
  STATIC_OBJECT   : 18
```

---

## 9. 电子对抗

### 9.1 模拟噪声干扰

```c
// J/S 比计算
float Pj   = 100.0;   // 100 W 干扰发射功率
float Gj   = 100.0;   // 干扰天线增益（线性）
float Gr   = 316.2;   // 雷达接收天线增益（线性）
float lam  = 0.03;
float Rj   = 2000.0;  // 干扰机距离
float Br   = 1e6;     // 雷达带宽

float js = RDF_JammingModel.CalculateNoiseJammingJS(Pj, Gj, Gr, lam, Rj, Br);
Print(string.Format("J/S ratio: %1 dB", js));

// 将干扰效果施加到扫描结果（会降低所有样本的 SNR）
RDF_JammingModel.ApplyNoiseJamming(samples, js, settings.m_DetectionThreshold);
```

### 9.2 注入假目标

```c
vector jammerPos = "500 0 500";
RDF_JammingModel.InjectFalseTargets(
    samples,     // 目标样本数组
    5,           // 注入 5 个假目标
    jammerPos[0], jammerPos[1], jammerPos[2],
    50.0,        // 最小距离 50 m
    1000.0,      // 最大距离 1000 m
    25.0         // 假目标 SNR (dB)
);
```

### 9.3 箔条云 RCS

```c
float chaffRCS = RDF_JammingModel.CalculateChaffRCS(1000, 0.03); // 1000 根偶极子
Print(string.Format("Chaff cloud RCS: %1 m^2", chaffRCS));
```

---

## 10. 数据导出

### 10.1 导出为 CSV

```c
// 导出全部射线（含未命中）
string csvAll  = RDF_RadarExport.ScanToCSV(samples);

// 仅导出命中射线
string csvHits = RDF_RadarExport.HitsToCSV(samples);

// 写到 Arma Reforger 可访问的路径（需要 FileIO 支持）
// 目前框架将 CSV 字符串打印到日志供调试使用
Print(csvHits);
```

CSV 列头：
```
Index, Hit, Distance_m, HitPos_X, HitPos_Y, HitPos_Z,
Dir_X, Dir_Y, Dir_Z, TransmitPower_W, ReceivedPower_W, SNR_dB,
RCS_m2, RCS_dBsm, PathLoss_dB, AtmLoss_dB, RainAtten_dB,
DopplerFreq_Hz, TargetVel_ms, IncidenceAngle_deg, ReflectionCoeff,
TimeOfFlight_s, MaterialType, EntityClass
```

---

## 11. 高级：SAR 处理

SAR 处理器需要多次扫描来积累"合成孔径"数据。典型流程：

```c
RDF_SARProcessor sar = new RDF_SARProcessor();
sar.m_MaxSnapshots = 16;  // 最多保留 16 次扫描

// --- 在飞行器飞行过程中，每个 tick 调用 ---
void OnFrameUpdate(IEntity platform)
{
    array<ref RDF_LidarSample> frameSamples = new array<ref RDF_LidarSample>();
    scanner.Scan(platform, frameSamples);

    sar.AddScan(frameSamples, platform.GetOrigin());

    // 当积累足够多的快照后生成图像
    if (sar.GetSnapshotCount() >= 8)
    {
        array<vector> positions   = new array<vector>();
        array<float>  intensities = new array<float>();

        sar.GenerateSARImage(
            500.0,             // 成像区域边长 500 m
            32,                // 32 x 32 分辨率格点
            platform.GetOrigin(), // 成像中心
            positions,
            intensities
        );

        // 可视化（需要每帧刷新以防止形状被 GC 回收）
        sar.VisualiseImage(positions, intensities, 1.0, 3.0);
    }
}
```

---

## 12. 单元测试与基准

在游戏日志中运行所有物理验证测试：

```c
// 在任意 GameMode 的 OnGameStart 或 Editor 按钮触发器中调用
RDF_RadarTests.RunAllTests();
```

测试覆盖：
- EM 波参数校验与波段识别
- FSPL / 大气 / 雨衰计算结果范围
- 球体 / 平板 / 圆柱 RCS 数值验证
- 雷达方程线性与 dB 形式一致性
- SNR 计算（含积分增益）
- 多普勒频移与速度反推
- 最大探测距离理论值验证

运行性能基准（测试不同射线数下的扫描耗时）：

```c
RDF_RadarBenchmark.RunAllBenchmarks();
```

---

## 13. 扩展框架

### 13.1 自定义工作模式

继承 `RDF_RadarMode` 并覆盖 `ProcessSample`：

```c
class MyCustomMode : RDF_RadarMode
{
    float m_SomeModeParam = 1.0;

    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        // 在此修改 sample 字段，例如附加测量噪声
        float noise = (Math.RandomFloat01() - 0.5) * 2.0 * m_SomeModeParam;
        sample.m_SignalToNoiseRatio = sample.m_SignalToNoiseRatio + noise;
    }
}
```

然后在扫描器中直接替换模式处理器字段（`m_RadarModeProcessor` 为 `protected`，建议继承 `RDF_RadarScanner`）：

```c
class MyRadarScanner : RDF_RadarScanner
{
    void MyRadarScanner(RDF_LidarSettings settings = null)
    {
        super.RDF_RadarScanner(settings);
        m_RadarModeProcessor = new MyCustomMode();
    }
}
```

### 13.2 自定义颜色策略

继承 `RDF_RadarColorStrategy` 并覆盖 `BuildPointColorFromRadarSample`：

```c
class MyColorStrategy : RDF_RadarColorStrategy
{
    override int BuildPointColorFromRadarSample(RDF_RadarSample sample,
                                                RDF_RadarSettings settings)
    {
        // 按目标速度着色（仅示例）
        float v = Math.AbsFloat(sample.m_TargetVelocity) / 100.0;
        if (v > 1.0) v = 1.0;
        return ARGBF(1.0, v, 1.0 - v, 0.0);
    }
}
```

### 13.3 自定义物理修正

在继承的 `RDF_RadarScanner` 中覆盖 `ApplyRadarPhysics` 并在调用 `super.ApplyRadarPhysics` 前后追加逻辑：

```c
class MyRadarScanner : RDF_RadarScanner
{
    override protected void ApplyRadarPhysics(IEntity radarEntity, RDF_RadarSample sample)
    {
        // 前处理
        super.ApplyRadarPhysics(radarEntity, sample);
        // 后处理：额外 3 dB 系统损耗
        sample.m_SignalToNoiseRatio = sample.m_SignalToNoiseRatio - 3.0;
    }
}
```

---

## 14. 常见问题

**Q：所有样本的 SNR 都是 -999，为什么？**  
A：`-999` 是初始占位值，表示该射线未命中任何目标（`m_Hit == false`）。
先检查 `m_Hit` 再访问 SNR 字段。

**Q：MTI 开启后所有目标都被过滤了？**  
A：MTI 会过滤径向速度低于 `m_MinTargetVelocity`（默认 1.0 m/s）的目标。
若场景中目标静止或移动缓慢，请降低该阈值或禁用 MTI。

**Q：FMCW 模式下距离分辨率很低？**  
A：距离分辨率 = `c / (2 * B)`，B 为扫频带宽（默认 500 MHz，分辨率约 0.3 m）。
如需更低分辨率，将 `RDF_FMCWRadarMode` 对象的 `m_SweepBandwidth` 改大。

**Q：相控阵模式 SNR 很低，目标探测不到？**  
A：相控阵模式按 `10 * log10(beamCount)` 惩罚 SNR（默认 beamCount=4，惩罚约 6 dB）。
同时应提高发射功率或天线增益以补偿。

**Q：PPI 图上看不到任何线条？**  
A：PPI 调用 `Shape.CreateLines` 创建调试形状，这些形状每帧会被 GC 回收。
需要每帧（在 `EOnFrame` 回调中）重新调用 `DrawPPI`。

**Q：如何在 Workbench Script Editor 之外使用这个框架？**  
A：框架是纯 EnScript 实现，无外部依赖。将整个 `scripts/Game/RDF/` 目录放入
目标 mod 的 `scripts/Game/` 下即可。

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-02-18 | 初始雷达系统实现，含全量物理模型、ECM、SAR、分类器 |
