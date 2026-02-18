# Radar Development Framework — 开发者指南

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Contact: 747384120@qq.com  
License: Apache-2.0

---

## 设计原则

- 框架默认**静默**，只有在明确启用时才运行，不干扰其他模组。
- 扫描核心、物理模型、可视化与演示驱动相互隔离，可独立替换。
- 对外暴露稳定的扩展接口（采样策略、颜色策略、扫描完成回调）。
- 雷达模块完整继承 LiDAR 架构（`RDF_RadarScanner` 继承 `RDF_LidarScanner`）。

---

## 完整模块布局

```
scripts/Game/RDF/
│
├── Lidar/                          ← LiDAR 激光雷达系统
│   ├── Core/
│   │   ├── RDF_LidarSettings.c        参数与校验
│   │   ├── RDF_LidarTypes.c           数据结构（RDF_LidarSample）
│   │   ├── RDF_LidarScanner.c         扫描器与射线构建
│   │   ├── RDF_LidarSampleStrategy.c  采样策略接口（均匀策略）
│   │   ├── RDF_HemisphereSampleStrategy.c
│   │   ├── RDF_ConicalSampleStrategy.c
│   │   ├── RDF_StratifiedSampleStrategy.c
│   │   ├── RDF_ScanlineSampleStrategy.c
│   │   └── RDF_SweepSampleStrategy.c  雷达扫掠策略
│   ├── Visual/
│   │   ├── RDF_LidarVisualSettings.c  可视化参数（含仅点云开关）
│   │   ├── RDF_LidarVisualizer.c      渲染与数据获取
│   │   ├── RDF_LidarColorStrategy.c   颜色策略接口与默认实现
│   │   ├── RDF_IndexColorStrategy.c
│   │   └── RDF_ThreeColorStrategy.c   近/中/远三段渐变
│   ├── Network/
│   │   ├── RDF_LidarNetworkAPI.c      网络同步基类
│   │   ├── RDF_LidarNetworkComponent.c Rpl 实现（服务器权威）
│   │   ├── RDF_LidarNetworkUtils.c    网络辅助工具
│   │   └── RDF_LidarNetworkScanner.c  网络扫描适配器
│   ├── Util/
│   │   ├── RDF_LidarSubjectResolver.c 解析扫描主体（玩家/载具）
│   │   ├── RDF_LidarExport.c          CSV 导出
│   │   ├── RDF_LidarSampleUtils.c     统计与过滤
│   │   └── RDF_LidarScanCompleteHandler.c 扫描完成回调基类
│   └── Demo/
│       ├── RDF_LidarAutoBootstrap.c   统一 bootstrap（默认关闭）
│       ├── RDF_LidarAutoRunner.c      演示唯一入口
│       ├── RDF_LidarDemoConfig.c      配置与预设工厂
│       ├── RDF_LidarDemo_Cycler.c     策略轮换
│       └── RDF_LidarDemoStatsHandler.c 内置统计回调
│
└── Radar/                          ← 电磁波雷达系统
    ├── Core/
    │   ├── RDF_EMWaveParameters.c     电磁波物理参数
    │   ├── RDF_RadarSample.c          雷达回波数据（继承 RDF_LidarSample）
    │   ├── RDF_RadarSettings.c        雷达扫描器配置（继承 RDF_LidarSettings）
    │   └── RDF_RadarScanner.c         雷达扫描核心（继承 RDF_LidarScanner）
    ├── Physics/
    │   ├── RDF_RadarPropagation.c     传播损耗：FSPL / 大气衰减 / 雨衰
    │   ├── RDF_RCSModel.c             目标 RCS 模型（解析 + 估算 + 地物）
    │   ├── RDF_RadarEquation.c        雷达方程与 SNR 计算
    │   └── RDF_DopplerProcessor.c     多普勒频移 + MTI 处理
    ├── Modes/
    │   └── RDF_RadarMode.c            工作模式：Pulse / CW / FMCW / PhasedArray
    ├── Visual/
    │   ├── RDF_RadarColorStrategy.c   四种颜色策略
    │   ├── RDF_RadarDisplay.c         PPI 显示 + A-Scope
    │   └── RDF_RadarSimpleDisplay.c   世界立柱标记 + ASCII 控制台地图
    ├── Advanced/
    │   └── RDF_SARProcessor.c         SAR 处理器（孔径积累）
    ├── ECM/
    │   └── RDF_JammingModel.c         电子对抗干扰模型
    ├── Classification/
    │   └── RDF_TargetClassifier.c     目标自动分类（5 类）
    ├── Demo/
    │   ├── RDF_RadarDemoConfig.c      五种预设配置工厂
    │   ├── RDF_RadarAutoRunner.c      雷达演示主驱动器（单例）
    │   ├── RDF_RadarDemoStatsHandler.c 控制台统计报告
    │   ├── RDF_RadarDemoCycler.c      预设自动轮换
    │   └── RDF_RadarAutoBootstrap.c   modded SCR_BaseGameMode 自启动
    ├── UI/
    │   └── RDF_RadarHUD.c             片上 HUD（CanvasWidget PPI 扫描图）
    ├── Tests/
    │   ├── RDF_RadarTests.c           单元测试
    │   └── RDF_RadarBenchmark.c       性能基准
    └── Util/
        └── RDF_RadarExport.c          CSV 数据导出
```

