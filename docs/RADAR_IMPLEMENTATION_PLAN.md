# 电磁波雷达系统实现计划
# Electromagnetic Radar System Implementation Plan

**项目**: Radar Development Framework
**文档版本**: 1.0
**创建日期**: 2026-02-18
**作者**: AI Assistant
**状态**: 规划中 (Planning)

---

## 📋 执行摘要 (Executive Summary)

本计划旨在将现有的 LiDAR 射线追踪框架扩展为完整的**电磁波雷达仿真系统**，引入真实的雷达物理特性，包括电磁波传播、信号衰减、多普勒效应、目标反射特性等。该系统将保持与现有 LiDAR 架构的兼容性，并提供独立的雷达模块供用户选择使用。

**核心目标**：
- 构建物理准确的电磁波传播模型
- 实现真实的雷达回波仿真
- 支持多种雷达工作模式（脉冲、连续波、相控阵等）
- 提供雷达信号处理基础能力
- 保持现有 LiDAR 系统完整性

**预期成果**：
- 可用于游戏内雷达系统开发
- 支持训练 AI 识别与分类目标
- 为雷达对抗系统提供基础

---

## 🎯 第一阶段：基础架构与数据模型 (Phase 1: Foundation & Data Model)

**目标**: 建立雷达系统的核心数据结构和配置体系

### 1.1 电磁波参数系统 (EM Wave Parameters)

#### 实现文件
- `scripts/Game/RDF/Radar/Core/RDF_EMWaveParameters.c`

#### 核心内容
```c
class RDF_EMWaveParameters
{
    // 基础物理参数
    float m_CarrierFrequency;        // 载波频率 (Hz)
    float m_Wavelength;              // 波长 (m) - 自动计算或手动设置
    string m_BandName;               // 波段名称 (L/S/C/X/Ku/Ka/W)
    
    // 发射参数
    float m_TransmitPower;           // 发射功率 (W)
    float m_PulseWidth;              // 脉冲宽度 (s) - 用于脉冲雷达
    float m_PRF;                     // 脉冲重复频率 (Hz)
    
    // 天线参数
    float m_AntennaGain;             // 天线增益 (dBi)
    float m_BeamwidthAzimuth;        // 水平波束宽度 (度)
    float m_BeamwidthElevation;      // 垂直波束宽度 (度)
    
    // 接收参数
    float m_ReceiverSensitivity;     // 接收机灵敏度 (dBm)
    float m_NoiseFigure;             // 噪声系数 (dB)
    
    // 自动计算波长
    void CalculateWavelength();
    // 根据频率确定波段
    void DetermineBand();
    // 验证参数合法性
    bool Validate();
}
```

**预设波段配置**：
| 波段 | 频率范围 | 典型应用 |
|------|----------|----------|
| L    | 1-2 GHz  | 远程监视雷达 |
| S    | 2-4 GHz  | 中程搜索雷达 |
| C    | 4-8 GHz  | 气象雷达、卫星通信 |
| X    | 8-12 GHz | 火控雷达、导弹制导 |
| Ku   | 12-18 GHz| 高分辨率成像 |
| Ka   | 27-40 GHz| 汽车雷达、短程高精度 |

### 1.2 雷达采样数据结构 (Radar Sample Structure)

#### 实现文件
- `scripts/Game/RDF/Radar/Core/RDF_RadarSample.c`

#### 核心内容
```c
class RDF_RadarSample : RDF_LidarSample
{
    // 继承自 LiDAR 的几何信息：
    // m_Index, m_Hit, m_Start, m_End, m_Dir, m_HitPos, m_Distance, m_Entity
    
    // === 电磁波特性 ===
    float m_TransmitPower;           // 发射功率 (W)
    float m_ReceivedPower;           // 接收功率 (W)
    float m_SignalToNoiseRatio;      // 信噪比 (dB)
    float m_RadarCrossSection;       // 目标 RCS (m²)
    
    // === 传播与衰减 ===
    float m_PathLoss;                // 路径损耗 (dB)
    float m_AtmosphericLoss;         // 大气衰减 (dB)
    float m_RainAttenuation;         // 雨衰 (dB)
    
    // === 相位与多普勒 ===
    float m_PhaseShift;              // 相位偏移 (rad)
    float m_DopplerFrequency;        // 多普勒频移 (Hz)
    float m_TargetVelocity;          // 目标径向速度 (m/s)
    
    // === 反射特性 ===
    float m_IncidenceAngle;          // 入射角 (度)
    float m_ReflectionCoefficient;   // 反射系数 (0-1)
    string m_MaterialType;           // 材质类型（金属/非金属/复合）
    
    // === 极化信息 ===
    string m_PolarizationType;       // 极化类型（水平/垂直/圆）
    float m_PolarizationLoss;        // 极化损耗 (dB)
    
    // === 时间信息 ===
    float m_TimeOfFlight;            // 往返时间 (s)
    float m_DelayTime;               // 相对延迟 (s)
    
    // 计算接收功率（基于雷达方程）
    void CalculateReceivedPower(RDF_EMWaveParameters emParams);
    // 判断是否超过检测门限
    bool IsDetectable(float threshold);
}
```

### 1.3 雷达配置系统 (Radar Settings)

#### 实现文件
- `scripts/Game/RDF/Radar/Core/RDF_RadarSettings.c`

#### 核心内容
```c
class RDF_RadarSettings : RDF_LidarSettings
{
    // 继承 LiDAR 的几何配置：
    // m_Range, m_RayCount, m_UpdateInterval 等
    
    // === 电磁波配置 ===
    ref RDF_EMWaveParameters m_EMWaveParams;
    
    // === 雷达工作模式 ===
    int m_RadarMode;                 // 工作模式枚举
    // PULSE = 0 (脉冲雷达)
    // CW = 1 (连续波)
    // FMCW = 2 (调频连续波)
    // PHASED_ARRAY = 3 (相控阵)
    
    // === 信号处理 ===
    float m_DetectionThreshold;      // 检测门限 (dB)
    bool m_EnableDopplerProcessing;  // 启用多普勒处理
    bool m_EnableMTI;                // 动目标显示
    bool m_EnableMTD;                // 动目标检测
    
    // === 环境模型 ===
    bool m_EnableAtmosphericModel;   // 大气衰减模型
    float m_Temperature;             // 环境温度 (℃)
    float m_Humidity;                // 相对湿度 (%)
    float m_RainRate;                // 降雨率 (mm/h)
    
    // === 目标特性 ===
    bool m_UseRCSModel;              // 使用 RCS 模型
    bool m_UseMaterialReflection;    // 材质反射特性
    
    // === 杂波抑制 ===
    bool m_EnableClutterFilter;      // 杂波滤波
    float m_MinTargetVelocity;       // 最小可检测速度 (m/s)
    
    void Validate();
}
```

---

## 🎯 第二阶段：物理模型实现 (Phase 2: Physics Models)

**目标**: 实现雷达物理特性的核心计算模块

### 2.1 电磁波传播模型 (EM Propagation Model)

#### 实现文件
- `scripts/Game/RDF/Radar/Physics/RDF_RadarPropagation.c`

#### 核心功能

**2.1.1 自由空间路径损耗 (Free Space Path Loss - FSPL)**
```c
class RDF_RadarPropagation
{
    // Friis 传输公式
    // FSPL(dB) = 20×log₁₀(d) + 20×log₁₀(f) + 20×log₁₀(4π/c)
    static float CalculateFSPL(float distance, float frequency)
    {
        // distance: 米 (m)
        // frequency: 赫兹 (Hz)
        const float SPEED_OF_LIGHT = 299792458.0; // m/s
        
        if (distance <= 0 || frequency <= 0)
            return 0;
            
        float distanceKm = distance / 1000.0;
        float frequencyGHz = frequency / 1e9;
        
        // 简化公式（dB）
        float fspl = 92.45 + (20.0 * Math.Log10(distanceKm)) + (20.0 * Math.Log10(frequencyGHz));
        return fspl;
    }
}
```

