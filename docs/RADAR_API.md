> **Languages / 语言**: [English](#english) · [中文](#中文)

# RDF Radar — Public Sensor API

## English

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Audience: mod authors who want plots / tracks / WLR without wiring physics internals.

Related: [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md) · [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md) · [API.md](API.md)

---

## One sentence

Use **`RDF_RadarSensor`**: configure a mode → tick / scan → read **plots** and **tracks** (optional WLR fixes).

Do **not** call `RDF_RadarBallistics`, `RDF_RadarClutterModel`, or the scatterer registry from gameplay code unless you are extending the framework.

---

## Layers (what you touch)

| Layer | Type | Touch? |
|-------|------|--------|
| **Sensor facade** | `RDF_RadarSensor` | **Yes — primary** |
| **Presentation** | `RDF_RadarAutoRunner` / HUD / Visualizer | Optional demo |
| **Entity attach** | `RDF_RadarComponent` | Vehicles / placed radars |
| **Domain** | Scanner / Tracker / Ballistics / DEM / EW | Framework only |
| **Adapters** | Network / Entity / DEM tiles | Framework only |

```text
Mod code
   └─ RDF_RadarSensor.ConfigureMode / Tick / GetPlots / GetTracks
         ├─ WorldModel   (scatterers / emitters)
         ├─ Channel      (equation, clutter, EW, CFAR, measurement)
         ├─ Tracker      (α-β, PredictAt, WLR + DEM ground)
         └─ Terrain      (DEM cache → GetSurfaceY fallback)
```

---

## Product modes

```c
enum ERDF_RadarSensorMode
{
    RDF_RADAR_MODE_SEARCH,  // sector search, vehicles + shells + emitters
    RDF_RADAR_MODE_STARE,   // wide stare, no mechanical scan
    RDF_RADAR_MODE_WLR,     // projectile focus + ballistic WLR + DEM ground
    RDF_RADAR_MODE_ESM      // receive-only: emitters via one-way Friis; silent platform
}
```

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR, 128);

// Or attach to an entity:
RDF_RadarComponent radar = ...;
radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH);
```

Preset factories (same settings objects):

- `RDF_RadarSensor.CreateSearchSettings`
- `RDF_RadarSensor.CreateStareSettings`
- `RDF_RadarSensor.CreateWlrSettings`
- `RDF_RadarSensor.CreateEsmSettings`
- `RDF_RadarDemoConfig.CreateSearch` / `CreateStare` / `CreateWlr` / `CreateEsm`

### ESM + anti-radiation aim (no weapon prefab)

```c
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM, 64);
// ... Tick each frame ...
IEntity ent;
vector aim;
bool radiating;
if (sensor.GetArmAim(ent, aim, radiating) && radiating)
{
    // Weapon / seeker reads aim; when the emitter goes silent, GetArmAim fails
    // and the lock is dropped (SetArmRequireLiveEmitter).
}

// Manual pick from an emitter track id:
sensor.LockArmTrackId(trackId);
```

Also on `RDF_RadarComponent`: `GetArmAim` / `LockArmTrackId`.  
Registry helper: `RDF_RadarEmitterRegistry.IsEmitting(entity)`.  
Regression (standalone): `RDF_RadarEsmArmAutoTest.Start()`.

`SEARCH` enables `m_EnableEsmReceive` so other radars that are emitting use Friis \(R^2\), not skin \(R^4\). Emitter entity links are kept when ESM receive is on so ARM can resolve RF.

### RWR — being searched / tracked / locked

Active radars (SEARCH / STARE / WLR) publish threats into `RDF_RadarRwr` after each dwell. Victims query:

```c
// Any entity (e.g. player vehicle):
if (RDF_RadarRwr.HasLockWarning(myEntity))
{
    // audio / HUD spike
}
else if (RDF_RadarRwr.HasSearchWarning(myEntity))
{
    // softer search tone
}

array<ref RDF_RadarRwrThreat> threats = new array<ref RDF_RadarRwrThreat>();
RDF_RadarRwr.CollectForVictim(myEntity, threats);
Print(RDF_RadarRwr.GetStatusShort(myEntity)); // RWR CLEAR / SEARCH / TRACK / LOCK

