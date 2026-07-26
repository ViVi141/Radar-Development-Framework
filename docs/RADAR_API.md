# RDF Radar — Public Sensor API

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
    RDF_RADAR_MODE_WLR      // projectile focus + ballistic WLR + DEM ground
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
- `RDF_RadarDemoConfig.CreateSearch` / `CreateStare` / `CreateWlr`

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
// e.g. SEARCH | DEM OK | plots=3 tracks=2 wlr=0
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

`GetStatusShort()` appends the lock state, e.g. `LOCK TRACKING id=3 r=812m az=41`.

Entity component convenience: `RDF_RadarComponent.LockTrackId`, `Unlock`,
`GetLockedTarget`, `GetLockManager`.

Demo / regression: `RDF_RadarLockAutoTest.Start()` spawns a moving vehicle and
verifies SEARCH → ACQUIRING → TRACKING with a usable aim point.

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
权威端 `RDF_RadarNetworkComponent` 现在直接挂 `RDF_RadarSensor`，不再维护第二套 Scanner 配置。
`SetForceLocalScan(true)` forces the local channel (used by scripted regression tests so Workbench `RplComponent` does not divert into the network path).

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

```c
RDF_RadarAutoRunner.SetShowcaseVisuals(true);  // default
RDF_RadarAutoRunner.SetShowcaseVisuals(false); // minimal / gameplay
```

PPI HUD sits in the **bottom-right** corner at 128×128 (compact, translucent).