**2.1.2 大气衰减模型 (Atmospheric Attenuation)**
```c
// ITU-R P.676 大气气体衰减模型（简化版）
static float CalculateAtmosphericAttenuation(
    float distance,         // 距离 (m)
    float frequency,        // 频率 (Hz)
    float temperature,      // 温度 (℃)
    float humidity,         // 湿度 (%)
    float pressure = 1013.25 // 气压 (hPa)
)
{
    // 简化模型：10 GHz 以下可忽略，以上随频率指数增长
    float frequencyGHz = frequency / 1e9;
    if (frequencyGHz < 10.0)
        return 0.0;
    
    // 氧气吸收 + 水蒸气吸收（简化）
    float oxygenAbs = 0.01 * frequencyGHz * frequencyGHz;
    float waterAbs = humidity / 100.0 * 0.05 * frequencyGHz;
    
    float totalAbs = (oxygenAbs + waterAbs) * (distance / 1000.0); // dB/km
    return totalAbs;
}
```

**2.1.3 雨衰模型 (Rain Attenuation)**
```c
// ITU-R P.838 降雨衰减模型
static float CalculateRainAttenuation(
    float distance,      // 距离 (m)
    float frequency,     // 频率 (Hz)
    float rainRate       // 降雨率 (mm/h)
)
{
    if (rainRate <= 0)
        return 0;
    
    float frequencyGHz = frequency / 1e9;
    
    // k 和 α 系数（水平极化，简化）
    float k = 0.0001 * Math.Pow(frequencyGHz, 2.3);
    float alpha = 1.0 + 0.03 * Math.Log10(frequencyGHz);
    
    // 比衰减 γ = k × R^α (dB/km)
    float specificAtten = k * Math.Pow(rainRate, alpha);
    
    return specificAtten * (distance / 1000.0);
}
```

### 2.2 雷达截面积 (RCS) 模型

#### 实现文件
- `scripts/Game/RDF/Radar/Physics/RDF_RCSModel.c`

#### 核心功能

**2.2.1 几何形状 RCS**
```c
class RDF_RCSModel
{
    // 球体 RCS (最简单的解析解)
    static float CalculateSphereRCS(float radius, float wavelength)
    {
        float circumference = 2.0 * Math.PI * radius;
        // 光学区（radius >> wavelength）
        if (circumference > 10.0 * wavelength)
            return Math.PI * radius * radius; // σ = πr²
        
        // 瑞利区（radius << wavelength）
        if (circumference < 0.1 * wavelength)
        {
            float k = 2.0 * Math.PI / wavelength;
            float vol = (4.0 / 3.0) * Math.PI * radius * radius * radius;
            return 9.0 * Math.PI * vol * vol * k * k * k * k;
        }
        
        // 过渡区（简化线性插值）
        return Math.PI * radius * radius * 0.5;
    }
    
    // 平板 RCS（正向入射）
    static float CalculatePlateRCS(float area, float wavelength)
    {
        // σ = 4π × A² / λ²
        return (4.0 * Math.PI * area * area) / (wavelength * wavelength);
    }
    
    // 圆柱体 RCS（侧向）
    static float CalculateCylinderRCS(float length, float radius, float wavelength)
    {
        // 简化模型：光学区
        return 2.0 * Math.PI * radius * length * length / wavelength;
    }
}
```

**2.2.2 材质修正因子**
```c
class RDF_MaterialRCS
{
    // 材质反射率数据库
    static float GetMaterialReflectivity(string materialName)
    {
        // 金属：高反射率
        if (materialName.Contains("metal") || materialName.Contains("steel"))
            return 1.0;
        
        // 混凝土：中等
        if (materialName.Contains("concrete"))
            return 0.3;
        
        // 木材：低
        if (materialName.Contains("wood"))
            return 0.1;
        
        // 玻璃：取决于频率和涂层
        if (materialName.Contains("glass"))
            return 0.15;
        
        // 植被：非常低
        if (materialName.Contains("vegetation") || materialName.Contains("grass"))
            return 0.05;
        
        // 默认中等
        return 0.5;
    }
    
    // 基于入射角的反射修正
    static float GetAngleDependentReflection(float incidenceAngle, bool isMetal)
    {
        float cosTheta = Math.Cos(incidenceAngle * Math.DEG2RAD);
        
        // 金属：菲涅尔反射
        if (isMetal)
            return 1.0;
        
        // 非金属：斜入射反射率下降
        return Math.Pow(cosTheta, 2.0);
    }
}
```

**2.2.3 实体 RCS 估算**
```c
// 从游戏实体估算 RCS
static float EstimateEntityRCS(
    IEntity entity,
    vector radarPos,
    vector rayDir,
    float wavelength
)
{
    if (!entity)
        return 0.0;
    
    // 获取实体包围盒
    vector mins, maxs;
    entity.GetBounds(mins, maxs);
    vector size = maxs - mins;
    
    // 估算等效球体半径
    float volume = size[0] * size[1] * size[2];
    float equivRadius = Math.Pow(volume / ((4.0/3.0) * Math.PI), 1.0/3.0);
    
    // 计算基础 RCS
    float baseRCS = CalculateSphereRCS(equivRadius, wavelength);
    
    // 材质修正
    GameMaterial mat = entity.GetMaterial();
    float reflectivity = 1.0;
    if (mat)
        reflectivity = GetMaterialReflectivity(mat.GetName());
    
    // 角度修正
    vector entityPos = entity.GetOrigin();
    vector toEntity = entityPos - radarPos;
    float incidenceAngle = Math.Acos(vector.Dot(rayDir, toEntity.Normalized())) * Math.RAD2DEG;
    float angleModifier = GetAngleDependentReflection(incidenceAngle, reflectivity > 0.8);
    
    return baseRCS * reflectivity * angleModifier;
}
```

### 2.3 雷达方程实现 (Radar Equation)

#### 实现文件
- `scripts/Game/RDF/Radar/Physics/RDF_RadarEquation.c`

#### 核心内容

**2.3.1 基础雷达方程**
```c
class RDF_RadarEquation
{
    // 单基地雷达方程
    // Pr = (Pt × Gt × Gr × λ² × σ) / ((4π)³ × R⁴ × L)
    static float CalculateReceivedPower(
        float transmitPower,     // Pt: 发射功率 (W)
        float antennaGain,       // Gt = Gr: 天线增益（线性）
        float wavelength,        // λ: 波长 (m)
        float rcs,               // σ: 目标 RCS (m²)
        float range,             // R: 距离 (m)
        float systemLoss         // L: 系统损耗（线性）
    )
    {
        if (range <= 0 || rcs <= 0)
            return 0;
        
        float numerator = transmitPower * antennaGain * antennaGain * wavelength * wavelength * rcs;
        float denominator = Math.Pow(4.0 * Math.PI, 3.0) * Math.Pow(range, 4.0) * systemLoss;
        
        return numerator / denominator;
    }
    
    // dB 形式（更直观）
    static float CalculateReceivedPowerDB(
        float transmitPowerDBm,  // 发射功率 (dBm)
        float antennaGainDB,     // 天线增益 (dBi)
        float wavelengthM,       // 波长 (m)
        float rcsDBsm,           // RCS (dBsm)
        float rangeM,            // 距离 (m)
        float systemLossDB       // 系统损耗 (dB)
    )
    {
        // Pr(dBm) = Pt(dBm) + 2×Gt(dBi) + 20×log₁₀(λ) + σ(dBsm) - 30×log₁₀(4π) - 40×log₁₀(R) - L(dB)
        
        float receivedPower = transmitPowerDBm;
        receivedPower += 2.0 * antennaGainDB;
        receivedPower += 20.0 * Math.Log10(wavelengthM);
        receivedPower += rcsDBsm;
        receivedPower -= 30.0 * Math.Log10(4.0 * Math.PI);
        receivedPower -= 40.0 * Math.Log10(rangeM);
        receivedPower -= systemLossDB;
        
        return receivedPower;
    }
    
    // 计算最大探测距离
    static float CalculateMaxDetectionRange(
        float transmitPower,
        float antennaGain,
        float wavelength,
        float minRCS,
        float receiverSensitivity, // 最小可检测功率 (W)
        float systemLoss = 1.0
    )
    {
        // R_max = ((Pt × Gt² × λ² × σ) / ((4π)³ × Pr_min × L))^(1/4)
        
        float numerator = transmitPower * antennaGain * antennaGain * wavelength * wavelength * minRCS;
        float denominator = Math.Pow(4.0 * Math.PI, 3.0) * receiverSensitivity * systemLoss;
        
        return Math.Pow(numerator / denominator, 0.25);
    }
}
```