// On RDF_RadarComponent attached to the victim platform:
if (radarComp.HasRwrLockWarning()) { ... }
```

Levels: `RDF_RWR_SEARCH` (detected plot) → `RDF_RWR_TRACK` (confirmed track) → `RDF_RWR_LOCK` (lock manager).  
ESM mode does not report (receive-only). Disable with `m_EnableRwrReporting = false`.  
Regression: `RDF_RadarRwrAutoTest.Start()`.

Sensor convenience (same semantics, any victim entity):

```c
sensor.HasRwrSearchWarning(victim);
sensor.HasRwrTrackWarning(victim);
sensor.HasRwrLockWarning(victim);
sensor.GetRwrStatusShort(victim);
sensor.CollectRwrThreats(victim, threats);
```

---

## Minimal usage

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);

IEntity subject = ...; // radar platform
sensor.Tick(subject, null);

array<ref RDF_RadarTarget> plots = sensor.GetPlots();
array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
RDF_RadarScanContext ctx = sensor.GetScanContext();

Print(sensor.GetStatusShort());
// e.g. SEARCH | DEM OK | plots=3 tracks=2 wlr=0 | reuse=… | ew=0 | LOCK …
```

### Callbacks

```c
class MyHandler : RDF_RadarScanCompleteHandler
{
    override void OnScanComplete(
        RDF_RadarSensor sensor,
        array<ref RDF_RadarTarget> plots,
        RDF_RadarProjectileTracker tracker,
        RDF_RadarScanContext context)
    {
        // React to each completed dwell.
    }
}

sensor.SetScanCompleteHandler(new MyHandler());
```

### Custom measurement / error model

Runs **after CFAR, before Tracker / WLR**, so noise affects tracks and weapon locate.
Default is CRLB + quantization (`RDF_RadarMeasurement`). Downstream mods override for gameplay feel.

```c
class MyGameplayNoise : RDF_RadarMeasurementModel
{
    override void SynthesizePlot(
        RDF_RadarTarget target,
        vector radarOrigin,
        RDF_RadarHardware hardware,
        RDF_RadarSettings settings)
    {
        // Optional: keep stock quantization / CRLB first.
        super.SynthesizePlot(target, radarOrigin, hardware, settings);

        // Then add a σ floor, bias table, weather, etc. Rewrite
        // target.m_Distance / m_AzimuthDeg / m_ElevationDeg / m_Position.
    }
}

sensor.SetMeasurementModel(new MyGameplayNoise());
// sensor.SetMeasurementModel(null); // restore default
```

Also assignable on settings: `cfg.m_MeasurementModel = new MyGameplayNoise();`

`m_EnableMeasurementSynthesis = false` disables the whole stage (ideal God-mode plots).

### Weather coupling

- **Wind** (already): ballistics / WLR via `RDF_RadarBallistics.SampleGlobalWind()`.
- **Rain / fog loss** (optional): `m_EnableWeatherDrivenRainLoss` samples
  `GetRainIntensity` / `GetFogAmount` once per scan and adds
  `intensity * m_RainLossDbPerKmAtFullIntensity` (+ fog term) on top of the
  manual `m_RainLossDbPerKmOneWay` floor. Enabled by `ApplyRealisticChannel()`.

---

## Read model (contracts)

| API | Returns |
|-----|---------|
| `GetPlots()` | Measurement plots (`RDF_RadarTarget`) after CFAR / synthesis |
| `GetTracks()` | Confirmed / tentative tracks |
| `GetScanContext()` | Origin, forward, range, serial, mode, network flag |
| `CountDetectedPlots()` / `CountConfirmedTracks()` / `CountWlrFixes()` | Counters |

WLR launch / impact live on each projectile track: `track.m_LastWlrFix`.

Default synthesis clears entity identity (`m_KeepEntityTruth = false`). Plots are sensor measurements, not God-mode transforms.

---

## Lock layer (search → acquire → track)

`RDF_RadarLockManager` turns tracks into a single **current locked target** with a
state machine and break-lock rules. The Sensor drives it every scan; read the
locked target from weapon / fire-control code.

```c
enum ERDF_RadarLockState
{
    RDF_RADAR_LOCK_SEARCH,     // no target held; optionally auto-acquiring
    RDF_RADAR_LOCK_ACQUIRING,  // candidate held, waiting for confirm hits
    RDF_RADAR_LOCK_TRACKING,   // stable lock
    RDF_RADAR_LOCK_COAST       // target briefly missing; dead-reckoning before drop
}
```

