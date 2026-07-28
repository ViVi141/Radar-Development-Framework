# RDF In-Game Radar Framework

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
4. `TraceMove` line-of-sight check. Clear path → direct detection.
   Blocked path → optional NLOS ground-bounce (default on): image-method path
   length + `|Gamma|^2` + early-blocker depth scale; mark `m_LosBlocked`.
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
- `m_UseScattererRegistry` (default true), `m_ScattererDiscoveryRangeScale`,
  `m_ScattererDiscoveryIntervalS`, `m_ScattererClassifyPerTick`,
  `m_ScattererRefreshPerTick`, `m_ScattererMaxEntries`
- `m_UseSphereQuery`, `m_SphereQueryAlsoActive` (legacy search path only)
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
settings.m_EwStack.Add(jammer);
```

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

Runtime DEM loading uses the existing V3 bake output under:

- `$profile:RDF/DemData/<world>/manifest.csv`
- `$profile:RDF/DemData/<world>/tiles/tile_<ix>_<iz>.csv`

The scanner initializes a bounded LRU cache (`RDF_DemRuntimeCache`) and
loads tiles on demand near sampled targets. Current defaults:

- max cached tiles: `16`
- per-scan sync tile load budget: `2`

When DEM data is unavailable or malformed, radar processing falls back to
thermal noise + EW noise only, so legacy maps continue to run.

## Detection output

Each `RDF_RadarTarget` now includes:

- azimuth/elevation and scan number
- velocity, radial speed, and Doppler
- RCS, received/processed power, MTI gain
- sampled DEM surface class and clutter power contribution
- SNR, detection flag, and selected beam name
- `m_LosBlocked`, `m_LosHitFraction`, `m_MultipathFactor` (NLOS bounce scale)
- `m_IsAnonymous`, `m_IsFalsePlot`, and `m_CfarPowerW`

## PPI HUD

`RDF_RadarHUD` draws a north-up plan-position display (green phosphor theme,
bottom-right) with range rings, a boresight sweep line, and one blip per target.

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

1. client calls `RequestScan()`,
2. server runs the authoritative scan path (Sensor / Scanner + Tracker / Lock / WLR),
3. server broadcasts compact payload (`M` meta + `T` plots + `K` tracks + `W` WLR + `L` lock),
4. clients update synced caches; `RDF_RadarSensor` injects tracks/lock and skips local recompute for that frame.
5. Slow knobs (range, sector, CFAR, Include*, emitting) use `[RplProp]`; HUD/DEM stay local.

### Minimal setup (entity-mounted radar)

1. On the radar entity prefab, add:
   - `RplComponent`
   - `RDF_RadarComponent`
   - `RDF_RadarNetworkComponent`
2. Ensure the entity is replicated in your scenario/session settings.
3. Start radar via existing entry points (`RDF_RadarComponent` tick or
   `RDF_RadarAutoRunner`), no extra script wiring required.
4. Optional: keep `RDF_RadarAutoRunner.SetHudEnabled(true)` to observe client HUD.

### Quick verification (server-authoritative)

1. Run a multiplayer session with at least host + one client.
2. On host, enable radar demo/config as usual.
3. On client, verify HUD mode text shows `PPI | NET`.
4. Move targets/emitter sources and confirm both host/client see consistent
   target count/type trends (allowing normal timing jitter).

## Automated live shell fire + WLR test

Script Debugger (Play mode). Spawns and launches real 82 mm HE shells:

```c
RDF_RadarShellFireAutoTest.Start();
```

About 42 s: fires O832DU every 6 s ahead of the local player, stares a wide
projectile sector, then checks detected plots, confirmed tracks, and WLR launch
error vs truth (pass ≤ 250 m). Report:
`$profile:RDF/RadarTests/radar_shellfire_autotest_<tick>.txt`.

## Automated ballistics / WLR math test

Script Debugger (synchronous, no tick loop):

```c
RDF_RadarBallisticsAutoTest.Start();
```

Checks freefall impact time, AirDrag range shortening, crosswind shift,
launch back-projection, and track apex WLR solve. Writes
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

Run the full suite with ideal vs realistic channels:

```c
RDF_RadarAutoTestSuite.StartAll();
RDF_RadarAutoTestSuite.StartAllRealistic();
```

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

- Target candidates remain entity-first (scatterer registry + sphere/active
  fallback) and LOS still relies on `TraceMove`.
- DEM runtime source is development profile data. For multiplayer parity each
  scanning machine must have matching baked DEM **tile files**; detection
  **results** can sync via `RDF_RadarNetworkComponent` (not a substitute for
  local DEM).
- EW supports directional noise plus simplified deception false-plot injection
  (not full DRFM / range-gate pull-off).
- Tracking associates by measurement gates (nearest neighbor), not entity
  identity. Default plots clear `m_Entity` unless `m_KeepEntityTruth`.
- Product target: playable sensor gameplay, not an EM simulator. See
  [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md) and [TODO.md](../TODO.md).
