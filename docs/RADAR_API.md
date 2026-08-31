> **Languages / 语言**: [English](#english) · [中文](#中文)

# RDF Radar — Public Sensor API

## English

Repository: https://github.com/ViVi141/Radar-Development-Framework  
Audience: mod authors who want plots / tracks / WLR without wiring physics internals.

Related: [RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md) · [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md) · [API.md](API.md) · **Recipes: [DEVELOPER_RECIPES.md](DEVELOPER_RECIPES.md)** · **Recipes: [DEVELOPER_RECIPES.md](DEVELOPER_RECIPES.md)**

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
    RDF_RADAR_MODE_SEARCH,         // sector search; MTI off by default (stationary vehicles)
    RDF_RADAR_MODE_STARE,          // wide stare, no mechanical scan
    RDF_RADAR_MODE_WLR,            // projectile focus + ballistic WLR + DEM ground
    RDF_RADAR_MODE_ESM,            // receive-only: emitters via one-way Friis; silent platform
    RDF_RADAR_MODE_PULSE_DOPPLER   // MTD bank + rotor sidebands + PRF stagger + track coast
}
```

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR, 128);

// Tangential helicopters / CPA: use Pulse-Doppler (do NOT rewrite PhysicalDetect).
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER, 96);

// Or attach to an entity:
RDF_RadarComponent radar = ...;
radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH);
```

Preset factories (same settings objects):

- `RDF_RadarSensor.CreateSearchSettings`
- `RDF_RadarSensor.CreateStareSettings`
- `RDF_RadarSensor.CreateWlrSettings`
- `RDF_RadarSensor.CreateEsmSettings`
- `RDF_RadarSensor.CreatePulseDopplerSettings`
- `RDF_RadarDemoConfig.CreateSearch` / `CreateStare` / `CreateWlr` / `CreateEsm` / `CreatePulseDoppler`

### Pulse-Doppler (MTD) — GBRS / AD search

Default `SEARCH` keeps MTI **off** so parked vehicles paint. For moving air targets that cross CPA (\(v_r\approx0\)):

```c
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER, 96);
// equivalent knobs on Hardware / Settings (Validate() clamps):
//   hw.m_EnableMti = true;
//   hw.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;   // or TWOPULSE / THREE_PULSE
//   hw.m_MtiStaggerDeblind = true;                  // classic: max over PRF set
//   hw.m_DopplerBinCount = 16;                      // 8–32 typical (MTD)
//   hw.m_MtiClutterFloor = 1e-4;                    // classic residue / MTD bin0
//   hw.m_MtdClutterLeakage = 1e-6;                  // non-zero MTD bins
//   hw.m_PrfStaggerRatio = 1.2;                     // or m_PrfSetHz[]
//   settings.m_EnableClutterMap = true;
//   settings.m_TrackCoastOnMiss = true;
//   settings.m_TrackCoastOnDopplerNull = true;
//   settings.m_TrackCoastGateGrowPerMiss = 0.25;
//   settings.m_TrackCoastMaxSec = 8.0;
```

Plots expose `m_DopplerHz`, `m_DopplerBin`, `m_PrfIndex`, `m_RadialSpeedMs` (network-synced).  
Helicopter signatures auto-fill rotor tip / blade / RCS fraction when conf omits them (`Helicopters/*`).  
Classic TwoPulse / ThreePulse now also score rotor spectrum lines (not body-only).

### Range–az clutter surface (cockpit MFD, default OFF)

Display-only DEM σ⁰·A/R⁴ grid for airborne / vehicle instruments. **Not** part of
detection / CFAR / tracks. **Do not** fold into `RDF_RadarHUD` corner PPI.

```c
sensor.EnableClutterSurface(true);   // Settings.m_EnableRangeAzClutterSurface
// Optional knobs (Validate clamps):
//   settings.m_ClutterSurfaceAzBins = 64;         // 8–128
//   settings.m_ClutterSurfaceRangeBins = 64;
//   settings.m_ClutterSurfaceCellsPerScan = 96;   // amortized DEM rays
//   settings.m_ClutterSurfaceDecayPerScan = 0.92; // phosphor
//   settings.m_ClutterSurfaceSectorHalfDeg = 0;   // 0 = use scan half-angle

int nAz, nRng;
float maxRangeM, sectorHalfDeg, scanAzDeg;
if (sensor.TryGetClutterSurfaceMeta(nAz, nRng, maxRangeM, sectorHalfDeg, scanAzDeg))
{
    int rev = sensor.GetClutterSurfaceRevision(); // bumps each Scan / grid resize
    float p = sensor.PeekClutterSurfacePower(nAz / 2, 0);     // linear σ⁰·A/R⁴
    float db = sensor.PeekClutterSurfacePowerDb(nAz / 2, 0);  // 10·log10; empty=-300
    array<float> buf = new array<float>();
    sensor.CopyClutterSurfacePowers(buf);   // az-major: az*nRng + range
    sensor.CopyClutterSurfacePowersDb(buf); // same layout, dB
    int rays, samp, azCursor;
    sensor.TryGetClutterSurfaceFill(rays, samp, azCursor); // last Scan budget stats
}
```

`StabilizeForRegression()` forces the surface **off**.

**Cockpit mount (official RTTexture pattern):**

1. Screen mesh material: `AlbedoMap` / `BCRMap` / `EmissiveMap` = `$rendertarget`
   (RDF ships `RadarData/ClutterSurface/Computer_Screen_RT.emat` for the vanilla
   `Computer_E_01` screen slot).