```c
RDF_RadarLockManager lockMgr = sensor.GetLockManager();
lockMgr.SetAutoAcquire(true);                 // auto-lock nearest eligible
lockMgr.SetTypeFilter(true, false, false);    // vehicles only
lockMgr.SetMaxLockRange(4000.0);              // 0 = use scan range
lockMgr.SetLockSector(0.0);                   // 0 = no sector gating
lockMgr.SetAcquireHits(2);                    // hits before TRACKING
lockMgr.SetCoastMaxSec(2.0);                  // coast before dropping

// Manual pick (e.g. HUD blip): lockMgr.LockTrackId(id); lockMgr.Unlock();

// Each frame in weapon code:
IEntity target;
vector aimPos;
if (sensor.GetLockedTarget(target, aimPos))
{
    // drive guidance toward aimPos (dead-reckoned while coasting)
}
```

`GetStatusShort()` appends scan reuse, EW burn-through (`ewJN` / `Reff`, or
`ew=0`), and lock state, e.g. `ewJN=12.3dB Reff=1800m | LOCK TRACKING id=3 …`.

Entity component convenience: `RDF_RadarComponent.LockTrackId`, `Unlock`,
`GetLockedTarget`, `LockArmTrackId`, `GetArmAim`, `GetLockManager`.

Demo / regression: `RDF_RadarLockAutoTest.Start()` spawns a moving vehicle and
verifies SEARCH → ACQUIRING → TRACKING with a usable aim point.
Rocket lock-fire: `RDF_RadarRocketLockFireAutoTest.Start()` (Hydra70 + ARH-style
boost / midcourse / terminal PN toward locked Mi-8; standalone, not in
`StartAll`). Guidance: `RDF_RadarRocketGuidance.Update` +
`RDF_RadarRocketGuidanceState`.
Heli duel / missile intercept: `RDF_RadarHeliDuelAutoTest.Start()` (standalone).
Ground SAM engage (BTR vs 6 Mi-8): `RDF_RadarSamEngageAutoTest.Start()` (standalone).
ESM / ARM: `RDF_RadarEsmArmAutoTest.Start()` (standalone, not in `StartAll`).

---

## Demo shell vs Sensor

| Class | Role |
|-------|------|
| `RDF_RadarSensor` | Logic facade (portable) |
| `RDF_RadarComponent` | Entity component wrapping Sensor + emitter register |
| `RDF_RadarAutoRunner` | Workbench demo: Sensor + PPI HUD + debug shapes |

Tests and gameplay should prefer Sensor / Component. AutoRunner remains the editor convenience entry (`StartWithConfig`, `SetHudEnabled`).

Regression tests follow the same rule: configure via `sensor.Configure` /
`Create*Settings`, read via `GetPlots` / `GetTracks` / `GetLockedTarget` /
`Count*`, and clear suite state with `ResetSession` / `ClearDemCache`.
AutoRunner only hosts `Tick` + optional PPI.

```c
// Demo still works:
RDF_RadarAutoRunner.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_STARE);
RDF_RadarAutoRunner.GetSensor().GetStatusShort();
```

---

## Network

Pass `RDF_RadarNetworkAPI` into `Tick` / `ScanOnce` when present.  
权威端 `RDF_RadarNetworkComponent` 直接挂 `RDF_RadarSensor`。Prefab 需同挂 `RplComponent`。

`SetForceLocalScan(true)` forces the local channel (used by scripted regression tests so Workbench `RplComponent` does not divert into the network path).

### Method split (official-style)

| Data | Mechanism |
|------|-----------|
| Slow config (enabled, range, interval, sector, maxTargets, WLR flags, CFAR mode, SNR, Include*) | `[RplProp]` + `Replication.BumpMe()`; Proxy `onRpl` → apply to local Sensor |
| Emitting / silent | `[RplProp] m_IsEmitting`; Proxy updates local `EmitterRegistry` only |
| Client requests (config / scan / emitting) | `RplRpc` Reliable → `Server` |
| Scan summary (tracks + lock + meta) | `RplRpc` **Reliable** → `Broadcast` (`PackScanRpc`, plots omitted) |
| Scan plots (HUD) | `RplRpc` **Unreliable** → `Broadcast` (`PackScanPlotsRpc`, capped) |
| Scale knobs | caps, min broadcast interval, fingerprint skip, interest radius |
| Scan results (JIP) | `RplSave` / `RplLoad` + `ScriptBitWriter` / `ScriptBitReader` |
| HUD / Visualizer / DEM tiles | Local only |