**2.3.2 信噪比计算**
```c
// 计算信噪比（SNR）
static float CalculateSNR(
    float receivedPower,      // 接收信号功率 (W)
    float noisePower,         // 噪声功率 (W)
    float integrationGain = 1.0 // 积分增益
)
{
    if (noisePower <= 0)
        return 1000.0; // 无噪声情况
    
    float snr = (receivedPower * integrationGain) / noisePower;
    return 10.0 * Math.Log10(snr); // 转换为 dB
}

// 计算噪声功率
static float CalculateNoisePower(
    float bandwidth,        // 带宽 (Hz)
    float noiseFigure,      // 噪声系数 (dB)
    float temperature = 290.0 // 噪声温度 (K)
)
{
    const float BOLTZMANN_CONSTANT = 1.38e-23; // J/K
    
    // 热噪声功率 N₀ = k × T × B
    float thermalNoise = BOLTZMANN_CONSTANT * temperature * bandwidth;
    
    // 考虑噪声系数
    float noiseFigureLinear = Math.Pow(10.0, noiseFigure / 10.0);
    
    return thermalNoise * noiseFigureLinear;
}
```

### 2.4 多普勒效应 (Doppler Effect)

#### 实现文件
- `scripts/Game/RDF/Radar/Physics/RDF_DopplerProcessor.c`

#### 核心内容

```c
class RDF_DopplerProcessor
{
    // 计算多普勒频移
    // fd = (2 × vr × f₀) / c
    static float CalculateDopplerShift(
        float radialVelocity,    // 径向速度 (m/s) - 正值接近，负值远离
        float carrierFrequency   // 载波频率 (Hz)
    )
    {
        const float SPEED_OF_LIGHT = 299792458.0;
        
        // 双程多普勒（发射+接收）
        return (2.0 * radialVelocity * carrierFrequency) / SPEED_OF_LIGHT;
    }
    
    // 从实体速度计算径向速度
    static float CalculateRadialVelocity(
        IEntity radarEntity,
        IEntity targetEntity
    )
    {
        if (!radarEntity || !targetEntity)
            return 0;
        
        // 获取雷达和目标位置
        vector radarPos = radarEntity.GetOrigin();
        vector targetPos = targetEntity.GetOrigin();
        
        // 获取目标速度（如果实体支持物理）
        Physics targetPhys = targetEntity.GetPhysics();
        vector targetVel = vector.Zero;
        if (targetPhys)
            targetVel = targetPhys.GetVelocity();
        
        // 雷达速度
        Physics radarPhys = radarEntity.GetPhysics();
        vector radarVel = vector.Zero;
        if (radarPhys)
            radarVel = radarPhys.GetVelocity();
        
        // 相对速度
        vector relativeVel = targetVel - radarVel;
        
        // 雷达到目标的方向
        vector direction = (targetPos - radarPos).Normalized();
        
        // 投影到方向向量（径向速度）
        return vector.Dot(relativeVel, direction);
    }
    
    // 多普勒滤波器（简化 MTI）
    static bool IsMovingTarget(
        float dopplerFrequency,
        float minDopplerThreshold = 10.0 // Hz
    )
    {
        return Math.AbsFloat(dopplerFrequency) > minDopplerThreshold;
    }
}
```

---

## 🎯 第三阶段：雷达扫描器实现 (Phase 3: Radar Scanner)

**目标**: 将物理模型集成到扫描流程中

### 3.1 雷达扫描器核心

#### 实现文件
- `scripts/Game/RDF/Radar/Core/RDF_RadarScanner.c`

#### 核心架构

```c
class RDF_RadarScanner : RDF_LidarScanner
{
    protected ref RDF_RadarSettings m_RadarSettings;
    protected ref RDF_RadarPropagation m_PropagationModel;
    protected ref RDF_RCSModel m_RCSModel;
    protected ref RDF_RadarEquation m_RadarEquation;
    protected ref RDF_DopplerProcessor m_DopplerProcessor;
    
    void RDF_RadarScanner(RDF_RadarSettings settings = null)
    {
        // 调用父类构造
        if (settings)
            m_RadarSettings = settings;
        else
            m_RadarSettings = new RDF_RadarSettings();
        
        // 初始化物理模型
        m_PropagationModel = new RDF_RadarPropagation();
        m_RCSModel = new RDF_RCSModel();
        m_RadarEquation = new RDF_RadarEquation();
        m_DopplerProcessor = new RDF_DopplerProcessor();
    }
    
    // 重写扫描方法，添加雷达物理计算
    override void Scan(IEntity subject, array<ref RDF_LidarSample> outSamples)
    {
        // 1. 首先执行基础几何扫描（复用 LiDAR 逻辑）
        super.Scan(subject, outSamples);
        
        // 2. 对每个样本应用雷达物理模型
        foreach (RDF_LidarSample baseSample : outSamples)
        {
            // 转换为雷达样本
            RDF_RadarSample radarSample = RDF_RadarSample.Cast(baseSample);
            if (!radarSample)
                continue;
            
            // 应用电磁波物理
            ApplyRadarPhysics(subject, radarSample);
        }
    }
    
    // 核心方法：应用雷达物理模型
    protected void ApplyRadarPhysics(IEntity radarEntity, RDF_RadarSample sample)
    {
        if (!sample || !m_RadarSettings || !m_RadarSettings.m_EMWaveParams)
            return;
        
        RDF_EMWaveParameters emParams = m_RadarSettings.m_EMWaveParams;
        
        // === 1. 计算传播损耗 ===
        float pathLoss = m_PropagationModel.CalculateFSPL(
            sample.m_Distance,
            emParams.m_CarrierFrequency
        );
        sample.m_PathLoss = pathLoss;
        
        // 大气衰减
        if (m_RadarSettings.m_EnableAtmosphericModel)
        {
            sample.m_AtmosphericLoss = m_PropagationModel.CalculateAtmosphericAttenuation(
                sample.m_Distance,
                emParams.m_CarrierFrequency,
                m_RadarSettings.m_Temperature,
                m_RadarSettings.m_Humidity
            );
            
            sample.m_RainAttenuation = m_PropagationModel.CalculateRainAttenuation(
                sample.m_Distance,
                emParams.m_CarrierFrequency,
                m_RadarSettings.m_RainRate
            );
        }
        
        // === 2. 计算目标 RCS ===
        if (sample.m_Hit && sample.m_Entity && m_RadarSettings.m_UseRCSModel)
        {
            sample.m_RadarCrossSection = m_RCSModel.EstimateEntityRCS(
                sample.m_Entity,
                sample.m_Start,
                sample.m_Dir,
                emParams.m_Wavelength
            );
            
            // 计算入射角
            vector normal = GetSurfaceNormal(sample);
            sample.m_IncidenceAngle = CalculateIncidenceAngle(sample.m_Dir, normal);
        }
        else
        {
            sample.m_RadarCrossSection = 0.0;
        }
        
        // === 3. 计算接收功率（雷达方程）===
        if (sample.m_Hit && sample.m_RadarCrossSection > 0)
        {
            // 线性增益
            float antennaGainLinear = Math.Pow(10.0, emParams.m_AntennaGain / 10.0);
            
            // 总系统损耗
            float totalLossDB = sample.m_PathLoss + sample.m_AtmosphericLoss + sample.m_RainAttenuation;
            float totalLossLinear = Math.Pow(10.0, totalLossDB / 10.0);
            
            sample.m_ReceivedPower = m_RadarEquation.CalculateReceivedPower(
                emParams.m_TransmitPower,
                antennaGainLinear,
                emParams.m_Wavelength,
                sample.m_RadarCrossSection,
                sample.m_Distance,
                totalLossLinear
            );
            
            // 计算信噪比
            float noisePower = m_RadarEquation.CalculateNoisePower(
                1e6,  // 假设 1 MHz 带宽
                emParams.m_NoiseFigure
            );
            sample.m_SignalToNoiseRatio = m_RadarEquation.CalculateSNR(
                sample.m_ReceivedPower,
                noisePower
            );
        }
        
        // === 4. 多普勒处理 ===
        if (m_RadarSettings.m_EnableDopplerProcessing && sample.m_Hit && sample.m_Entity)
        {
            sample.m_TargetVelocity = m_DopplerProcessor.CalculateRadialVelocity(
                radarEntity,
                sample.m_Entity
            );
            
            sample.m_DopplerFrequency = m_DopplerProcessor.CalculateDopplerShift(
                sample.m_TargetVelocity,
                emParams.m_CarrierFrequency
            );
        }
        
        // === 5. 检测判决 ===
        sample.m_Hit = sample.IsDetectable(m_RadarSettings.m_DetectionThreshold);
        
        // 动目标指示滤波
        if (m_RadarSettings.m_EnableMTI && sample.m_Hit)
        {
            bool isMoving = m_DopplerProcessor.IsMovingTarget(sample.m_DopplerFrequency);
            if (!isMoving)
                sample.m_Hit = false; // 滤除静止目标
        }
    }
    
    // 辅助方法
    protected vector GetSurfaceNormal(RDF_RadarSample sample)
    {
        // 简化：使用射线方向的反向作为法向量
        // 实际应用中可从地形或模型获取精确法向量
        return -sample.m_Dir;
    }
    
    protected float CalculateIncidenceAngle(vector rayDir, vector normal)
    {
        float cosAngle = Math.AbsFloat(vector.Dot(rayDir, normal));
        return Math.Acos(cosAngle) * Math.RAD2DEG;
    }
}
```

