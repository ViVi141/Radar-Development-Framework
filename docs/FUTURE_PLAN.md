# Radar Development Framework — 未来计划

本文档记录雷达和激光雷达系统的未来开发方向、优先级和时间规划。

**项目当前状态（2026-03-15）：**
- ✅ LiDAR 系统：功能完整，维护阶段
- ⚠️ 雷达系统：核心功能完成，开发中
- ✅ EM 体素场：Phase 1-5 全部完成，含高级空间优化

---

## 一、雷达系统开发计划

### Phase 1: 生产环境稳定化（预计 2-3 周）

**目标：** 移除"IN DEVELOPMENT"标记，达到生产可用状态

#### 1.1 核心功能验证
- [ ] 大规模边界测试（极端距离、极端RCS值）
- [ ] 性能基准测试与优化（1000+ 射线场景）
- [ ] 内存泄漏检测与修复
- [ ] 多线程安全性验证

#### 1.2 PoC 特性完善
- [ ] EM 体素场集成（Phase-2）- 完善能量注入与回波写入
- [ ] 多路径效应模型（Phase-4）- 地面反射增强/减弱
- [ ] 盲速抑制 - 参数调优与阈值优化

**验收标准：**
- 连续运行 24 小时无崩溃
- 内存占用稳定在 200MB 以内
- 512 射线扫描帧率稳定在 30 FPS 以上

---

### Phase 2: 物理模型增强（预计 3-4 周）

**目标：** 提升物理仿真的真实度

#### 2.1 天线方向图模型
```c
class RDF_AntennaPattern
{
    // 替代当前的均匀波束假设
    float GetGainAtAngle(float azimuthDeg, float elevationDeg);
    
    // 支持的波束类型：
    // - 理想余弦波束
    // - 高斯波束
    // - 实际天线测量数据插值
}
```

- [ ] 实现天线方向图 API
- [ ] 支持余弦波束、高斯波束
- [ ] 支持自定义波束数据导入
- [ ] 更新雷达方程使用方向图增益

#### 2.2 相位相干检测
- [ ] 多脉冲相位积累算法
- [ ] 脉冲压缩（chirp 信号）
- [ ] 脉冲多普勒处理（Pulse-Doppler）
- [ ] SNR 提升验证（理论 √N 增益）

#### 2.3 杂波功率谱模型
```c
class RDF_ClutterModel
{
    // 不同类型杂波的功率谱密度
    float GetClutterPowerSpectrum(
        EClutterType type,      // 气象/海杂波/地杂波
        float dopplerFreq,
        float range
    );
}
```

- [ ] 气象杂波模型（雨/雪/雹）
- [ ] 海杂波模型（海面散射）
- [ ] 地杂波模型（地面散射特性）
- [ ] 杂波抑制滤波器设计

**验收标准：**
- 天线方向图增益误差 < 1 dB
- 相位积累 SNR 提升达到理论值的 90% 以上
- 杂波抑制比 > 20 dB

---

### Phase 3: 可视化增强（预计 2-3 周）

**目标：** 改善用户体验和调试能力

#### 3.1 PPI 扫描动画
```c
class RDF_RadarHUD
{
    // 替代当前的整屏刷新
    void AnimateSweepLine(float scanAngle);
    void UpdateSector(float startAngle, float endAngle);
}
```

- [ ] 旋转扫描线动画（渐隐尾迹）
- [ ] 按角度逐步刷新目标点
- [ ] 可配置扫描速度（RPM）

#### 3.2 目标历史轨迹
- [ ] 保留最近 N 帧目标位置
- [ ] 轨迹淡出效果（时间衰减）
- [ ] 轨迹颜色编码（速度/方位角）
- [ ] 可配置历史长度（5-20 帧）

#### 3.3 A-Scope 接入 HUD
- [ ] 在 PPI 右侧叠加距离-幅度图
- [ ] 多目标回波显示
- [ ] 可切换 PPI/A-Scope/分屏模式

#### 3.4 目标详情面板
- [ ] 鼠标悬停显示目标信息
- [ ] 目标分类、速度、方位、距离
- [ ] RCS 曲线历史（最近 10 帧）
- [ ] 可锁定跟踪单个目标