### Live scan RPC layout

Aligned with stock scale patterns (summary Reliable, presentation Unreliable):

| Channel | Payload |
|---------|---------|
| Reliable summary | meta + capped confirmed tracks + WLR + lock (`includePlots=false`) |
| Unreliable plots | plot count + capped plot SoA (no tracks) |

Throttle / interest Attributes on `RDF_RadarNetworkComponent`: `m_MaxSyncedTracks`, `m_MaxSyncedPlots`, `m_MinReliableBroadcastIntervalS`, `m_MinPlotBroadcastIntervalS`, `m_SkipUnchangedSummary`, `m_InterestRadiusM`, `m_SyncPlotsUnreliable`.

Proxy `RDF_RadarSensor` path: ingest plots + **inject tracks/lock**, skip local Tracker/Lock recompute for that frame. Authority still runs the full local chain then broadcasts.

Not synced this sprint: full `Hardware` / `EwStack` / `MeasurementModel` objects, DEM tiles.

### Datalink & multi-radar fusion

Station-to-station sharing (not weapon midcourse uplink):

```c
RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
hub.SetIffResolver(new RDF_RadarDefaultIffResolver());
// After authority scans, NetworkComponent publishes confirmed tracks automatically.
array<ref RDF_RadarDatalinkTrack> near = new array<ref RDF_RadarDatalinkTrack>();
hub.GetTracksInRadius(origin, 8000.0, near);
array<ref RDF_RadarFusedTrack> fused = hub.GetFusedTracks();
Print(hub.GetStatusShort());
```

- `ERDF_RadarIff`: UNKNOWN / FRIEND / FOE / NEUTRAL (mod via `RDF_RadarIffResolver`)
- `RDF_RadarFusionService`: world-gate association + optional dual-station horizontal cross-fix
- Optional `RDF_RadarDatalinkComponent`: live 2-array Broadcast (`PackDatalinkRpc`) + `RplSave`/`RplLoad`
- Test: `RDF_RadarFusionAutoTest.Start()` (standalone)

Do **not** confuse with `RDF_RadarWeaponBridge.TryGetMidcourseAim` (missile uplink).

---

## Advanced escape hatches

Only if you are extending RDF itself:

- `GetScanner()` / `GetTracker()` / `GetSettings()`
- `Configure(RDF_RadarSettings)` for full knob control

Prefer the session helpers when a consumer only needs cleanup between scenarios:

- `ClearTracks()` / `ClearDemCache()` / `ResetSession()` / `GetDemStatusShort()`

Still prefer modes + a few settings overrides over rebuilding the pipeline.

### Showcase visuals (demo)

AutoRunner defaults to world-space showcase (not LiDAR-style point clouds):

- search **sector fan** + sweep edge
- **range rings**
- plot markers + phosphor **afterglow**
- **lock beam** cone while ACQUIRING / TRACKING / COAST
- projectile **track ribbon** + larger WLR launch/impact markers
- WLR uses multi-point vacuum ballistic fit + AirDrag ground intersect
  (min hits/span/RMS gates; EMA smooth); not apex-only anymore

```c
RDF_RadarAutoRunner.SetShowcaseVisuals(true);  // default
RDF_RadarAutoRunner.SetShowcaseVisuals(false); // minimal / gameplay
```

PPI HUD sits in the **bottom-right** (panel **144×184**, PPI canvas **128×128**,
compact, translucent), loaded from `UI/layouts/RDF/RadarPPI.layout` via
`CreateWidgets`.
Public API: `RDF_RadarHUD.Show` / `Hide` / `FeedScan` / `SetMode` / `SetDisplayRange`.

---

## 中文

仓库：https://github.com/ViVi141/Radar-Development-Framework  
读者：需要 plots / 航迹 / WLR、又不想直接接线物理内部的模组作者。

相关：[RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md) · [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md) · [API.md](API.md)

---

## 一句话