### 3.2 雷达工作模式

#### 实现文件
- `scripts/Game/RDF/Radar/Modes/RDF_RadarMode.c`

#### 基类定义

```c
// 雷达工作模式基类
class RDF_RadarMode
{
    // 应用模式特定的信号处理
    void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings);
    
    // 获取模式名称
    string GetModeName();
}

// === 脉冲雷达模式 ===
class RDF_PulseRadarMode : RDF_RadarMode
{
    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        // 脉冲雷达特性
        float pulseWidth = settings.m_EMWaveParams.m_PulseWidth;
        float prf = settings.m_EMWaveParams.m_PRF;
        
        // 距离分辨率 = c × τ / 2
        float rangeResolution = (299792458.0 * pulseWidth) / 2.0;
        
        // 最大不模糊距离 = c / (2 × PRF)
        float maxUnambiguousRange = 299792458.0 / (2.0 * prf);
        
        // 检查距离模糊
        if (sample.m_Distance > maxUnambiguousRange)
        {
            // 标记为模糊回波
            sample.m_Hit = false; // 或设置模糊标志
        }
    }
    
    override string GetModeName() { return "Pulse Radar"; }
}

// === 连续波（CW）雷达模式 ===
class RDF_CWRadarMode : RDF_RadarMode
{
    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        // CW 雷达特性：只能测速度，不能测距
        // 必须依赖多普勒频移
        if (!settings.m_EnableDopplerProcessing)
        {
            sample.m_Hit = false;
            return;
        }
        
        // 只检测运动目标
        if (Math.AbsFloat(sample.m_TargetVelocity) < settings.m_MinTargetVelocity)
            sample.m_Hit = false;
    }
    
    override string GetModeName() { return "Continuous Wave"; }
}

// === 调频连续波（FMCW）雷达模式 ===
class RDF_FMCWRadarMode : RDF_RadarMode
{
    override void ProcessSample(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        // FMCW 可同时测距和测速
        // 通过频率差测距，通过多普勒测速
        
        // 这里是简化实现
        // 实际需要线性调频信号的差拍频率计算
    }
    
    override string GetModeName() { return "FMCW"; }
}
```

---

## 🎯 第四阶段：可视化增强 (Phase 4: Visualization Enhancement)

**目标**: 为雷达数据提供专用的可视化方案

### 4.1 雷达颜色策略

#### 实现文件
- `scripts/Game/RDF/Radar/Visual/RDF_RadarColorStrategy.c`

#### 核心实现

```c
// 基于信噪比的颜色映射
class RDF_SNRColorStrategy : RDF_LidarColorStrategy
{
    override int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        // 此方法被新方法覆盖
        return ARGB(255, 255, 255, 255);
    }
    
    // 新方法：基于雷达样本
    int BuildPointColorFromRadarSample(RDF_RadarSample sample, RDF_LidarVisualSettings settings)
    {
        if (!sample.m_Hit)
            return ARGB(128, 50, 50, 50); // 暗灰色 - 未检测
        
        float snr = sample.m_SignalToNoiseRatio;
        
        // SNR 颜色映射
        // 0-10 dB: 红色（弱信号）
        // 10-20 dB: 黄色
        // 20-30 dB: 绿色
        // >30 dB: 青色（强信号）
        
        if (snr < 10.0)
        {
            float t = snr / 10.0;
            return ARGB(255, 255, int(t * 255), 0);
        }
        else if (snr < 20.0)
        {
            float t = (snr - 10.0) / 10.0;
            return ARGB(255, int(255 * (1-t)), 255, 0);
        }
        else if (snr < 30.0)
        {
            float t = (snr - 20.0) / 10.0;
            return ARGB(255, 0, 255, int(t * 255));
        }
        else
        {
            return ARGB(255, 0, 255, 255);
        }
    }
}

// 基于 RCS 的颜色映射
class RDF_RCSColorStrategy : RDF_LidarColorStrategy
{
    int BuildPointColorFromRadarSample(RDF_RadarSample sample, RDF_LidarVisualSettings settings)
    {
        if (!sample.m_Hit)
            return ARGB(64, 30, 30, 30);
        
        // RCS 对数映射
        float rcsDBsm = 10.0 * Math.Log10(Math.Max(sample.m_RadarCrossSection, 1e-6));
        
        // -20 dBsm ~ +20 dBsm 映射到颜色
        // 小目标 -> 蓝色
        // 中等 -> 绿色
        // 大目标 -> 红色
        
        float normalizedRCS = Math.Clamp((rcsDBsm + 20.0) / 40.0, 0.0, 1.0);
        
        if (normalizedRCS < 0.33)
        {
            // 蓝到青
            float t = normalizedRCS / 0.33;
            return ARGB(255, 0, int(t * 255), 255);
        }
        else if (normalizedRCS < 0.66)
        {
            // 青到黄
            float t = (normalizedRCS - 0.33) / 0.33;
            return ARGB(255, int(t * 255), 255, int(255 * (1-t)));
        }
        else
        {
            // 黄到红
            float t = (normalizedRCS - 0.66) / 0.34;
            return ARGB(255, 255, int(255 * (1-t)), 0);
        }
    }
}

// 多普勒速度颜色映射
class RDF_DopplerColorStrategy : RDF_LidarColorStrategy
{
    int BuildPointColorFromRadarSample(RDF_RadarSample sample, RDF_LidarVisualSettings settings)
    {
        if (!sample.m_Hit)
            return ARGB(64, 20, 20, 20);
        
        float velocity = sample.m_TargetVelocity;
        
        // 速度映射：-50 m/s ~ +50 m/s
        // 接近（正）-> 红色
        // 静止 -> 白色
        // 远离（负）-> 蓝色
        
        float normalizedVel = Math.Clamp((velocity + 50.0) / 100.0, 0.0, 1.0);
        
        if (normalizedVel < 0.5)
        {
            // 蓝到白
            float t = normalizedVel / 0.5;
            return ARGB(255, int(t * 255), int(t * 255), 255);
        }
        else
        {
            // 白到红
            float t = (normalizedVel - 0.5) / 0.5;
            return ARGB(255, 255, int(255 * (1-t)), int(255 * (1-t)));
        }
    }
}
```

