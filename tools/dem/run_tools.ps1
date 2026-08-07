# RDF tools/dem Windows helper.
# Usage (from tools/dem):
#   .\run_tools.ps1 help
#   .\run_tools.ps1 test
#   .\run_tools.ps1 full-sim
#   .\run_tools.ps1 demo
#   .\run_tools.ps1 demo -Ew
#   .\run_tools.ps1 knife-lut
#   .\run_tools.ps1 pattern-site
#   .\run_tools.ps1 voxel-em
#   .\run_tools.ps1 quadtree

param(
    [Parameter(Position = 0)]
    [ValidateSet("help", "test", "full-sim", "demo", "knife-lut", "pattern-site", "voxel-em", "quadtree")]
    [string]$Command = "help",

    [switch]$Ew
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

function Show-Help {
    Write-Host @"
RDF tools/dem helper (catalog: ..\README.md)

  .\run_tools.ps1 test       Run golden suite (test_rdf_*.py incl. MTD/CPA)
  .\run_tools.ps1 full-sim   Capability smoke -> out\full_sim_report.json
  .\run_tools.ps1 demo       Framework demo (--preset shorad)
  .\run_tools.ps1 demo -Ew   Framework demo with EW
  .\run_tools.ps1 knife-lut  Knife-edge ν LUT bake + Enforce profile
  .\run_tools.ps1 pattern-site  Pattern + site-path LUT bake
  .\run_tools.ps1 voxel-em   3D voxel EM power-field validation
  .\run_tools.ps1 quadtree   Quadtree vs dense memory measure

Linux/macOS: ./run_tools.sh <same commands>

Also common:
  python rdf_dem_pack_surface_json.py --world GM_Eden --from-bin
  python rdf_dem_pack_height_json.py --world GM_Eden --terrain "...\worlds\Eden\Terrain"
  python rdf_ttile_unpack.py
  python rdf_radar_mass_battle_sim.py
  python rdf_radar_hw_calibrate.py --preset shorad
  python rdf_sig_patch_heli_rotors.py
"@
}

switch ($Command) {
    "help" {
        Show-Help
        exit 0
    }
    "test" {
        python -m unittest discover -s . -p "test_rdf_*.py" -v
        exit $LASTEXITCODE
    }
    "full-sim" {
        python rdf_radar_full_sim.py
        exit $LASTEXITCODE
    }
    "demo" {
        if ($Ew) {
            python rdf_radar_framework_demo.py --preset shorad --ew
        }
        else {
            python rdf_radar_framework_demo.py --preset shorad
        }
        exit $LASTEXITCODE
    }
    "knife-lut" {
        python rdf_radar_knife_lut_validate.py
        exit $LASTEXITCODE
    }
    "pattern-site" {
        python rdf_radar_pattern_site_validate.py
        exit $LASTEXITCODE
    }
    "voxel-em" {
        python rdf_voxel_em_validate.py --plot
        exit $LASTEXITCODE
    }
    "quadtree" {
        python rdf_voxel_quadtree_measure.py
        exit $LASTEXITCODE
    }
}