**验收标准：**
- PPI 动画流畅（60 FPS）
- 轨迹显示清晰，无杂乱感
- A-Scope 响应延迟 < 100ms

---

### Phase 4: 网络同步（预计 2-3 周）

**目标：** 实现雷达扫描结果的多客户端同步

#### 4.1 网络组件设计
```c
class RDF_RadarNetworkComponent : ScriptComponent
{
    [RplProp] vector m_RadarOrigin;
    [RplProp] int m_RadarMode;
    
    // 扫描结果广播
    [RplRpc(RplChannel.Reliable)]
    void RpcDo_ScanComplete(string payload);
}
```

- [ ] 服务器权威扫描架构
- [ ] CSV 序列化与 RLE 压缩（复用 LiDAR 实现）
- [ ] 分片传输（1000 字节/片）
- [ ] 可靠/不可靠通道选择

#### 4.2 客户端接收与渲染
- [ ] 载荷重组与解压
- [ ] 客户端 HUD 更新（独立渲染）
- [ ] 网络延迟补偿（插值预测）

- [ ] 连接状态指示器
- [ ] 丢包重传机制
- [ ] 带宽自适应（根据网络质量调整扫描频率）

**验收标准：**
- 512 射线扫描结果在 100ms 内同步到客户端
- 丢包率 10% 时仍能正常显示
- 网络带宽占用 < 1 Mbps

---

### Phase 5: 工程质量提升（预计 2 周）

**目标：** 提升工具性和可维护性

#### 5.1 调试 UI 集成
- [ ] DiagMenu 参数控制（扫描频率、量程、波段）
- [ ] 实时性能监控（FPS、内存、CPU）
- [ ] 可视化调试开关（射线、光斑、轨迹）

#### 5.2 单元测试扩展
```c
class RDF_RadarAdvancedTests
{
    // 新增测试用例
    void TestAntennaPatternAccuracy();
    void TestPhaseCoherence();
    void TestClutterSuppression();
    void TestNetworkPacketLoss();
}
```

- [ ] 天线方向图精度测试
- [ ] 相位积累增益测试
- [ ] 杂波抑制比测试
- [ ] 网络丢包场景测试

#### 5.3 配置持久化
- [ ] JSON 格式配置文件
- [ ] 雷达参数保存/加载
- [ ] 预设配置管理（5种预设 + 自定义）
- [ ] 配置导入/导出功能

**验收标准：**
- 所有测试覆盖率 > 80%
- 配置文件加载/保存成功率 100%
- DiagMenu 响应延迟 < 50ms

---

## 二、LiDAR 系统增强计划

### Phase 1: 物理拟真（预计 2-3 周）

**目标：** 提升物理仿真真实性

#### 1.1 激光束发散角
```c
class RDF_LidarSettings
{
    float m_BeamDivergenceRad = 0.001;  // 1 mrad
    float m_BeamWattage = 0.5;          // 0.5 瓦
}

class RDF_LidarScanner
{
    float CalculateSpotRadius(float distance)
    {
        return distance * Math.Tan(m_Settings.m_BeamDivergenceRad);
    }
    
    bool DetectTarget(float spotRadius, float targetSize)
    {
        // 光斑必须完全覆盖目标才能检测
        return spotRadius >= targetSize;
    }
}
```

- [ ] 添加光斑半径计算
- [ ] 远距离检测概率衰减模型
- [ ] 可配置发散角（0.1-3 mrad）

#### 1.2 大气衰减模型
```c
class RDF_LidarAtmosphericModel
{
    // Beer-Lambert 定律
    static float CalculateAttenuation(
        float distance,
        float wavelengthNM,      // 905nm / 1550nm
        float visibilityKm,      // 能见度
        float humidityPct,
        float temperatureC
    );
}
```

- [ ] 大气吸收系数计算
- [ ] 雾/霾/雨/雪衰减
- [ ] 波长依赖性（905nm vs 1550nm）
- [ ] 天气系统集成（获取当前天气）