使用 **`RDF_RadarSensor`**：配置模式 → tick / 扫描 → 读取 **plots** 与 **航迹**（可选 WLR 解算）。

除非你在扩展框架本身，否则**不要**从玩法代码调用 `RDF_RadarBallistics`、`RDF_RadarClutterModel` 或散射体注册表。

---

## 分层（你该碰什么）

| 层 | 类型 | 是否触碰？ |
|-------|------|--------|
| **Sensor 门面** | `RDF_RadarSensor` | **是 — 首选** |
| **呈现** | `RDF_RadarAutoRunner` / HUD / Visualizer | 可选演示 |
| **实体挂载** | `RDF_RadarComponent` | 载具 / 放置雷达 |
| **领域** | Scanner / Tracker / Ballistics / DEM / EW | 仅框架内部 |
| **适配器** | Network / Entity / DEM tiles | 仅框架内部 |

```text
Mod code
   └─ RDF_RadarSensor.ConfigureMode / Tick / GetPlots / GetTracks
         ├─ WorldModel   (scatterers / emitters)
         ├─ Channel      (equation, clutter, EW, CFAR, measurement)
         ├─ Tracker      (α-β, PredictAt, WLR + DEM ground)
         └─ Terrain      (DEM cache → GetSurfaceY fallback)
```

---

## 产品模式

```c
enum ERDF_RadarSensorMode
{
    RDF_RADAR_MODE_SEARCH,  // sector search, vehicles + shells + emitters
    RDF_RADAR_MODE_STARE,   // wide stare, no mechanical scan
    RDF_RADAR_MODE_WLR,     // projectile focus + ballistic WLR + DEM ground
    RDF_RADAR_MODE_ESM      // receive-only: emitters via one-way Friis; silent platform
}
```

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR, 128);

// Or attach to an entity:
RDF_RadarComponent radar = ...;
radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH);
```

预设工厂（同一套 settings 对象）：

- `RDF_RadarSensor.CreateSearchSettings`
- `RDF_RadarSensor.CreateStareSettings`
- `RDF_RadarSensor.CreateWlrSettings`
- `RDF_RadarSensor.CreateEsmSettings`
- `RDF_RadarDemoConfig.CreateSearch` / `CreateStare` / `CreateWlr` / `CreateEsm`

### ESM + 反辐射瞄点（不含武器 prefab）

```c
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_ESM, 64);
// ... Tick each frame ...
IEntity ent;
vector aim;
bool radiating;
if (sensor.GetArmAim(ent, aim, radiating) && radiating)
{
    // Weapon / seeker reads aim; when the emitter goes silent, GetArmAim fails
    // and the lock is dropped (SetArmRequireLiveEmitter).
}

// Manual pick from an emitter track id:
sensor.LockArmTrackId(trackId);
```

`RDF_RadarComponent` 上同样有：`GetArmAim` / `LockArmTrackId`。  
注册表辅助：`RDF_RadarEmitterRegistry.IsEmitting(entity)`。  
回归（独立）：`RDF_RadarEsmArmAutoTest.Start()`。

`SEARCH` 会开启 `m_EnableEsmReceive`，使正在发射的其他雷达走 Friis \(R^2\)，而非蒙皮 \(R^4\)。开启 ESM 接收时会保留辐射源实体链接，以便 ARM 解析 RF。

### RWR — 被搜索 / 跟踪 / 锁定

主动雷达（SEARCH / STARE / WLR）在每次驻留后向 `RDF_RadarRwr` 发布威胁。受害方查询：

```c
// Any entity (e.g. player vehicle):
if (RDF_RadarRwr.HasLockWarning(myEntity))
{
    // audio / HUD spike
}
else if (RDF_RadarRwr.HasSearchWarning(myEntity))
{
    // softer search tone
}

array<ref RDF_RadarRwrThreat> threats = new array<ref RDF_RadarRwrThreat>();
RDF_RadarRwr.CollectForVictim(myEntity, threats);
Print(RDF_RadarRwr.GetStatusShort(myEntity)); // RWR CLEAR / SEARCH / TRACK / LOCK

