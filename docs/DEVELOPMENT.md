# RDF — 开发者指南（LiDAR）

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Contact: 747384120@qq.com  
License: Apache-2.0

---

## 设计原则

- 框架默认**静默**，只有在明确启用时才运行，不干扰其他模组。
- 扫描核心、可视化与演示驱动相互隔离，可独立替换。
- 对外暴露稳定的扩展接口（采样策略、颜色策略、扫描完成回调）。

---

## 模块布局

```
scripts/Game/RDF/
└── Lidar/
    ├── Core/
    │   ├── RDF_LidarSettings.c        参数与校验
    │   ├── RDF_LidarTypes.c           数据结构（RDF_LidarSample）
    │   ├── RDF_LidarScanner.c         扫描器与射线构建
    │   ├── RDF_LidarSampleStrategy.c  采样策略接口（均匀策略）
    │   ├── RDF_HemisphereSampleStrategy.c
    │   ├── RDF_ConicalSampleStrategy.c
    │   ├── RDF_StratifiedSampleStrategy.c
    │   ├── RDF_ScanlineSampleStrategy.c
    │   └── RDF_SweepSampleStrategy.c  扇区扫掠策略
    ├── Visual/
    │   ├── RDF_LidarVisualSettings.c  可视化参数（含仅点云开关）
    │   ├── RDF_LidarVisualizer.c      渲染与数据获取
    │   ├── RDF_LidarColorStrategy.c   颜色策略接口与默认实现
    │   ├── RDF_LidarMaterialColorStrategy.c  按密度 g/cm³ 着色、透明度随距离
    │   ├── RDF_IndexColorStrategy.c
    │   └── RDF_ThreeColorStrategy.c   近/中/远三段渐变
    ├── Network/
    │   ├── RDF_LidarNetworkAPI.c      网络同步基类
    │   ├── RDF_LidarNetworkComponent.c Rpl 实现（服务器权威）
    │   ├── RDF_LidarNetworkUtils.c    网络辅助工具
    │   ├── RDF_LidarNetworkScanner.c  网络扫描适配器
    │   └── RDF_LidarNetworkSetupExample.c 网络接入示例（ScriptComponent）
    ├── Util/
    │   ├── RDF_LidarSubjectResolver.c 解析扫描主体（玩家/载具）
    │   ├── RDF_LidarExport.c          CSV 导出
    │   ├── RDF_LidarSampleUtils.c     统计与过滤
    │   └── RDF_LidarScanCompleteHandler.c 扫描完成回调基类
    └── Demo/
        ├── RDF_LidarAutoBootstrap.c   统一 bootstrap（默认关闭）
        ├── RDF_LidarAutoRunner.c      演示唯一入口
        ├── RDF_LidarDemoConfig.c      配置与预设工厂
        ├── RDF_LidarDemo_Cycler.c     策略轮换
        └── RDF_LidarDemoStatsHandler.c 内置统计回调
```

---

## 数据流

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
RDF_LidarScanCompleteHandler.OnScanComplete()  ← 可选回调（如 RDF_LidarHUD）
```

---

## 烟雾遮挡

启用 `m_TraceSmokeOcclusion` 后，采用双 Trace 区分烟雾与实体：若 visibility occluder（烟雾）先于实体被命中，则该射线视为未命中（无有效回波），烟雾及后方区域均不返回数据。

```c
// 直接启用
scanner.GetSettings().m_TraceSmokeOcclusion = true;
scanner.GetSettings().Validate();

// Demo 预设
RDF_LidarDemoConfig cfg = RDF_LidarDemoConfig.CreateDefault(256);
cfg.m_TraceSmokeOcclusion = true;
RDF_LidarAutoRunner.StartWithConfig(cfg);

// 运行时切换
RDF_LidarAutoRunner.SetDemoTraceSmokeOcclusion(true);
```

实现方式：双 Trace（带 VISIBILITY 与纯几何）对比 hitFraction，若烟雾先被命中则视为未命中。优化：第一次 Trace 无命中时跳过第二次 Trace（开阔/天空等场景节省约半额 Trace 调用）。原版烟雾弹已验证有效。

---

## 扩展点

### 采样策略
```c
class MyStrategy : RDF_LidarSampleStrategy
{
    override vector BuildDirection(int index, int count)
    {
        // 返回归一化方向向量
    }
}
// 注入：
scanner.SetSampleStrategy(new MyStrategy());
```

### 颜色策略
```c
class MyColorStrategy : RDF_LidarColorStrategy
{
    override int BuildPointColor(float dist, bool hit, float lastRange, RDF_LidarVisualSettings settings)
    {
        // 返回 ARGB int
    }
    override int BuildRayColorAtT(float t, bool hit, RDF_LidarVisualSettings settings)
    {
        // 返回 ARGB int
    }
}
visualizer.SetColorStrategy(new MyColorStrategy());
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
RDF_LidarAutoRunner.SetScanCompleteHandler(new MyHandler());
```

---

## HUD 实现说明（无 .layout 文件）

Enfusion 的 `.layout` 文件是**二进制格式**，只能通过 Workbench 可视化编辑器创建。  
`RDF_LidarHUD` 完全使用脚本动态构建所有控件：

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

- 降低 `m_RayCount`（默认 512）或增大 `m_MinTickInterval`
- 减少 `m_RaySegments` 可降低绘制开销
- HUD：节流间隔 `UPDATE_INTERVAL`（默认 0.5s）防止过度重绘
- CanvasWidget `SetDrawCommands`：每次调用重建动态命令，静态命令缓存复用

详见 [OPTIMIZATION_AND_MEMORY.md](OPTIMIZATION_AND_MEMORY.md)。

---

## 开发与贡献指南

- 代码风格：Google C++ Style（与项目现有约定一致）
- 文件放在对应模块目录
- PR 说明：变更动机、兼容性影响、性能影响