#### 1.3 表面反射率模型
```c
class RDF_LidarReflectivityModel
{
    static ref map<string, float> m_MaterialReflectivity = {
        ["asphalt"] = 0.08,    // 沥青
        ["concrete"] = 0.20,    // 混凝土
        ["grass"] = 0.35,        // 植被
        ["snow"] = 0.85,         // 雪
        ["metal"] = 0.80,        // 金属
        ["water"] = 0.05         // 水
    };
    
    static float GetReflectivity(string material);
}
```

- [ ] 材质反射率查找表
- [ ] 反射率查询 API
- [ ] 返回强度基于反射率计算

#### 1.4 接收器噪声与灵敏度
```c
class RDF_LidarScanner
{
    protected float m_MinDetectablePower = 1e-9;  // 1 nW
    protected float m_NoiseFloor = 1e-10;         // 噪声底
    
    bool IsDetectionValid(float receivedPower)
    {
        return (receivedPower > m_NoiseFloor) &&
               (receivedPower > m_MinDetectablePower);
    }
}
```

- [ ] 最小可检测功率阈值
- [ ] 接收器噪声底模拟
- [ ] SNR 计算
- [ ] 检测概率曲线

**验收标准：**
- 光斑半径计算误差 < 5%
- 大气衰减误差 < 10%
- 反射率模型覆盖 50+ 材质

---

### Phase 2: 设备特性（预计 2-3 周）

**目标：** 模拟真实 LiDAR 设备行为

#### 2.1 旋转式扫描模式
```c
class RDF_RotatingLidarStrategy : RDF_LidarSampleStrategy
{
    float m_RotationSpeedRPM = 600;   // 10 Hz
    float m_VerticalFOVDeg = 40;      // 垂直视场
    int   m_VerticalChannels = 32;     // 垂直通道数
    
    override vector BuildDirection(int index, int count, float worldTime)
    {
        // 根据时间计算旋转角度
        float currentAz = (worldTime * m_RotationSpeedRPM * 6.0) % 360.0;
        float el = GetVerticalAngle(index, m_VerticalChannels, m_VerticalFOVDeg);
        return SphericalToCartesian(currentAz, el);
    }
}
```

- [ ] 360° 旋转扫描
- [ ] 可配置 RPM（60-1200）
- [ ] 垂直 FOV 与通道数
- [ ] 点云密度随距离衰减

#### 2.2 飞行时间精确测量
```c
class RDF_LidarSample
{
    float m_TimeOfFlight;        // 飞行时间（秒）
    float m_PulseWidth = 5e-9;  // 脉冲宽度（5ns）
    int64 m_TimestampNS;         // 纳秒级时间戳
}

// 计算
sample.m_TimeOfFlight = distance * 2.0 / 299792458.0;
sample.m_TimestampNS = GetCurrentTimeNS();
```

- [ ] 飞行时间字段
- [ ] 纳秒级时间戳
- [ ] 距离精度提升（mm 级）

#### 2.3 多回波处理
```c
class RDF_LidarSample
{
    ref array<float> m_ReturnDistances;   // 多回波距离
    ref array<float> m_ReturnIntensities; // 多回波强度
    int   m_ReturnNumber;                // 当前回波序号
    int   m_NumberOfReturns;            // 总回波数
}

// 使用 TraceParam 检测多次碰撞
param.Flags = TraceFlags.ALL;
world.TraceMove(param, null);
```

- [ ] 多回波数据结构
- [ ] 连续碰撞检测
- [ ] 第一回波/最强回波/最后回波模式

#### 2.4 阳光干扰模型
```c
class RDF_LidarScanner
{
    float CalculateSunlightNoise()
    {
        // 根据太阳角度计算干扰
        float sunAngle = GetSunElevationAngle();
        if (sunAngle < 0) return 0.0;
        
        float sunlightIntensity = Math.Pow(Math.Sin(sunAngle), 2.0);
        return sunlightIntensity * m_SunlightSensitivity;
    }
}
```

- [ ] 太阳角度计算
- [ ] 阳光强度模型
- [ ] 信噪比降低模拟
- [ ] 日间/夜间模式切换

**验收标准：**
- 旋转扫描 10 Hz 稳定运行
- 飞行时间精度 < 1 ns
- 多回波检测准确率 > 90%