// On RDF_RadarComponent attached to the victim platform:
if (radarComp.HasRwrLockWarning()) { ... }
```

等级：`RDF_RWR_SEARCH`（检出 plot）→ `RDF_RWR_TRACK`（确认航迹）→ `RDF_RWR_LOCK`（锁控管理器）。  
ESM 模式不报告（仅接收）。用 `m_EnableRwrReporting = false` 关闭。  
回归：`RDF_RadarRwrAutoTest.Start()`。

Sensor 便捷方法（语义相同，任意受害实体）：

```c
sensor.HasRwrSearchWarning(victim);
sensor.HasRwrTrackWarning(victim);
sensor.HasRwrLockWarning(victim);
sensor.GetRwrStatusShort(victim);
sensor.CollectRwrThreats(victim, threats);
```

---

## 最小用法

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);

IEntity subject = ...; // radar platform
sensor.Tick(subject, null);

array<ref RDF_RadarTarget> plots = sensor.GetPlots();
array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
RDF_RadarScanContext ctx = sensor.GetScanContext();

Print(sensor.GetStatusShort());
// e.g. SEARCH | DEM OK | plots=3 tracks=2 wlr=0 | reuse=… | ew=0 | LOCK …
```

### 回调

```c
class MyHandler : RDF_RadarScanCompleteHandler
{
    override void OnScanComplete(
        RDF_RadarSensor sensor,
        array<ref RDF_RadarTarget> plots,
        RDF_RadarProjectileTracker tracker,
        RDF_RadarScanContext context)
    {
        // React to each completed dwell.
    }
}

sensor.SetScanCompleteHandler(new MyHandler());
```

### 自定义测量 / 误差模型

在 **CFAR 之后、Tracker / WLR 之前**运行，因此噪声会影响航迹与武器定位。
默认是 CRLB + 量化（`RDF_RadarMeasurement`）。下游模组可 override 以调玩法手感。

```c
class MyGameplayNoise : RDF_RadarMeasurementModel
{
    override void SynthesizePlot(
        RDF_RadarTarget target,
        vector radarOrigin,
        RDF_RadarHardware hardware,
        RDF_RadarSettings settings)
    {
        // Optional: keep stock quantization / CRLB first.
        super.SynthesizePlot(target, radarOrigin, hardware, settings);

        // Then add a σ floor, bias table, weather, etc. Rewrite
        // target.m_Distance / m_AzimuthDeg / m_ElevationDeg / m_Position.
    }
}

sensor.SetMeasurementModel(new MyGameplayNoise());
// sensor.SetMeasurementModel(null); // restore default
```

也可在 settings 上赋值：`cfg.m_MeasurementModel = new MyGameplayNoise();`

`m_EnableMeasurementSynthesis = false` 关闭整段（理想上帝视角 plots）。

### 天气耦合

- **风**（已有）：弹道 / WLR 经 `RDF_RadarBallistics.SampleGlobalWind()`。
- **雨 / 雾损耗**（可选）：`m_EnableWeatherDrivenRainLoss` 每扫一次采样
  `GetRainIntensity` / `GetFogAmount`，在手动 `m_RainLossDbPerKmOneWay` 下限之上叠加
  `intensity * m_RainLossDbPerKmAtFullIntensity`（另加雾项）。由 `ApplyRealisticChannel()` 开启。

---

## 读模型（契约）

| API | 返回 |
|-----|---------|
| `GetPlots()` | CFAR / 合成后的量测 plots（`RDF_RadarTarget`） |
| `GetTracks()` | 确认 / 试探航迹 |
| `GetScanContext()` | 原点、前向、量程、序号、模式、网络标志 |
| `CountDetectedPlots()` / `CountConfirmedTracks()` / `CountWlrFixes()` | 计数器 |

WLR 发射 / 落点挂在每条弹道航迹上：`track.m_LastWlrFix`。

默认合成会清除实体身份（`m_KeepEntityTruth = false`）。Plots 是传感器量测，不是上帝视角变换。

---

## 锁定层（搜索 → 截获 → 跟踪）

`RDF_RadarLockManager` 将航迹收敛为单个**当前锁定目标**，带状态机与断锁规则。Sensor 每次扫描驱动它；武器 / 火控代码读取锁定目标。

```c
enum ERDF_RadarLockState
{
    RDF_RADAR_LOCK_SEARCH,     // no target held; optionally auto-acquiring
    RDF_RADAR_LOCK_ACQUIRING,  // candidate held, waiting for confirm hits
    RDF_RADAR_LOCK_TRACKING,   // stable lock
    RDF_RADAR_LOCK_COAST       // target briefly missing; dead-reckoning before drop
}
```

