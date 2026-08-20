> **Workshop copy** — plain English, ready to paste into Arma Reforger / Steam Workshop.

---

## Summary (≤ 200 chars)

Realistic radar, LiDAR and DEM sensor framework for Arma Reforger. Physical detection, terrain clutter, ESM/RWR/EW, datalink fusion, and an opt-in system layer.

---

## Description (≤ 5000 chars)

# Radar Development Framework (RDF) — v1.1.2

A modular, workshop-ready sensor framework for Arma Reforger: **Radar**,
**LiDAR**, and **DEM** bake/runtime. It models the detection chain as a
physical process — not "god-mode" coordinates — so plots are quantized, noisy
measurements with real thresholds, clutter, jamming, and uncertainty.

## Radar (shipped)

- Physical detection chain: radar equation, RCS + Swerling, Doppler, MTI / MTD
  with rotor micro-Doppler
- DEM sigma0 terrain clutter, knife-edge diffraction, NLOS multipath
- CA / GO / SO-CFAR with thermal-noise false alarms
- Measurement synthesis (quantized + noisy plots; anonymous by default)
- ESM / RWR / anti-radiation (ARM) aiming
- EW: noise jamming (soft + burn-through) and deception (false plots, range
  walk-off, angle scintillation, intermittent)
- Networking (typed Rpc + Reliable summary / Unreliable plots), datalink Hub,
  and multi-radar fusion
- Weapon-locating radar (WLR / counter-battery) with ballistic tracking

## System layer (opt-in, new in 1.0.0)

- **Dwell / resource management** — beam-time budget for fire-control + track
  dwells (priority + EDF + hard budget + deadline-miss)
- **ECCM decision layer** — auto sidelobe blanking / PRF / frequency agility /
  burn-through in response to jamming
- **JPDA soft association** — dense multi-target tracking without track stealing

## LiDAR (maintaining)

Ray point cloud, showcase (afterglow / rings / sweep), PPI phosphor HUD, CSV
export, multiplayer sync.

## DEM (shipped)

Bake V3 CSV; publish SURF (+ optional HEIGHT) JSON. Ships SURF + HEIGHT for the
three official maps **GM_Eden, GM_Cain, GM_Arland**. Other maps fall back to live
terrain: detection still works, but without ground clutter.

## Quick start

```cpp
RDF_RadarSensor sensor = new RDF_RadarSensor();
sensor.ConfigureMode(ERDF_RadarSensorMode.RDF_RADAR_MODE_SEARCH, 64);
sensor.Tick(subject, null);
array<ref RDF_RadarTarget> plots = sensor.GetPlots();
array<ref RDF_RadarTrack> tracks = sensor.GetTracks();
```

## Performance (plain language)

Off until you enable it. A radar blinks about 4–5 times per second; it does
**not** run every frame. One default radar is usually invisible in frame time
(heavy soak measured **4.4 ms** average, 8 ms worst, no hitch). Clients only
draw the PPI — the server does the work. Cost adds up if you spawn many radars
or crank LiDAR to thousands of rays (keep gameplay ≤ 1024). Full Eden terrain
clutter preload is ~300 MB RAM. Details: `docs/PERFORMANCE_EVALUATION.md`.

## Important

This is a **framework**, not a ready-to-play weapon mod. It ships sensors and
APIs; vehicle / weapon prefabs are left to mod authors (a lock → weapon bridge
is included). Enable features explicitly — the framework is silent by default.

## Links

- GitHub: https://github.com/ViVi141/Radar-Development-Framework
- Full docs (bilingual): docs/ in the repository
- License: Apache-2.0

---

## Changelog (unlimited)

## 1.1.2 — 2026-08-20

Patch over 1.1.0 (`f4a726b` … `0f81218`).

- Dedicated server / Workshop: load packaged DemData via `$AddonId:DemData/`
  (bare `DemData/` only worked in Workbench). Fixes `DEM/SURF missing for
  GM_Eden` → LIVE. Profile `$profile:RDF/DemData/` still first.
- Sector-sweep scan (`m_SectorSweepEnabled`) for narrow azimuth search.
- WLR launch/impact: full-drag Nelder–Mead fit (fallback vacuum + prefab drag).
- Fusion IFF: keep NEUTRAL distinct from UNKNOWN when merging.

