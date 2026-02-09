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
    RDF_LidarAutoRunner.c
    RDF_LidarAutoEntity.c
```

## Data Flow
1. `RDF_LidarScanner` produces `RDF_LidarSample` data based on `RDF_LidarSettings`.
2. `RDF_LidarVisualizer` renders samples using `RDF_LidarVisualSettings`.
3. Demo utilities (AutoRunner/AutoEntity) optionally drive the scan loop.

## Extension Points
- Custom sampling: replace `BuildUniformDirection()` or create a new scanner class.
- Custom output: consume `RDF_LidarSample` array for CSV/export/AI.
- Visual styles: override `BuildPointColor()` or `BuildRayColorAtT()`.

## Demo Isolation
- `RDF_LidarAutoRunner` is off by default and only runs when started explicitly.
- `RDF_LidarAutoEntity` lets you toggle the demo in-world without modifying game mode.

## Performance Tips
- Lower `m_RayCount` for runtime use.
- Increase `m_UpdateInterval` for heavy scenes.
- Reduce `m_RaySegments` to limit line segments.