```c
RDF_RadarLockManager lockMgr = sensor.GetLockManager();
lockMgr.SetAutoAcquire(true);                 // auto-lock nearest eligible
lockMgr.SetTypeFilter(true, false, false);    // vehicles only
lockMgr.SetMaxLockRange(4000.0);              // 0 = use scan range
lockMgr.SetLockSector(0.0);                   // 0 = no sector gating
lockMgr.SetAcquireHits(2);                    // hits before TRACKING
lockMgr.SetCoastMaxSec(2.0);                  // coast before dropping

// Manual pick (e.g. HUD blip): lockMgr.LockTrackId(id); lockMgr.Unlock();

// Each frame in weapon code:
IEntity target;
vector aimPos;
if (sensor.GetLockedTarget(target, aimPos))
{
    // drive guidance toward aimPos (dead-reckoned while coasting)
}
```

`GetStatusShort()` 会追加扫描复用、EW 烧穿（`ewJN` / `Reff`，或 `ew=0`）与锁状态，例如 `ewJN=12.3dB Reff=1800m | LOCK TRACKING id=3 …`。

实体组件便捷方法：`RDF_RadarComponent.LockTrackId`、`Unlock`、
`GetLockedTarget`、`LockArmTrackId`、`GetArmAim`、`GetLockManager`。

演示 / 回归：`RDF_RadarLockAutoTest.Start()` 生成移动载具并验证
SEARCH → ACQUIRING → TRACKING 且瞄点可用。
火箭锁射：`RDF_RadarRocketLockFireAutoTest.Start()`（Hydra70 + ARH 风格
助推 / 中段 / 末段 PN 指向锁定 Mi-8；独立，不进 `StartAll`）。制导：
`RDF_RadarRocketGuidance.Update` + `RDF_RadarRocketGuidanceState`。
机战 / 拦截导弹：`RDF_RadarHeliDuelAutoTest.Start()`（独立）。
地面 SAM 交战（BTR vs 6×Mi-8）：`RDF_RadarSamEngageAutoTest.Start()`（独立）。
ESM / ARM：`RDF_RadarEsmArmAutoTest.Start()`（独立，不进 `StartAll`）。

---

## Demo 外壳 vs Sensor

| 类 | 角色 |
|-------|------|
| `RDF_RadarSensor` | 逻辑门面（可移植） |
| `RDF_RadarComponent` | 包装 Sensor + 辐射源登记的实体组件 |
| `RDF_RadarAutoRunner` | Workbench 演示：Sensor + PPI HUD + debug shapes |

测试与玩法应优先 Sensor / Component。AutoRunner 仍是编辑器便捷入口（`StartWithConfig`、`SetHudEnabled`）。

回归测试同一规则：用 `sensor.Configure` / `Create*Settings` 配置，用
`GetPlots` / `GetTracks` / `GetLockedTarget` / `Count*` 读取，用
`ResetSession` / `ClearDemCache` 清理套件状态。AutoRunner 只托管 `Tick` + 可选 PPI。

```c
// Demo still works:
RDF_RadarAutoRunner.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_STARE);
RDF_RadarAutoRunner.GetSensor().GetStatusShort();
```

---

## 网络

有 `RDF_RadarNetworkAPI` 时传入 `Tick` / `ScanOnce`。  
权威端 `RDF_RadarNetworkComponent` 直接挂 `RDF_RadarSensor`。Prefab 需同挂 `RplComponent`。

`SetForceLocalScan(true)` 强制走本地通道（脚本回归测试使用，避免 Workbench `RplComponent` 拐进网络路径）。

### 方法拆分（官方风格）

| 数据 | 机制 |
|------|-----------|
| 慢配置（enabled、range、interval、sector、maxTargets、WLR flags、CFAR mode、SNR、Include*） | `[RplProp]` + `Replication.BumpMe()`；Proxy `onRpl` → 应用到本地 Sensor |
| 发射 / 静默 | `[RplProp] m_IsEmitting`；Proxy 只更新本地 `EmitterRegistry` |
| 客户端请求（config / scan / emitting） | `RplRpc` Reliable → `Server` |
| 扫描摘要（航迹 + 锁 + 元数据） | `RplRpc` **Reliable** → `Broadcast`（`PackScanRpc`，不含 plots） |
| 扫描 plots（HUD） | `RplRpc` **Unreliable** → `Broadcast`（`PackScanPlotsRpc`，有上限） |
| 规模旋钮 | 上限、最小广播间隔、指纹跳过、兴趣半径 |
| 扫描结果（JIP） | `RplSave` / `RplLoad` + `ScriptBitWriter` / `ScriptBitReader` |
| HUD / Visualizer / DEM tiles | 仅本地 |