---

### Phase 3: 数据质量（预计 2 周）

**目标：** 提升点云数据质量和元数据

#### 3.1 点云元数据扩展
```c
class RDF_LidarSample
{
    // 新增元数据
    float m_ReturnNumber;        // 回波序号
    float m_NumberOfReturns;    // 总回波数
    float m_ScanDirection;      // 扫描镜方向
    int   m_ScanChannel;       // 扫描通道号
    int   m_GPSWeek;           // GPS 周
    float m_GPSTime;           // GPS 时间
    float m_Roll;              // 载具横滚角
    float m_Pitch;             // 载具俯仰角
    float m_Yaw;               // 载具偏航角
}
```

- [ ] 完整元数据字段
- [ ] GPS 时间同步
- [ ] 载具姿态记录
- [ ] 扫描通道标识

#### 3.2 返回强度量化
```c
class RDF_LidarSample
{
    float m_Intensity;      // 原始强度
    int   m_IntensityRaw;  // 量化后值
    int   m_IntensityBits = 12;  // ADC 位数
}

// 量化
sample.m_IntensityRaw = Math.Floor(
    sample.m_Intensity * Math.Pow(2, m_IntensityBits)
);
```

- [ ] 8/12/16 位 ADC 量化
- [ ] 增益控制模拟
- [ ] 强度噪声模拟

#### 3.3 深度融合
```c
class RDF_LidarDepthFusion
{
    protected ref map<vector, array<float>> m_DepthBuffer;
    
    float FuseDepth(vector direction, float newDepth, float confidence)
    {
        // 指数移动平均
        float alpha = 0.3;
        float fusedDepth = alpha * newDepth + (1 - alpha) * GetPreviousDepth(direction);
        return fusedDepth;
    }
}
```

- [ ] 多帧深度融合
- [ ] 卡尔曼滤波
- [ ] 噪声抑制
- [ ] 可配置融合窗口大小

**验收标准：**
- 元数据完整性 100%
- 强度量化误差 < 1%
- 深度融合 SNR 提升 > 5 dB

---

### Phase 4: 高级特性（预计 3-4 周）

**目标：** 实现高级 LiDAR 功能

#### 4.1 自适应扫描率
```c
class RDF_AdaptiveScanner : RDF_LidarScanner
{
    float m_CurrentScanRate = 10.0;  // Hz
    
    void AdaptScanRate()
    {
        float density = CalculateEnvironmentDensity();
        
        if (density > 0.8)
            m_CurrentScanRate = 5.0;   // 密集环境
        else if (density < 0.2)
            m_CurrentScanRate = 20.0;  // 开阔环境
    }
}
```

- [ ] 环境密度计算
- [ ] 动态扫描率调整
- [ ] 性能监控与反馈

#### 4.2 温度漂移补偿
```c
class RDF_LidarCalibration
{
    float m_TemperatureDriftRangeError = 0.0;
    
    void ApplyTemperatureCompensation(float ambientTempC)
    {
        m_TemperatureDriftRangeError = (ambientTempC - 20.0) * 0.001;
    }
    
    void AutoCalibrate()
    {
        // 模拟校准过程
        m_TemperatureDriftRangeError = 0.0;
    }
}
```

- [ ] 温度漂移模型
- [ ] 自动校准机制
- [ ] 校准提示 UI

#### 4.3 故障模式模拟
```c
class RDF_LidarFaultModel
{
    enum EFaultType
    {
        NONE,
        SENSOR_MISMATCH,
        BLOCKAGE,
        OVERHEATING,
        CALIBRATION_DRIFT
    }
    
    void SimulateFault(EFaultType fault)
    {
        switch (fault)
        {
            case BLOCKAGE:
                SetSectorBlindAngle(0, 180);
                break;
            case OVERHEATING:
                SetScanRate(GetScanRate() * 0.5);
                break;
        }
    }
}
```

- [ ] 多种故障类型
- [ ] 故障检测逻辑
- [ ] 故障警告 UI