### 4.2 雷达显示模式

#### 实现文件
- `scripts/Game/RDF/Radar/Visual/RDF_RadarDisplay.c`

#### 核心功能

```c
// PPI（Plan Position Indicator）平面显示
class RDF_PPIDisplay
{
    protected float m_DisplayRadius = 100.0; // 显示半径（米）
    protected vector m_CenterPos;            // 显示中心
    
    // 绘制 PPI 圆形显示器
    void DrawPPI(array<ref RDF_RadarSample> samples, IEntity radarEntity)
    {
        if (!radarEntity)
            return;
        
        m_CenterPos = radarEntity.GetOrigin();
        
        // 绘制圆形网格
        DrawPPIGrid();
        
        // 绘制目标点
        foreach (RDF_RadarSample sample : samples)
        {
            if (!sample.m_Hit)
                continue;
            
            DrawPPITarget(sample);
        }
    }
    
    protected void DrawPPIGrid()
    {
        // 绘制同心圆
        int rings = 5;
        for (int i = 1; i <= rings; i++)
        {
            float radius = m_DisplayRadius * i / rings;
            DrawCircle(m_CenterPos, radius, ARGB(128, 100, 100, 100));
        }
        
        // 绘制方位线（每 30 度）
        for (int angle = 0; angle < 360; angle += 30)
        {
            float rad = angle * Math.DEG2RAD;
            vector direction = Vector(Math.Cos(rad), 0, Math.Sin(rad));
            vector endPos = m_CenterPos + direction * m_DisplayRadius;
            
            Shape.CreateLine(ARGB(128, 80, 80, 80), ShapeFlags.NOZBUFFER, 
                           m_CenterPos, endPos, 1.0);
        }
    }
    
    protected void DrawPPITarget(RDF_RadarSample sample)
    {
        // 投影到水平面
        vector targetPos = sample.m_HitPos;
        vector relativePos = targetPos - m_CenterPos;
        relativePos[1] = 0; // 压平到水平
        
        // 距离裁剪
        float dist = relativePos.Length();
        if (dist > m_DisplayRadius)
            return;
        
        // 绘制目标点
        int color = GetTargetColor(sample);
        float pointSize = GetTargetSize(sample);
        
        Shape.CreateSphere(color, ShapeFlags.NOZBUFFER | ShapeFlags.ONCE,
                          m_CenterPos + relativePos, pointSize);
    }
    
    protected int GetTargetColor(RDF_RadarSample sample)
    {
        // 基于 SNR
        if (sample.m_SignalToNoiseRatio > 20.0)
            return ARGB(255, 0, 255, 0);     // 强信号 - 绿色
        else if (sample.m_SignalToNoiseRatio > 10.0)
            return ARGB(255, 255, 255, 0);   // 中等 - 黄色
        else
            return ARGB(255, 255, 100, 0);   // 弱信号 - 橙色
    }
    
    protected float GetTargetSize(RDF_RadarSample sample)
    {
        // 基于 RCS
        float rcsDBsm = 10.0 * Math.Log10(Math.Max(sample.m_RadarCrossSection, 1e-6));
        return Math.Clamp(0.2 + rcsDBsm / 50.0, 0.1, 1.0);
    }
    
    protected void DrawCircle(vector center, float radius, int color)
    {
        int segments = 32;
        for (int i = 0; i < segments; i++)
        {
            float angle1 = (i * 360.0 / segments) * Math.DEG2RAD;
            float angle2 = ((i + 1) * 360.0 / segments) * Math.DEG2RAD;
            
            vector p1 = center + Vector(Math.Cos(angle1) * radius, 0, Math.Sin(angle1) * radius);
            vector p2 = center + Vector(Math.Cos(angle2) * radius, 0, Math.Sin(angle2) * radius);
            
            Shape.CreateLine(color, ShapeFlags.NOZBUFFER, p1, p2, 1.0);
        }
    }
}

// A-Scope（距离-幅度显示）
class RDF_AScopeDisplay
{
    // 简化的距离-信号强度图
    void DrawAScope(array<ref RDF_RadarSample> samples)
    {
        // 在屏幕空间绘制距离-幅度曲线
        // 实现略（需要 UI 系统支持）
    }
}
```

---

## 🎯 第五阶段：高级特性 (Phase 5: Advanced Features)

### 5.1 合成孔径雷达 (SAR) 基础

#### 实现文件
- `scripts/Game/RDF/Radar/Advanced/RDF_SARProcessor.c`

#### 概念实现

```c
// 简化的 SAR 成像原型
class RDF_SARProcessor
{
    // SAR 成像需要平台运动和多次扫描数据
    protected array<ref array<ref RDF_RadarSample>> m_ScanHistory;
    protected array<vector> m_PlatformPositions;
    
    void AddScan(array<ref RDF_RadarSample> samples, vector platformPos)
    {
        m_ScanHistory.Insert(samples);
        m_PlatformPositions.Insert(platformPos);
    }
    
    // 简化的后向投影算法
    void GenerateSARImage(int resolution, float areaSize)
    {
        // 创建图像网格
        // 对每个像素，累加所有扫描的相干贡献
        // 这是极度简化的版本，仅用于演示概念
        
        Print("[SAR] Generating image with " + m_ScanHistory.Count() + " scans");
        
        // 实际实现需要相位历史和精确几何校正
    }
}
```

### 5.2 电子对抗 (ECM)

#### 实现文件
- `scripts/Game/RDF/Radar/ECM/RDF_JammingModel.c`

#### 核心内容

```c
// 干扰模型
class RDF_JammingModel
{
    // 噪声干扰
    static float CalculateNoiseJamming(
        float jammerPower,      // 干扰机功率 (W)
        float jammerGain,       // 干扰天线增益（线性）
        float radarGain,        // 雷达接收增益（线性）
        float distance,         // 距离 (m)
        float bandwidth         // 雷达带宽 (Hz)
    )
    {
        // J/S = (Pj × Gj × Gr × λ²) / ((4π)² × R² × Br)
        
        float wavelength = 0.03; // 假设 10 GHz
        float numerator = jammerPower * jammerGain * radarGain * wavelength * wavelength;
        float denominator = Math.Pow(4.0 * Math.PI, 2.0) * distance * distance * bandwidth;
        
        return numerator / denominator;
    }
    
    // 欺骗干扰
    static void GenerateFalseTargets(
        array<ref RDF_RadarSample> samples,
        int falseTargetCount
    )
    {
        // 插入虚假目标样本
        for (int i = 0; i < falseTargetCount; i++)
        {
            RDF_RadarSample fakeSample = new RDF_RadarSample();
            // 随机参数
            fakeSample.m_Hit = true;
            fakeSample.m_Distance = Math.RandomFloat(100.0, 1000.0);
            // ... 设置其他参数
            
            samples.Insert(fakeSample);
        }
    }
}
```

