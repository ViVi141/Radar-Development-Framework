# mass_battle package

Offline large-scale air-defense + artillery (WLR) battle simulation.

## Layout (facade-only split)

| Module | Role |
|--------|------|
| `_impl.py` | **Canonical implementation** (~all logic lives here) |
| `cli.py` | `main()` entry |
| `signatures.py`, `scenario.py`, `plot_radar.py`, `measurement.py`, `wlr_fixes.py`, `prediction.py`, `render_dem.py` | Thin re-exports from `_impl` for import clarity |
| `__init__.py` | Re-exports `_impl` + `main` |

The thematic modules are **facades**, not extracted bodies. Prefer editing
`_impl.py` unless you are doing a real domain cut. Compatibility CLI:
`tools/dem/rdf_radar_mass_battle_sim.py`.
