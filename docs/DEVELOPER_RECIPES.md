> **Languages / 语言**: [中文](#中文) · [English](#english)

# RDF — 开发者食谱（可照做的示例）

面向模组作者的**操作示例**。API 契约见 [RADAR_API.md](RADAR_API.md)；内部链路见 [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md)；地形数据见 [DEM.md](DEM.md)。

---

## 中文

## 目录

1. [给实体挂上雷达并使用](#1-给实体挂上雷达并使用)
2. [更改雷达参数](#2-更改雷达参数)
3. [新地图：制作 HEIGHT + SURF 并让游戏识别](#3-新地图制作-height--surf-并让游戏识别)
4. [给某个 prefab 单独写 RCS 签名](#4-给某个-prefab-单独写-rcs-签名)

---

## 1. 给实体挂上雷达并使用

### 方式 A（推荐）：Workbench 挂 `RDF_RadarComponent`

1. 打开载具 / 放置物 prefab（或世界里的实体）。
2. 添加脚本组件 **`RDF_RadarComponent`**（分类通常在 `GameScripted/RDF`）。
3. 可选：同实体再挂 `RDF_RadarNetworkComponent`（联机同步）。
4. Play：组件在 `EOnInit` 里默认 `SEARCH`、每帧 `Tick`。

读结果（你的武器 / HUD 脚本）：

```c
RDF_RadarComponent radar = RDF_RadarComponent.Cast(
    vehicle.FindComponent(RDF_RadarComponent));
if (!radar || !radar.IsEnabled())
    return;

RDF_RadarSensor sensor = radar.GetSensor();
array<ref RDF_RadarTarget> plots = sensor.GetPlots();
array<ref RDF_RadarTrack> tracks = sensor.GetTracks();

IEntity locked;
vector aimPos;
if (radar.GetLockedTarget(locked, aimPos))
{
    // 制导瞄向 aimPos
}

Print(sensor.GetStatusShort());
// 例：SEARCH | SURF+H | plots=… tracks=… | reuse=… demBlk=… trace=…
```

开关与模式：

```c
radar.SetEnabled(true);
radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH);
// 反炮兵：RDF_RADAR_MODE_WLR
// 被动：RDF_RADAR_MODE_ESM（平台不登记为辐射源）
// 动目标：RDF_RADAR_MODE_PULSE_DOPPLER
```

### 方式 B：纯脚本门面（不挂组件）

适合临时测试 / AutoRunner：

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
sensor.SetEnabled(true);

// 每帧或按间隔：
IEntity subject = ...; // 雷达平台实体
sensor.Tick(subject, null);

array<ref RDF_RadarTarget> plots = sensor.GetPlots();
```

快速演示：Debugger / 控制台用 `RDF_RadarAutoRunner`（见 [RADAR_API.md](RADAR_API.md)）。

---

## 2. 更改雷达参数

预设工厂只给合理起点；细调请 **改 Settings 再 `Configure`**。

```c
RDF_RadarComponent radar = ...;
RDF_RadarSensor sensor = radar.GetSensor();

// 从 SEARCH 预设拷一份再改（不要直接改共享单例）
RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(96);

cfg.m_Range = 4000.0;                 // 量程 [m]
cfg.m_SectorHalfAngleDeg = 60.0;      // 半扇区 [deg]
cfg.m_UpdateInterval = 0.15;          // 驻留间隔 [s]
cfg.m_MaxLosTracesPerScan = 48;       // TraceMove 预算
cfg.m_OriginOffset = Vector(0, 2.5, 0); // 天线相对实体原点

cfg.m_IncludeVehicles = true;
cfg.m_IncludeProjectiles = true;
cfg.m_IncludeRadarEmitters = true;

cfg.m_EnableDemClutter = true;        // DEM 杂波进噪声分母
cfg.m_EnableDemLosPrecheck = true;    // HEIGHT 地形先挡再 Trace（默认已开）
cfg.m_EnableNlosMultipath = true;

// 硬件（功率 / PRF / 波束等）
if (cfg.m_Hardware)
{
    cfg.m_Hardware.m_PeakPowerW = 25000.0;
    cfg.m_Hardware.m_AntennaGainDb = 28.0;
}

cfg.Validate();
sensor.Configure(cfg);   // 立刻生效；下一 Tick 用新设置
```

只换模式、沿用预设：

```c
radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR);
// 或：
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER, 96);
```

按需保真（默认关，见 API）：

```c
cfg.EnableAtmosphericPathLoss(true);
cfg.EnableLosTwoRayMultipath();
cfg.SetMeasurementNoise(1.5, 2.0, 0.1, 0.05);
```

AutoTest 稳定用：`cfg.StabilizeForRegression();`（关掉可选保真噪声，**不是**玩法档位）。

---

## 3. 新地图：制作 HEIGHT + SURF 并让游戏识别

运行时用**世界文件名**当 key（无扩展名）。例：世界是 `worlds/MyIsland/GM_MyIsland.ent` → 目录必须是：

```text
DemData/GM_MyIsland/
  surf_manifest.json
  surf_chunks/…
  height_manifest.json    ← 可选但强烈建议（LOS 预检 + 免 GetSurfaceY）
  height_chunks/…
```

查找顺序：`$profile:RDF/DemData/<key>/` → 模组 `DemData/<key>/` → LIVE。

### 3.1 确认世界 key

Play 后看日志 / HUD：`world=GM_MyIsland`。或用路径规则：`GetWorldFile()` 末段去扩展名（`RDF_DemWorldKey`）。

### 3.2 打包 HEIGHT（官方 `.ttile`，与 `GetSurfaceY` 同源）

需要游戏解包地形：`worlds/<Stem>/Terrain/Terrain.terr` + `.Data/*.ttile`。  
`GM_MyIsland` → 地形目录名一般是 `MyIsland`（去掉 `GM_`）。

```powershell
cd "…\Radar-Development-Framework"

# 直接指定 Terrain 目录：
python tools\dem\rdf_dem_pack_height_json.py --world GM_MyIsland --terrain "D:\arma_reforger_full\worlds\MyIsland\Terrain"

# 或先解包再打：
python tools\dem\rdf_ttile_unpack.py "D:\arma_reforger_full\worlds\MyIsland\Terrain"
python tools\dem\rdf_dem_pack_height_json.py --world GM_MyIsland --from-ttile-npz
```

默认写出到模组旁 / profile 的 `DemData/GM_MyIsland/`（脚本会找 SURF 网格对齐；尚无 SURF 时用默认 2 m）。

### 3.3 打包 SURF（地表类）

**有 RDF 烘焙 / `.dem.data` 时（推荐有类）：**

1. Workbench Play 该图 → 烘焙 DEM（`BakeDemFull.flag` 或编辑器菜单）→ `$profile:RDF/DemData/GM_MyIsland/` V3 CSV。  
2. 再：

```powershell
python tools\dem\rdf_dem_pack_surface_json.py --world GM_MyIsland --from-bin
# 若网格与 HEIGHT 不一致：
python tools\dem\rdf_dem_pack_surface_json.py --world GM_MyIsland --match-height --from-bin
```

**还没有地表类源时（先让 HEIGHT 能挂上）：**

```powershell
python tools\dem\rdf_dem_pack_surface_json.py --world GM_MyIsland --match-height
```

会生成与 HEIGHT 同网格的 **UNKNOWN** SURF（杂波弱，但 `SURF+H` / LOS 预检可用）。有 bake 后再 `--from-bin` 重填类。

### 3.4 保存到模组并被识别

1. 把整份目录拷进**本模组根**：

```text
addons/Radar-Development-Framework/DemData/GM_MyIsland/
```

（或你的下游模组同样路径；需能被 Resource 看到。）

2. Workbench 重载 / 再 Play 该世界。  
3. 权威端约 2 s 后异步预热；日志应类似：

```text
[RDF DEM Runtime] using packaged SURF JSON: DemData/GM_MyIsland/
[RDF DEM Runtime] HEIGHT pack attached …
[RDF DEM Runtime] ready world=GM_MyIsland … mode=SURF … heightPack=1
```

HUD：`SURF+H`。若只有 SURF：`SURF RAM`；皆无：`LIVE`。

### 3.5 常见踩坑

| 现象 | 原因 |
|------|------|
| `world=X` 但读的是 `DemData/Y` | 目录名必须与 `.ent` 文件名一致 |
| 有 HEIGHT 无 SURF | HEIGHT 附着在 SURF 根上；至少 `--match-height` 出 UNKNOWN SURF |
| 仍 `liveY=1` | HEIGHT 未常驻 / 预热未完；等 flush 或查 `heightPack=` |
| 杂波全 UNKNOWN | 只用了 match-height；需 bake + `--from-bin` |

细节与体积：[DEM.md](DEM.md)。

---

## 4. 给某个 prefab 单独写 RCS 签名

签名按 **prefab `ResourceName` 全串** 索引（所有该模型实例共用一条），不是按实体 ID。

键示例：

```text
{259EE7B78C51B624}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469.et
```

### 4.1 工坊主表（推荐）

编辑模组内：

```text
Signatures/rdf_radar_signatures.conf
```

在 `m_aEntries` 增加或改一条（GUID 勿与现有冲突）：

```
RDF_RadarSignatureEntryConf "{C8A3F15E00ABCDEF}" {
 m_sKey "{你的GUID}Prefabs/…/YourVehicle.et"
 m_fSizeX 2.5
 m_fSizeY 2.0
 m_fSizeZ 5.5
 m_fCharLengthM 5.5
 m_fMeanRcsM2 12.0
 m_iSwerling 1
 m_iTypeHint 0
 // 直升机可选微多普勒：
 // m_fRotorTipSpeedMs 220
 // m_iBladeCount 2
 // m_fRotorRcsFraction 0.35
}
```

Workbench 保存后确认 Resource Browser 已 Register（已有 `.meta`：`{C8A3F15E902B47D1}`）。  
下次进游戏日志：`[RDF Radar Sig] baked conf loaded: …`。

### 4.2 从 CSV 批量再打包

开发可先维护 CSV，再**打进工坊 conf**（运行时优先加载 conf；有 conf 时不会再读 profile CSV）：

```text
$profile:RDF/Signatures/rdf_radar_signatures.csv
```

表头见库常量 `RDF_RADAR_SIG_V2`（含 `mean_rcs_m2`、旋翼列等）。改完后：

```powershell
python tools\dem\rdf_sig_pack_conf.py
```

把生成的 conf 覆盖进模组 `Signatures/`，并保留/更新 `.meta`。

### 4.3 运行时自动测量（兜底）

表里没有的模型：首次被扫描时会 **现场测 AABB → 估 RCS**，并可写回 profile CSV。第三方模组载具可先靠自动测量，再把满意的行固化进 conf。

查某实体键：

```c
string key = RDF_RadarSignatureLibrary.MakeKey(entity);
RDF_RadarSignature sig = RDF_RadarSignatureLibrary.Find(key);
if (sig)
    Print("meanRcs=" + sig.m_MeanRcsM2.ToString());
```

### 4.4 注意

- **改错 `m_sKey`**（少 GUID、路径大小写不同）→ 找不到行 → 走测量/默认。  
- 旋翼边带：填 tip / blades；空则直升机路径可能套家族默认。  
- 不要在 AutoTest 里为通过而人为抬目标 RCS（回归约定）。

---

## 还想看什么

| 主题 | 文档 |
|------|------|
| 锁定 / 武器 | [VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md) |
| 通视缓存 / DEM 预检 | [RADAR_API.md](RADAR_API.md) § 扫描通视 |
| AutoTest / DemLosBench | [AUTOTEST_CI_LIMITS.md](AUTOTEST_CI_LIMITS.md) |
| 工具脚本索引 | [tools/README.md](../tools/README.md) |

---

## English

# RDF — Developer recipes (copy-paste examples)

Practical recipes for mod authors. Contracts: [RADAR_API.md](RADAR_API.md). Internals: [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md). Terrain: [DEM.md](DEM.md).

## Contents

1. [Attach radar to an entity](#1-attach-radar-to-an-entity)
2. [Change radar parameters](#2-change-radar-parameters)
3. [New world: HEIGHT + SURF packs](#3-new-world-height--surf-packs)
4. [Per-prefab RCS signature](#4-per-prefab-rcs-signature)

---

## 1. Attach radar to an entity

### A (recommended): Workbench `RDF_RadarComponent`

1. Open the vehicle / placeable prefab.
2. Add script component **`RDF_RadarComponent`**.
3. Optionally add `RDF_RadarNetworkComponent` for MP.
4. On Play, init uses `SEARCH` and ticks every frame.

```c
RDF_RadarComponent radar = RDF_RadarComponent.Cast(
    vehicle.FindComponent(RDF_RadarComponent));
if (!radar || !radar.IsEnabled())
    return;

RDF_RadarSensor sensor = radar.GetSensor();
array<ref RDF_RadarTarget> plots = sensor.GetPlots();
array<ref RDF_RadarTrack> tracks = sensor.GetTracks();

IEntity locked;
vector aimPos;
if (radar.GetLockedTarget(locked, aimPos))
{
    // guide toward aimPos
}
```

```c
radar.SetEnabled(true);
radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH);
```

### B: Sensor façade only

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
sensor.Tick(subject, null);
```

---

## 2. Change radar parameters

```c
RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(96);
cfg.m_Range = 4000.0;
cfg.m_SectorHalfAngleDeg = 60.0;
cfg.m_UpdateInterval = 0.15;
cfg.m_MaxLosTracesPerScan = 48;
cfg.m_OriginOffset = Vector(0, 2.5, 0);
cfg.m_EnableDemClutter = true;
cfg.m_EnableDemLosPrecheck = true;
if (cfg.m_Hardware)
{
    cfg.m_Hardware.m_PeakPowerW = 25000.0;
    cfg.m_Hardware.m_AntennaGainDb = 28.0;
}
cfg.Validate();
sensor.Configure(cfg);
```

Mode presets: `SetMode` / `ConfigureMode`. Optional fidelity helpers: see RADAR_API.

---

## 3. New world: HEIGHT + SURF packs

Folder key = world `.ent` basename (e.g. `GM_MyIsland.ent` → `DemData/GM_MyIsland/`).

```powershell
python tools\dem\rdf_dem_pack_height_json.py --world GM_MyIsland --terrain "D:\…\worlds\MyIsland\Terrain"
python tools\dem\rdf_dem_pack_surface_json.py --world GM_MyIsland --match-height
# later, after bake:
python tools\dem\rdf_dem_pack_surface_json.py --world GM_MyIsland --match-height --from-bin
```

Copy into mod `DemData/GM_MyIsland/`. Expect log `mode=SURF` `heightPack=1` and HUD `SURF+H`.

Lookup: `$profile:RDF/DemData/<key>/` then mod `DemData/<key>/`.

---

## 4. Per-prefab RCS signature

Keyed by full prefab `ResourceName`. Edit `Signatures/rdf_radar_signatures.conf` (`m_sKey`, `m_fMeanRcsM2`, extents, optional rotor fields). Or edit `$profile:RDF/Signatures/rdf_radar_signatures.csv` and run `python tools\dem\rdf_sig_pack_conf.py` to **rebuild the workshop conf** (runtime loads conf first; profile CSV is only a fallback if conf fails to load).

```c
string key = RDF_RadarSignatureLibrary.MakeKey(entity);
RDF_RadarSignature sig = RDF_RadarSignatureLibrary.Find(key);
```

Unknown prefabs are measured on first sighting and may export to profile CSV.

---

## See also

| Topic | Doc |
|-------|-----|
| Lock / weapons | [VEHICLE_RADAR_LOCK_GUIDE.md](VEHICLE_RADAR_LOCK_GUIDE.md) |
| LOS / DEM precheck | [RADAR_API.md](RADAR_API.md) § Scan LOS |
| AutoTest | [AUTOTEST_CI_LIMITS.md](AUTOTEST_CI_LIMITS.md) |
| Tools | [tools/README.md](../tools/README.md) |