### 5.3 目标分类器

#### 实现文件
- `scripts/Game/RDF/Radar/Classification/RDF_TargetClassifier.c`

#### 核心内容

```c
// 基于 RCS 和多普勒的简单分类器
class RDF_TargetClassifier
{
    enum TargetClass
    {
        UNKNOWN = 0,
        INFANTRY = 1,
        VEHICLE_LIGHT = 2,
        VEHICLE_HEAVY = 3,
        AIRCRAFT = 4,
        STATIC_OBJECT = 5
    }
    
    static TargetClass ClassifyTarget(RDF_RadarSample sample)
    {
        if (!sample.m_Hit)
            return UNKNOWN;
        
        float rcs = sample.m_RadarCrossSection;
        float velocity = Math.AbsFloat(sample.m_TargetVelocity);
        
        // 规则基分类（简化）
        
        // 静止目标
        if (velocity < 0.5)
            return STATIC_OBJECT;
        
        // 步兵：小 RCS，低速
        if (rcs < 0.5 && velocity < 5.0)
            return INFANTRY;
        
        // 轻型车辆：中等 RCS，中速
        if (rcs < 10.0 && velocity < 30.0)
            return VEHICLE_LIGHT;
        
        // 重型车辆：大 RCS，中低速
        if (rcs >= 10.0 && velocity < 40.0)
            return VEHICLE_HEAVY;
        
        // 飞行器：高速
        if (velocity > 40.0)
            return AIRCRAFT;
        
        return UNKNOWN;
    }
    
    static string GetClassName(TargetClass cls)
    {
        switch (cls)
        {
            case INFANTRY: return "Infantry";
            case VEHICLE_LIGHT: return "Light Vehicle";
            case VEHICLE_HEAVY: return "Heavy Vehicle";
            case AIRCRAFT: return "Aircraft";
            case STATIC_OBJECT: return "Static";
            default: return "Unknown";
        }
    }
}
```

---

## 🎯 第六阶段：集成与测试 (Phase 6: Integration & Testing)

### 6.1 演示配置预设

#### 实现文件
- `scripts/Game/RDF/Radar/Demo/RDF_RadarDemoConfig.c`

#### 核心内容

```c
class RDF_RadarDemoConfig : RDF_LidarDemoConfig
{
    // 雷达特定配置
    ref RDF_EMWaveParameters m_EMWaveParams;
    int m_RadarMode;
    
    // === 预设工厂方法 ===
    
    // X 波段搜索雷达
    static RDF_RadarDemoConfig CreateXBandSearch(int rayCount = 512)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        
        // 基础配置
        cfg.m_Enable = true;
        cfg.m_RayCount = rayCount;
        cfg.m_MinTickInterval = 0.5;
        cfg.m_SampleStrategy = new RDF_UniformSampleStrategy();
        
        // 电磁波参数
        cfg.m_EMWaveParams = new RDF_EMWaveParameters();
        cfg.m_EMWaveParams.m_CarrierFrequency = 10e9;  // 10 GHz
        cfg.m_EMWaveParams.m_TransmitPower = 1000.0;   // 1 kW
        cfg.m_EMWaveParams.m_AntennaGain = 30.0;       // 30 dBi
        cfg.m_EMWaveParams.CalculateWavelength();
        
        cfg.m_RadarMode = 0; // Pulse
        
        return cfg;
    }
    
    // Ka 波段汽车雷达
    static RDF_RadarDemoConfig CreateAutomotiveRadar(int rayCount = 256)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        
        cfg.m_Enable = true;
        cfg.m_RayCount = rayCount;
        cfg.m_MinTickInterval = 0.1; // 高更新率
        cfg.m_SampleStrategy = new RDF_ConicalSampleStrategy(15.0); // 窄波束
        
        // Ka 波段
        cfg.m_EMWaveParams = new RDF_EMWaveParameters();
        cfg.m_EMWaveParams.m_CarrierFrequency = 77e9;  // 77 GHz
        cfg.m_EMWaveParams.m_TransmitPower = 0.1;      // 100 mW
        cfg.m_EMWaveParams.m_AntennaGain = 25.0;       // 25 dBi
        cfg.m_EMWaveParams.CalculateWavelength();
        
        cfg.m_RadarMode = 2; // FMCW
        
        return cfg;
    }
    
    // 气象雷达（S 波段）
    static RDF_RadarDemoConfig CreateWeatherRadar(int rayCount = 1024)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        
        cfg.m_Enable = true;
        cfg.m_RayCount = rayCount;
        cfg.m_MinTickInterval = 2.0; // 慢扫描
        cfg.m_SampleStrategy = new RDF_HemisphereSampleStrategy();
        
        // S 波段
        cfg.m_EMWaveParams = new RDF_EMWaveParameters();
        cfg.m_EMWaveParams.m_CarrierFrequency = 3e9;   // 3 GHz
        cfg.m_EMWaveParams.m_TransmitPower = 50000.0;  // 50 kW
        cfg.m_EMWaveParams.m_AntennaGain = 40.0;       // 40 dBi
        cfg.m_EMWaveParams.CalculateWavelength();
        
        return cfg;
    }
    
    // 相控阵雷达（扫掠扫描）
    static RDF_RadarDemoConfig CreatePhasedArrayRadar(int rayCount = 2048)
    {
        RDF_RadarDemoConfig cfg = new RDF_RadarDemoConfig();
        
        cfg.m_Enable = true;
        cfg.m_RayCount = rayCount;
        cfg.m_MinTickInterval = 0.2;
        cfg.m_SampleStrategy = new RDF_SweepSampleStrategy(60.0, 30.0, 90.0); // 宽扇区快速扫描
        
        // C 波段
        cfg.m_EMWaveParams = new RDF_EMWaveParameters();
        cfg.m_EMWaveParams.m_CarrierFrequency = 5.6e9; // 5.6 GHz
        cfg.m_EMWaveParams.m_TransmitPower = 10000.0;  // 10 kW
        cfg.m_EMWaveParams.m_AntennaGain = 35.0;       // 35 dBi
        cfg.m_EMWaveParams.CalculateWavelength();
        
        cfg.m_RadarMode = 3; // Phased Array
        
        return cfg;
    }
}
```

### 6.2 测试场景

#### 实现文件
- `scripts/Game/RDF/Radar/Tests/RDF_RadarTests.c`

#### 测试内容

