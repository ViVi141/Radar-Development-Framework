# RDF In-Game Radar Framework

The Enforce implementation now follows the same contracts as the offline
prototype while remaining entity-first for real-time performance.

Capabilities vs real EM radar (what works / what is missing / what is still
feasible): [RADAR_CAPABILITIES.md](RADAR_CAPABILITIES.md).

## Runtime chain

1. Resolve radar origin and current boresight.
2. Enumerate active vehicle/projectile/emitter candidates.
3. Apply range and azimuth dwell.
4. `TraceMove` line-of-sight check. Clear path → direct detection.
   Blocked path → optional NLOS ground-bounce (default on): image-method path
   length + `|Gamma|^2` + early-blocker depth scale; mark `m_LosBlocked`.
5. Read projectile or rigid-body velocity.
6. Estimate entity RCS; scale received power by `m_MultipathFactor`.
7. Select the strongest configured elevation beam.
8. Evaluate monostatic received power, Doppler, MTI, processing gain, noise,
   DEM clutter power, and SNR.
9. Keep detections over the configured SNR threshold.
10. Feed accepted detections to the alpha-beta entity tracker.

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
- `m_EnableNlosMultipath` (default true)
- `m_NlosReflectionAbs`, `m_NlosMinFactor`, `m_NlosMaxTargetAglM`
- `m_AdditionalNoisePowerW`
- `m_EwStack`

`RDF_RadarHardware`:

- frequency, peak power, antenna gain, system loss, noise figure
- pulse width, bandwidth, PRF, pulse integration, MTI
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

## PPI HUD

`RDF_RadarHUD` draws a north-up plan-position display (green phosphor theme,
bottom-right) with range rings, a boresight sweep line, and one blip per target.

- Blip colour: green vehicle, orange projectile, magenta emitter; cyan for
  detected NLOS multipath (`m_LosBlocked`); dim grey for undetected returns when
  `m_KeepUndetected` is on.
- Blip size scales with SNR.
- Data row shows detected/total, confirmed track count, and the strongest target
  (type, range, SNR).

Control:

```c
RDF_RadarAutoRunner.SetHudEnabled(true);   // show + auto-feed every scan
RDF_RadarHUD.SetMode("SHORAD");            // header label
RDF_RadarHUD.SetDisplayRange(2000.0);      // overridden by scan range each feed
```

The runner feeds the HUD automatically from `RDF_RadarScanner.GetLastOrigin()`,
`GetLastForward()`, and `GetLastRange()`. To drive it from a custom system call
`RDF_RadarHUD.FeedScan(targets, origin, forward, range, tracker)`.

HUD mode label now includes DEM status (`DEM OK` or `DEM OFF`).

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

The test injects synthetic moving radar-emitter targets by reusing active
entities as emitter owners and updating registry positions every tick. This
avoids dependence on aircraft AI behavior in editor tests.

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

- Target candidates and LOS are still entity-first (`GetActiveEntities` +
  `TraceMove`). DEM currently contributes clutter power to noise modeling, not
  anonymous-plot generation.
- DEM runtime source is development profile data. For multiplayer parity each
  scanning machine must have matching baked DEM files.
- EW currently adds directional noise. Deception/false plots remain offline.
- Tracking associates by entity identity; anonymous plot association remains
  offline.
