> **Languages / 语言**: [English](#english) · [中文](#中文)

# RDF In-Game Radar Framework

## English

The Enforce implementation now follows the same contracts as the offline
prototype. Game entities are scatterers/emitters for the physics chain;
published plots are quantized, noisy measurements (not raw entity poses).

Capabilities vs real EM radar (what works / what is missing / what is still
feasible): [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md).

## Runtime chain

1. Resolve radar origin and current boresight.
2. Advance the global scatterer table (`RDF_RadarScattererRegistry`): periodic
   discovery sweep, amortized classification, round-robin kinematics refresh.
   Type and RCS are cached per entry, so scans never re-classify entities.
3. Read candidates from the table, then apply range and azimuth dwell.
4. `TraceMove` line-of-sight check via `RDF_RadarScanGeometry.TraceLineOfSight`
   (`ANY_CONTACT`, reused `TraceParam`, `ExcludeArray` = subject + target,
   start clearance, fraction remapped to origin→end). Clear path → direct
   detection. Blocked path → optional NLOS (default on): ground-bounce
   (image-method + `|Gamma|^2` + early-hit depth) **and/or** single knife-edge
   diffraction (DEM/`GetSurfaceY` samples, Fresnel ν); factor = max of paths;
   tag `m_LosBlocked` (`/nlos` vs `/diff` on beam name).
5. Read projectile or rigid-body velocity (**truth, physics only**).
6. Estimate entity RCS; scale received power by `m_MultipathFactor`.
7. Select the strongest configured elevation beam.
8. Evaluate monostatic received power, Doppler, MTI, processing gain, noise,
   DEM clutter power, optional atmospheric/rain/weather loss, and SNR.
9. Optional coarse-bin CA-CFAR over azimuth/range power bins; empty cells may
   be filled with thermal noise (`m_EnableCfarThermalFill`) so Pfa is measurable.
10. **Measurement synthesis** (`RDF_RadarMeasurement`): quantize range to bin
    center, add SNR-scaled angle/Doppler noise (scale via settings / ideal vs
    realistic), rebuild plot kinematics; clear `m_Entity` unless
    `m_KeepEntityTruth`. Optional post-CFAR override:
    `RDF_RadarMeasurementModel`.
11. Feed accepted plots to the measurement-driven alpha-beta tracker
    (nearest-neighbor gates + `PredictAt`). Anonymous / false plots can
    associate; they are not display-only.
12. Optional lock layer (`RDF_RadarLockManager`) on tracks for SEARCH →
    ACQUIRING → TRACKING → COAST; exposed via `RDF_RadarSensor.GetLockedTarget`.

## Main configuration

`RDF_RadarSettings`:

- `m_Hardware`: `RDF_RadarHardware`
- `m_EnablePhysicalDetection`
- `m_EnableMechanicalScan`
- `m_DetectionSnrDb`
- `m_KeepUndetected`
- `m_EnableDemClutter`
- `m_DemCacheMaxTiles`
- `m_DemTileLoadsPerScan`
- `m_DemClutterScale`
- `m_ScattererDiscoveryRangeScale`, `m_ScattererDiscoveryIntervalS`,
  `m_ScattererClassifyPerTick`, `m_ScattererRefreshPerTick`, `m_ScattererMaxEntries`
- `m_MaxLosTracesPerScan` (default 48; bounds TraceMove hitch)
- `m_EnableNlosMultipath` (default true)
- `m_NlosReflectionAbs`, `m_NlosMinFactor`, `m_NlosMaxTargetAglM`
- `m_AdditionalNoisePowerW`
- `m_EwStack`
- `m_EnableCfarGate`, `m_CfarGuardCells`, `m_CfarTrainingCells`, `m_CfarPfa`,
  `m_RangeBinCount`, `m_EnableCfarThermalFill`, `m_CfarMode` (CA/GO/SO)
- Scan optimization: `m_LosCacheMaxAgeS`, reuse ages/shifts, priority ranges /
  speeds / band intervals, `m_FreshUpdateBudgetMin/Max`
- `m_EnableWlrHudAlerts`, `m_WlrHudAlertRadiusM`
- `m_EnableAtmosphericLoss` / weather-driven rain-fog loss (see RADAR_API)
- `m_EnableMeasurementSynthesis` (default true), `m_KeepEntityTruth` (debug),
  `m_MeasurementModel`, measurement noise scale
- `m_TrackGateRangeM`, `m_TrackGateAzimuthDeg`, `m_TrackConfirmHits`, `m_TrackMaxMisses`
- `m_EnableEsmReceive` (Friis \(R^2\) for emitters; SEARCH presets enable this)
- `m_EnableRwrReporting` (publish SEARCH/TRACK/LOCK threats after each dwell)
- `m_FairScanCursor` (fair round-robin dwell cursor; default true)
- Include* flags (`m_IncludeVehicles` / projectiles / emitters, etc.)

`RDF_RadarHardware`:

- frequency, peak power, antenna gain, system loss, noise figure
- pulse width, bandwidth, PRF, pulse integration, MTI
- `GetRangeBinM()` ≈ `c/(2B)` for measurement quantization
- scan RPM and `RDF_RadarElevationBeam[]`

## Presets

```c
RDF_RadarSettings shorad = RDF_RadarDemoConfig.CreateDefault(64);
RDF_RadarSettings p18 = RDF_RadarDemoConfig.CreateP18Like(128);
```

Presets are examples only. A game system may construct its own hardware and
beam list.

## Optional EW noise

```c
RDF_RadarNoiseJammerEffect jammer = new RDF_RadarNoiseJammerEffect();
jammer.m_Position = jammerWorldPosition;
jammer.m_ErpW = 10000.0;
jammer.m_BandwidthHz = 5000000.0;
// Default: SEARCH_AVG + -40 dB sidelobe (playable soft). Hard stare:
// jammer.ConfigurePhysicsBeam(-25.0);
// Mainlobe sector only: jammer.ConfigureMainlobeOnly();
// Extra soft knob: jammer.m_CouplingGain = 0.25;
settings.m_EwStack.Add(jammer);
```

Burn-through helper: `RDF_RadarEwBurnThrough.RangeM(R0, N, J)`.  
Status: `scanner.GetEwStatsShort()` / Sensor `GetStatusShort` appends `ewJN=…dB Reff=…m`.

## Optional EW deception / false plots

```c
RDF_RadarDeceptionJammerEffect deceive = new RDF_RadarDeceptionJammerEffect();
deceive.AddFalsePlot(1600.0, -18.0, 0.0000000000012, 25.0, 0.0);
settings.m_EwStack.Add(deceive);

// Range walk-off / angle scintillation / intermittent false plot:
RDF_RadarRangeWalkOffEffect walk = new RDF_RadarRangeWalkOffEffect();
walk.m_BaseRangeM = 1200.0;
walk.m_RangeRateMs = 80.0;
settings.m_EwStack.Add(walk);

RDF_RadarAngleScintillationEffect scint = new RDF_RadarAngleScintillationEffect();
scint.m_RangeM = 1500.0;
scint.m_AzimuthJitterDeg = 3.0;
settings.m_EwStack.Add(scint);

RDF_RadarIntermittentFalsePlotEffect burst = new RDF_RadarIntermittentFalsePlotEffect();
burst.m_PeriodS = 2.0;
burst.m_DutyCycle = 0.35;
settings.m_EwStack.Add(burst);
```

False plots are emitted as anonymous targets (`m_IsAnonymous=true`,
`m_IsFalsePlot=true`) and pass through the same CFAR gate.

## Runtime DEM clutter source

Preferred runtime path (see [DEM.md](DEM.md)):

- Height: live `BaseWorld.GetSurfaceY`
- Surface class: `RDF_SURF_JSON_V1` under
  `$profile` or mod `DemData/<world>/surf_manifest.json` + `surf_chunks/`
- Electromagnetic params: `RadarData/SurfaceTable.conf` (workshop) with JSON fallback

Development fallback: V3 CSV tiles (`manifest.csv` + `tiles/tile_*.csv`).
If nothing is available → `mode=LIVE` (height only, surface class UNKNOWN).

The scanner initializes a bounded LRU cache (`RDF_DemRuntimeCache`) and
loads tiles/chunks on demand near sampled targets. Current defaults:

- max cached tiles: `16`
- per-scan sync tile load budget: `2`

When DEM / SURF data is unavailable or malformed, radar processing falls back to
thermal noise + EW noise only (LIVE height still works), so legacy maps continue to run.

## Detection output

Each `RDF_RadarTarget` now includes:

- azimuth/elevation and scan number
- velocity, radial speed, and Doppler
- RCS, received/processed power, MTI gain
- sampled DEM surface class and clutter power contribution
- SNR, detection flag, and selected beam name
- `m_LosBlocked`, `m_LosHitFraction`, `m_MultipathFactor` (NLOS bounce and/or knife-edge scale)
- `m_IsAnonymous`, `m_IsFalsePlot`, and `m_CfarPowerW`

## PPI HUD

`RDF_RadarHUD` draws a north-up plan-position display (green phosphor theme,
bottom-right panel **144×184**, PPI canvas **128×128**) with range rings, a
boresight sweep line, and one blip per target.

- Blip colour: pale-yellow anonymous measurement plots (default), white
  deception false plot, cyan for detected NLOS multipath (`m_LosBlocked`);
  soft type colours only when `m_KeepEntityTruth` is on; dim grey for
  undetected returns when `m_KeepUndetected` is on.
- Blip size scales with SNR.
- Data row shows detected/total, confirmed track count, and the strongest target
  (type, range, SNR).
- Tracks come from measurement association; use `RDF_RadarTrack.PredictAt(t)`
  for extrapolated lock / lead points.

Control:

```c
RDF_RadarAutoRunner.SetHudEnabled(true);   // show + auto-feed every scan
RDF_RadarHUD.SetMode("SHORAD");            // header label
RDF_RadarHUD.SetDisplayRange(2000.0);      // overridden by scan range each feed
```

The runner feeds the HUD automatically from `RDF_RadarScanner.GetLastOrigin()`,
`GetLastForward()`, and `GetLastRange()`. To drive it from a custom system call
`RDF_RadarHUD.FeedScan(targets, origin, forward, range, tracker)`.

HUD mode label includes DEM status in local mode and `NET` in synchronized mode.

## Network authority path

Use `RDF_RadarNetworkComponent` on the radar owner together with `RplComponent`.
`RDF_RadarAutoRunner` and `RDF_RadarComponent` auto-detect
`RDF_RadarNetworkAPI` on the subject. Prefer driving gameplay through
`RDF_RadarSensor` on the authority; the network component syncs scan results.
Full contract: [RADAR_API.md](RADAR_API.md) § Network.

1. client calls `RequestScan()`,
2. server runs the authoritative scan path (Sensor / Scanner + Tracker / Lock / WLR),
3. server packs via `RDF_RadarNetCodec` into typed `array<int>` / `array<float>` (not CSV strings),
4. **Reliable** Broadcast: scan **summary** (capped confirmed tracks + WLR + lock + meta; plots omitted),
5. optional **Unreliable** Broadcast: capped plots for HUD (`m_SyncPlotsUnreliable`),
6. clients update synced caches; proxy `RDF_RadarSensor` injects tracks/lock and skips local Tracker/Lock recompute for that frame,
7. JIP via `RplSave` / `RplLoad` + `ScriptBitWriter` / `ScriptBitReader`,
8. slow knobs (range, sector, CFAR, Include*, emitting) use `[RplProp]`; HUD/DEM stay local.

Scale Attributes on `RDF_RadarNetworkComponent`: `m_MaxSyncedTracks` / `m_MaxSyncedPlots`,
`m_MinReliableBroadcastIntervalS` / `m_MinPlotBroadcastIntervalS`,
`m_SkipUnchangedSummary` (fingerprint), `m_InterestRadiusM`, `m_SyncPlotsUnreliable`.

### Datalink & multi-radar fusion

Station-to-station sharing (not weapon midcourse uplink — see WeaponBridge
“datalink” naming carefully):

- `RDF_RadarDatalinkHub` — confirmed-track library with TTL + light IFF
- `RDF_RadarFusionService` — association gate + optional dual-station horizontal cross-fix
- Optional `RDF_RadarDatalinkComponent` — typed-array Broadcast of hub state (same NetCodec style; caps/throttle/interest)

Authority may `PublishTracksToDatalink` after a successful scan. API:
`RDF_RadarDatalinkAPI` / `GetFusedTracks`. Regression: `RDF_RadarFusionAutoTest.Start()`.

### Minimal setup (entity-mounted radar)

1. On the radar entity prefab, add:
   - `RplComponent`
   - `RDF_RadarComponent`
   - `RDF_RadarNetworkComponent`
2. Ensure the entity is replicated in your scenario/session settings.
3. Start radar via existing entry points (`RDF_RadarComponent` tick or
   `RDF_RadarAutoRunner`), no extra script wiring required.
4. Optional: keep `RDF_RadarAutoRunner.SetHudEnabled(true)` to observe client HUD.
5. Optional: `RDF_RadarDatalinkComponent` for station Hub Broadcast.

### Quick verification (server-authoritative)

1. Run a multiplayer session with at least host + one client.
2. On host, enable radar demo/config as usual.
3. On client, verify HUD mode text shows `PPI | NET`.
4. Move targets/emitter sources and confirm both host/client see consistent
   track / lock trends (plots may lag or drop under Unreliable + caps).

## Automated live shell fire + WLR test

Script Debugger (Play mode). Spawns and launches real 82 mm HE shells:

```c
RDF_RadarShellFireAutoTest.Start();
```

About 42 s: fires O832DU every 6 s ahead of the local player, stares a wide
projectile sector, then checks detected plots, confirmed tracks, and WLR launch
error vs truth (ideal pass ≤ **250 m**; realistic channel ≤ **800 m** via
`WLR_LAUNCH_PASS_REALISTIC_M`). Report:
`$profile:RDF/RadarTests/radar_shellfire_autotest_<tick>.txt`.

## Automated ballistics / WLR math test

Script Debugger (synchronous, no tick loop):

```c
RDF_RadarBallisticsAutoTest.Start();
```

Checks freefall impact time, AirDrag range shortening, crosswind shift,
launch back-projection, and multi-point vacuum ballistic fit + AirDrag ground
intersect (WLR-style gates; not apex-only). Writes
`$profile:RDF/RadarTests/radar_ballistics_autotest_<tick>.txt`.

Offline Python mirror:

```
cd tools/dem
python -m unittest test_rdf_radar_ballistics.py -v
```

## Automated DEM regression test

You can run a fully automated in-game test from Script Debugger:

```c
RDF_RadarAutoTest.Start();
```

It executes four phases automatically:

1. DEM clutter OFF baseline
2. DEM clutter ON (`scale=1.0`)
3. DEM clutter ON (`scale=5.0`)
4. DEM clutter ON with tile-load budget `0` (degraded path)

The test spawns real unaided Mi-8 targets along the boresight (no emitter
marks, no RCS boost, no pre-seeded scatterer rows). Targets must be found by
discovery and detected via their own returns (`discovered_unaided` guard).

Run the full suite with ideal vs realistic channels (order:
Ballistics → DEM → Lock → Airborne → ShellFire):

```c
RDF_RadarAutoTestSuite.StartAll();
RDF_RadarAutoTestSuite.StartAllRealistic();
```

Suite order: Ballistics → DEM → Lock → Air → ShellFire → Perf → **Play**.

Play soak (`RDF_RadarPlayAutoTest.Start()`): AutoRunner + PPI HUD + DEM clutter +
normal discovery (UAZ×6, orbiting Mi-8, wandering `OriginOffset`). Measures
`scanMs` and full `RadarTick` wall time. Report:
`$profile:RDF/RadarTests/radar_play_autotest_<tick>.txt`.

Standalone regressions (**not** in `StartAll`; run one at a time):

```c
RDF_RadarRwrAutoTest.Start();            // RWR SEARCH/TRACK/LOCK on victim
RDF_RadarEsmArmAutoTest.Start();         // ESM + GetArmAim / silence unlock
RDF_RadarRocketLockFireAutoTest.Start(); // lock Mi-8 → Hydra70 guidance
RDF_RadarHeliDuelAutoTest.Start();       // heli vs heli + intercept
RDF_RadarSamEngageAutoTest.Start();      // ground SAM search / ID / engage demo
RDF_RadarFusionAutoTest.Start();         // datalink Hub + fusion / cross-fix
RDF_RadarStressAutoTest.Start();         // heavy soak load
```

Offline Python capability suite (no DEM required):

```
cd tools/dem
python rdf_radar_full_sim.py
python -m unittest discover -s . -p "test_rdf_*.py" -v
```

Map markers while tests run: `RDF_RadarAutoTestMapOverlay` (e.g. Lock / Airborne).

Output:

- console prints phase metrics and final `PASS/FAIL`
- report file written to `$profile:RDF/RadarTests/radar_dem_autotest_<tick>.txt`

Control:

```c
RDF_RadarAutoTest.IsRunning();
RDF_RadarAutoTest.Stop();
```

## Automated airborne target test (Mi-8)

Script Debugger:

```c
RDF_RadarAirborneScanTest.Start();
RDF_RadarAirborneScanTest.StartKeepTarget();
```

This test automatically:

1. chooses a nearby flatter radar start position,
2. spawns `{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et`,
3. forces airborne circular motion with vertical oscillation,
4. runs radar scan and verifies the spawned target is seen/detected/classified.

`StartKeepTarget()` keeps the spawned airborne target after test completion for
manual PPI/world-visual checks.

Output report:

- `$profile:RDF/RadarTests/radar_airborne_scan_<tick>.txt`

## Current boundaries

- Target candidates remain entity-first (scatterer registry) and LOS uses
  `RDF_RadarScanGeometry.TraceLineOfSight` (`ANY_CONTACT`,
  reused TraceParam, ExcludeArray, start clearance); NLOS may use ground-bounce
  weak detection + **single knife-edge diffraction** (not multi-edge/UTD).
- DEM / surface: prefer SURF JSON + live `GetSurfaceY`; V3 CSV is the dev fallback.
  Multiplayer parity for clutter still needs matching local SURF/DEM (or LIVE);
  detection **results** sync via `RDF_RadarNetworkComponent` (not a substitute
  for local terrain data). Bandwidth: Reliable track summary / Unreliable plots
  + caps / throttle / interest radius.
- Station datalink / fusion shipped (Hub + FusionService); not crypto IFF /
  full datalink protocol.
- EW supports directional noise plus deception (static false plots, range
  walk-off, angle scintillation, intermittent false plots). Noise jammers
  expose coupling modes (`SEARCH_AVG` default / `BEAM` / `MAINLOBE_ONLY`),
  sidelobe level, and `m_CouplingGain`; burn-through range is observable.
  Not full DRFM / coherent range-gate pull-off.
- Tracking associates by measurement gates (nearest neighbor), not entity
  identity. Default plots clear `m_Entity` unless `m_KeepEntityTruth`.
- Product target: playable sensor gameplay, not an EM simulator. See
  [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md) and [TODO.md](../TODO.md).

---

## 中文

Enforce 实现现已遵循与离线原型相同的契约。游戏实体是物理链中的散射体/辐射源；
对外 plots 为量化、带噪的量测（不是原始实体位姿）。

相对真实电磁波雷达的能力（能做什么 / 缺什么 / 仍可做什么）：
[RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md)。

## 运行时链路

1. 解析雷达原点与当前瞄准轴（boresight）。
2. 推进全局散射体表（`RDF_RadarScattererRegistry`）：周期发现扫描、摊销分类、轮询运动学刷新。
   类型与 RCS 按条目缓存，扫描不再重新分类实体。
3. 从表中读取候选，再施加距离与方位驻留。
4. `TraceMove` 通视检查（`RDF_RadarScanGeometry.TraceLineOfSight`：
   `ANY_CONTACT`、复用 `TraceParam`、`ExcludeArray`=主体+目标、起点出壳、
   分数映射回 origin→end）。通畅 → 直接检测。
   遮挡 → 可选 NLOS（默认开）：地面反射（镜像法 + `|Gamma|^2` + 前阻挡深度）
   **和/或** 单刃绕射（DEM/`GetSurfaceY` 沿程，Fresnel ν）；因子取较强路径；
   标记 `m_LosBlocked`（beam 名 `/nlos` 或 `/diff`）。
5. 读取炮弹或刚体速度（**真值，仅物理用**）。
6. 估计实体 RCS；接收功率乘以 `m_MultipathFactor`。
7. 选择配置中最强的俯仰波束。
8. 计算单站接收功率、多普勒、MTI、处理增益、噪声、DEM 杂波功率、可选大气/雨/天气损耗与 SNR。
9. 可选方位/距离功率粗栅 CA-CFAR；空单元可用热噪声填充（`m_EnableCfarThermalFill`），以便可测 Pfa。
10. **测量合成**（`RDF_RadarMeasurement`）：距离量化到门中心，叠加 SNR 缩放的角度/多普勒噪声
    （经 settings / 理想 vs 逼真档缩放），重建 plot 运动学；除非 `m_KeepEntityTruth`，否则清除 `m_Entity`。
    可选 CFAR 后 override：`RDF_RadarMeasurementModel`。
11. 将接受的 plots 送入量测驱动的 α-β 跟踪器（最近邻波门 + `PredictAt`）。匿名 / 假 plots 也可关联；并非仅显示。
12. 可选锁定层（`RDF_RadarLockManager`）作用于航迹：SEARCH → ACQUIRING → TRACKING → COAST；
    经 `RDF_RadarSensor.GetLockedTarget` 暴露。

## 主要配置

`RDF_RadarSettings`：

- `m_Hardware`：`RDF_RadarHardware`
- `m_EnablePhysicalDetection`
- `m_EnableMechanicalScan`
- `m_DetectionSnrDb`
- `m_KeepUndetected`
- `m_EnableDemClutter`
- `m_DemCacheMaxTiles`
- `m_DemTileLoadsPerScan`
- `m_DemClutterScale`
- `m_ScattererDiscoveryRangeScale`、`m_ScattererDiscoveryIntervalS`、
  `m_ScattererClassifyPerTick`、`m_ScattererRefreshPerTick`、`m_ScattererMaxEntries`
- `m_MaxLosTracesPerScan`（默认 48；限制 TraceMove 卡顿）
- `m_EnableNlosMultipath`（默认 true）
- `m_NlosReflectionAbs`、`m_NlosMinFactor`、`m_NlosMaxTargetAglM`
- `m_AdditionalNoisePowerW`
- `m_EwStack`
- `m_EnableCfarGate`、`m_CfarGuardCells`、`m_CfarTrainingCells`、`m_CfarPfa`、
  `m_RangeBinCount`、`m_EnableCfarThermalFill`、`m_CfarMode`（CA/GO/SO）
- 扫描优化：`m_LosCacheMaxAgeS`、复用年龄/移位、优先级距离 /
  速度 / 波段间隔、`m_FreshUpdateBudgetMin/Max`
- `m_EnableWlrHudAlerts`、`m_WlrHudAlertRadiusM`
- `m_EnableAtmosphericLoss` / 天气驱动雨雾损耗（见 RADAR_API）
- `m_EnableMeasurementSynthesis`（默认 true）、`m_KeepEntityTruth`（调试）、
  `m_MeasurementModel`、测量噪声缩放
- `m_TrackGateRangeM`、`m_TrackGateAzimuthDeg`、`m_TrackConfirmHits`、`m_TrackMaxMisses`
- `m_EnableEsmReceive`（辐射源走 Friis \(R^2\)；SEARCH 预设开启）
- `m_EnableRwrReporting`（每次驻留后发布 SEARCH/TRACK/LOCK 威胁）
- `m_FairScanCursor`（公平轮询驻留游标；默认 true）
- Include* 标志（`m_IncludeVehicles` / projectiles / emitters 等）

`RDF_RadarHardware`：

- 频率、峰值功率、天线增益、系统损耗、噪声系数
- 脉宽、带宽、PRF、脉冲积累、MTI
- `GetRangeBinM()` ≈ `c/(2B)`，用于测量量化
- 扫描 RPM 与 `RDF_RadarElevationBeam[]`

## 预设

```c
RDF_RadarSettings shorad = RDF_RadarDemoConfig.CreateDefault(64);
RDF_RadarSettings p18 = RDF_RadarDemoConfig.CreateP18Like(128);
```

预设仅为示例。游戏系统可自行构造硬件与波束列表。

## 可选 EW 噪声

```c
RDF_RadarNoiseJammerEffect jammer = new RDF_RadarNoiseJammerEffect();
jammer.m_Position = jammerWorldPosition;
jammer.m_ErpW = 10000.0;
jammer.m_BandwidthHz = 5000000.0;
// 默认：SEARCH_AVG + -40 dB 副瓣（可玩软压制）。硬凝视：
// jammer.ConfigurePhysicsBeam(-25.0);
// 仅主瓣：jammer.ConfigureMainlobeOnly();
// 再软：jammer.m_CouplingGain = 0.25;
settings.m_EwStack.Add(jammer);
```

烧穿：`RDF_RadarEwBurnThrough.RangeM(R0, N, J)`。  
状态：`scanner.GetEwStatsShort()` / Sensor `GetStatusShort` 附加 `ewJN=…dB Reff=…m`。

## 可选 EW 欺骗 / 假 plots

```c
RDF_RadarDeceptionJammerEffect deceive = new RDF_RadarDeceptionJammerEffect();
deceive.AddFalsePlot(1600.0, -18.0, 0.0000000000012, 25.0, 0.0);
settings.m_EwStack.Add(deceive);

// Range walk-off / angle scintillation / intermittent false plot:
RDF_RadarRangeWalkOffEffect walk = new RDF_RadarRangeWalkOffEffect();
walk.m_BaseRangeM = 1200.0;
walk.m_RangeRateMs = 80.0;
settings.m_EwStack.Add(walk);

RDF_RadarAngleScintillationEffect scint = new RDF_RadarAngleScintillationEffect();
scint.m_RangeM = 1500.0;
scint.m_AzimuthJitterDeg = 3.0;
settings.m_EwStack.Add(scint);

RDF_RadarIntermittentFalsePlotEffect burst = new RDF_RadarIntermittentFalsePlotEffect();
burst.m_PeriodS = 2.0;
burst.m_DutyCycle = 0.35;
settings.m_EwStack.Add(burst);
```

假 plots 以匿名目标发出（`m_IsAnonymous=true`，`m_IsFalsePlot=true`），并走同一 CFAR 门。

## 运行时 DEM 杂波源

首选运行时路径（见 [DEM.md](DEM.md)）：

- 高程：实时 `BaseWorld.GetSurfaceY`
- 地表类别：`$profile` 或模组 `DemData/<world>/surf_manifest.json` + `surf_chunks/` 下的 `RDF_SURF_JSON_V1`
- 电磁参数：`RadarData/SurfaceTable.conf`（工坊），JSON 回退

开发回退：V3 CSV 瓦片（`manifest.csv` + `tiles/tile_*.csv`）。
皆无 → `mode=LIVE`（仅高程，地表类 UNKNOWN）。

扫描器初始化有界 LRU 缓存（`RDF_DemRuntimeCache`），并在采样目标附近按需加载瓦片/块。当前默认：

- 最大缓存瓦片：`16`
- 每扫同步瓦片加载预算：`2`

当 DEM / SURF 数据不可用或损坏时，雷达处理回退为仅热噪声 + EW 噪声（LIVE 高程仍可用），旧地图可继续运行。

## 检测输出

每个 `RDF_RadarTarget` 现包含：

- 方位/俯仰与扫描序号
- 速度、径向速度与多普勒
- RCS、接收/处理功率、MTI 增益
- 采样的 DEM 地表类与杂波功率贡献
- SNR、检测标志与所选波束名
- `m_LosBlocked`、`m_LosHitFraction`、`m_MultipathFactor`（NLOS bounce / 刀刃绕射缩放）
- `m_IsAnonymous`、`m_IsFalsePlot`、`m_CfarPowerW`

## PPI HUD

`RDF_RadarHUD` 绘制北向上平面位置显示（绿磷光主题，右下角面板 **144×184**，PPI 画布 **128×128**），含距离环、瞄准轴扫描线与每目标一个光点。

- 光点颜色：默认淡黄色匿名量测 plots；白色欺骗假点；青色表示检出的 NLOS 多径（`m_LosBlocked`）；
  仅当 `m_KeepEntityTruth` 开启时用柔和类型色；`m_KeepUndetected` 开启时未检出回波为暗灰。
- 光点大小随 SNR 缩放。
- 数据行显示检出/总数、确认航迹数与最强目标（类型、距离、SNR）。
- 航迹来自量测关联；用 `RDF_RadarTrack.PredictAt(t)` 做外推锁定 / 前置点。

控制：

```c
RDF_RadarAutoRunner.SetHudEnabled(true);   // show + auto-feed every scan
RDF_RadarHUD.SetMode("SHORAD");            // header label
RDF_RadarHUD.SetDisplayRange(2000.0);      // overridden by scan range each feed
```

Runner 自动从 `RDF_RadarScanner.GetLastOrigin()`、`GetLastForward()`、`GetLastRange()` 喂 HUD。
自定义系统可调用 `RDF_RadarHUD.FeedScan(targets, origin, forward, range, tracker)`。

本地模式下 HUD 模式标签含 DEM 状态；同步模式下含 `NET`。

## 网络权威路径

在雷达所有者上同时使用 `RDF_RadarNetworkComponent` 与 `RplComponent`。
`RDF_RadarAutoRunner` 与 `RDF_RadarComponent` 会在 subject 上自动检测 `RDF_RadarNetworkAPI`。
玩法优先在权威端驱动 `RDF_RadarSensor`；网络组件同步扫描结果。
完整契约见 [RADAR_API.md](RADAR_API.md) § Network。

1. 客户端调用 `RequestScan()`，
2. 服务器跑权威扫描路径（Sensor / Scanner + Tracker / Lock / WLR），
3. 经 `RDF_RadarNetCodec` 打成类型化 `array<int>` / `array<float>`（非 CSV 字符串），
4. **Reliable** Broadcast：扫描**摘要**（有上限确认航迹 + WLR + 锁 + 元数据；不含 plots），
5. 可选 **Unreliable** Broadcast：有上限 plots（HUD，`m_SyncPlotsUnreliable`），
6. 客户端更新同步缓存；Proxy 上的 `RDF_RadarSensor` 注入航迹/锁定并跳过该帧本地 Tracker/Lock 重算，
7. JIP 经 `RplSave` / `RplLoad` + `ScriptBitWriter` / `ScriptBitReader`，
8. 慢旋钮（range、sector、CFAR、Include*、emitting）用 `[RplProp]`；HUD/DEM 保持本地。

规模属性：`m_MaxSyncedTracks` / `m_MaxSyncedPlots`、
`m_MinReliableBroadcastIntervalS` / `m_MinPlotBroadcastIntervalS`、
`m_SkipUnchangedSummary`（指纹）、`m_InterestRadiusM`、`m_SyncPlotsUnreliable`。

### 站间数据链与多雷达融合

站间共享（≠ 武器中制导 uplink — 注意 WeaponBridge「datalink」命名）：

- `RDF_RadarDatalinkHub` — 确认航迹库（TTL）+ 轻量 IFF
- `RDF_RadarFusionService` — 关联门限 + 可选双站水平交会
- 可选 `RDF_RadarDatalinkComponent` — Hub 状态类型化 Broadcast（同 NetCodec；限频/上限/兴趣）

权威扫描成功后可 `PublishTracksToDatalink`。API：`RDF_RadarDatalinkAPI` / `GetFusedTracks`。
回归：`RDF_RadarFusionAutoTest.Start()`。

### 最小搭建（实体挂载雷达）

1. 在雷达实体 prefab 上添加：
   - `RplComponent`
   - `RDF_RadarComponent`
   - `RDF_RadarNetworkComponent`
2. 确保实体在场景/会话设置中可复制。
3. 经现有入口启动雷达（`RDF_RadarComponent` tick 或 `RDF_RadarAutoRunner`），无需额外脚本接线。
4. 可选：保持 `RDF_RadarAutoRunner.SetHudEnabled(true)` 以观察客户端 HUD。
5. 可选：`RDF_RadarDatalinkComponent` 做站间 Hub Broadcast。

### 快速验证（服务器权威）

1. 运行至少主机 + 一个客户端的多人会话。
2. 在主机上照常启用雷达演示/配置。
3. 在客户端确认 HUD 模式文本显示 `PPI | NET`。
4. 移动目标/辐射源，确认主机/客户端航迹/锁定趋势一致（plots 在 Unreliable + 上限下可能滞后或丢包）。

## 自动化实弹射击 + WLR 测试

Script Debugger（Play 模式）。生成并发射真实 82 mm HE 炮弹：

```c
RDF_RadarShellFireAutoTest.Start();
```

约 42 s：每 6 s 在本地玩家前方发射 O832DU，宽扇区凝视弹道，然后检查检出 plots、确认航迹，以及相对真值的 WLR 发射误差（理想通道通过 ≤ **250 m**；逼真通道 ≤ **800 m**，经 `WLR_LAUNCH_PASS_REALISTIC_M`）。报告：
`$profile:RDF/RadarTests/radar_shellfire_autotest_<tick>.txt`。

## 自动化弹道 / WLR 数学测试

Script Debugger（同步，无 tick 循环）：

```c
RDF_RadarBallisticsAutoTest.Start();
```

检查自由落体落点时间、AirDrag 距离缩短、侧风偏移、发射点反推，以及多点真空弹道拟合 + AirDrag 地面交点（WLR 风格门限；非仅顶点）。写入
`$profile:RDF/RadarTests/radar_ballistics_autotest_<tick>.txt`。

离线 Python 镜像：

```
cd tools/dem
python -m unittest test_rdf_radar_ballistics.py -v
```

## 自动化 DEM 回归测试

可在 Script Debugger 中运行完整自动化游戏内测试：

```c
RDF_RadarAutoTest.Start();
```

自动执行四个阶段：

1. DEM 杂波关闭基线
2. DEM 杂波开启（`scale=1.0`）
3. DEM 杂波开启（`scale=5.0`）
4. DEM 杂波开启且瓦片加载预算为 `0`（降级路径）

测试沿瞄准轴生成真实无辅助 Mi-8 目标（无辐射源标记、无 RCS 加成、无预填散射体行）。目标必须由发现找到、并靠自身回波检出（`discovered_unaided` 守卫）。

以理想 vs 逼真通道跑全套件（顺序：Ballistics → DEM → Lock → Airborne → ShellFire）：

```c
RDF_RadarAutoTestSuite.StartAll();
RDF_RadarAutoTestSuite.StartAllRealistic();
```

独立回归（**不**进 `StartAll`；一次跑一个）：

```c
RDF_RadarRwrAutoTest.Start();            // RWR SEARCH/TRACK/LOCK on victim
RDF_RadarEsmArmAutoTest.Start();         // ESM + GetArmAim / silence unlock
RDF_RadarRocketLockFireAutoTest.Start(); // lock Mi-8 → Hydra70 guidance
RDF_RadarHeliDuelAutoTest.Start();       // 机打机 + 拦截
RDF_RadarSamEngageAutoTest.Start();      // 地面 SAM 搜索/识别/打击演示
RDF_RadarFusionAutoTest.Start();         // 数据链 Hub + 融合 / 交会
RDF_RadarStressAutoTest.Start();         // 重负载 soak
```

离线 Python 全能力套件（无需 DEM）：

```
cd tools/dem
python rdf_radar_full_sim.py
python -m unittest discover -s . -p "test_rdf_*.py" -v
```

测试运行时地图标记：`RDF_RadarAutoTestMapOverlay`（如 Lock / Airborne）。

输出：

- 控制台打印阶段指标与最终 `PASS/FAIL`
- 报告写入 `$profile:RDF/RadarTests/radar_dem_autotest_<tick>.txt`

控制：

```c
RDF_RadarAutoTest.IsRunning();
RDF_RadarAutoTest.Stop();
```

## 自动化空中目标测试（Mi-8）

Script Debugger：

```c
RDF_RadarAirborneScanTest.Start();
RDF_RadarAirborneScanTest.StartKeepTarget();
```

该测试自动：

1. 选择附近较平坦的雷达起点，
2. 生成 `{6BDF7D3E72D31F29}Prefabs/Scenarios/SP01/SP01_Mi8MT_unarmed_transport.et`，
3. 强制空中圆周运动并带垂直振荡，
4. 跑雷达扫描并验证生成目标被看见/检出/分类。

`StartKeepTarget()` 在测试结束后保留生成的空中目标，便于手动 PPI/世界视觉检查。

输出报告：

- `$profile:RDF/RadarTests/radar_airborne_scan_<tick>.txt`

## 当前边界

- 目标候选仍以实体优先（散射体表），通视走
  `RDF_RadarScanGeometry.TraceLineOfSight`（`ANY_CONTACT`、复用 TraceParam、
  ExcludeArray、起点出壳）；NLOS 可选地面反射弱检 + **单刃绕射**（非多刃/UTD）。
- DEM / 地表：优先 SURF JSON + 实时 `GetSurfaceY`；V3 CSV 为开发回退。
  杂波多人一致仍需匹配的本地 SURF/DEM（或 LIVE）；
  检测**结果**经 `RDF_RadarNetworkComponent` 同步（不能替代本地地形数据）。
  带宽：Reliable 航迹摘要 / Unreliable plots + 上限降频 / 兴趣半径。
- 站间数据链 / 融合已落地（Hub + FusionService）；非密码学 IFF / 完整数据链协议。
- EW 支持定向噪声与欺骗（静态假点、拖距、角闪烁、间歇假点）。噪声干扰可调
  耦合模式（默认 `SEARCH_AVG` / `BEAM` / `MAINLOBE_ONLY`）、副瓣与
  `m_CouplingGain`；可观测烧穿距离。非完整 DRFM / 相干距离门拖引。
- 跟踪按量测波门关联（最近邻），不按实体身份。默认 plots 清除 `m_Entity`，除非 `m_KeepEntityTruth`。
- 产品目标：可玩的传感器玩法，不是电磁仿真器。见
  [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md) 与 [TODO.md](../TODO.md)。