### 实时扫描 RPC 布局

对齐官方规模模式（摘要 Reliable、观感 Unreliable）：

| 通道 | 载荷 |
|---------|---------|
| Reliable 摘要 | meta + 有上限确认航迹 + WLR + 锁（`includePlots=false`） |
| Unreliable plots | plot 数量 + 有上限 plot SoA（无航迹） |

`RDF_RadarNetworkComponent` 属性：`m_MaxSyncedTracks`、`m_MaxSyncedPlots`、`m_MinReliableBroadcastIntervalS`、`m_MinPlotBroadcastIntervalS`、`m_SkipUnchangedSummary`、`m_InterestRadiusM`、`m_SyncPlotsUnreliable`。

Proxy 上的 `RDF_RadarSensor` 路径：吞入 plots + **注入航迹/锁定**，该帧跳过本地 Tracker/Lock 重算。权威端仍跑完整本地链再广播。

本 sprint 不同步：完整 `Hardware` / `EwStack` / `MeasurementModel` 对象、DEM 瓦片。

### 数据链与多雷达融合

站间共享（不是武器中制导上行）：

```c
RDF_RadarDatalinkHub hub = RDF_RadarDatalinkHub.Get();
hub.SetIffResolver(new RDF_RadarDefaultIffResolver());
array<ref RDF_RadarDatalinkTrack> near = new array<ref RDF_RadarDatalinkTrack>();
hub.GetTracksInRadius(origin, 8000.0, near);
array<ref RDF_RadarFusedTrack> fused = hub.GetFusedTracks();
Print(hub.GetStatusShort());
```

- `ERDF_RadarIff`：UNKNOWN / FRIEND / FOE / NEUTRAL（模组通过 `RDF_RadarIffResolver`）
- `RDF_RadarFusionService`：世界坐标门关联 + 可选双站水平交会
- 可选 `RDF_RadarDatalinkComponent`：实时 2 数组 Broadcast（`PackDatalinkRpc`）+ `RplSave`/`RplLoad`
- 测试：`RDF_RadarFusionAutoTest.Start()`（独立）

勿与 `RDF_RadarWeaponBridge.TryGetMidcourseAim`（导弹上行）混淆。

---

## 高级逃生舱

仅在扩展 RDF 本身时使用：

- `GetScanner()` / `GetTracker()` / `GetSettings()`
- `Configure(RDF_RadarSettings)` 做全旋钮控制

若消费者只需场景间清理，优先会话辅助：

- `ClearTracks()` / `ClearDemCache()` / `ResetSession()` / `GetDemStatusShort()`

仍优先用模式 + 少量 settings 覆盖，而不是重建整条流水线。

### 展示视觉（演示）

AutoRunner 默认世界空间展示（非 LiDAR 式点云）：

- 搜索**扇区扇面** + 扫描边缘
- **距离环**
- plot 标记 + 磷光**余辉**
- ACQUIRING / TRACKING / COAST 时的**锁定波束**锥
- 弹道**航迹丝带** + 更大的 WLR 发射/落点标记
- WLR 使用多点真空弹道拟合 + AirDrag 地面交点
  （最小命中数/跨度/RMS 门限；EMA 平滑）；不再仅用顶点

```c
RDF_RadarAutoRunner.SetShowcaseVisuals(true);  // default
RDF_RadarAutoRunner.SetShowcaseVisuals(false); // minimal / gameplay
```

PPI HUD 位于**右下角**（面板 **144×184**，PPI 画布 **128×128**，
紧凑半透明），经 `CreateWidgets` 从 `UI/layouts/RDF/RadarPPI.layout` 加载。
公开 API：`RDF_RadarHUD.Show` / `Hide` / `FeedScan` / `SetMode` / `SetDisplayRange`。
