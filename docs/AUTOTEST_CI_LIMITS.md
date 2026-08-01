# AutoTest CI limits (Workbench Play)

RDF game-side AutoTests require **Workbench Play** with a loaded world.
There is no supported headless Enforce / Game.exe path in this repo.

## What Python CI already covers

GitHub Actions (`.github/workflows/python-dem-tests.yml`) runs:

1. `unittest discover` for `tools/dem/test_rdf_*.py` (physics, CFAR, track, MTD, calib, …)
2. `rdf_radar_full_sim.py` coverage gates

That is the regression backbone for radar **physics**. It does **not** spawn entities or call TraceMove.

## Play-mode batch (no Debugger paste)

1. Start Workbench → Play on a suitable world (keep Play running).
2. Create `$profile:RDF/RunAutoTestSuite.flag`
   - empty / `ideal` → `RDF_RadarAutoTestSuite.StartAll()`
   - contents containing `realistic` → `StartAllRealistic()`
3. `RDF_RadarAutoTestBatch` (armed from `SCR_BaseGameMode.OnGameStart`) consumes the flag once, runs the suite, then **re-arms** so another flag can be written in the same Play session.
4. When finished, writes `$profile:RDF/RadarTests/radar_autotest_suite_result.txt`
   - `ok=1` only when every suite step passed
   - `ok=0` on step FAIL, step timeout, or suite start failure
   - also: `timed_out=`, `fail_count=`, `fail_steps=` (comma-separated step names)

Manual Debugger still works: `RDF_RadarAutoTestSuite.StartAll()` / `StartAllRealistic()` / `Stop()`.

## What CI will not do (by design)

- Install Arma Reforger / Workbench in GitHub Actions
- Run AutoTestSuite without a human Play session
- Treat uploaded suite reports as required merge gates (optional artifact only)

## Related

- Flag pattern mirrors DEM bake: `$profile:RDF/BakeDemFull.flag`
- Suite steps: see `RDF_RadarAutoTestSuite.c`
- Offline calib (not in-game): `tools/dem/rdf_radar_hw_calibrate.py`