#### 4.4 数据流限制
```c
class RDF_LidarDataPipe
{
    protected float m_MaxDataRateMbps = 100.0;
    
    bool CanSendData(int pointCount, int bytesPerPoint)
    {
        float packetSize = pointCount * bytesPerPoint * 8.0 / 1e6;
        return (m_AccumulatedData + packetSize <= m_MaxDataRateMbps);
    }
}
```

- [ ] 带宽限制模拟
- [ ] 数据包优先级
- [ ] 丢包策略

**验收标准：**
- 自适应扫描率响应时间 < 1s
- 温度漂移补偿误差 < 1cm
- 故障检测准确率 > 90%

---

## 三、EM 体素场系统计划

### Phase 6: 性能优化（预计 2-3 周）

**目标：** 进一步提升 EM 体素场性能

#### 6.1 GPU 加速（研究阶段）
- [ ] 评估 Enfusion GPU 计算能力
- [ ] 衰减计算 GPU 移植可行性
- [ ] 插值计算 GPU 加速
- [ ] 性能基准测试

#### 6.2 多帧分片处理
```c
class EMVoxelField
{
    // 将衰减计算分散到多帧
    protected int m_ChunkProcessPerFrame = 50;
    protected int m_CurrentProcessIndex = 0;
    
    void TickDecayMultiFrame(float deltaT)
    {
        for (int i = 0; i < m_ChunkProcessPerFrame; i++)
        {
            if (m_CurrentProcessIndex >= m_ActiveChunks.Count())
                break;
            
            m_ActiveChunks[m_CurrentProcessIndex].TickDecay(deltaT);
            m_CurrentProcessIndex++;
        }
    }
}
```

- [ ] 每帧处理固定数量块
- [ ] 摊销衰减计算开销
- [ ] 可配置处理速率

**验收标准：**
- GPU 加速（如可行）性能提升 > 5x
- 多帧分片 CPU 开销降低 > 50%

---

### Phase 7: 功能扩展（预计 3-4 周）

**目标：** 扩展 EM 体素场应用场景

#### 7.1 多频段支持
```c
class EMVoxelFieldMultiBand : EMVoxelField
{
    protected ref map<int, ref EMVoxelField> m_Bands;
    
    void InjectPowerBand(
        vector pos,
        float power,
        int frequencyBand,  // 频段标识
        vector direction
    );
}
```

- [ ] 多个独立体素场实例
- [ ] 频段标识与路由
- [ ] 跨频段干扰模拟

#### 7.2 持久化
```c
class EMVoxelFieldSerializer
{
    static bool SaveToFile(string path, EMVoxelField field);
    static bool LoadFromFile(string path, out EMVoxelField field);
}
```

- [ ] 二进制序列化格式
- [ ] 场景保存/加载
- [ ] 回放系统支持

#### 7.3 被动传感器高级功能
```c
class EMPassiveSensor
{
    // 信号描述子提取
    ref SignalDescriptor ExtractSignalDescriptor(
        vector pos,
        float frequency
    );
    
    // 波形识别
    EWaveformType IdentifyWaveform(SignalDescriptor desc);
}
```

- [ ] 波形分类（脉冲/CW/FMCW/LPI）
- [ ] 调制参数提取
- [ ] 信号指纹识别

**验收标准：**
- 多频段独立运行无干扰
- 持久化加载时间 < 1s
- 波形识别准确率 > 85%

---

## 四、文档与示例计划

### Phase 1: 文档完善（预计 1-2 周）

- [ ] API 文档自动生成
- [ ] 代码注释完整性检查
- [ ] 示例代码库扩充
- [ ] 视频教程制作

### Phase 2: 社区建设（持续）

- [ ] GitHub Wiki 搭建
- [ ] Issue 模板规范化
- [ ] PR 审查指南
- [ ] 贡献者指南

---

## 五、时间线总览

### 2026 Q2（4-6月）
- ✅ 雷达 Phase 1: 生产环境稳定化
- ✅ LiDAR Phase 1: 物理拟真
- ✅ EM 体素场 Phase 6: 性能优化

### 2026 Q3（7-9月）
- ✅ 雷达 Phase 2: 物理模型增强
- ✅ 雷达 Phase 3: 可视化增强
- ✅ LiDAR Phase 2: 设备特性