2. **Play demo (recommended):** Script Console while Play is running:
   `RDF_RadarClutterSurfaceManualDemo.Start();` / `Stop();`
   Spawns `Computer_E_01_on_RT` (vanilla `Computer_E_01_on` + RT MaterialAssign).
   `StartMonitorOnly()` / `StartVanillaOn()` for siblings. Or attach
   `RDF_RadarClutterSurfaceDemo` (picker: on / Monitor_on / off / *_RT).
3. Product vehicles: call `RDF_RadarClutterSurfaceScreen.EnableScreen(screenMesh, sensor)`
   only while the local player is in the compartment; `DisableScreen()` on leave.
4. Optional workspace preview: Demo attribute `m_bWorkspacePreviewFallback` if spawn fails.

Grid axes are **boresight-relative** (center column = current scan azimuth).

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
Default intercept is geometric (report if this radar already detected the victim).  
Opt-in `m_EnableRwrFriis` uses one-way Friis on `GetEffectivePeakPowerW()` vs RWR noise (`GetNoisePowerW()×4`) and `m_RwrDetectSnrDb`. LPI search therefore shrinks intercept range.  
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
- **Rain / fog loss** (optional): `EnableAtmosphericPathLoss(true)` or set
  `m_EnableWeatherDrivenRainLoss` — samples `GetRainIntensity` / `GetFogAmount`
  once per scan and adds rain/fog terms on top of `m_RainLossDbPerKmOneWay`.

### Opt-in channel fidelity (no ideal/realistic tiers)

Enable only what you need. Defaults leave optional fidelity **off**.

| Helper / field | Effect |
|----------------|--------|
| `SetMeasurementNoise(scale, …)` | Measurement noise / bias |
| `EnableCfarThermalFill(true)` | Thermal fill of empty CFAR cells |
| `EnableAtmosphericPathLoss(weatherDriven)` | Clear-air (+ optional weather) path loss |
| `EnableLosTwoRayMultipath()` | Clear-LOS two-ray power lobes (skips projectiles) |
| `EnableAtmosphericRefraction()` | 4/3-Earth horizon soft factor + elevation bias |
| `EnablePrfAmbiguityFolds(range, doppler)` | PRF ambiguity folds (Doppler skipped for WLR) |
| `EnableAntennaPatternFidelity(sll, pol)` | Sidelobe floor + polarization match tables |
| `EnablePatternAndSiteLuts(pattern, site)` | Segmented pattern LUT + fixed-site path LUT |
| `m_EnablePatternLut` | Az segmented pattern (default on) |
| `m_EnableSitePathLut` | Polar DEM path factors (default off; needs bake) |
| `m_EnableDualKnifeEdge` / `m_KnifeEdgeMinUSeparation` | Dual-dominant knife-edge (Deygout-lite) |
| `m_EnableKnifeEdgeLut` | ν→factor LUT (`RDF_RadarKnifeEdgeLut`) |
| `Hardware.m_PolarizationMode` | `RDF_POL_H` / `V` / `CIRCULAR` match table |
| `Hardware.GetBand()` | VHF/UHF/L/S/C/X/Ku/K/Ka from `m_FrequencyHz` (σ⁰ + foliage attenuation) |
| `Hardware.GetScanFrequencyHz(scan)` | Hop carrier when hop is active (ECCM or `m_FrequencyHopEnabled`) |
| `Hardware.ScaleApertureToFrequency(f)` | One-shot same-aperture retune (G ∝ f², HPBW ∝ 1/f) |
| `Hardware.m_PolarizationFactor` | Extra linear trim on received power |
| `Hardware.m_SidelobeLevelDb` | One-way sidelobe floor (two-way = lin²) |
| `NoiseJammerEffect.EnableSlb(true)` | Blank sidelobe-only jam coupling |
| `StabilizeForRegression()` | AutoTest helper: turn optional fidelity **off** |

```c
RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(64);
// Pick what the mission needs — no mega-preset:
cfg.SetMeasurementNoise(2.0, 3.0, 0.15, 0.1);
cfg.EnableAtmosphericPathLoss(true);
cfg.EnableLosTwoRayMultipath();
cfg.EnableAtmosphericRefraction();
cfg.EnablePrfAmbiguityFolds(true, true);
cfg.EnableAntennaPatternFidelity(true, true);
cfg.m_Hardware.m_PolarizationMode = ERDF_RadarPolarization.RDF_POL_CIRCULAR;
cfg.m_Hardware.m_PolarizationFactor = 0.9;
cfg.m_Hardware.m_SidelobeLevelDb = -30.0;
cfg.Validate();
```

### Scan LOS (what mods need to know)

**You do not change call sites.** Keep using `Configure` / `Tick` / `ScanOnce`.
Ship `DemData/<world>/` with SURF + HEIGHT (`HUD: SURF+H`) and the defaults already help.

```text
candidate
  → LOS/reuse cache hit?  ──yes──► reuse last LOS / target (biggest wall-time win)
  → no → DEM HEIGHT ready & m_EnableDemLosPrecheck?
           ──yes & terrain pierces ray──► skip TraceMove (demBlk++) ; may still try NLOS
           ──no / clear / unknown──────► TraceMove (trace++) ; then NLOS if blocked
```