---

## 数据流

### LiDAR 数据流
```
RDF_LidarSettings
       ↓
RDF_LidarScanner.Scan(entity)
  ├── SampleStrategy.BuildDirection() × N
  ├── Trace(ray) → RDF_LidarSample
  └── 返回 array<ref RDF_LidarSample>
       ↓
RDF_LidarVisualizer.RenderWithSamples()   ← 可选，3D 点云/射线
       ↓
RDF_LidarScanCompleteHandler.OnScanComplete()  ← 可选回调
```

### 雷达数据流
```
RDF_RadarSettings + RDF_EMWaveParameters
       ↓
RDF_RadarScanner.Scan(entity)
  ├── 继承 Lidar 射线 Trace 流程
  └── ApplyRadarPhysics(sample) 对每个命中点：
        1. FSPL 计算（RDF_RadarPropagation）
        2. 大气衰减（RDF_RadarPropagation）
        3. 雨衰（RDF_RadarPropagation）
        4. RCS 估算（RDF_RCSModel）
        5. 雷达方程 → 接收功率（RDF_RadarEquation）
        6. 距离门限（m_MinRange）
        7. 噪声功率 kTBF → SNR（RDF_RadarEquation）
        8. 多普勒频移（RDF_DopplerProcessor）
        9. 检测门限判断
       10. 杂波过滤（可选 MTI）
       ↓
RDF_RadarWorldMarkerDisplay.DrawWorldMarkers()  ← 3D 立柱标记
RDF_RadarTextDisplay.PrintRadarMap()            ← ASCII 控制台地图
RDF_PPIDisplay / RDF_AScopeDisplay              ← 调试用 2D 显示
RDF_RadarHUD.OnScanComplete()                   ← 片上 CanvasWidget PPI HUD
```

---

## 继承关系

```
Managed
└── RDF_LidarScanCompleteHandler
    └── RDF_RadarHUD                  ← 实现 OnScanComplete，更新 HUD

RDF_LidarSettings
└── RDF_RadarSettings                 ← 新增 m_MinRange、m_SystemLossDB 等

RDF_LidarSample
└── RDF_RadarSample                   ← 新增 SNR、RCS、多普勒、相位等字段

RDF_LidarScanner
└── RDF_RadarScanner                  ← 重写 Scan()，加 ApplyRadarPhysics()

RDF_LidarAutoRunner
└── RDF_RadarAutoRunner               ← 雷达演示驱动器（单例）

modded SCR_BaseGameMode
└── (LiDAR bootstrap fields)
└── (Radar bootstrap fields)          ← RDF_RadarAutoBootstrap.c
```

---

## 扩展点

### 采样策略
```c
class MyStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int total, RDF_LidarSettings settings)
    {
        // 返回归一化方向向量
    }
}
// 注入：
scanner.SetSampleStrategy(new MyStrategy());
```

