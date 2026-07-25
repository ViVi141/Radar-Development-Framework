# RDF Radar Simulation Framework

The offline prototype is technology- and era-agnostic. Named radars are example
presets, not core dependencies.

## Layers

- `rdf_dem_io.py`: terrain, material, and vertical-span environment.
- `rdf_radar_materials.py`: calibratable band/sea-state sigma-zero tables.
- `rdf_radar_physics.py`: hardware, waveform, elevation beams, radar equation,
  pulse compression, integration, Doppler, MTI, and CFAR.
- `rdf_radar_channel.py`: frequency retune, multipath, Swerling RCS, spectral
  overlap helpers.
- `rdf_radar_targets.py`: target state and time-parametric trajectories.
- `rdf_radar_sector_sim.py`: static full-sector RF/clutter snapshot.
- `rdf_radar_scan.py`: `scan(t)`, dwell updates, persistent PPI, moving-track
  intercepts, per-beam measurements, hop-aware retune.
- `rdf_radar_ew.py`: pluggable noise/deception effects and frequency schedules.
- `rdf_radar_track.py`: nearest-neighbor association and alpha-beta filtering.
- `rdf_radar_framework_demo.py`: clean/EW validation output and CSV dumps.

## Framework contracts

- `RadarHardware`: waveform and receiver parameters.
- `ElevationBeam[]`: zero, one, or many elevation beams.
- `TargetTrajectory.sample(t)`: target motion without coupling to the radar.
- `hardware_at_frequency(f)`: retune wavelength / atmosphere / band label.
- `EWEffect.apply(power, context)`: optional EW processing before CFAR.
- `FrequencyHopSchedule.frequency_at(t)`: configurable frequency agility.
- `associate_and_filter(detections)`: track birth / update / coast.

## Processing chain per dwell

1. Mechanical/electronic `boresight_at(t)`
2. Frequency hop → retuned `RadarHardware`
3. Scale clutter by radar-constant and mild σ⁰ frequency factor
4. Target RCS (Swerling) × multipath × pattern × radar equation
5. Pulse compression / integration / MTI
6. EW plugins (noise spectral overlap, deception)
7. CA-CFAR
8. Cross-scan alpha-beta tracker

## Examples

```powershell
python tools\dem\rdf_radar_framework_demo.py --preset shorad
python tools\dem\rdf_radar_framework_demo.py --preset shorad --ew
python tools\dem\rdf_radar_framework_demo.py --preset p18
```

Outputs under `tools/dem/out/`:

- `*_framework_*.png`
- `*_detections.csv`
- `*_tracks.csv`

## Current fidelity boundaries

- No IQ baseband sampling, phase noise, AGC loops, or full PD Doppler filter bank.
- Multipath is a two-ray engineering factor, not a full terrain-bounce solver.
- Moving-target LOS currently uses terrain height (static clutter still uses spans).
- Frequency-dependent clutter remapping is a scale factor, not a full re-bake.
- Long-range presets are geometrically limited by the loaded map DEM.