| Status token | Meaning |
|--------------|---------|
| `losHit=` / `reuse=` | Cache hits (repeat dwells) |
| `demBlk=` | Terrain blocked via HEIGHT RAM (no Trace) |
| `trace=` | Real `TraceMove` count this scan |

| Knob | Default | When to touch |
|------|---------|----------------|
| `m_EnableDemLosPrecheck` | **on** | Set `false` only to force every candidate through Trace (debug) |
| `m_DemLosPrecheckSamples` | 8 (2–24) | More samples = safer terrain reject, slightly more CPU |
| `m_KnifeEdgeClearanceSlackM` | ~2 m | Extra metres of clearance in the pierce test (shared with knife-edge NLOS) |
| `m_MaxLosTracesPerScan` | 48 | Cap TraceMove after DEM skips |
| LOS / reuse ages & fresh budget | (see Settings) | Tune dwell cadence; leave alone unless profiling |

No HEIGHT pack → precheck is a **no-op** (same as old Trace-only path).  
In-game matrix bench: Debugger `RDF_RadarDemLosBenchAutoTest.Start()` → [AUTOTEST_CI_LIMITS.md](AUTOTEST_CI_LIMITS.md).

### Mechanical-scan phase offset (paused-antenna resume)

`m_EnableMechanicalScan` drives `GetScanForward` from the **world clock**:
`angle = worldTime * ScanRpm * 2π/60`. `RDF_RadarSettings.m_ScanPhaseOffsetRad`
(default **0**) is added to that angle, letting a mod freeze the antenna
visually (stop driving its bone / yaw) while the sensor is disabled, then
resume from the frozen bearing instead of snapping to the world-clock angle:

```c
// pause: record the frozen bearing, disable the sensor (no scan, no emission)
float frozenRad = ...;                    // antenna bearing at pause
sensor.SetEnabled(false);

// resume: offset so the first scan starts at the frozen bearing
settings.m_ScanPhaseOffsetRad = frozenRad - world.GetWorldTime() * 0.001
    * settings.m_Hardware.m_ScanRpm * Math.PI * 2.0 / 60.0;
sensor.Configure(settings);
sensor.SetEnabled(true);
```

The offset applies to `GetScanForward` only (mechanical mode, `ScanRpm > 0`);
`Validate()` leaves it untouched, and 0 reproduces the stock world-clock scan.
Keep antenna visuals on the same formula so the mesh and the scan beam stay in
sync across pause/resume cycles.

---

### Dwell / resource management (phased-array beam-time budget)

Opt-in (`m_EnableDwellScheduler`, default off). When on, the Sensor schedules
FIRE_CONTROL (the locked target) and TRACK (confirmed tracks) dwells within a
beam-time budget; the scanner then forces a full update for the scheduled
scatterer ids. SEARCH keeps using the fair scan cursor. When off, the classic
scan-everything path is unchanged.

```c
cfg.m_EnableDwellScheduler = true;
cfg.m_DwellBudgetMs = 8.0;            // beam-time budget per scan
cfg.m_FireControlDwellPeriodS = 0.25; // locked-target revisit
cfg.m_FireControlDwellMs = 4.0;       // cost of one fire-control dwell
cfg.m_TrackDwellPeriodS = 1.0;        // confirmed-track revisit
cfg.m_TrackDwellMs = 2.0;             // cost of one track dwell
cfg.Validate();
```

| Knob | Default | When to touch |
|------|---------|---------------|
| `m_EnableDwellScheduler` | **off** | Enable for phased-array / fire-control radars |
| `m_DwellBudgetMs` | 20 | Lower to create contention; must still fit fire-control |
| `m_FireControlDwellPeriodS` / `m_FireControlDwellMs` | 0.25 / 4.0 | Locked-target update rate / cost |
| `m_TrackDwellPeriodS` / `m_TrackDwellMs` | 1.0 / 2.0 | Track revisit rate / cost |

Scheduling rule: class priority (fire-control > track > search), earliest
deadline first within a class, hard budget cap. Deadline miss is reported to
the sensor (currently unused; a future slice may coast missed tracks). Offline
mirror + golden: `tools/dem/rdf_radar_dwell.py`. In-game regression:
`RDF_RadarDwellAutoTest.Start()`.

---

### Multi-band channels (one carrier per scan)

Opt-in (`m_EnableMultiBand`, default off). When on, `m_BandChannels` holds
separate antennas / hardware objects. Each scan, the Sensor picks the
highest-priority **scheduled** dwell kind and points `m_Hardware` at that
channel (`SelectBandForDwell`). SEARCH uses `m_SearchBandIndex` unless a
TRACK / FIRE_CONTROL dwell is scheduled. Dual-band factories must **not**
call `ScaleApertureToFrequency` between channels — those are two apertures.

```c
RDF_RadarSettings cfg = RDF_RadarDemoConfig.CreateDualBandVhfSearchXTrack();
// Or hand-wire:
cfg.m_EnableMultiBand = true;
cfg.m_BandChannels.Insert(RDF_RadarHardware.CreateP18Like());
cfg.m_BandChannels.Insert(RDF_RadarHardware.CreateShorad());
cfg.m_SearchBandIndex = 0;
cfg.m_TrackBandIndex = 1;
cfg.m_FireControlBandIndex = 1;
cfg.m_EnableDwellScheduler = true;
cfg.Validate();
```