### 颜色策略（雷达）
```c
class MyColorStrategy : RDF_RadarColorStrategy
{
    override int BuildPointColorFromRadarSample(RDF_RadarSample s, RDF_LidarVisualSettings vs)
    {
        // 返回 ARGB int
    }
}
ppiDisplay.SetColorStrategy(new MyColorStrategy());
```

### 扫描完成回调
```c
class MyHandler : RDF_LidarScanCompleteHandler
{
    override void OnScanComplete(array<ref RDF_LidarSample> samples)
    {
        // 处理扫描结果
    }
}
RDF_RadarAutoRunner.SetScanCompleteHandler(new MyHandler());
```

### 自定义工作模式
```c
class MyMode : RDF_RadarMode
{
    override void ApplyMode(RDF_RadarSample sample, RDF_RadarSettings settings)
    {
        // 修改 sample 字段
    }
}
scanner.SetRadarMode(new MyMode());
```

---

## HUD 实现说明（无 .layout 文件）

Enfusion 的 `.layout` 文件是**二进制格式**，只能通过 Workbench 可视化编辑器创建。  
`RDF_RadarHUD` 完全使用脚本动态构建所有控件：

```c
// 创建背景面板
Widget bg = ws.CreateWidgetInWorkspace(
    WidgetType.FrameWidgetTypeID,
    x, y, w, h,
    0,    // flags（0 = 默认）
    null, // Color（null = 不透明白色，之后用 SetColorInt 覆盖）
    90    // z-order（背景在底层）
);
bg.SetColorInt(ARGB(210, 0, 12, 7));
bg.SetVisible(true);

// 创建 CanvasWidget PPI 扫描图
CanvasWidget canvas = CanvasWidget.Cast(ws.CreateWidgetInWorkspace(
    WidgetType.CanvasWidgetTypeID, x, y, 210, 210, 0, null, 92));
canvas.SetSizeInUnits(Vector(210, 210, 0));  // 单位 1:1 像素

// 绘制圆形（TessellateCircle + PolygonDrawCommand）
array<float> verts = new array<float>();
canvas.TessellateCircle(Vector(105, 105, 0), 100.0, 48, verts);
PolygonDrawCommand disc = new PolygonDrawCommand();
disc.m_iColor   = ARGB(240, 0, 18, 9);
disc.m_Vertices = verts;
drawCmds.Insert(disc);
canvas.SetDrawCommands(drawCmds);
```

**关键约束**：
- `SetDrawCommands` 只保存指针，调用者必须持有 `drawCmds` 数组的引用（成员变量）
- 目标光点每次扫描重建，静态背景命令保持不变，避免重复分配
- 坐标系：`delta[0]` = 东（右），`delta[2]` = 北（上，负 screenY）

---

## Enfusion Script 关键约束

| 约束 | 正确写法 |
|------|---------|
| 无三目运算符 | `if/else` 替代 `? :` |
| 无函数式类型转换 | `(float)x` 替代 `float(x)` |
| 无 `string.ToLower()` | 直接检查大写或小写字面量 |
| 无 `enum` 整数转换 | 预填 `EMyEnum[]` 数组查表 |
| `string.Format` 用位置说明符 | `%1 %2` 替代 `%d %.1f` |
| `.ToString()` 不能在临时表达式调用 | 先赋给局部变量再 `.ToString()` |
| `out` 是关键字 | 局部变量改名，如 `outSamples` |
| 非 ASCII 字符报语法错误 | 全文件保持纯 ASCII |

---

## 性能建议

- 雷达：降低 `m_RayCount`（默认 512）或增大 `m_MinTickInterval`
- LiDAR：减少 `m_RayCount` 或 `m_RaySegments`
- HUD：节流间隔 `UPDATE_INTERVAL`（默认 0.5s）防止过度重绘
- CanvasWidget `SetDrawCommands`：每次调用重建动态命令，静态命令缓存复用

---

## 开发与贡献指南

- 代码风格：Google C++ Style（与项目现有约定一致）
- 文件放在对应模块目录
- PR 说明：变更动机、兼容性影响、性能影响
- 测试：`RDF_RadarTests.c` 中提供单元测试；`RDF_RadarBenchmark.c` 提供基准