```c
class RDF_RadarTests
{
    // 测试 1: 雷达方程验证
    static void TestRadarEquation()
    {
        Print("[Test] Radar Equation Validation");
        
        // 已知参数
        float Pt = 1000.0;           // 1 kW
        float Gt = 1000.0;           // 30 dBi = 1000 线性
        float wavelength = 0.03;     // 10 GHz -> 3 cm
        float sigma = 1.0;           // 1 m² RCS
        float range = 10000.0;       // 10 km
        
        float Pr = RDF_RadarEquation.CalculateReceivedPower(Pt, Gt, wavelength, sigma, range, 1.0);
        
        Print(string.Format("  Range: %.1f km, RCS: %.1f m²", range/1000.0, sigma));
        Print(string.Format("  Received Power: %.2e W (%.1f dBm)", Pr, 10*Math.Log10(Pr/1e-3)));
        
        // 预期：应该在微瓦级别
    }
    
    // 测试 2: 多普勒计算
    static void TestDoppler()
    {
        Print("[Test] Doppler Effect");
        
        float velocity = 30.0;       // 30 m/s (约 108 km/h)
        float frequency = 10e9;      // 10 GHz
        
        float dopplerShift = RDF_DopplerProcessor.CalculateDopplerShift(velocity, frequency);
        
        Print(string.Format("  Velocity: %.1f m/s, Carrier: %.1f GHz", velocity, frequency/1e9));
        Print(string.Format("  Doppler Shift: %.1f Hz", dopplerShift));
        
        // 预期：约 2000 Hz
    }
    
    // 测试 3: RCS 模型
    static void TestRCS()
    {
        Print("[Test] RCS Models");
        
        float wavelength = 0.03;
        
        // 球体
        float sphereRCS = RDF_RCSModel.CalculateSphereRCS(1.0, wavelength);
        Print(string.Format("  Sphere (r=1m): %.2f m² (%.1f dBsm)", sphereRCS, 10*Math.Log10(sphereRCS)));
        
        // 平板（1m²，正向）
        float plateRCS = RDF_RCSModel.CalculatePlateRCS(1.0, wavelength);
        Print(string.Format("  Plate (1m²): %.2f m² (%.1f dBsm)", plateRCS, 10*Math.Log10(plateRCS)));
    }
    
    // 测试 4: 检测距离
    static void TestDetectionRange()
    {
        Print("[Test] Maximum Detection Range");
        
        float Pt = 10000.0;          // 10 kW
        float Gt = 10000.0;          // 40 dBi
        float wavelength = 0.1;      // 3 GHz
        float sigma = 10.0;          // 10 m² (车辆)
        float sensitivity = 1e-13;   // -100 dBm
        
        float maxRange = RDF_RadarEquation.CalculateMaxDetectionRange(
            Pt, Gt, wavelength, sigma, sensitivity
        );
        
        Print(string.Format("  Max Range for 10 m² target: %.1f km", maxRange/1000.0));
    }
    
    // 运行所有测试
    static void RunAllTests()
    {
        Print("=== Radar Physics Tests ===");
        TestRadarEquation();
        Print("");
        TestDoppler();
        Print("");
        TestRCS();
        Print("");
        TestDetectionRange();
        Print("=== Tests Complete ===");
    }
}
```

### 6.3 性能基准测试

#### 实现文件
- `scripts/Game/RDF/Radar/Tests/RDF_RadarBenchmark.c`

```c
class RDF_RadarBenchmark
{
    static void BenchmarkRadarScan(int rayCount)
    {
        Print(string.Format("[Benchmark] Radar Scan with %d rays", rayCount));
        
        // 创建雷达扫描器
        RDF_RadarScanner scanner = new RDF_RadarScanner();
        RDF_RadarSettings settings = scanner.GetSettings();
        settings.m_RayCount = rayCount;
        settings.m_Range = 500.0;
        
        // 设置电磁波参数
        settings.m_EMWaveParams = new RDF_EMWaveParameters();
        settings.m_EMWaveParams.m_CarrierFrequency = 10e9;
        settings.m_EMWaveParams.m_TransmitPower = 1000.0;
        settings.m_EMWaveParams.m_AntennaGain = 30.0;
        settings.m_EMWaveParams.CalculateWavelength();
        
        // 获取测试实体
        IEntity testEntity = GetGame().GetPlayerManager().GetPlayerControlledEntity(0);
        if (!testEntity)
        {
            Print("  [Error] No player entity found");
            return;
        }
        
        array<ref RDF_LidarSample> samples = new array<ref RDF_LidarSample>();
        
        // 测量时间
        float startTime = System.GetTickCount();
        
        scanner.Scan(testEntity, samples);
        
        float endTime = System.GetTickCount();
        float duration = endTime - startTime;
        
        // 统计
        int hitCount = 0;
        foreach (RDF_LidarSample sample : samples)
        {
            if (sample.m_Hit)
                hitCount++;
        }
        
        Print(string.Format("  Duration: %.2f ms", duration));
        Print(string.Format("  Samples: %d, Hits: %d", samples.Count(), hitCount));
        Print(string.Format("  Performance: %.2f rays/ms", rayCount / duration));
    }
}
```

---

## 🎯 第七阶段：文档与导出 (Phase 7: Documentation & Export)

### 7.1 扩展 CSV 导出

#### 实现文件
- 扩展 `scripts/Game/RDF/Lidar/Util/RDF_LidarExport.c`

#### 新增方法

```c
// 雷达专用 CSV 格式
static string GetRadarCSVHeader()
{
    return "Index,Hit,Distance,HitPos_X,HitPos_Y,HitPos_Z," +
           "ReceivedPower,SNR_dB,RCS_m2,RCS_dBsm," +
           "PathLoss_dB,AtmLoss_dB,RainAtten_dB," +
           "DopplerFreq_Hz,TargetVel_ms," +
           "IncidenceAngle_deg,ReflectionCoeff," +
           "MaterialType,EntityClass";
}

static string RadarSampleToCSVRow(RDF_RadarSample sample)
{
    string row = string.Format("%d,%d,%.3f,%.3f,%.3f,%.3f",
        sample.m_Index,
        sample.m_Hit ? 1 : 0,
        sample.m_Distance,
        sample.m_HitPos[0], sample.m_HitPos[1], sample.m_HitPos[2]
    );
    
    // 电磁波参数
    row += string.Format(",%.6e,%.2f,%.6f,%.2f",
        sample.m_ReceivedPower,
        sample.m_SignalToNoiseRatio,
        sample.m_RadarCrossSection,
        10.0 * Math.Log10(Math.Max(sample.m_RadarCrossSection, 1e-10))
    );
    
    // 传播损耗
    row += string.Format(",%.2f,%.2f,%.2f",
        sample.m_PathLoss,
        sample.m_AtmosphericLoss,
        sample.m_RainAttenuation
    );
    
    // 多普勒
    row += string.Format(",%.2f,%.2f",
        sample.m_DopplerFrequency,
        sample.m_TargetVelocity
    );
    
    // 反射特性
    row += string.Format(",%.2f,%.3f,%s",
        sample.m_IncidenceAngle,
        sample.m_ReflectionCoefficient,
        sample.m_MaterialType
    );
    
    // 实体信息
    string entityClass = "";
    if (sample.m_Entity)
        entityClass = sample.m_Entity.GetClassName();
    row += "," + entityClass;
    
    return row;
}
```

### 7.2 API 文档更新

创建独立的雷达 API 文档：`docs/RADAR_API.md`

内容包括：
- 所有新增类的详细说明
- 电磁波参数配置指南
- 物理模型公式参考
- 使用示例代码
- 性能优化建议

### 7.3 使用教程

创建 `docs/RADAR_TUTORIAL.md`

包含：
1. 快速入门（5 分钟设置）
2. 基础概念讲解
3. 预设配置使用
4. 自定义雷达开发
5. 常见问题解答
6. 故障排除

---

## 📊 实施路线图 (Implementation Roadmap)

### 里程碑时间线

| 阶段 | 预估工作量 | 关键交付物 | 优先级 |
|------|-----------|-----------|--------|
| **Phase 1: 基础架构** | 2-3 周 | 数据结构、配置系统 | P0 (最高) |
| **Phase 2: 物理模型** | 3-4 周 | 传播、RCS、雷达方程 | P0 |
| **Phase 3: 扫描器** | 2-3 周 | RadarScanner、工作模式 | P0 |
| **Phase 4: 可视化** | 2 周 | 颜色策略、PPI 显示 | P1 |
| **Phase 5: 高级特性** | 3-4 周 | SAR、ECM、分类器 | P2 (可选) |
| **Phase 6: 集成测试** | 1-2 周 | 演示、测试、基准 | P0 |
| **Phase 7: 文档** | 1 周 | API 文档、教程 | P1 |