| Knob | Default | When to touch |
|------|---------|---------------|
| `m_EnableMultiBand` | **off** | VHF search + X track/fire-control on one platform |
| `m_BandChannels` | empty | One `RDF_RadarHardware` per antenna |
| `m_SearchBandIndex` / `m_TrackBandIndex` / `m_FireControlBandIndex` | 0 | Which channel each dwell kind uses |
| `Hardware.m_FrequencyHopEnabled` / `m_HopSetHz` | off / empty | Intra-band hop every scan (or ECCM `freq`) |

Instantaneous RCS treats signature `m_MeanRcsM2` as optical-region and scales
with wavelength (`ka<10` Rayleigh). Small shells drop out on VHF; vehicles
and aircraft remain. Offline: `tools/dem/test_rdf_radar_multiband.py`.

---

### ECCM decision (auto anti-jam response)

Opt-in (`m_EnableEccmDecision`, default off). When on, the Sensor reads EW
observables each scan and auto-responds:

```c
cfg.m_EnableEccmDecision = true;
cfg.m_EccmJnOnDb = 6.0;             // jam detect threshold (J/N dB)
cfg.m_EccmJnHysteresisDb = 2.0;     // release hysteresis
cfg.m_EccmSidelobeCouplingOn = 0.3; // sidelobe vs mainlobe split
```

| Threat | Auto response |
|--------|---------------|
| Sidelobe noise jam | sidelobe blanking (SLB, wired to the jammers) |
| Mainlobe noise jam | frequency agility (hops carrier next scan) |
| Deception false plots | PRF agility (reported; use `m_PrfSetHz` for runtime stagger) |
| Locked target under jam | burn-through (reported only) |

SLB is wired to the jammer stack. Frequency agility arms `Hardware.SetHopActive`
for the **next** scan (`GetScanFrequencyHz` cycles `m_HopSetHz` or a 5% pair;
G / beamwidth stay put). PRF stagger is already config-time. Burn-through is
still status-only via `Sensor.GetEccmStatusShort()`.
Offline mirror + golden: `tools/dem/rdf_radar_eccm.py`. In-game regression:
`RDF_RadarEccmAutoTest.Start()`.

---

### JPDA soft association (dense multi-target)

Opt-in (`m_EnableJpda`, default off). When on, the tracker uses soft association
instead of hard nearest-neighbor (GNN); when off, the classic GNN path is
unchanged.

```c
cfg.m_EnableJpda = true;
cfg.m_JpdaPd = 0.9;   // per-scan detection probability (miss hypothesis weight)
```

| Knob | Default | When to touch |
|------|---------|---------------|
| `m_EnableJpda` | **off** | Dense multi-target where tracks swap / steal plots |
| `m_JpdaPd` | 0.9 | Miss-hypothesis weight in the joint events |

Association: gate (range + azimuth) → union-find clusters → joint-event
enumeration → marginal association probabilities β_ij + miss β_i0 → beta-weighted
alpha-beta update. Clusters larger than 4×4 fall back to hard GNN assignment.
Offline mirror + golden: `tools/dem/rdf_radar_jpda.py`. In-game regression:
`RDF_RadarJpdaAutoTest.Start()`.

---

### NCTR (rotor / fan / fixed)

Opt-in (`m_EnableNctr`, default off). Classifies from **micro-Doppler observables**
(rotor/fan tip, RCS fraction, sideband used, SNR) — never entity type / prefab.
Plots and tracks carry `m_NctrClass` / `m_NctrConfidence`. Fusion keeps the class
when both sides agree; mismatch → `UNKNOWN`. Network scan-track codec stride is
unchanged (local + in-process datalink only).

```c
cfg.m_EnableNctr = true;
// Optional signature fan fields; jets without authored fans get defaults
// (tip 320 m/s, 17 blades, frac 0.07) if the key looks like Aircraft/Jet.
```

| Class | Cue |
|-------|-----|
| `RDF_NCTR_ROTOR` | rotor tip > 40 m/s and frac ≥ 0.15 (+ sideband or SNR ≥ 6) |
| `RDF_NCTR_FAN` | fan tip > 40 m/s and 0 < frac < 0.149 (+ sideband or SNR ≥ 8) |
| `RDF_NCTR_FIXED` | otherwise SNR ≥ 4 |
| `RDF_NCTR_UNKNOWN` | low SNR / no detect |

Offline: `tools/dem/rdf_radar_nctr.py`.

---

### LPI search mode

`Hardware.m_SearchMode = RDF_SEARCH_LPI` scales **peak power** (`m_LpiPeakPowerScale`,
default 0.15) and **scan rpm** (`m_LpiScanRpmScale`, default 0.5) via
`GetEffectivePeakPowerW()` / `GetEffectiveScanRpm()`. Radar equation and clutter
use the effective peak; ESM *emit* fallback still uses authored `m_PeakPowerW`.

---

### Track confidence, weapon-grade lock, designation

Tracks recompute `m_Confidence` from hit age, SNR, and residual/gate (×0.6 while
coasting). HUD `GetLockedTarget` is unchanged.

```c
cfg.m_WeaponGradeMinConfidence = 0.55;  // 0 = legacy, no extra fire gate
sensor.DesignateScattererId(scattererId);  // FIRE_CONTROL dwells even without lock
sensor.ClearDesignation();
```

`RDF_RadarWeaponBridge.CanAuthorizeFire()` still requires TRACKING (or COAST if
that flag is off) **and** `lock.PassesWeaponGradeConfidence(min)` when min > 0.
Dwell scheduler must be on for designation to take effect.

---

### Environment extras (glint / rain-sea / duct)