## 1.1.0 — 2026-08-19

Minor over 1.0.2 (`194d65d` … `e5dab5d`). All new knobs default off; SHORAD
search behaviour unchanged until you Enable*.

- Pulse-width minimum-range eclipse (`m_EnablePulseBlindZone`).
- Per-band VHF/L/S/C/X clutter σ⁰ + OBB aspect RCS (including roll).
- Multi-band channel switch per dwell; Rayleigh RCS vs wavelength; optional
  aperture retune and intra-band hop.
- Opt-in NCTR (rotor/fan/fixed from micro-Doppler), LPI search + Friis RWR,
  track confidence / weapon-grade fire / designate dwells, multipath glint,
  rain-sea σ⁰ damp, atmospheric duct.

## 1.0.2 — 2026-08-18

Patch over 1.0.1 (`99efd5d` … `a1292be`).

- Track birth: leftover plots already inside an existing gate are dropped
  (no parallel file for the same contact). Parked beam:
  `m_bScanAngleLocked` uses phase offset as absolute azimuth.
- Cross-frame LOS TraceMove queue + WLR solve budget; adaptive
  BudgetGovernor (denominator = one server frame via GetFPS; scan ≤ ~30%
  of the frame). Hot-path reuse (tracker / NetCodec / TruthSample) and
  integer scatterer-grid keys.
- Stress soak (24 UAZ + 3 Mi-8, DEM clutter, HUD, 60 s): tickAvg
  **9.4 → 4.4 ms**, p95 15→5, max 23→8, hitch 16/33 = 0.
- Correctness: MTD bin-centre int/int (clutter cancel was dead);
  track-fingerprint float promotion (silent skip of legal broadcasts);
  RGPO used absolute world time; LiDAR network timeout / demo gate used
  frozen world clock. Convention: `docs/TIME_HANDLING.md`.

## 1.0.1 — 2026-08-16

Maintenance after 1.0.0 (before `99efd5d`): GetWorldTime ms/s unification,
DemData resource registration, `[Attribute]` script-default cleanup,
mechanical-scan phase offset, Workshop copy.

## 1.0.0 — 2026-08-14

System-layer depth (§9) shipped and verified end-to-end (offline golden + C
port + in-game AutoTest):

- S1 — Dwell / resource management: beam-time budget, fire-control + track
  dwells, class priority + EDF + hard budget + deadline-miss.
- S2 — JPDA soft association: gate + union-find clustering + joint-event
  enumeration + marginalization + weighted alpha-beta; GNN stays default.
- S3 — ECCM decision layer: hysteresis jam detection + sidelobe/mainlobe
  coupling selects SLB / frequency agility, deception -> PRF agility, locked ->
  burn-through; SLB wired, rest reported.

## 2026-08-07 — Radar fidelity

- Clutter-map asymmetric EMA + footprint sigma0 mix + sea-state.
- Toy FDTD vs max-fidelity game-path comparison.
- Segmented antenna-pattern LUT + fixed-site path LUT.
- Dual-dominant knife-edge diffraction + nu LUT.
- Sidelobe floor + polarization match + optional sidelobe blanking.
- Forward truth / inverse observation split (no entity leakage into tracks).

## 2026-08-06 — LOS / DEM / performance

- DEM-before-Trace LOS precheck + LOS / reuse caches + shard budgets.
- Optional HEIGHT JSON bake pack (official .ttile) into RAM.
- 2 m DEM bake grid; runtime SURF/HEIGHT RAM or GetSurfaceY fallback.

## 2026-08-01 — MTD / MTI / track coast

- MTD Doppler filter bank + rotor micro-Doppler + clutter map + track coast.
- Classic two-pulse / three-pulse MTI + PRF stagger de-blind.
- Track coast kinematics + soft-miss + blind-speed association.

## Earlier

- Search -> plots -> track -> lock/fire-control -> WLR chain.
- Measurement noise / bias, thermal fill, GO/SO-CFAR, atmosphere / rain loss.
- ESM / RWR / ARM, EW noise soft + deception, datalink Hub + fusion.
- LiDAR Sensor / showcase / rectangular FOV; DEM bake V3 + SURF publish.
- Python golden test suite + GitHub Actions CI (drift guard).