**总预估时间**: 14-19 周（约 3.5-5 个月）

### 依赖关系

```
Phase 1 (基础)
    ↓
Phase 2 (物理) ───→ Phase 3 (扫描器)
    ↓                      ↓
Phase 4 (可视化) ←────────┘
    ↓
Phase 5 (高级) ← 可选
    ↓
Phase 6 (测试)
    ↓
Phase 7 (文档)
```

---

## ⚠️ 风险与挑战 (Risks & Challenges)

### 技术风险

1. **性能开销**
   - **风险**: 雷达物理计算可能显著增加每帧开销
   - **缓解**: 
     - 提供简化模式开关
     - 异步计算选项
     - LOD 系统（远距离简化计算）

2. **数值稳定性**
   - **风险**: 极端参数下可能出现数值溢出/下溢
   - **缓解**:
     - 使用 dB 尺度计算
     - 参数范围限制与验证
     - 单元测试覆盖边界情况

3. **引擎限制**
   - **风险**: Arma Reforger 可能无法提供某些物理信息（如实体速度、精确材质）
   - **缓解**:
     - 提供回退估算方法
     - 使用近似模型
     - 文档说明限制

### 兼容性风险

1. **与现有 LiDAR 系统的冲突**
   - **缓解**: 继承体系设计，独立命名空间，清晰的文档说明

2. **网络同步复杂度**
   - **缓解**: Phase 1 先实现单机版，后续逐步添加网络支持

### 项目管理风险

1. **范围蔓延**
   - **缓解**: 严格的优先级划分（P0/P1/P2），Phase 5 可作为未来扩展

2. **测试覆盖不足**
   - **缓解**: Phase 6 专门用于测试，建立自动化测试框架

---

## 📈 成功指标 (Success Metrics)

### 功能性指标

- [ ] 支持至少 3 种雷达波段（L/X/Ka）
- [ ] 实现至少 2 种工作模式（脉冲/FMCW）
- [ ] 物理模型误差 < 10%（与理论值对比）
- [ ] 支持至少 512 条射线的实时扫描（>30 FPS）

### 性能指标

- [ ] 雷达扫描开销 < LiDAR 扫描的 2 倍
- [ ] 内存占用增长 < 50%（相比 LiDAR）
- [ ] 支持多人游戏（4+ 玩家同时使用）

### 可用性指标

- [ ] 完整的 API 文档（中英双语）
- [ ] 至少 5 个预设配置
- [ ] 至少 3 个完整示例场景
- [ ] 社区可独立扩展（插件化设计）

---

## 🔮 未来展望 (Future Vision)

### 长期目标

1. **雷达仿真平台**
   - 成为 Arma Reforger 社区的标准雷达开发平台
   - 支持第三方雷达模组快速开发

2. **AI 训练数据集**
   - 自动生成标注的雷达数据
   - 训练目标检测/分类/跟踪模型

3. **跨引擎移植**
   - 抽象核心算法为独立库
   - 移植到 Unreal/Unity 等引擎

4. **硬件在环仿真**
   - 与真实雷达系统对接
   - 用于雷达算法验证

### 社区生态

- **插件市场**: 预设配置商店
- **教程系列**: 视频教程、博客文章
- **竞赛活动**: 雷达算法挑战赛
- **学术合作**: 与大学/研究所合作

---

## 📞 支持与贡献 (Support & Contribution)

### 贡献指南

1. **代码风格**: 遵循 Google C++ Style Guide（Enfusion 适配版）
2. **提交规范**: Conventional Commits（feat/fix/docs/perf）
3. **测试要求**: 所有新增功能必须有对应测试
4. **文档要求**: 公开 API 必须有注释和文档

### 联系方式

- **项目仓库**: [GitHub - Radar-Development-Framework](https://github.com/ViVi141/Radar-Development-Framework)
- **技术讨论**: 在 Issues 中提出
- **邮件**: 747384120@qq.com

---

## 📚 参考资料 (References)

### 雷达理论

1. Skolnik, M. I. (2008). *Radar Handbook*. McGraw-Hill.
2. Richards, M. A. (2014). *Fundamentals of Radar Signal Processing*. McGraw-Hill.
3. ITU-R P.676: "Attenuation by atmospheric gases"
4. ITU-R P.838: "Specific attenuation model for rain"

### 软件设计

1. Gamma, E. et al. (1994). *Design Patterns*. Addison-Wesley.
2. Martin, R. C. (2008). *Clean Code*. Prentice Hall.

### Arma Reforger 开发

1. Bohemia Interactive: Enfusion Script Documentation
2. Arma Reforger Wiki: Modding Guidelines

---

## 📄 附录 (Appendix)

### A. 常用雷达频段表

| 波段 | 频率范围 | 波长 | 典型应用 | 特点 |
|------|----------|------|----------|------|
| HF | 3-30 MHz | 10-100 m | 超视距雷达 | 电离层反射 |
| VHF | 30-300 MHz | 1-10 m | 远程预警 | 抗干扰 |
| UHF | 300-1000 MHz | 0.3-1 m | 远程监视 | 穿透能力强 |
| L | 1-2 GHz | 15-30 cm | 远程搜索 | 低衰减 |
| S | 2-4 GHz | 7.5-15 cm | 中程监视 | 折中性能 |
| C | 4-8 GHz | 3.75-7.5 cm | 气象/跟踪 | 平衡 |
| X | 8-12 GHz | 2.5-3.75 cm | 火控/导航 | 高分辨率 |
| Ku | 12-18 GHz | 1.67-2.5 cm | 高精度测距 | 受雨衰影响 |
| K | 18-27 GHz | 1.11-1.67 cm | 科研 | - |
| Ka | 27-40 GHz | 0.75-1.11 cm | 汽车雷达 | 高衰减 |
| V | 40-75 GHz | 0.4-0.75 cm | 短程 | 极高衰减 |
| W | 75-110 GHz | 0.27-0.4 cm | 毫米波成像 | 大气窗口 |

### B. RCS 参考值

| 目标类型 | 典型 RCS | 备注 |
|---------|---------|------|
| 昆虫 | -30 ~ -20 dBsm | 毫米级 |
| 鸟类 | -20 ~ -10 dBsm | 10-100 cm² |
| 人体 | -10 ~ 0 dBsm | 0.1-1 m² |
| 摩托车 | 0 ~ 5 dBsm | 1-3 m² |
| 小汽车 | 5 ~ 15 dBsm | 3-30 m² |
| 卡车 | 15 ~ 25 dBsm | 30-300 m² |
| 战斗机 | -10 ~ 10 dBsm | 隐身设计 |
| 大型飞机 | 20 ~ 40 dBsm | 100-10000 m² |
| 舰船 | 30 ~ 50 dBsm | 1000-100000 m² |

### C. 典型雷达系统参数

| 系统类型 | 频率 | 功率 | 增益 | 探测距离 |
|---------|------|------|------|----------|
| 汽车毫米波雷达 | 77 GHz | 0.1 W | 25 dBi | 200 m |
| 火控雷达 | 10 GHz | 1-10 kW | 30-40 dBi | 20-50 km |
| 搜索雷达 | 3 GHz | 50-100 kW | 35-45 dBi | 100-300 km |
| 气象雷达 | 5 GHz | 250 kW | 45 dBi | 400 km |
| 预警雷达 | 1 GHz | 1-10 MW | 40-50 dBi | 500-3000 km |

---

**文档结束**

---

## 版本历史 (Version History)

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|----------|
| 1.0 | 2026-02-18 | AI Assistant | 初始版本创建 |

---

**© 2026 Radar Development Framework - All Rights Reserved**
