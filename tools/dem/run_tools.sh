#!/usr/bin/env bash
# RDF tools/dem Linux/macOS helper (mirrors run_tools.ps1).
# Usage (from tools/dem):
#   ./run_tools.sh help
#   ./run_tools.sh test
#   ./run_tools.sh full-sim
#   ./run_tools.sh demo
#   ./run_tools.sh demo --ew
#   ./run_tools.sh knife-lut

set -euo pipefail
cd "$(dirname "$0")"

cmd="${1:-help}"
ew=0
if [[ "${2:-}" == "--ew" || "${2:-}" == "-Ew" ]]; then
  ew=1
fi

show_help() {
  cat <<'EOF'
RDF tools/dem helper (catalog: ../README.md)

  ./run_tools.sh test       Run golden suite (test_rdf_*.py)
  ./run_tools.sh full-sim   Capability smoke -> out/full_sim_report.json
  ./run_tools.sh demo       Framework demo (--preset shorad)
  ./run_tools.sh demo --ew  Framework demo with EW
  ./run_tools.sh knife-lut  Knife-edge ν LUT bake + Enforce profile

Also common:
  python3 rdf_dem_pack_surface_json.py --world GM_Eden --from-bin
  python3 rdf_dem_pack_height_json.py --world GM_Eden --terrain ".../worlds/Eden/Terrain"
  python3 rdf_ttile_unpack.py
  python3 rdf_radar_mass_battle_sim.py
  python3 rdf_radar_hw_calibrate.py --preset shorad
  python3 rdf_sig_patch_heli_rotors.py
EOF
}

case "$cmd" in
  help|-h|--help)
    show_help
    ;;
  test)
    python3 -m unittest discover -s . -p 'test_rdf_*.py' -v
    ;;
  full-sim)
    python3 rdf_radar_full_sim.py
    ;;
  demo)
    if [[ "$ew" -eq 1 ]]; then
      python3 rdf_radar_framework_demo.py --preset shorad --ew
    else
      python3 rdf_radar_framework_demo.py --preset shorad
    fi
    ;;
  knife-lut)
    python3 rdf_radar_knife_lut_validate.py
    ;;
  *)
    echo "unknown command: $cmd" >&2
    show_help
    exit 2
    ;;
esac
