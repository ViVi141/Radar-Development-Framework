# LiDAR Framework Developer Guide

## Goals
- Keep the framework inert unless explicitly invoked.
- Separate scan core from visualization and demo utilities.
- Provide stable extension points for other mod authors.

## Module Layout
```
scripts/Game/RDF/Lidar/
  Core/
    RDF_LidarSettings.c
    RDF_LidarTypes.c
    RDF_LidarScanner.c
  Visual/
    RDF_LidarVisualSettings.c
    RDF_LidarVisualizer.c
  Util/
    RDF_LidarSubjectResolver.c
  Demo/
    RDF_LidarAutoBootstrap.c
    RDF_LidarAutoRunner.c
```

## Data Flow
1. `RDF_LidarScanner` produces `RDF_LidarSample` data based on `RDF_LidarSettings`.
2. `RDF_LidarVisualizer` renders samples using `RDF_LidarVisualSettings`.
3. Demo utilities (AutoRunner) optionally drive the scan loop.

## Extension Points
- Custom sampling: replace `BuildUniformDirection()` or set a custom `RDF_LidarSampleStrategy` via `RDF_LidarScanner.SetSampleStrategy()`.
- Custom output: use `RDF_LidarVisualizer.GetLastSamples()` to obtain scan data and export externally (e.g. CSV/JSON).
- Visual styles: provide a custom `RDF_LidarColorStrategy` via `RDF_LidarVisualizer` to override point/ray color mapping.

## Demo Isolation
- `RDF_LidarAutoRunner` is off by default and only runs when started explicitly.
- The bootstrap file no longer enables the demo by default; call `SCR_BaseGameMode.SetBootstrapEnabled(true)` to opt in.

## Performance Tips
- Lower `m_RayCount` for runtime use. `m_RayCount` is clamped to [1, 4096] for safety.
- Increase `m_UpdateInterval` for heavy scenes. `m_UpdateInterval` has a minimum enforced value of 0.01s.
- Reduce `m_RaySegments` to limit line segments.
- Visualizer creates debug shapes per render. Be mindful of `m_RayCount` and `m_RaySegments` which influence the number of created shapes; reduce them to limit visual load.