All default off. `StabilizeForRegression()` clears them.

| Knob | Effect |
|------|--------|
| `m_EnableMultipathGlint` | Low-AGL elevation bias toward the surface (water strongest) |
| `m_EnableRainSeaClutterDamp` | Rain intensity scales water σ⁰ down (does not replace rain path loss) |
| `m_EnableAtmosphericDuct` | Multiplies radio horizon when `m_EnableAtmosphericRefraction` is on |

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

Not synced (by design for now): full `Hardware` / `EwStack` / `MeasurementModel` objects, DEM tiles.

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

相关：[RADAR_GAME_FRAMEWORK.md](RADAR_GAME_FRAMEWORK.md) · [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md) · [API.md](API.md) · **操作示例：[DEVELOPER_RECIPES.md](DEVELOPER_RECIPES.md)**

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
    RDF_RADAR_MODE_SEARCH,         // 扇区搜索；默认关 MTI（静止载具可报）
    RDF_RADAR_MODE_STARE,          // 宽波束凝视
    RDF_RADAR_MODE_WLR,            // 弹道 / 武器定位
    RDF_RADAR_MODE_ESM,            // 纯接收 ESM
    RDF_RADAR_MODE_PULSE_DOPPLER   // MTD 通道 + 旋翼边带 + 参差 PRF + 航迹 coast
}
```

```c
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_WLR, 128);

// 切向直升机 / CPA：用脉冲多普勒（不要在 GBRS 里重写 PhysicalDetect）
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER, 96);