### 2026 Q4（10-12月）
- ✅ 雷达 Phase 4: 网络同步
- ✅ LiDAR Phase 3: 数据质量
- ✅ 文档完善

### 2027 Q1（1-3月）
- ✅ 雷达 Phase 5: 工程质量提升
- ✅ LiDAR Phase 4: 高级特性
- ✅ EM 体素场 Phase 7: 功能扩展

---

## 六、优先级矩阵

| 功能 | 重要性 | 难度 | 优先级 | 预计时间 |
|------|--------|--------|--------|---------|
| **雷达 - 生产稳定化** | 🔴 高 | 🟡 中 | P0 | 2-3 周 |
| **雷达 - 天线方向图** | 🔴 高 | 🟡 中 | P0 | 1 周 |
| **雷达 - 网络同步** | 🔴 高 | 🔴 高 | P1 | 2-3 周 |
| **雷达 - PPI 动画** | 🟡 中 | 🟢 低 | P1 | 1 周 |
| **雷达 - 杂波模型** | 🟡 中 | 🔴 高 | P2 | 2 周 |
| **LiDAR - 激光束发散** | 🔴 高 | 🟢 低 | P0 | 1 周 |
| **LiDAR - 大气衰减** | 🔴 高 | 🟡 中 | P0 | 1 周 |
| **LiDAR - 旋转扫描** | 🟡 中 | 🟡 中 | P1 | 2 周 |
| **LiDAR - 多回波** | 🟡 中 | 🔴 高 | P2 | 2 周 |
| **EM 体素场 - GPU** | 🟢 低 | 🔴 高 | P3 | 4 周 |

**优先级说明：**
- **P0**: 核心功能，必须实现
- **P1**: 重要功能，显著提升体验
- **P2**: 增强功能，提升拟真度
- **P3**: 研究性功能，长期规划

---

## 七、资源需求

### 开发资源
- **核心开发**: 1-2 名全职开发者
- **测试**: 每阶段 1 名测试工程师（2-4 周）
- **文档**: 1 名技术写作（兼职）

### 测试环境
- Arma Reforger Dedicated Server × 2
- 多客户端测试环境（10+ 客户端）
- 性能监控工具

### 社区资源
- GitHub Actions CI/CD
- Discord 社区服务器
- Wiki 平台

---

## 八、风险评估

### 技术风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| Enfusion 引擎 API 限制 | 🟡 中 | 🔴 高 | 提前验证可行性，准备备选方案 |
| 性能优化效果不佳 | 🟢 低 | 🟡 中 | 分阶段验证，及时调整方向 |
| 网络同步复杂度超预期 | 🟡 中 | 🔴 高 | 参考 LiDAR 实现，渐进式开发 |

### 进度风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| 开发资源不足 | 🟡 中 | 🔴 高 | 优先实现核心功能，推迟次要特性 |
| 需求变更 | 🟡 中 | 🟡 中 | 保持模块化设计，支持快速迭代 |
| 测试不充分 | 🟢 低 | 🔴 高 | 建立自动化测试体系 |

---

## 九、成功标准

### 雷达系统
- ✅ 移除"IN DEVELOPMENT"标记
- ✅ 通过 24 小时稳定性测试
- ✅ 物理模型误差 < 15%
- ✅ 网络同步延迟 < 100ms

### LiDAR 系统
- ✅ 物理拟真度提升 50%+
- ✅ 支持主流扫描模式（旋转式/固态）
- ✅ 点云质量接近商用设备

### EM 体素场
- ✅ CPU 开销降低 50%+
- ✅ 支持多频段场景
- ✅ 持久化功能可用

### 整体项目
- ✅ 文档覆盖率 > 90%
- ✅ 单元测试覆盖率 > 80%
- ✅ 社区活跃用户 > 100

---

## 十、联系方式

- **项目维护**: ViVi141
- **技术问题**: 747384120@qq.com
- **GitHub**: https://github.com/ViVi141/Radar-Development-Framework
- **更新频率**: 每季度更新此文档

---

*文档版本: v1.0*  
*创建日期: 2026-03-15*  
*下次更新: 2026-06-15*