// Or attach to an entity:
RDF_RadarComponent radar = ...;
radar.SetMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH);
```

预设工厂（同一套 settings 对象）：

- `RDF_RadarSensor.CreateSearchSettings`
- `RDF_RadarSensor.CreateStareSettings`
- `RDF_RadarSensor.CreateWlrSettings`
- `RDF_RadarSensor.CreateEsmSettings`
- `RDF_RadarSensor.CreatePulseDopplerSettings`
- `RDF_RadarDemoConfig.CreateSearch` / `CreateStare` / `CreateWlr` / `CreateEsm` / `CreatePulseDoppler`

### 脉冲多普勒（MTD）— GBRS / 防空搜索

默认 `SEARCH` **关闭** MTI，保证静止载具可报。对掠过 CPA（\(v_r\approx0\)）的空中目标：

```c
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_PULSE_DOPPLER, 96);
// 等价旋钮（Validate 会钳位）：
//   hw.m_MtiMode = ERDF_MtiMode.RDF_MTI_MTD_BANK;   // 亦可 TWOPULSE / THREE_PULSE
//   hw.m_MtiStaggerDeblind = true;                  // 经典 canceller：多 PRF 取 max
//   hw.m_DopplerBinCount = 16;
//   hw.m_PrfStaggerRatio = 1.2;
//   settings.m_EnableClutterMap = true;
//   settings.m_TrackCoastOnMiss = true;
//   settings.m_TrackCoastOnDopplerNull = true;
//   settings.m_TrackCoastGateGrowPerMiss = 0.25;
//   settings.m_TrackCoastMaxSec = 8.0;
```

Plot 带 `m_DopplerHz` / `m_DopplerBin` / `m_PrfIndex` / `m_RadialSpeedMs`（网络同步）。  
直升机 signature 缺旋翼字段时，运行时对 `Helicopters/*` 自动填 UH-1/Mi-8 类默认。  
经典 TwoPulse / ThreePulse 也会对旋翼谱线取 max（不再只吃机身 Doppler）。

### Range–az 杂波面（座舱仪表，默认关）

仅显示用的 DEM σ⁰·A/R⁴ 粗栅，**不进**检测 / CFAR / 航迹。**不要**叠进 `RDF_RadarHUD` 角标 PPI。

```c
sensor.EnableClutterSurface(true);
// settings.m_ClutterSurfaceAzBins / RangeBins / CellsPerScan / DecayPerScan / SectorHalfDeg
sensor.TryGetClutterSurfaceMeta(nAz, nRng, maxRangeM, sectorHalfDeg, scanAzDeg);
int rev = sensor.GetClutterSurfaceRevision(); // 每扫 / 改栅递增
sensor.PeekClutterSurfacePower(azBin, rangeBin);     // 线性
sensor.PeekClutterSurfacePowerDb(azBin, rangeBin);   // dB，空=-300
sensor.CopyClutterSurfacePowers(buf);   // az 主序
sensor.CopyClutterSurfacePowersDb(buf); // 同布局，dB
sensor.TryGetClutterSurfaceFill(rays, samp, azCursor); // 上扫摊销进度
```

`StabilizeForRegression()` 强制关闭。

座舱挂载：Play 后在 **Script Console** 执行
`RDF_RadarClutterSurfaceManualDemo.Start();`（停：`Stop();`）——刷
`Prefabs/RDF/ClutterSurface/Computer_E_01_on_RT.et`（继承官方 `Computer_E_01_on`，
屏槽 `MaterialAssign`→`Computer_Screen_RT.emat`）。仅显示器：`StartMonitorOnly();`；
原版 on + 运行时 remap：`StartVanillaOn();`。也可挂组件 `RDF_RadarClutterSurfaceDemo`
（Attribute 可选 on / Monitor_on / off 等同族）。
离舱/停用必须 `Stop()` / `DisableScreen()`。栅为**相对扫向**。

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
默认截获是几何的（本雷达已检出受害方才告警）。  
按需 `m_EnableRwrFriis` 用单向 Friis 对 `GetEffectivePeakPowerW()` 与 RWR 噪声（`GetNoisePowerW()×4`）以及 `m_RwrDetectSnrDb` 做门限。LPI 搜索会缩短截获距离。  
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
- **雨 / 雾损耗**（可选）：`EnableAtmosphericPathLoss(true)` 或自设
  `m_EnableWeatherDrivenRainLoss`，每扫采样雨/雾强度并叠加损耗。

### 按需通道保真（无理想/逼真双档）

需要什么开什么；可选保真项默认关。

| 辅助 / 字段 | 作用 |
|-------------|------|
| `SetMeasurementNoise(scale, …)` | 测量噪声 / 偏差 |
| `EnableCfarThermalFill(true)` | CFAR 空单元热填空 |
| `EnableAtmosphericPathLoss(weatherDriven)` | 晴空（+可选天气）路径损耗 |
| `EnableLosTwoRayMultipath()` | 通视双射线功率瓣（弹丸跳过） |
| `EnableAtmosphericRefraction()` | 4/3 地球地平线软衰减 + 俯仰偏置 |
| `EnablePrfAmbiguityFolds(range, doppler)` | PRF 模糊折叠（WLR 跳过多普勒） |
| `EnableAntennaPatternFidelity(sll, pol)` | 旁瓣地板 + 极化匹配表 |
| `EnablePatternAndSiteLuts(pattern, site)` | 分段方向图 LUT + 固定站路径 LUT |
| `m_EnablePatternLut` | 方位分段方向图（默认开） |
| `m_EnableSitePathLut` | 极坐标 DEM 路径因子（默认关；需 bake） |
| `m_EnableDualKnifeEdge` / `m_KnifeEdgeMinUSeparation` | 双主导刃（Deygout-lite） |
| `m_EnableKnifeEdgeLut` | ν→factor LUT（`RDF_RadarKnifeEdgeLut`） |
| `Hardware.m_PolarizationMode` | `RDF_POL_H` / `V` / `CIRCULAR` 匹配表 |
| `Hardware.GetBand()` | 由 `m_FrequencyHz` 派生 VHF/UHF/L/S/C/X/Ku/K/Ka（σ⁰ 与植被衰减） |
| `Hardware.GetScanFrequencyHz(scan)` | 捷变激活时的扫描载频（ECCM 或 `m_FrequencyHopEnabled`） |
| `Hardware.ScaleApertureToFrequency(f)` | 同一孔径一次改频（G ∝ f²，HPBW ∝ 1/f） |
| `Hardware.m_PolarizationFactor` | 接收功率额外极化微调 |
| `Hardware.m_SidelobeLevelDb` | 单程旁瓣地板（双程 = lin²） |
| `NoiseJammerEffect.EnableSlb(true)` | 旁瓣耦合消隐 |
| `StabilizeForRegression()` | AutoTest：关掉可选保真项 |

```c
RDF_RadarSettings cfg = RDF_RadarSensor.CreateSearchSettings(64);
// 按任务需要开项 — 无大包预设：
cfg.SetMeasurementNoise(2.0, 3.0, 0.15, 0.1);
cfg.EnableAtmosphericPathLoss(true);
cfg.EnableLosTwoRayMultipath();
cfg.EnableAtmosphericRefraction();
cfg.EnablePrfAmbiguityFolds(true, true);
cfg.EnableAntennaPatternFidelity(true, true);
cfg.m_Hardware.m_PolarizationMode = ERDF_RadarPolarization.RDF_POL_CIRCULAR;
cfg.m_Hardware.m_PolarizationFactor = 0.9;
cfg.m_Hardware.m_SidelobeLevelDb = -30.0;
cfg.Validate();
```

### 扫描通视（模组只要知道这些）

**不用改调用点。** 继续 `Configure` / `Tick` / `ScanOnce`。  
工坊带上 `DemData/<world>/` 的 SURF+HEIGHT（HUD 显示 `SURF+H`），默认配置就会受益。

```text
候选目标
  → LOS/reuse 缓存命中？──是──► 复用上次通视/目标（复扫墙钟收益最大）
  → 否 → HEIGHT 就绪且 m_EnableDemLosPrecheck？
           ──是且地形刺穿射线──► 跳过 TraceMove（demBlk++）；仍可走 NLOS
           ──否 / 通视 / 不确定──► TraceMove（trace++）；挡了再 NLOS
```

| 状态字 | 含义 |
|--------|------|
| `losHit=` / `reuse=` | 缓存命中（复扫） |
| `demBlk=` | HEIGHT RAM 判地形挡（未做 Trace） |
| `trace=` | 本扫真实 `TraceMove` 次数 |

| 旋钮 | 默认 | 何时动 |
|------|------|--------|
| `m_EnableDemLosPrecheck` | **开** | 仅调试「强制全 Trace」时设 `false` |
| `m_DemLosPrecheckSamples` | 8（2–24） | 采样越多地形拒识越稳，略费 CPU |
| `m_KnifeEdgeClearanceSlackM` | ~2 m | 刺穿判定额外净空（与刀刃 NLOS 共用） |
| `m_MaxLosTracesPerScan` | 48 | DEM 跳过后的 Trace 上限 |
| LOS/reuse 年龄与 fresh 预算 | （见 Settings） | 调驻留节奏；一般别动 |

无 HEIGHT 包 → 预检是**空操作**（与旧版纯 Trace 相同）。  
游戏内四象限基准：Debugger `RDF_RadarDemLosBenchAutoTest.Start()` → [AUTOTEST_CI_LIMITS.md](AUTOTEST_CI_LIMITS.md)。

### 机械扫描相位偏移（暂停天线续转）

`m_EnableMechanicalScan` 时 `GetScanForward` 用**世界钟**定角度：
`angle = worldTime * ScanRpm * 2π/60`。`RDF_RadarSettings.m_ScanPhaseOffsetRad`
（默认 **0**）叠加到该角度上，让模组在 Sensor 禁用期间冻结天线视觉
（停止驱动骨骼/偏航），恢复时从冻结方位续转，而不是跳到世界钟角度：

```c
// 暂停：记录冻结方位并禁用 Sensor（不扫描、不辐射）
float frozenRad = ...;                    // 暂停时的天线方位（弧度）
sensor.SetEnabled(false);

// 恢复：偏移使首次扫描从冻结方位开始
settings.m_ScanPhaseOffsetRad = frozenRad - world.GetWorldTime() * 0.001
    * settings.m_Hardware.m_ScanRpm * Math.PI * 2.0 / 60.0;
sensor.Configure(settings);
sensor.SetEnabled(true);
```

偏移只作用于 `GetScanForward`（机械模式且 `ScanRpm > 0`）；`Validate()` 不动它，
0 即原世界钟扫描。天线视觉应使用同一公式，保证网格与扫描波束在暂停/恢复
循环中始终同步。

---

### 驻留 / 资源管理（相控阵波束时间预算）

按需开启（`m_EnableDwellScheduler`，默认关）。开启后 Sensor 在波束时间预算内调度
火控（锁定目标）与跟踪（确认航迹）驻留，Scanner 再对命中的散射体强制全量更新；
搜索仍走公平扫描游标。关闭时走经典全扫描路径，行为不变。

```c
cfg.m_EnableDwellScheduler = true;
cfg.m_DwellBudgetMs = 8.0;            // 每扫波束时间预算
cfg.m_FireControlDwellPeriodS = 0.25; // 锁定目标重访
cfg.m_FireControlDwellMs = 4.0;       // 单次火控驻留成本
cfg.m_TrackDwellPeriodS = 1.0;        // 确认航迹重访
cfg.m_TrackDwellMs = 2.0;             // 单次跟踪驻留成本
cfg.Validate();
```

| 旋钮 | 默认 | 何时动 |
|------|------|--------|
| `m_EnableDwellScheduler` | **关** | 相控阵 / 火控雷达开启 |
| `m_DwellBudgetMs` | 20 | 调小制造争抢；仍须装得下火控 |
| `m_FireControlDwellPeriodS` / `m_FireControlDwellMs` | 0.25 / 4.0 | 锁定目标更新率 / 成本 |
| `m_TrackDwellPeriodS` / `m_TrackDwellMs` | 1.0 / 2.0 | 航迹重访率 / 成本 |

调度规则：类优先级（火控>跟踪>搜索）、类内最早截止优先、硬预算上限。截止错过上报给
Sensor（当前未用；后续可驱动航迹 coast）。离线镜像 + 金标：`tools/dem/rdf_radar_dwell.py`。
游戏内回归：`RDF_RadarDwellAutoTest.Start()`。

---

### 多波段通道（一扫一个载频）

按需开启（`m_EnableMultiBand`，默认关）。开启后 `m_BandChannels` 各持一份天线 /
硬件。每扫 Sensor 取已调度驻留中优先级最高的 kind，把 `m_Hardware` 指到对应通道
（`SelectBandForDwell`）。无 TRACK / FIRE_CONTROL 驻留时走 `m_SearchBandIndex`。
双波段工厂**不要**在通道之间调用 `ScaleApertureToFrequency`——那是两副天线。

```c
RDF_RadarSettings cfg = RDF_RadarDemoConfig.CreateDualBandVhfSearchXTrack();
// 或手写：
cfg.m_EnableMultiBand = true;
cfg.m_BandChannels.Insert(RDF_RadarHardware.CreateP18Like());
cfg.m_BandChannels.Insert(RDF_RadarHardware.CreateShorad());
cfg.m_SearchBandIndex = 0;
cfg.m_TrackBandIndex = 1;
cfg.m_FireControlBandIndex = 1;
cfg.m_EnableDwellScheduler = true;
cfg.Validate();
```

| 旋钮 | 默认 | 何时动 |
|------|------|--------|
| `m_EnableMultiBand` | **关** | 同一平台 VHF 搜索 + X 跟踪/火控 |
| `m_BandChannels` | 空 | 每副天线一份 `RDF_RadarHardware` |
| `m_SearchBandIndex` / `m_TrackBandIndex` / `m_FireControlBandIndex` | 0 | 各驻留 kind 用哪条通道 |
| `Hardware.m_FrequencyHopEnabled` / `m_HopSetHz` | 关 / 空 | 带内捷变（或 ECCM `freq`） |

瞬时 RCS 把签名 `m_MeanRcsM2` 当光学区，再按波长缩放（`ka<10` 进入瑞利）。
VHF 上小炮弹几乎看不见，载具/飞机仍可见。离线：`tools/dem/test_rdf_radar_multiband.py`。

---

### ECCM 决策（自动抗干扰响应）

按需开启（`m_EnableEccmDecision`，默认关）。开启后 Sensor 每扫读取 EW 观测量并自动响应：

```c
cfg.m_EnableEccmDecision = true;
cfg.m_EccmJnOnDb = 6.0;             // 压制检测阈值（J/N dB）
cfg.m_EccmJnHysteresisDb = 2.0;     // 释放滞回
cfg.m_EccmSidelobeCouplingOn = 0.3; // 旁瓣/主瓣分界
```

| 威胁 | 自动响应 |
|------|---------|
| 旁瓣噪声压制 | 旁瓣消隐（SLB，已接干扰机） |
| 主瓣噪声压制 | 频率捷变（下一扫跳载频） |
| 欺骗假点 | PRF 捷变（上报；运行时参差用 `m_PrfSetHz`） |
| 锁定目标被压制 | 烧穿保持（仅上报） |

SLB 已接干扰栈。频率捷变在**下一扫**激活 `Hardware.SetHopActive`
（`GetScanFrequencyHz` 循环 `m_HopSetHz` 或 5% 对；不改 G / 波束宽度）。
PRF 参差本来就是配置期。烧穿仍只经 `Sensor.GetEccmStatusShort()` 上报。
离线镜像 + 金标：`tools/dem/rdf_radar_eccm.py`。游戏内回归：
`RDF_RadarEccmAutoTest.Start()`。

---

### JPDA 软关联（密集多目标）

按需开启（`m_EnableJpda`，默认关）。开启后 tracker 用软关联取代硬最近邻（GNN）；
关闭时经典 GNN 路径不变。

```c
cfg.m_EnableJpda = true;
cfg.m_JpdaPd = 0.9;   // 每扫检测概率（miss 假设权重）
```

| 旋钮 | 默认 | 何时动 |
|------|------|--------|
| `m_EnableJpda` | **关** | 密集多目标、航迹互抢/互换 |
| `m_JpdaPd` | 0.9 | 联合事件中 miss 假设权重 |

关联流程：门控（距离+方位）→ 并查集聚类 → 联合事件枚举 → 边缘关联概率 β_ij + miss
β_i0 → β 加权 α-β 更新。簇大于 4×4 回退 GNN 硬指派。离线镜像 + 金标：
`tools/dem/rdf_radar_jpda.py`。游戏内回归：`RDF_RadarJpdaAutoTest.Start()`。

---

### NCTR（旋翼 / 风扇 / 固定）

按需开启（`m_EnableNctr`，默认关）。只根据**微多普勒可观测量**（桨尖/风扇尖、RCS
份额、边带是否用上、SNR）分类，**不用**实体类型 / prefab。点迹与航迹带
`m_NctrClass` / `m_NctrConfidence`。融合同类保留，冲突回 `UNKNOWN`。网络航迹
编解码步长不变（仅本机 + 进程内数据链）。

```c
cfg.m_EnableNctr = true;
// 签名可选风扇字段；Aircraft/Jet 类键无作者风扇时给默认（尖速 320、17 叶、份额 0.07）
```

| 类 | 线索 |
|----|------|
| `RDF_NCTR_ROTOR` | 旋翼尖 > 40 m/s 且份额 ≥ 0.15（边带或 SNR ≥ 6） |
| `RDF_NCTR_FAN` | 风扇尖 > 40 m/s 且 0 < 份额 < 0.149（边带或 SNR ≥ 8） |
| `RDF_NCTR_FIXED` | 其余 SNR ≥ 4 |
| `RDF_NCTR_UNKNOWN` | 低 SNR / 未检出 |

离线：`tools/dem/rdf_radar_nctr.py`。

---

### LPI 搜索档

`Hardware.m_SearchMode = RDF_SEARCH_LPI` 通过 `GetEffectivePeakPowerW()` /
`GetEffectiveScanRpm()` 缩放峰值（`m_LpiPeakPowerScale`，默认 0.15）与扫描转速
（`m_LpiScanRpmScale`，默认 0.5）。雷达方程与杂波用有效峰值；ESM **发射**回退仍用
作者 `m_PeakPowerW`。

---

### 航迹置信度、武器级锁、指定驻留

航迹按命中龄期、SNR、残差/波门重算 `m_Confidence`（coast 时 ×0.6）。HUD
`GetLockedTarget` 不变。

```c
cfg.m_WeaponGradeMinConfidence = 0.55;  // 0 = 旧行为，无额外开火门
sensor.DesignateScattererId(scattererId);  // 无锁也走 FIRE_CONTROL 驻留
sensor.ClearDesignation();
```

`RDF_RadarWeaponBridge.CanAuthorizeFire()` 仍要求 TRACKING（或关闭该开关时允许
COAST），**并且**当 min > 0 时 `lock.PassesWeaponGradeConfidence(min)`。指定驻留
需要打开驻留调度器。

---

### 环境附加（闪烁 / 雨阻海杂波 / 波导）

全部默认关。`StabilizeForRegression()` 会清掉。

| 旋钮 | 效果 |
|------|------|
| `m_EnableMultipathGlint` | 低 AGL 俯仰朝地表偏（水面最强） |
| `m_EnableRainSeaClutterDamp` | 雨强压低水面 σ⁰（不替代雨衰路径损耗） |
| `m_EnableAtmosphericDuct` | 在 `m_EnableAtmosphericRefraction` 开启时拉伸无线电地平 |

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

仍不同步（当前设计）：完整 `Hardware` / `EwStack` / `MeasurementModel` 对象、DEM 瓦片。

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
